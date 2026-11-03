#include "groups.hpp"

#include <llvm/ADT/STLExtras.h>
#include <llvm/ADT/StringExtras.h>
#include <llvm/Support/JSON.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/Path.h>

#include <algorithm>

namespace dd {

    // Reads one rule; called by llvm::json for the elements of `rules` (found by argument-dependent lookup).
    bool fromJSON(const llvm::json::Value& value, GroupRule& rule, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && map.map("group", rule.group) && map.mapOptional("name_contains", rule.nameContains) &&
            map.mapOptional("names", rule.names) && map.mapOptional("file_contains", rule.fileContains);
    }

    // Reads the whole groups file.
    bool fromJSON(const llvm::json::Value& value, GroupConfig& config, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && map.map("rules", config.rules) && map.mapOptional("fallback", config.fallback);
    }

    namespace {

        // True when text contains one of parts.
        bool containsAny(llvm::StringRef text, const std::vector<std::string>& parts) {
            return std::ranges::any_of(parts, [text](const std::string& part) { return text.contains(part); });
        }

        // True when one of rule's tests matches function.
        bool matches(const GroupRule& rule, const Function& function) {
            return containsAny(function.luaName, rule.nameContains) ||
                llvm::is_contained(rule.names, function.luaName) || containsAny(function.loc.file, rule.fileContains);
        }

        // The table a function is bound in (`TYPE_MAP`, `io`), as root text and owner keys; empty for globals.
        std::string ownerGroup(const Function& function) {
            std::vector<std::string> parts;
            if (!function.root.empty()) {
                parts.push_back(function.root);
            }
            parts.insert(parts.end(), function.owner.begin(), function.owner.end());
            return llvm::join(parts, ".");
        }

        // The group named after a source file: `script/usertypes/spawn_lua.cpp` → `spawn`.
        std::string fileGroup(llvm::StringRef file) {
            llvm::StringRef stem = llvm::sys::path::stem(file, llvm::sys::path::Style::posix);
            stem.consume_back("_lua");
            return stem.str();
        }

        // The group of one function, in the order assignGroups documents.
        std::string groupOf(const Function& function, const GroupConfig& config) {
            if (!function.doc.group.empty()) {
                return function.doc.group;
            }
            if (std::string owner = ownerGroup(function); !owner.empty()) {
                return owner;
            }
            const auto rule =
                std::ranges::find_if(config.rules, [&function](const GroupRule& r) { return matches(r, function); });
            if (rule != config.rules.end()) {
                return rule->group;
            }
            return config.fallback.empty() ? fileGroup(function.loc.file) : config.fallback;
        }

    } // namespace

    llvm::Expected<GroupConfig> loadGroups(const std::string& path) {
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path, true);
        if (!buffer) {
            return llvm::createStringError(buffer.getError(), "cannot read " + path);
        }
        llvm::Expected<llvm::json::Value> json = llvm::json::parse((*buffer)->getBuffer());
        if (!json) {
            return json.takeError();
        }
        GroupConfig config;
        llvm::json::Path::Root root(path);
        if (!fromJSON(*json, config, root)) {
            return root.getError();
        }
        return config;
    }

    void assignGroups(Extraction& extraction, const GroupConfig& config) {
        for (auto& [key, function] : extraction.functions) {
            function.group = groupOf(function, config);
        }
    }

} // namespace dd
