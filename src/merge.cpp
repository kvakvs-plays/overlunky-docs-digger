#include "merge.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBuffer.h>

#include <algorithm>
#include <map>
#include <set>
#include <utility>
#include <vector>

namespace dd {

    namespace {

        // Docs of each key in one block: the lines after a line naming a key, up to the next key line.
        using KeyDocs = std::map<std::string, std::vector<std::string>>;

        // An enum value as the runtime file writes it: a number, or a string in quotes.
        EnumValue runtimeValue(llvm::StringRef key, llvm::StringRef text) {
            EnumValue value;
            value.luaName = key.str();
            value.text = text.str();
            value.origin = EnumValueOrigin::RuntimeFile;
            if (long long number = 0; !text.getAsInteger(0, number)) {
                value.number = number;
            }
            else if (text.size() >= 2 && text.starts_with("\"") && text.ends_with("\"")) {
                value.string = text.drop_front().drop_back().str();
            }
            return value;
        }

        // True when table has a value named name, from any origin.
        bool hasValue(const Enum& table, const std::string& name) {
            const auto it = table.values.lower_bound(MemberKey{name, {}});
            return it != table.values.end() && it->first.luaName == name;
        }

        // Adds one `KEY = value,` or `["KEY"] = value,` line unless the sources already have the key.
        void addRuntimeValue(Enum& table, llvm::StringRef line) {
            auto [key, text] = line.trim().rtrim(',').split(" = ");
            key = key.trim();
            if (key.consume_front("[\"")) {
                key.consume_back("\"]");
            }
            if (!hasValue(table, key.str())) {
                table.values.emplace(MemberKey{key.str(), {}}, runtimeValue(key, text.trim()));
            }
        }

        // The table a `NAME = {` line opens.
        Enum& runtimeTable(Extraction& extraction, llvm::StringRef line) {
            const std::string name = line.split(" = ").first.trim().str();
            Enum& table = extraction.enums[name];
            table.luaName = name;
            return table;
        }

        // The Lua names of entries (enum values or usertype members).
        template <typename Map>
        std::set<std::string> keysOf(const Map& entries) {
            std::set<std::string> keys;
            for (const auto& [key, entry] : entries) {
                keys.insert(entry.luaName);
            }
            return keys;
        }

        // Splits block into the docs of the keys it names; a key named twice keeps its last docs, as before.
        KeyDocs docsByKey(const BlockDoc& block, const std::set<std::string>& keys) {
            KeyDocs docs;
            const std::string* current = nullptr;
            for (const std::string& line : block.lines) {
                if (keys.contains(line)) {
                    current = &line;
                    docs[line].clear();
                }
                else if (current != nullptr) {
                    docs[*current].push_back(line);
                }
            }
            return docs;
        }

        // Gives every entry without docs the block's docs for its key.
        template <typename Map>
        void fillDocs(Map& entries, const BlockDoc& block) {
            const KeyDocs docs = docsByKey(block, keysOf(entries));
            for (auto& [key, entry] : entries) {
                const auto found = docs.find(entry.luaName);
                if (found != docs.end() && entry.doc.lines.empty()) {
                    entry.doc.lines = found->second;
                }
            }
        }

        // Adds the identifiers in spelling (`std::vector<ENT_TYPE>` → std, vector, ENT_TYPE) to names.
        void addNames(llvm::StringRef spelling, std::set<std::string>& names) {
            constexpr llvm::StringRef IdentifierChars =
                "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
            while (!spelling.empty()) {
                spelling = spelling.drop_until([&](char c) { return IdentifierChars.contains(c); });
                const llvm::StringRef name = spelling.take_while([&](char c) { return IdentifierChars.contains(c); });
                if (!name.empty()) {
                    names.insert(name.str());
                }
                spelling = spelling.drop_front(name.size());
            }
        }

