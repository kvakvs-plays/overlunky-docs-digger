#pragma once

#include <llvm/ADT/StringRef.h>

#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

// The per-translation-unit result cache (plan DIG-4b): a SQLite file that maps each parsed source to its partial
// extraction, valid while the files it included keep their content.
namespace dd {

    // Hex xxh3 hash of text, for cache keys.
    std::string hashText(llvm::StringRef text);

    // Content hashes of files, each file read once per run; shared by the parsing threads.
    class FileHashes {
    public:
        // The hash of the file's content, or nothing when it cannot be read.
        std::optional<std::string> hash(const std::string& path);

    private:
        // Guards hashes_.
        std::mutex mutex_;
        // Hashes by path.
        std::map<std::string, std::optional<std::string>> hashes_;
    };

    // Identity of a cache row.
    struct UnitKey {
        // The translation unit's source file.
        std::string source;
        // Hash of what else decides its result: digger build, root, compile command.
        std::string key;
    };

    // The cache file. Lookups and stores may come from several threads; one connection serves them in turn.
    class ResultCache {
    public:
        // Opens or creates the cache at path; nullptr after writing the reason to error.
        static std::unique_ptr<ResultCache> open(const std::string& path, std::string& error);


        // The stored result of the unit when its key matches and every file it included still has the same content.
        std::optional<std::string> lookup(const UnitKey& unit, FileHashes& hashes);

        // Stores the result of the unit with the files it included (hashed now); replaces an older row.
        void store(const UnitKey& unit, const std::vector<std::string>& dependencies, FileHashes& hashes,
                   const std::string& result);

    private:
        // Closes the connection when the cache goes away.
        struct CloseDatabase {
            // sqlite3_close.
            void operator()(sqlite3* database) const;
        };

        // Takes over an open connection.
        explicit ResultCache(sqlite3* database);

        // The connection.
        std::unique_ptr<sqlite3, CloseDatabase> database_;
        // Serializes the use of the connection.
        std::mutex mutex_;
    };

} // namespace dd
