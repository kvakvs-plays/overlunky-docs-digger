#include "runner.hpp"

#include "bindings.hpp"
#include "callbacks.hpp"
#include "codec.hpp"
#include "merge.hpp"
#include "source_root.hpp"

#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/Tooling/ArgumentsAdjusters.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/Path.h>
#include <llvm/Support/ThreadPool.h>
#include <llvm/Support/Threading.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

namespace dd {

    namespace {

        // Clang's builtin headers (stddef.h, intrinsics) for the same version as the linked libraries.
        constexpr const char* ResourceDirFlag = "-resource-dir=" DOCS_DIGGER_RESOURCE_DIR;

        // How often the units still being parsed are reported.
        constexpr std::chrono::seconds HeartbeatInterval{10};

        // A clock for parse times.
        using Clock = std::chrono::steady_clock;

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
        clang::tooling::CommandLineArguments
        dropPrecompiledHeaders(const clang::tooling::CommandLineArguments& arguments, llvm::StringRef /*file*/) {
            clang::tooling::CommandLineArguments result;
            for (const std::string& argument : arguments) {
                if (!isPrecompiledHeaderArgument(argument)) {
                    result.push_back(argument);
                }
            }
            return result;
        }

        // One translation unit's result.
        struct Unit {
            // What its passes found.
            Extraction extraction;
            // Every file it read.
            std::vector<std::string> files;
            // Clang reported no errors.
            bool parsed = false;
        };

        // Parses source with its own passes, so units can run in parallel and be cached one by one.
        Unit parseUnit(const RunSettings& settings, const clang::tooling::CompilationDatabase& database,
                       const std::string& source) {
            using clang::tooling::ArgumentInsertPosition;
            Unit unit;
            const SourceRoot root(settings.root);
            TableAssignmentCollector collector(root, unit.extraction);
            CallbackCollector callbacks(root, unit.extraction.callbackFacts);
            clang::ast_matchers::MatchFinder finder;
            collector.registerMatchers(finder);
            callbacks.registerMatchers(finder);
            const std::unique_ptr<clang::tooling::FrontendActionFactory> factory =
                clang::tooling::newFrontendActionFactory(&finder);
            clang::tooling::ClangTool tool(database, {source});
            tool.appendArgumentsAdjuster(dropPrecompiledHeaders);
            // -w: warnings in the parsed code are not ours to fix.
            // overlunky is built with Clang 17; two of its dependencies fail with this newer Clang:
            // - sol2 3.3.0 optional<T&>::emplace calls a member that does not exist: delayed template parsing only
            //   parses template bodies that get instantiated, and this one never is;
            // - fmt's FMT_STRING subtracts pointers into a string literal during constant evaluation, which the
            //   classic evaluator now rejects (CWG2765); the new constant interpreter accepts it.
            tool.appendArgumentsAdjuster(clang::tooling::getInsertArgumentAdjuster(
                {"-w", ResourceDirFlag, "-Xclang", "-fdelayed-template-parsing", "-Xclang",
                 "-fexperimental-new-constant-interpreter"},
                ArgumentInsertPosition::BEGIN));
            tool.appendArgumentsAdjuster(
                clang::tooling::getInsertArgumentAdjuster(settings.extraArgs, ArgumentInsertPosition::END));
            unit.parsed = tool.run(factory.get()) == 0;
            unit.files = collector.files();
            return unit;
        }

        // The cache key of source: this digger build, the root, and the compile command with the extra arguments.
        std::string unitKey(const RunSettings& settings, const clang::tooling::CompilationDatabase& database,
                            const std::string& source) {
            std::string text = settings.version + '\n' + settings.root + '\n';
            for (const clang::tooling::CompileCommand& command : database.getCompileCommands(source)) {
                text += command.Directory + '\n' + llvm::join(command.CommandLine, "\n") + '\n';
            }
            return hashText(text + llvm::join(settings.extraArgs, "\n"));
        }

        // Progress lines from all parsing threads: one per finished unit, and every HeartbeatInterval the units
        // still being parsed, so a run never stays silent for long.
        class Progress {
        public:
            // Reports on total units.
            explicit Progress(std::size_t total) : total_(total), thread_([this] { heartbeat(); }) {}
            Progress(const Progress&) = delete;
            Progress& operator=(const Progress&) = delete;
            Progress(Progress&&) = delete;
            Progress& operator=(Progress&&) = delete;
            ~Progress() {
                {
                    const std::lock_guard lock(mutex_);
                    done_ = true;
                }
                wake_.notify_one();
                thread_.join();
            }

            // A unit starts parsing.
            void started(const std::string& source) {
                const std::lock_guard lock(mutex_);
                active_.emplace(source, Clock::now());
            }