        // Adds the names in a type and its inner and returned types.
        void addNames(const TypeInfo& type, std::set<std::string>& names) {
            addNames(type.spelling, names);
            for (const TypeInfo& inner : type.inner) {
                addNames(inner, names);
            }
            for (const TypeInfo& returned : type.returns) {
                addNames(returned, names);
            }
        }

        // Adds the names in the parameter types (with their callbacks) and the return types.
        void addNames(const std::vector<Param>& params, const std::vector<TypeInfo>& returns,
                      std::set<std::string>& names) {
            for (const Param& param : params) {
                addNames(param.type, names);
                for (const CallbackSig& callback : param.callback) {
                    addNames(callback.params, callback.returns, names);
                }
            }
            for (const TypeInfo& returned : returns) {
                addNames(returned, names);
            }
        }

        // Adds the names in the parameter and return types of signatures.
        void addNames(const std::vector<FunctionSig>& signatures, std::set<std::string>& names) {
            for (const FunctionSig& sig : signatures) {
                addNames(sig.params, sig.returns, names);
            }
        }

        // Adds the names in a usertype's constructors and members.
        void addNames(const Usertype& usertype, std::set<std::string>& names) {
            addNames(usertype.constructors, names);
            for (const auto& [key, member] : usertype.members) {
                addNames(member.type, names);
                addNames(member.overloads, names);
                addNames(member.getter, names);
                addNames(member.setter, names);
            }
        }

        // Every identifier in the types of compiled bindings: what Lua-visible types are written with.
        std::set<std::string> namesInUse(const Extraction& extraction) {
            std::set<std::string> names;
            for (const auto& [key, function] : extraction.functions) {
                addNames(function.overloads, names);
            }
            for (const auto& [key, value] : extraction.values) {
                addNames(value.type, names);
            }
            for (const auto& [key, usertype] : extraction.usertypes) {
                addNames(usertype, names);
            }
            for (const auto& [key, callback] : extraction.scriptCallbacks) {
                addNames(callback.type, names);
            }
            for (const auto& [key, event] : extraction.events) {
                addNames(event.callback.params, event.callback.returns, names);
            }
            return names;
        }

        // A usertype seen again: the header comes from the unit with the new_usertype call, members from all (the
        // ones seen first win).
        void mergeUsertype(Usertype& merged, Usertype&& part) {
            if (!merged.luaName.empty() || part.luaName.empty()) {
                merged.members.merge(part.members);
                return;
            }
            std::map<MemberKey, Member> earlier = std::move(merged.members);
            merged = std::move(part);
            earlier.merge(merged.members);
            merged.members = std::move(earlier);
        }

        // An enum seen again: the header comes from the unit that creates it, values from all (the ones seen first
        // win).
        void mergeEnum(Enum& merged, Enum&& part) {
            const bool filled = merged.filledAtRuntime || part.filledAtRuntime;
            if (!merged.loc.file.empty() || part.loc.file.empty()) {
                merged.values.merge(part.values);
            }
            else {
                std::map<MemberKey, EnumValue> earlier = std::move(merged.values);
                merged = std::move(part);
                earlier.merge(merged.values);
                merged.values = std::move(earlier);
            }
            merged.filledAtRuntime = filled;
        }

        // Merges map entries with merge for entries already present, keeping the order of first appearance.
        template <typename Map, typename MergeEntry>
        void mergeEach(Map& merged, Map&& part, const MergeEntry& mergeEntry) {
            for (auto& [key, entry] : part) {
                const auto [it, inserted] = merged.try_emplace(key);
                if (inserted) {
                    it->second = std::move(entry);
                }
                else {
                    mergeEntry(it->second, std::move(entry));
                }
            }
        }

        // Merges the callback facts of a unit (first fact per key wins, sets are joined).
        void mergeFacts(CallbackFacts& merged, CallbackFacts& part) {
            merged.flows.merge(part.flows);
            merged.sites.merge(part.sites);
            merged.edges.merge(part.edges);
            merged.containerKeys.merge(part.containerKeys);
            for (auto& [storage, selectors] : part.storageSelectors) {
                merged.storageSelectors[storage].merge(selectors);
            }
            merged.vtableHooks.merge(part.vtableHooks);
            merged.consumedBindings.merge(part.consumedBindings);
        }

