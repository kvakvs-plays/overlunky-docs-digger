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

    // Reads a group and section pair: a type rule's place or the type fallback.
    bool fromJSON(const llvm::json::Value& value, TypePlace& place, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && map.map("group", place.group) && map.mapOptional("section", place.section);
    }

    // Reads one type rule.
    bool fromJSON(const llvm::json::Value& value, TypeRule& rule, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && fromJSON(value, rule.place, path) && map.mapOptional("requires_base", rule.requiresBase) &&
            map.mapOptional("bases_contain", rule.basesContain) &&
            map.mapOptional("name_contains", rule.nameContains) && map.mapOptional("names", rule.names);
    }

    // The `types` object of a groups file: rules and the fallback place.
    struct TypeSection {
        // Rules tried in order.
        std::vector<TypeRule> rules;
        // Place when no rule matches.
        TypePlace fallback;
    };

    // Reads the `types` object.
    bool fromJSON(const llvm::json::Value& value, TypeSection& types, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        return map && map.map("rules", types.rules) && map.mapOptional("fallback", types.fallback);
    }

    // Reads the whole groups file.
    bool fromJSON(const llvm::json::Value& value, GroupConfig& config, llvm::json::Path path) {
        llvm::json::ObjectMapper map(value, path);
        TypeSection types;
        if (!map || !map.map("rules", config.rules) || !map.mapOptional("fallback", config.fallback) ||
            !map.mapOptional("types", types)) {
            return false;
        }
        config.typeRules = std::move(types.rules);
        config.typeFallback = std::move(types.fallback);
        return true;
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

        // True when one of a usertype's C++ bases contains one of parts.
        bool baseContainsAny(const Usertype& usertype, const std::vector<std::string>& parts) {
            return std::ranges::any_of(usertype.bases,
                                       [&parts](const std::string& base) { return containsAny(base, parts); });
        }

        // True when rule applies to usertype: a required base is there (if any are named) and one test matches.
        bool matches(const TypeRule& rule, const Usertype& usertype) {
            if (!rule.requiresBase.empty() && !baseContainsAny(usertype, rule.requiresBase)) {
                return false;
            }
            return baseContainsAny(usertype, rule.basesContain) || containsAny(usertype.luaName, rule.nameContains) ||
                llvm::is_contained(rule.names, usertype.luaName);
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

        // The section of a group named by a `/// Group:` directive: that of the first rule with this group, else the
        // fallback's.
        std::string sectionOf(const std::string& group, const GroupConfig& config) {
            const auto rule =
                std::ranges::find_if(config.typeRules, [&group](const TypeRule& r) { return r.place.group == group; });
            return rule != config.typeRules.end() ? rule->place.section : config.typeFallback.section;
        }

        // The group and section of one usertype, in the order assignGroups documents.
        TypePlace placeOf(const Usertype& usertype, const GroupConfig& config) {
            if (!usertype.doc.group.empty()) {
                return {usertype.doc.group, sectionOf(usertype.doc.group, config)};
            }
            const auto rule =
                std::ranges::find_if(config.typeRules, [&usertype](const TypeRule& r) { return matches(r, usertype); });
            if (rule != config.typeRules.end()) {
                return rule->place;
            }
            if (config.typeFallback.group.empty()) {
                return {fileGroup(usertype.loc.file), config.typeFallback.section};
            }
            return config.typeFallback;
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
        for (auto& [cppType, usertype] : extraction.usertypes) {
            TypePlace place = placeOf(usertype, config);
            usertype.group = std::move(place.group);
            usertype.section = std::move(place.section);
        }
    }

} // namespace dd