            // A unit is finished: status is `cached` (it never started), its parse time, or that time with `errors`.
            void finished(const std::string& source, bool parsed) {
                const std::lock_guard lock(mutex_);
                std::string status = "cached";
                if (const auto found = active_.find(source); found != active_.end()) {
                    const std::chrono::duration<double> elapsed = Clock::now() - found->second;
                    status = llvm::formatv("{0:f1} s{1}", elapsed.count(), parsed ? "" : ", errors").str();
                    active_.erase(found);
                }
                llvm::errs() << llvm::formatv("docs-digger: [{0}/{1}] {2,-8} {3}\n", ++finished_, total_, status,
                                              source);
            }

        private:
            // Thread body: every HeartbeatInterval until done_, how many units are being parsed and the longest ones.
            void heartbeat() {
                std::unique_lock lock(mutex_);
                while (!wake_.wait_for(lock, HeartbeatInterval, [this] { return done_; })) {
                    if (!active_.empty()) {
                        llvm::errs() << llvm::formatv("docs-digger:   still parsing {0} of the {1} left: {2}\n",
                                                      active_.size(), total_ - finished_, longestRunning());
                    }
                }
            }

            // The units parsing the longest (at most MaxListed), with their time so far.
            std::string longestRunning() const {
                constexpr std::size_t MaxListed = 4;
                std::vector<std::pair<Clock::time_point, std::string>> running;
                running.reserve(active_.size());
                for (const auto& [source, start] : active_) {
                    running.emplace_back(start, llvm::sys::path::filename(source).str());
                }
                std::ranges::sort(running);
                running.resize(std::min(running.size(), MaxListed));
                std::vector<std::string> listed;
                for (const auto& [start, name] : running) {
                    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(Clock::now() - start);
                    listed.push_back(llvm::formatv("{0} ({1} s)", name, seconds.count()).str());
                }
                return llvm::join(listed, ", ") + (active_.size() > MaxListed ? ", ..." : "");
            }

            // Number of units.
            std::size_t total_;
            // Units finished so far.
            std::size_t finished_ = 0;
            // Units being parsed, with their start time.
            std::map<std::string, Clock::time_point> active_;
            // Guards the counters, active_ and done_, and keeps lines whole.
            std::mutex mutex_;
            // Wakes the heartbeat when the run ends.
            std::condition_variable wake_;
            // Set when the run ends.
            bool done_ = false;
            // The heartbeat thread; declared last so the members it uses exist before it starts.
            std::thread thread_;
        };

        // The result of one source: from the cache when valid, else parsed (and cached when free of errors).
        Unit loadOrParse(const RunSettings& settings, const clang::tooling::CompilationDatabase& database,
                         FileHashes& hashes, Progress& progress, const std::string& source) {
            const UnitKey key{source, settings.cache == nullptr ? std::string() : unitKey(settings, database, source)};
            if (settings.cache != nullptr && settings.readCache) {
                Unit cached;
                const std::optional<std::string> stored = settings.cache->lookup(key, hashes);
                if (stored && decodeExtraction(*stored, cached.extraction)) {
                    cached.parsed = true;
                    progress.finished(source, true);
                    return cached;
                }
            }
            progress.started(source);
            Unit unit = parseUnit(settings, database, source);
            if (settings.cache != nullptr && unit.parsed) {
                settings.cache->store(key, unit.files, hashes, encodeExtraction(unit.extraction));
            }
            progress.finished(source, unit.parsed);
            return unit;
        }

    } // namespace

    unsigned runAll(const RunSettings& settings, const clang::tooling::CompilationDatabase& database,
                    FileHashes& hashes, Extraction& out) {
        std::vector<Unit> units(settings.sources.size());
        const llvm::ThreadPoolStrategy strategy = llvm::hardware_concurrency(settings.jobs);
        llvm::errs() << llvm::formatv("docs-digger: {0} translation units, {1} parsed at a time{2}\n",
                                      settings.sources.size(), strategy.compute_thread_count(),
                                      settings.cache == nullptr ? "" : ", unchanged ones from the cache");
        {
            Progress progress(settings.sources.size());
            llvm::DefaultThreadPool pool(strategy);
            for (std::size_t i = 0; i < settings.sources.size(); ++i) {
                pool.async(
                    [&, i] { units[i] = loadOrParse(settings, database, hashes, progress, settings.sources[i]); });
            }
            pool.wait();
        }
        unsigned failed = 0;
        for (Unit& unit : units) {
            failed += unit.parsed ? 0 : 1;
            mergePartial(out, std::move(unit.extraction));
        }
        return failed;
    }

} // namespace dd