        // True when two block docs are the same block (a header scanned by several units).
        bool sameBlock(const BlockDoc& a, const BlockDoc& b) { return a.target == b.target && a.lines == b.lines; }

    } // namespace

    void mergePartial(Extraction& merged, Extraction&& part) {
        merged.functions.merge(part.functions);
        merged.values.merge(part.values);
        mergeEach(merged.usertypes, std::move(part.usertypes), mergeUsertype);
        mergeEach(merged.enums, std::move(part.enums), mergeEnum);
        merged.aliases.merge(part.aliases);
        merged.libraries.merge(part.libraries);
        merged.scriptCallbacks.merge(part.scriptCallbacks);
        mergeFacts(merged.callbackFacts, part.callbackFacts);
        for (BlockDoc& block : part.blockDocs) {
            if (std::ranges::none_of(merged.blockDocs,
                                     [&](const BlockDoc& known) { return sameBlock(known, block); })) {
                merged.blockDocs.push_back(std::move(block));
            }
        }
    }

    bool addRuntimeEnums(Extraction& extraction, const std::string& path) {
        llvm::ErrorOr<std::unique_ptr<llvm::MemoryBuffer>> buffer = llvm::MemoryBuffer::getFile(path, true);
        if (!buffer) {
            return false;
        }
        llvm::SmallVector<llvm::StringRef, 0> lines;
        (*buffer)->getBuffer().split(lines, '\n');
        Enum* table = nullptr;
        for (const llvm::StringRef raw : lines) {
            const llvm::StringRef line = raw.rtrim();
            if (line.ends_with("{")) {
                table = &runtimeTable(extraction, line);
            }
            else if (line.ends_with("{}")) {
                runtimeTable(extraction, line);
            }
            else if (line == "}") {
                table = nullptr;
            }
            else if (table != nullptr && !line.trim().empty()) {
                addRuntimeValue(*table, line);
            }
        }
        return true;
    }

    void attachBlockDocs(Extraction& extraction) {
        std::map<std::string, Usertype*> usertypes;
        for (auto& [key, usertype] : extraction.usertypes) {
            usertypes.emplace(usertype.luaName, &usertype);
        }
        for (const BlockDoc& block : extraction.blockDocs) {
            if (const auto table = extraction.enums.find(block.target); table != extraction.enums.end()) {
                fillDocs(table->second.values, block);
            }
            else if (const auto usertype = usertypes.find(block.target); usertype != usertypes.end()) {
                fillDocs(usertype->second->members, block);
            }
        }
        extraction.blockDocs.clear();
    }

    void dropEmptyEnums(Extraction& extraction) {
        std::erase_if(extraction.enums,
                      [](const auto& entry) { return entry.second.values.empty() && !entry.second.filledAtRuntime; });
    }

    void keepUsedAliases(Extraction& extraction) {
        std::set<std::string> names = namesInUse(extraction);
        // An alias used only inside another kept alias (`using Callback = std::function<void(CallbackId)>`) counts.
        size_t known = 0;
        while (known != names.size()) {
            known = names.size();
            for (const auto& [name, alias] : extraction.aliases) {
                if (names.contains(name)) {
                    addNames(alias.type, names);
                }
            }
        }
        std::erase_if(extraction.aliases, [&names](const auto& entry) { return !names.contains(entry.first); });
    }

    void collectGlobalVariables(Extraction& extraction) {
        std::set<std::string> names;
        for (const auto& [key, value] : extraction.values) {
            const bool global = key.owner.empty() && value.root.empty() && !value.luaName.starts_with('[');
            if (global && !value.doc.lines.empty() && !value.doc.nodoc) {
                names.insert(value.luaName);
            }
        }
        extraction.globalVariables.assign(names.begin(), names.end());
    }

} // namespace dd
