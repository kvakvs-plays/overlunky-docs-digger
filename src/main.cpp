#include "bindings.hpp"
#include "cache.hpp"
#include "emit_lua.hpp"
#include "groups.hpp"
#include "lua_names.hpp"
#include "merge.hpp"
#include "model.hpp"
#include "runner.hpp"
#include "usertypes.hpp"

#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/Regex.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {

    // Command line of the tool. Own options instead of clang::tooling::CommonOptionsParser: that one loads the -p
    // database only when source paths are given, and parsing the selected TUs of a database is the normal use here.
    // Created in main, not as globals, so their construction runs where failures can be reported.
    struct Options {
        // Groups the tool's own options in --help, apart from LLVM's.
        llvm::cl::OptionCategory category{"docs-digger options"};

        // Directory of compile_commands.json (overlunky configured with CMAKE_EXPORT_COMPILE_COMMANDS).
        llvm::cl::opt<std::string> database{"p", llvm::cl::desc("Directory that holds compile_commands.json"),
                                            llvm::cl::value_desc("dir"), llvm::cl::cat(category)};

        // Explicit translation units; when empty, --select picks them from the database.
        llvm::cl::list<std::string> sources{llvm::cl::Positional, llvm::cl::desc("[<source> ...]"),
                                            llvm::cl::cat(category)};

        // Root of the sources to report on (overlunky's src/game_api, or a fixture directory).
        llvm::cl::opt<std::string> root{"root",
                                        llvm::cl::desc("Report only code under <dir>; paths are relative to it"),
                                        llvm::cl::value_desc("dir"), llvm::cl::init("."), llvm::cl::cat(category)};

        // Where the result goes; "-" keeps it on stdout for quick looks.
        llvm::cl::opt<std::string> output{"o", llvm::cl::desc("Output file (default: stdout)"),
                                          llvm::cl::value_desc("file"), llvm::cl::init("-"), llvm::cl::cat(category)};

        // Translation units taken from the compilation database when no source paths are given.
        llvm::cl::opt<std::string> select{
            "select",
            llvm::cl::desc("Regex over compilation database paths: the translation units to parse when no source "
                           "paths are given"),
            llvm::cl::value_desc("regex"), llvm::cl::init(R"([/\\]src[/\\]game_api[/\\])"), llvm::cl::cat(category)};

        // Flags a database lacks for parsing with this Clang, without editing the database.
        llvm::cl::list<std::string> extraArgs{"extra-arg",
                                              llvm::cl::desc("Argument to append to every compile command"),
                                              llvm::cl::value_desc("arg"), llvm::cl::cat(category)};

        // Enum values a running game reports (overlunky's docs/game_data/lua_enums.txt), for the tables the code
        // fills at runtime.
        llvm::cl::opt<std::string> runtimeEnums{
            "runtime-enums", llvm::cl::desc("lua_enums.txt dump of the enum tables of a running game"),
            llvm::cl::value_desc("file"), llvm::cl::cat(category)};

        // Function group rules (config/groups.json); without them every function is grouped by its source file.
        llvm::cl::opt<std::string> groups{"groups",
                                          llvm::cl::desc("JSON rules that put functions into documentation groups"),
                                          llvm::cl::value_desc("file"), llvm::cl::cat(category)};

        // Lua names for C++ types the type facts cannot name (config/lua_types.json).
        llvm::cl::opt<std::string> luaTypes{"lua-types", llvm::cl::desc("JSON names of C++ types in the Lua outputs"),
                                            llvm::cl::value_desc("file"), llvm::cl::cat(category)};

        // Markdown text fragments that the outputs insert by name (config/fragments: `bit_flags.md`).
        llvm::cl::opt<std::string> fragments{
            "fragments", llvm::cl::desc("Directory of Markdown text fragments (<name>.md) inserted into the docs"),
            llvm::cl::value_desc("dir"), llvm::cl::cat(category)};

        // Directory for the LuaLS definition files (spel2*.lua); none are written without it.
        llvm::cl::opt<std::string> luaOut{"lua-out", llvm::cl::desc("Write LuaLS definition files into <dir>"),
                                          llvm::cl::value_desc("dir"), llvm::cl::cat(category)};

        // Annotation dialect of the Lua files: LuaLS by default, classic EmmyLua for older IDE plugins.
        llvm::cl::opt<dd::LuaStyle> luaStyle{
            "lua-style", llvm::cl::desc("Annotation dialect of the Lua definition files"),
            llvm::cl::values(
                clEnumValN(dd::LuaStyle::LuaLS, "luals", "lua-language-server (default)"),
                clEnumValN(dd::LuaStyle::EmmyLua, "emmylua", "classic EmmyLua: no `?`, tuples or operators")),
            llvm::cl::init(dd::LuaStyle::LuaLS), llvm::cl::cat(category)};

        // A second directory with the same files in the classic EmmyLua dialect, so one run gives both downloads.
        llvm::cl::opt<std::string> emmyluaOut{
            "emmylua-out", llvm::cl::desc("Also write the definition files in the classic EmmyLua dialect into <dir>"),
            llvm::cl::value_desc("dir"), llvm::cl::cat(category)};

        // Under the 500 KB LuaLS preloads by default, with a margin.
        llvm::cl::opt<std::size_t> luaMaxFileSize{
            "lua-max-file-size", llvm::cl::desc("Largest Lua definition file in bytes; more continues in _2, _3"),
            llvm::cl::value_desc("bytes"), llvm::cl::init(480000), llvm::cl::cat(category)};

        // Hand-written declarations for the globals file (config/lua_prelude.lua).
        llvm::cl::opt<std::string> luaPrelude{"lua-prelude",
                                              llvm::cl::desc("Lua declarations put at the start of spel2.lua"),
                                              llvm::cl::value_desc("file"), llvm::cl::cat(category)};

        // Where the `(#anchor)` links of the docs point in the Lua files (the published HTML documentation).
        llvm::cl::opt<std::string> luaDocUrl{"lua-doc-url",
                                             llvm::cl::desc("Base URL for the `(#anchor)` links in Lua doc comments"),
                                             llvm::cl::value_desc("url"), llvm::cl::cat(category)};

        // The per-TU result cache (dig.bat passes build/digger-cache.sqlite); empty for none.
        llvm::cl::opt<std::string> cache{"cache",
                                         llvm::cl::desc("SQLite file that keeps each translation unit's result while "
                                                        "the files it read are unchanged"),
                                         llvm::cl::value_desc("file"), llvm::cl::cat(category)};

        // Parses everything even when a cache is given (the new results are still stored).
        llvm::cl::opt<bool> noCache{"no-cache", llvm::cl::desc("Do not take results from the cache"),
                                    llvm::cl::cat(category)};

        // Parallel parsing; a sol2 translation unit takes up to 2 GB.
        llvm::cl::opt<unsigned> jobs{
            "jobs",
            llvm::cl::desc("Translation units parsed at the same time (default: LLVM's thread count for heavy work)"),
            llvm::cl::value_desc("n"), llvm::cl::init(0), llvm::cl::cat(category)};
    };

    // The `-- <flags>` database when given (tests), else the -p one; nullptr after printing why.
    std::unique_ptr<clang::tooling::CompilationDatabase>
    loadDatabase(const Options& options, std::unique_ptr<clang::tooling::CompilationDatabase> fixed) {
        if (fixed) {
            return fixed;
        }
        if (options.database.empty()) {
            llvm::errs() << "docs-digger: give -p <dir with compile_commands.json> or -- <compiler flags>\n";
            return nullptr;
        }
        std::string error;
        std::unique_ptr<clang::tooling::CompilationDatabase> database =
            clang::tooling::CompilationDatabase::autoDetectFromDirectory(options.database, error);
        if (!database) {
            llvm::errs() << "docs-digger: " << error << '\n';
        }
        return database;
    }

    // The explicit sources, or the database entries matching --select.
    std::vector<std::string> selectSources(const Options& options,
                                           const clang::tooling::CompilationDatabase& database) {
        if (!options.sources.empty()) {
            return {options.sources.begin(), options.sources.end()};
        }
        const llvm::Regex select(options.select);
        std::vector<std::string> sources;
        for (const std::string& file : database.getAllFiles()) {
            if (select.match(file)) {
                sources.push_back(file);
            }
        }
        return sources;
    }

    // Writes the extraction as indented JSON (keys sorted, so the output is stable).
    void writeExtraction(const dd::Extraction& extraction, llvm::raw_ostream& out) {
        out << llvm::formatv("{0:2}", dd::toJSON(extraction)) << '\n';
    }

    // Writes to stdout or to the -o file; returns false when the file cannot be created.
    bool writeOutput(const Options& options, const dd::Extraction& extraction) {
        if (options.output == "-") {
            writeExtraction(extraction, llvm::outs());
            return true;
        }
        std::error_code error;
        llvm::raw_fd_ostream out(options.output, error, llvm::sys::fs::OF_None);
        if (error) {
            llvm::errs() << "docs-digger: cannot write " << options.output << ": " << error.message() << '\n';
            return false;
        }
        writeExtraction(extraction, out);
        return true;
    }

    // Writes one set of definition files in style into dir; false after printing why it failed.
    bool writeLuaStyle(dd::LuaFileSettings settings, dd::LuaStyle style, const std::string& dir,
                       const dd::Extraction& extraction) {
        settings.style = style;
        if (llvm::Error error = dd::writeLuaFiles(dd::luaFiles(extraction, settings), dir)) {
            llvm::errs() << "docs-digger: " << llvm::toString(std::move(error)) << '\n';
            return false;
        }
        return true;
    }

    // Writes the Lua definition files of --lua-out and --emmylua-out; false after printing why it failed.
    bool writeLua(const Options& options, const dd::Extraction& extraction) {
        if (options.luaOut.empty() && options.emmyluaOut.empty()) {
            return true;
        }
        dd::LuaFileSettings settings;
        settings.docUrl = options.luaDocUrl;
        settings.maxFileSize = options.luaMaxFileSize;
        if (!options.luaPrelude.empty()) {
            llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> prelude =
                llvm::MemoryBuffer::getFile(options.luaPrelude, true);
            if (!prelude) {
                llvm::errs() << "docs-digger: cannot read " << options.luaPrelude << '\n';
                return false;
            }
            settings.prelude = (*prelude)->getBuffer().rtrim().str() + "\n";
        }
        return (options.luaOut.empty() || writeLuaStyle(settings, options.luaStyle, options.luaOut, extraction)) &&
            (options.emmyluaOut.empty() ||
             writeLuaStyle(settings, dd::LuaStyle::EmmyLua, options.emmyluaOut, extraction));
    }

    // The configuration files named on the command line; each is empty when its option is not given.
    struct Configs {
        // --groups rules.
        dd::GroupConfig groups;
        // --lua-types names.
        std::map<std::string, std::string> luaTypes;
        // --fragments texts by name (file name without `.md`).
        std::map<std::string, std::string> fragments;
    };

    // True when error holds none; otherwise prints it.
    bool succeeded(llvm::Error error) {
        if (!error) {
            return true;
        }
        llvm::errs() << "docs-digger: " << llvm::toString(std::move(error)) << '\n';
        return false;
    }

    // Adds the `<name>.md` files of directory to fragments by name; an error when it cannot be listed or a file read.
    llvm::Error loadFragments(const std::string& directory, std::map<std::string, std::string>& fragments) {
        std::error_code error;
        for (llvm::sys::fs::directory_iterator it(directory, error), end; it != end && !error; it.increment(error)) {
            if (llvm::sys::path::extension(it->path()) != ".md") {
                continue;
            }
            llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(it->path());
            if (!buffer) {
                return llvm::createStringError(buffer.getError(), "cannot read " + it->path());
            }
            fragments.emplace(llvm::sys::path::stem(it->path()).str(), (*buffer)->getBuffer().str());
        }
        return error ? llvm::createStringError(error, "cannot list " + directory) : llvm::Error::success();
    }

    // Reads the configuration files; nullopt after printing why one cannot be used.
    std::optional<Configs> loadConfigs(const Options& options) {
        Configs configs;
        if (!options.groups.empty()) {
            llvm::Expected<dd::GroupConfig> groups = dd::loadGroups(options.groups);
            if (!groups) {
                succeeded(groups.takeError());
                return std::nullopt;
            }
            configs.groups = std::move(*groups);
        }
        if (!options.luaTypes.empty() && !succeeded(dd::loadLuaTypes(options.luaTypes, configs.luaTypes))) {
            return std::nullopt;
        }
        if (!options.fragments.empty() && !succeeded(loadFragments(options.fragments, configs.fragments))) {
            return std::nullopt;
        }
        return configs;
    }

    // Runs the steps that need every translation unit; false when the runtime enum file cannot be read.
    bool merge(const Options& options, const Configs& configs, dd::Extraction& extraction) {
        dd::resolveAliases(extraction);
        dd::attachVtableHooks(extraction);
        dd::attachOwnedBindings(extraction);
        if (!options.runtimeEnums.empty() && !dd::addRuntimeEnums(extraction, options.runtimeEnums)) {
            llvm::errs() << "docs-digger: cannot read " << options.runtimeEnums << '\n';
            return false;
        }
        dd::attachBlockDocs(extraction);
        dd::dropEmptyEnums(extraction);
        dd::markFlagEnums(extraction);
        if (const auto fragment = configs.fragments.find("bit_flags"); fragment != configs.fragments.end()) {
            dd::addFlagUsage(extraction, fragment->second);
        }
        dd::attachCallbacks(extraction);
        dd::buildEvents(extraction);
        dd::checkProse(extraction);
        dd::keepUsedAliases(extraction);
        dd::collectGlobalVariables(extraction);
        dd::assignGroups(extraction, configs.groups);
        dd::assignLuaNames(extraction, dd::luaNaming(extraction, configs.luaTypes));
        return true;
    }

    // The cache named by --cache, or nullptr (none given, or it cannot be opened: then everything is parsed).
    std::unique_ptr<dd::ResultCache> openCache(const Options& options) {
        if (options.cache.empty()) {
            return nullptr;
        }
        std::string error;
        std::unique_ptr<dd::ResultCache> cache = dd::ResultCache::open(options.cache, error);
        if (cache == nullptr) {
            llvm::errs() << "docs-digger: cannot open the cache " << options.cache << ": " << error << '\n';
        }
        return cache;
    }

    // What runAll needs from the options, without the cache.
    dd::RunSettings runSettings(const Options& options, std::vector<std::string> sources, dd::FileHashes& hashes,
                                const char* argv0) {
        dd::RunSettings settings;
        settings.sources = std::move(sources);
        settings.extraArgs.assign(options.extraArgs.begin(), options.extraArgs.end());
        settings.root = options.root;
        settings.readCache = !options.noCache;
        // The executable's own hash: a rebuilt digger never takes the results of an older one.
        static int anchor = 0;
        const std::string executable = llvm::sys::fs::getMainExecutable(argv0, &anchor);
        settings.version = hashes.hash(executable).value_or(executable);
        settings.jobs =
            options.jobs != 0 ? options.jobs : llvm::heavyweight_hardware_concurrency().compute_thread_count();
        return settings;
    }

    // Parses the selected TUs of database and writes what the passes found; returns the process exit code.
    int dig(const Options& options, const clang::tooling::CompilationDatabase& database, const char* argv0) {
        std::vector<std::string> sources = selectSources(options, database);
        if (sources.empty()) {
            llvm::errs() << "docs-digger: no translation units selected\n";
            return 1;
        }
        const std::optional<Configs> configs = loadConfigs(options);
        if (!configs) {
            return 1;
        }
        dd::FileHashes hashes;
        dd::RunSettings settings = runSettings(options, std::move(sources), hashes, argv0);
        const std::unique_ptr<dd::ResultCache> cache = openCache(options);
        settings.cache = cache.get();
        dd::Extraction extraction;
        const unsigned failed = dd::runAll(settings, database, hashes, extraction);
        const std::size_t total = settings.sources.size();
        llvm::errs() << "docs-digger: merging the results of " << total << " translation units\n";
        if (!merge(options, *configs, extraction)) {
            return 1;
        }
        if (failed != 0) {
            llvm::errs() << "docs-digger: " << failed << " of " << total << " translation units had errors\n";
        }
        const bool written = writeOutput(options, extraction) && writeLua(options, extraction);
        return written && failed == 0 ? 0 : 1;
    }

} // namespace

int main(int argc, const char** argv) {
    Options options;
    // Takes `-- <flags>` off the command line before the options are parsed.
    std::string fixedError;
    std::unique_ptr<clang::tooling::CompilationDatabase> fixed =
        clang::tooling::FixedCompilationDatabase::loadFromCommandLine(argc, argv, fixedError);
    llvm::cl::HideUnrelatedOptions(options.category);
    if (!fixedError.empty() || !llvm::cl::ParseCommandLineOptions(argc, argv, "Extracts overlunky's Lua API\n")) {
        llvm::errs() << fixedError;
        return 1;
    }
    const std::unique_ptr<clang::tooling::CompilationDatabase> database = loadDatabase(options, std::move(fixed));
    return database ? dig(options, *database, argv[0]) : 1;
}
