#include "cache.hpp"

#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/xxhash.h>

#include <sqlite3.h>

#include <algorithm>

namespace dd {

    namespace {

        // Finalizes a prepared statement.
        struct FinalizeStatement {
            // sqlite3_finalize.
            void operator()(sqlite3_stmt* statement) const { sqlite3_finalize(statement); }
        };

        // A prepared statement, finalized when it goes out of scope.
        using Statement = std::unique_ptr<sqlite3_stmt, FinalizeStatement>;

        // One row per source: the key it was parsed under (digger version, root, compile command), the files it
        // included with their content hashes, and its partial extraction.
        constexpr const char* Schema = "CREATE TABLE IF NOT EXISTS translation_units ("
                                       "source TEXT PRIMARY KEY, key TEXT NOT NULL, "
                                       "dependencies TEXT NOT NULL, result TEXT NOT NULL)";

        // Prepares sql; a null statement when it fails (the cache then misses or skips the store).
        Statement prepare(sqlite3* database, const char* sql) {
            sqlite3_stmt* statement = nullptr;
            sqlite3_prepare_v2(database, sql, -1, &statement, nullptr);
            return Statement(statement);
        }

        // Binds text to the 1-based parameter index (copied, so the string may go away).
        void bindText(sqlite3_stmt* statement, int index, llvm::StringRef text) {
            sqlite3_bind_text(statement, index, text.data(), static_cast<int>(text.size()), SQLITE_TRANSIENT);
        }

        // The text of a result column.
        std::string column(sqlite3_stmt* statement, int index) {
            const auto* text = reinterpret_cast<const char*>(sqlite3_column_text(statement, index));
            return text == nullptr ? std::string() : std::string(text, sqlite3_column_bytes(statement, index));
        }

        // True when a stored `[path, hash]` pair still has that hash.
        bool sameHash(const llvm::json::Value& file, FileHashes& hashes) {
            const llvm::json::Array* pair = file.getAsArray();
            const std::optional<llvm::StringRef> path =
                pair == nullptr || pair->size() != 2 ? std::nullopt : (*pair)[0].getAsString();
            const std::optional<llvm::StringRef> hash = path ? (*pair)[1].getAsString() : std::nullopt;
            return hash && hashes.hash(path->str()) == hash->str();
        }

        // True when every `[path, hash]` of the stored dependency list still has that hash.
        bool unchanged(llvm::StringRef dependencies, FileHashes& hashes) {
            llvm::Expected<llvm::json::Value> json = llvm::json::parse(dependencies);
            if (!json) {
                llvm::consumeError(json.takeError());
                return false;
            }
            const llvm::json::Array* files = json->getAsArray();
            if (files == nullptr) {
                return false;
            }
            return std::ranges::all_of(*files, [&](const llvm::json::Value& file) { return sameHash(file, hashes); });
        }

    } // namespace

    std::string hashText(llvm::StringRef text) {
        return llvm::utohexstr(llvm::xxh3_64bits(llvm::arrayRefFromStringRef(text)));
    }

    std::optional<std::string> FileHashes::hash(const std::string& path) {
        {
            const std::lock_guard lock(mutex_);
            if (const auto found = hashes_.find(path); found != hashes_.end()) {
                return found->second;
            }
        }
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path, false, false);
        const std::optional<std::string> hashed =
            buffer ? std::optional(hashText((*buffer)->getBuffer())) : std::nullopt;
        const std::lock_guard lock(mutex_);
        return hashes_.emplace(path, hashed).first->second;
    }

    void ResultCache::CloseDatabase::operator()(sqlite3* database) const { sqlite3_close(database); }

    ResultCache::ResultCache(sqlite3* database) : database_(database) {}

    std::unique_ptr<ResultCache> ResultCache::open(const std::string& path, std::string& error) {
        sqlite3* database = nullptr;
        const int opened = sqlite3_open(path.c_str(), &database);
        std::unique_ptr<ResultCache> cache(new ResultCache(database)); // closes the connection on every path
        if (opened != SQLITE_OK || sqlite3_exec(database, Schema, nullptr, nullptr, nullptr) != SQLITE_OK) {
            error = sqlite3_errmsg(database);
            return nullptr;
        }
        sqlite3_exec(database, "PRAGMA journal_mode=WAL; PRAGMA synchronous=NORMAL", nullptr, nullptr, nullptr);
        return cache;
    }

    std::optional<std::string> ResultCache::lookup(const UnitKey& unit, FileHashes& hashes) {
        std::string dependencies;
        std::string result;
        {
            const std::lock_guard lock(mutex_);
            const Statement select = prepare(
                database_.get(), "SELECT dependencies, result FROM translation_units WHERE source = ? AND key = ?");
            if (select == nullptr) {
                return std::nullopt;
            }
            bindText(select.get(), 1, unit.source);
            bindText(select.get(), 2, unit.key);
            if (sqlite3_step(select.get()) != SQLITE_ROW) {
                return std::nullopt;
            }
            dependencies = column(select.get(), 0);
            result = column(select.get(), 1);
        }
        return unchanged(dependencies, hashes) ? std::optional(std::move(result)) : std::nullopt;
    }

    void ResultCache::store(const UnitKey& unit, const std::vector<std::string>& dependencies, FileHashes& hashes,
                            const std::string& result) {
        llvm::json::Array files;
        for (const std::string& path : dependencies) {
            const std::optional<std::string> hash = hashes.hash(path);
            if (!hash) {
                return; // a file that cannot be read cannot be checked later: do not cache
            }
            files.push_back(llvm::json::Array{path, *hash});
        }
        const std::string dependencyText = llvm::formatv("{0}", llvm::json::Value(std::move(files))).str();
        const std::lock_guard lock(mutex_);
        const Statement insert =
            prepare(database_.get(), "INSERT OR REPLACE INTO translation_units VALUES (?, ?, ?, ?)");
        if (insert == nullptr) {
            return;
        }
        bindText(insert.get(), 1, unit.source);
        bindText(insert.get(), 2, unit.key);
        bindText(insert.get(), 3, dependencyText);
        bindText(insert.get(), 4, result);
        sqlite3_step(insert.get());
    }

} // namespace dd
