#include "bindings.hpp"
#include "cache.hpp"
#include "merge.hpp"
#include "model.hpp"
#include "runner.hpp"
#include "usertypes.hpp"

#include <clang/Tooling/CompilationDatabase.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/Regex.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
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

    // Runs the steps that need every translation unit; false when the runtime enum file cannot be read.
    bool merge(const Options& options, dd::Extraction& extraction) {
        dd::resolveAliases(extraction);
        dd::attachVtableHooks(extraction);
        dd::attachOwnedBindings(extraction);
        if (!options.runtimeEnums.empty() && !dd::addRuntimeEnums(extraction, options.runtimeEnums)) {
            llvm::errs() << "docs-digger: cannot read " << options.runtimeEnums << '\n';
            return false;
        }
        dd::attachBlockDocs(extraction);
        dd::dropEmptyEnums(extraction);
        dd::attachCallbacks(extraction);
        dd::buildEvents(extraction);
        dd::checkProse(extraction);
        dd::keepUsedAliases(extraction);
        dd::collectGlobalVariables(extraction);
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
        dd::FileHashes hashes;
        dd::RunSettings settings = runSettings(options, std::move(sources), hashes, argv0);
        const std::unique_ptr<dd::ResultCache> cache = openCache(options);
        settings.cache = cache.get();
        dd::Extraction extraction;
        const unsigned failed = dd::runAll(settings, database, hashes, extraction);
        const std::size_t total = settings.sources.size();
        llvm::errs() << "docs-digger: merging the results of " << total << " translation units\n";
        if (!merge(options, extraction)) {
            return 1;
        }
        if (failed != 0) {
            llvm::errs() << "docs-digger: " << failed << " of " << total << " translation units had errors\n";
        }
        return writeOutput(options, extraction) && failed == 0 ? 0 : 1;
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
