#include "merge.hpp"

#include <llvm/ADT/SmallVector.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/MemoryBuffer.h>

#include <map>
#include <set>
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

        // Adds the names in the parameter and return types of signatures.
        void addNames(const std::vector<FunctionSig>& signatures, std::set<std::string>& names) {
            for (const FunctionSig& sig : signatures) {
                for (const Param& param : sig.params) {
                    addNames(param.type, names);
                }
                for (const TypeInfo& returned : sig.returns) {
                    addNames(returned, names);
                }
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
            return names;
        }

    } // namespace

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
