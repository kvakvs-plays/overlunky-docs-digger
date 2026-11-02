#include "bindings.hpp"
#include "source_root.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/Format.h>
#include <llvm/Support/Regex.h>
#include <llvm/Support/raw_ostream.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

namespace {

    // Clang's builtin headers (stddef.h, intrinsics) for the same version as the linked libraries.
    constexpr const char* ResourceDirFlag = "-resource-dir=" DOCS_DIGGER_RESOURCE_DIR;

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
            llvm::cl::value_desc("regex"), llvm::cl::init(R"([/\\]src[/\\]game_api[/\\]script[/\\])"),
            llvm::cl::cat(category)};

        // Flags a database lacks for parsing with this Clang, without editing the database.
        llvm::cl::list<std::string> extraArgs{"extra-arg",
                                              llvm::cl::desc("Argument to append to every compile command"),
                                              llvm::cl::value_desc("arg"), llvm::cl::cat(category)};

        // Per-TU parse times, to decide whether parallel execution is worth it.
        llvm::cl::opt<bool> timings{"timings", llvm::cl::desc("Print the parse time of every translation unit"),
                                    llvm::cl::cat(category)};
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

    // True for clang-cl precompiled-header flags: a configure-only database has no .pch files to use.
    bool isPrecompiledHeaderArgument(llvm::StringRef argument) {
        for (const llvm::StringRef prefix : {"/Yu", "-Yu", "/Yc", "-Yc", "/Fp", "-Fp"}) {
            if (argument.starts_with(prefix)) {
                return true;
            }
        }
        return false;
    }

    // Drops precompiled-header flags; the /FI of the generated PCH header stays, so its includes are still seen.
    clang::tooling::CommandLineArguments dropPrecompiledHeaders(const clang::tooling::CommandLineArguments& arguments,
                                                                llvm::StringRef /*file*/) {
        clang::tooling::CommandLineArguments result;
        for (const std::string& argument : arguments) {
            if (!isPrecompiledHeaderArgument(argument)) {
                result.push_back(argument);
            }
        }
        return result;
    }

    // Parses one translation unit; returns whether Clang reported no errors.
    bool runOne(const Options& options, const clang::tooling::CompilationDatabase& database, const std::string& source,
                clang::tooling::FrontendActionFactory& factory) {
        using clang::tooling::ArgumentInsertPosition;
        clang::tooling::ClangTool tool(database, {source});
        tool.appendArgumentsAdjuster(dropPrecompiledHeaders);
        // -w: warnings in the parsed code are not ours to fix.
        // overlunky is built with Clang 17; two of its dependencies fail with this newer Clang:
        // - sol2 3.3.0 optional<T&>::emplace calls a member that does not exist: delayed template parsing only
        //   parses template bodies that get instantiated, and this one never is;
        // - fmt's FMT_STRING subtracts pointers into a string literal during constant evaluation, which the
        //   classic evaluator now rejects (CWG2765); the new constant interpreter accepts it.
        tool.appendArgumentsAdjuster(
            clang::tooling::getInsertArgumentAdjuster({"-w", ResourceDirFlag, "-Xclang", "-fdelayed-template-parsing",
                                                       "-Xclang", "-fexperimental-new-constant-interpreter"},
                                                      ArgumentInsertPosition::BEGIN));
        const clang::tooling::CommandLineArguments extra(options.extraArgs.begin(), options.extraArgs.end());
        tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(extra, ArgumentInsertPosition::END));
        return tool.run(&factory) == 0;
    }

    // Parses every source; returns how many had errors.
    unsigned runAll(const Options& options, const clang::tooling::CompilationDatabase& database,
                    const std::vector<std::string>& sources, clang::tooling::FrontendActionFactory& factory) {
        unsigned failed = 0;
        for (const std::string& source : sources) {
            const auto start = std::chrono::steady_clock::now();
            if (!runOne(options, database, source, factory)) {
                ++failed;
            }
            if (options.timings) {
                const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - start;
                llvm::errs() << llvm::format("%8.2f s  ", elapsed.count()) << source << '\n';
            }
        }
        return failed;
    }

    // Writes the found key paths, one `file:line:column: path` per line.
    void writeBindings(const dd::TableAssignmentCollector& collector, llvm::raw_ostream& out) {
        for (const dd::KeyPathBinding& binding : collector.bindings()) {
            out << binding.loc.file << ':' << binding.loc.line << ':' << binding.loc.column << ": " << binding.path
                << '\n';
        }
    }

    // Writes to stdout or to the -o file; returns false when the file cannot be created.
    bool writeOutput(const Options& options, const dd::TableAssignmentCollector& collector) {
        if (options.output == "-") {
            writeBindings(collector, llvm::outs());
            return true;
        }
        std::error_code error;
        llvm::raw_fd_ostream out(options.output, error, llvm::sys::fs::OF_None);
        if (error) {
            llvm::errs() << "docs-digger: cannot write " << options.output << ": " << error.message() << '\n';
            return false;
        }
        writeBindings(collector, out);
        return true;
    }

    // Parses the selected TUs of database and writes what the passes found; returns the process exit code.
    int dig(const Options& options, const clang::tooling::CompilationDatabase& database) {
        const std::vector<std::string> sources = selectSources(options, database);
        if (sources.empty()) {
            llvm::errs() << "docs-digger: no translation units selected\n";
            return 1;
        }
        const dd::SourceRoot root(options.root);
        dd::TableAssignmentCollector collector(root);
        clang::ast_matchers::MatchFinder finder;
        collector.registerMatchers(finder);
        const std::unique_ptr<clang::tooling::FrontendActionFactory> factory =
            clang::tooling::newFrontendActionFactory(&finder);

        const unsigned failed = runAll(options, database, sources, *factory);
        if (failed != 0) {
            llvm::errs() << "docs-digger: " << failed << " of " << sources.size() << " translation units had errors\n";
        }
        return writeOutput(options, collector) && failed == 0 ? 0 : 1;
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
    return database ? dig(options, *database) : 1;
}
