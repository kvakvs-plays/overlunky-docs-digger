#include "comments.hpp"
#include "merge.hpp"

#include <llvm/ADT/StringExtras.h>
#include <llvm/ADT/StringRef.h>

#include <algorithm>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

// The callback steps after all translation units (plan C1, C3, C4, C5, C7): joins the facts of src/callbacks.cpp
// with the bound signatures. No Clang AST here.
namespace dd {

    namespace {

        // Dispatch sites of one storage, by the key that selects them.
        struct SiteGroups {
            // Sites per enum constant (`ON::FRAME`).
            std::map<std::string, std::vector<const DispatchSite*>> keyed;
            // Sites no key selects: the generic dispatch.
            std::vector<const DispatchSite*> generic;
        };

        // A function parameter that receives enum keys.
        using Selector = std::pair<std::string, unsigned>;

        // Adds the constants passed to (function, param), following callers that forward their own parameter.
        void addConstants(const Selector& selector, const CallbackFacts& facts, std::set<Selector>& visited,
                          std::set<std::string>& out) {
            if (!visited.insert(selector).second) {
                return;
            }
            for (auto it = facts.edges.lower_bound(SelectorEdge{selector.first, selector.second, {}, {}, 0});
                 it != facts.edges.end() && it->callee == selector.first && it->param == selector.second; ++it) {
                if (!it->constant.empty()) {
                    out.insert(it->constant);
                }
                else {
                    addConstants({it->caller, it->callerParam}, facts, visited, out);
                }
            }
        }

        // The keys of a site; nothing for a site of the generic dispatch (no guard, or a container written without
        // a key). A keyed site whose keys are not found gets an empty set.
        std::optional<std::set<std::string>> siteKeys(const DispatchSite& site, const CallbackFacts& facts) {
            std::set<std::string> keys;
            if (!site.constant.empty()) {
                keys.insert(site.constant);
                return keys;
            }
            if (!site.selectorFunction.empty()) {
                std::set<Selector> visited;
                addConstants({site.selectorFunction, site.selectorParam}, facts, visited, keys);
                return keys;
            }
            for (const auto& [container, constant] : facts.containerKeys) {
                if (container == site.container) {
                    keys.insert(constant);
                }
            }
            return keys.empty() ? std::nullopt : std::optional(keys);
        }

        // Adds site to group under its keys, or as generic.
        void addSite(SiteGroups& group, const DispatchSite& site, const CallbackFacts& facts) {
            const std::optional<std::set<std::string>> keys = siteKeys(site, facts);
            if (!keys) {
                group.generic.push_back(&site);
                return;
            }
            for (const std::string& constant : *keys) {
                group.keyed[constant].push_back(&site);
            }
        }

        // All dispatch sites grouped by storage (`Record::field`) and key; with loops, also by `Record::field in
        // Container` for sites inside a range-for over a container, which is how stored callbacks of one record type
        // in different containers are told apart.
        std::map<std::string, SiteGroups> groupSites(const CallbackFacts& facts, bool withLoops) {
            std::map<std::string, SiteGroups> groups;
            for (const auto& [key, site] : facts.sites) {
                addSite(groups[site.storage], site, facts);
                if (withLoops && !site.loop.empty()) {
                    addSite(groups[site.storage + " in " + site.loop], site, facts);
                }
            }
            return groups;
        }

        // A type for comparisons: the resolved type when known, else the spelling.
        const std::string& comparable(const TypeInfo& type) {
            return type.canonical.empty() ? type.spelling : type.canonical;
        }

        // `(T a, U b) -> R`: a signature for messages.
        std::string signatureText(const CallbackSig& sig) {
            std::vector<std::string> params;
            params.reserve(sig.params.size());
            for (const Param& param : sig.params) {
                params.push_back(param.name.empty() ? param.type.spelling : param.type.spelling + " " + param.name);
            }
            std::vector<std::string> returns;
            returns.reserve(sig.returns.size());
            for (const TypeInfo& type : sig.returns) {
                returns.push_back(type.spelling);
            }
            return "(" + llvm::join(params, ", ") + ") -> " + (returns.empty() ? "nil" : llvm::join(returns, ", "));
        }

        // True when two sites pass the same types and read the same result.
        bool sameSignature(const CallbackSig& a, const CallbackSig& b) {
            const auto sameType = [](const TypeInfo& x, const TypeInfo& y) { return comparable(x) == comparable(y); };
            const auto sameParam = [&](const Param& x, const Param& y) { return sameType(x.type, y.type); };
            return std::ranges::equal(a.params, b.params, sameParam) &&
                std::ranges::equal(a.returns, b.returns, sameType);
        }

        // `file:line`, for messages.
        std::string where(const SourceLoc& loc) { return loc.file + ":" + std::to_string(loc.line); }

        // Several sites of one storage or key: the first one's signature, differing ones as conflicts.
        CallbackSig mergeSites(const std::vector<const DispatchSite*>& sites, CallbackOrigin origin) {
            CallbackSig merged = sites.front()->signature;
            merged.origin = origin;
            merged.storedIn = sites.front()->storage;
            for (const DispatchSite* site : sites) {
                if (!sameSignature(site->signature, merged)) {
                    merged.conflicts.push_back(where(site->signature.loc) + " passes " +
                                               signatureText(site->signature) + ", " + where(merged.loc) + " passes " +
                                               signatureText(merged));
                }
            }
            return merged;
        }

        // An Unresolved callback with the reason, keeping the storage.
        CallbackSig unresolvedCallback(std::string reason, std::string storedIn) {
            CallbackSig sig;
            sig.unresolved = std::move(reason);
            sig.storedIn = std::move(storedIn);
            return sig;
        }

        // The callback a stored one gets from the sites reading its storage.
        CallbackSig storedCallback(CallbackSig flow, const std::map<std::string, SiteGroups>& groups) {
            auto group = groups.find(flow.storedIn);
            if (group == groups.end()) {
                group = groups.find(llvm::StringRef(flow.storedIn).split(" in ").first.str());
            }
            if (group == groups.end()) {
                return unresolvedCallback("stored in " + flow.storedIn + ", which no handle_function call reads",
                                          flow.storedIn);
            }
            if (!group->second.keyed.empty()) {
                flow.origin = CallbackOrigin::EventDispatch;
                flow.selector = llvm::StringRef(group->second.keyed.begin()->first).split("::").first.str();
                return flow;
            }
            if (group->second.generic.empty()) {
                return unresolvedCallback("stored in " + flow.storedIn + ", read only under keys that were not found",
                                          flow.storedIn);
            }
            CallbackSig merged = mergeSites(group->second.generic, CallbackOrigin::StoredHandleFunction);
            merged.storedIn = std::move(flow.storedIn);
            return merged;
        }

        // True for std::function types, whose signature is in the type (C1).
        bool isStdFunction(const TypeInfo& type) {
            return type.kind == TypeKind::Function && llvm::StringRef(comparable(type)).starts_with("std::function<");
        }

        // C1: the callback a std::function<R(A...)> parameter receives, with sol2's conversion of the result.
        CallbackSig stdFunctionCallback(const TypeInfo& type) {
            CallbackSig sig;
            sig.origin = CallbackOrigin::StdFunction;
            for (const TypeInfo& inner : type.inner) {
                sig.params.emplace_back().type = inner;
            }
            sig.returns = type.returns;
            const TypeInfo* result = sig.returns.empty() ? nullptr : &sig.returns.front();
            sig.nilAllowed = result == nullptr || result->kind == TypeKind::Bool || result->nullable;
            if (result == nullptr) {
                sig.nilMeaning = "ignored";
            }
            else if (result->kind == TypeKind::Bool) {
                sig.nilMeaning = "treated as false";
            }
            else {
                sig.nilMeaning = result->nullable ? "passed on as empty"
                                                  : "not allowed: sol2 converts the result to " + result->spelling;
            }
            return sig;
        }

        // Gives param its callback from its type or from the flow of its function.
        void attachCallback(Param& param, const CallbackFacts& facts, const std::map<std::string, SiteGroups>& groups) {
            if (!param.callback.empty()) {
                return;
            }
            if (isStdFunction(param.type)) {
                param.callback = {stdFunctionCallback(param.type)};
                return;
            }
            const auto flow = facts.flows.find(param.key);
            if (flow == facts.flows.end()) {
                return;
            }
            const bool stored = flow->second.origin == CallbackOrigin::StoredHandleFunction;
            param.callback = {stored ? storedCallback(flow->second, groups) : flow->second};
        }

        // Calls visit for every signature of usertype: constructors and member accessors and overloads.
        template <typename Visit>
        void forEachUsertypeSignature(Usertype& usertype, const Visit& visit) {
            std::ranges::for_each(usertype.constructors, visit);
            for (auto& [key, member] : usertype.members) {
                std::ranges::for_each(member.overloads, visit);
                std::ranges::for_each(member.getter, visit);
                std::ranges::for_each(member.setter, visit);
            }
        }

        // Calls visit for every function signature in extraction.
        template <typename Visit>
        void forEachSignature(Extraction& extraction, const Visit& visit) {
            for (auto& [key, function] : extraction.functions) {
                std::ranges::for_each(function.overloads, visit);
            }
            for (auto& [key, usertype] : extraction.usertypes) {
                forEachUsertypeSignature(usertype, visit);
            }
        }

        // The signatures of every Lua function by key path: functions by owner and name, members by usertype name.
        std::map<std::vector<std::string>, std::vector<FunctionSig>*> signaturesByPath(Extraction& extraction) {
            std::map<std::vector<std::string>, std::vector<FunctionSig>*> byPath;
            for (auto& [key, function] : extraction.functions) {
                std::vector<std::string> path = function.owner;
                path.push_back(function.luaName);
                byPath.emplace(std::move(path), &function.overloads);
            }
            for (auto& [key, usertype] : extraction.usertypes) {
                for (auto& [memberKey, member] : usertype.members) {
                    byPath.emplace(std::vector<std::string>{usertype.luaName, member.luaName}, &member.overloads);
                }
            }
            return byPath;
        }

        // A forwarded callback (`lua["Entity"]["set_pre_x"](ent, fun)`) copied from the target's parameter.
        void resolveForward(Param& param, const std::map<std::vector<std::string>, std::vector<FunctionSig>*>& byPath) {
            if (param.callback.empty() || param.callback.front().forwardedTo.empty() ||
                param.callback.front().origin != CallbackOrigin::Unresolved) {
                return;
            }
            const CallbackSig& forwarded = param.callback.front();
            const auto target = byPath.find(forwarded.forwardedTo);
            if (target == byPath.end()) {
                return;
            }
            for (const FunctionSig& sig : *target->second) {
                const Param* receiver =
                    forwarded.forwardedParam < sig.params.size() ? &sig.params[forwarded.forwardedParam] : nullptr;
                if (receiver != nullptr && !receiver->callback.empty() &&
                    receiver->callback.front().origin != CallbackOrigin::Unresolved) {
                    param.callback = receiver->callback;
                    return;
                }
            }
        }

        // The docs of the enum value an event is keyed by, or empty.
        Doc valueDoc(const Extraction& extraction, const Event& event) {
            const auto found = extraction.enums.find(event.table);
            if (found == extraction.enums.end()) {
                return {};
            }
            for (const auto& [key, value] : found->second.values) {
                if (value.luaName == event.luaName && !value.doc.lines.empty()) {
                    return value.doc;
                }
            }
            return {};
        }

        // Adds an event (its table, name and storage set) with its callback and the docs of its enum value.
        void addEvent(Extraction& extraction, Event event, CallbackSig callback, bool generic) {
            event.callback = std::move(callback);
            event.generic = generic;
            event.doc = valueDoc(extraction, event);
            auto key = std::tuple(event.table, event.luaName, event.storedIn);
            extraction.events.emplace(std::move(key), std::move(event));
        }

        // An event without callback for constant (`ON::FRAME`) of storage.
        Event eventFor(llvm::StringRef constant, const std::string& storage) {
            const auto [table, name] = constant.split("::");
            return Event{table.str(), name.str(), storage, {}, false, {}};
        }

        // The Lua tables whose every value is a key of storage: enums of its keys that its binding takes.
        std::set<std::string> keyTables(const std::string& storage, const SiteGroups& group,
                                        const CallbackFacts& facts) {
            std::set<std::string> tables;
            const auto selectors = facts.storageSelectors.find(storage);
            for (const auto& [constant, sites] : group.keyed) {
                const std::string table = llvm::StringRef(constant).split("::").first.str();
                if (selectors != facts.storageSelectors.end() && selectors->second.contains(table)) {
                    tables.insert(table);
                }
            }
            return tables;
        }

        // Gives every value of table without an event of storage the generic signature, or an unresolved one.
        void fillTable(Extraction& extraction, const std::string& storage, const std::string& table,
                       const SiteGroups& group) {
            const auto found = extraction.enums.find(table);
            if (found == extraction.enums.end()) {
                return;
            }
            std::set<std::string> names;
            for (const auto& [key, value] : found->second.values) {
                names.insert(value.luaName);
            }
            for (const std::string& name : names) {
                if (name.empty() || extraction.events.contains(std::tuple(table, name, storage))) {
                    continue;
                }
                CallbackSig callback = group.generic.empty()
                    ? unresolvedCallback("no dispatch site for this key", storage)
                    : mergeSites(group.generic, CallbackOrigin::EventDispatch);
                addEvent(extraction, Event{table, name, storage, {}, false, {}}, std::move(callback),
                         !group.generic.empty());
            }
        }

        // The disagreements between a callback found in the code and the prose; checkReturn also reports a result
        // the code reads but the prose leaves out.
        std::vector<std::string> proseConflicts(const CallbackSig& code, const CallbackSig& prose, bool checkReturn) {
            std::vector<std::string> conflicts;
            if (code.params.size() != prose.params.size()) {
                conflicts.push_back("the doc lists " + std::to_string(prose.params.size()) +
                                    " parameters, the code passes " + std::to_string(code.params.size()) + ": " +
                                    signatureText(code));
            }
            if (!code.returns.empty() && prose.returns.empty() && checkReturn) {
                conflicts.push_back("the doc returns nil, the code reads " + signatureText(code));
            }
            if (code.returns.empty() && !prose.returns.empty()) {
                conflicts.push_back("the doc returns " + prose.returns.front().spelling +
                                    ", the code ignores the result: " + signatureText(code));
            }
            return conflicts;
        }

        // The callback parameter of sig that prose about name describes: the one of that name, else the only one.
        Param* proseTarget(FunctionSig& sig, const std::string& name) {
            Param* only = nullptr;
            int count = 0;
            for (Param& param : sig.params) {
                if (param.callback.empty()) {
                    continue;
                }
                if (param.name == name) {
                    return &param;
                }
                only = &param;
                ++count;
            }
            return count == 1 ? only : nullptr;
        }

        // C7 for one parameter: prose fills an unresolved callback, or is compared with the extracted one.
        void applyProse(Param& param, const ProseCallback& prose) {
            CallbackSig& code = param.callback.front();
            if (code.origin == CallbackOrigin::EventDispatch) {
                return;
            }
            if (code.origin == CallbackOrigin::Unresolved) {
                std::string reason = std::move(code.unresolved);
                code = prose.callback;
                code.unresolved = std::move(reason);
                return;
            }
            code.prose = prose.callback.prose;
            for (std::string& conflict : proseConflicts(code, prose.callback, true)) {
                if (std::ranges::find(code.conflicts, conflict) == code.conflicts.end()) {
                    code.conflicts.push_back(std::move(conflict));
                }
            }
        }

        // C7 for overloads, with the prose of the binding's docs and of each signature's C++ declaration.
        void applyProse(std::vector<FunctionSig>& overloads, const std::vector<std::string>& docLines) {
            for (FunctionSig& sig : overloads) {
                std::vector<ProseCallback> proses = proseCallbacks(docLines);
                std::ranges::move(proseCallbacks(sig.doc.lines), std::back_inserter(proses));
                for (const ProseCallback& prose : proses) {
                    if (Param* param = proseTarget(sig, prose.name)) {
                        applyProse(*param, prose);
                    }
                }
            }
        }

        // The tables whose event docs use the `Params:` convention; only those are checked against the code.
        std::set<std::string> tablesWithParams(const Extraction& extraction) {
            std::set<std::string> tables;
            for (const auto& [key, event] : extraction.events) {
                if (std::ranges::any_of(event.doc.lines, [](const std::string& line) {
                        return llvm::StringRef(line).trim().starts_with("Params:");
                    })) {
                    tables.insert(event.table);
                }
            }
            return tables;
        }

        // C7 for an event: the `Params:` / `Return:` lines against its signature.
        void checkEventProse(Event& event) {
            if (event.callback.origin == CallbackOrigin::Unresolved) {
                return;
            }
            const CallbackSig prose = eventProse(event.doc.lines);
            event.callback.prose = prose.prose;
            for (std::string& conflict : proseConflicts(event.callback, prose, false)) {
                event.callback.conflicts.push_back(std::move(conflict));
            }
        }

    } // namespace

    void attachVtableHooks(Extraction& extraction) {
        std::map<std::string, Usertype*> byName;
        for (auto& [key, usertype] : extraction.usertypes) {
            byName.emplace(usertype.luaName, &usertype);
        }
        for (auto& [name, members] : extraction.callbackFacts.vtableHooks) {
            const auto usertype = byName.find(name);
            if (usertype == byName.end()) {
                continue;
            }
            for (Member& member : members) {
                usertype->second->members.emplace(MemberKey{member.luaName, member.loc}, std::move(member));
            }
        }
        const std::set<SourceLoc>& consumed = extraction.callbackFacts.consumedBindings;
        std::erase_if(extraction.functions, [&](const auto& entry) {
            return !entry.second.root.empty() && consumed.contains(entry.second.loc);
        });
    }

    void attachCallbacks(Extraction& extraction) {
        const CallbackFacts& facts = extraction.callbackFacts;
        const std::map<std::string, SiteGroups> groups = groupSites(facts, true);
        forEachSignature(extraction, [&](FunctionSig& sig) {
            for (Param& param : sig.params) {
                attachCallback(param, facts, groups);
            }
        });
        const auto byPath = signaturesByPath(extraction);
        forEachSignature(extraction, [&](FunctionSig& sig) {
            for (Param& param : sig.params) {
                resolveForward(param, byPath);
            }
        });
    }

    void buildEvents(Extraction& extraction) {
        const CallbackFacts& facts = extraction.callbackFacts;
        for (const auto& [storage, group] : groupSites(facts, false)) {
            for (const auto& [constant, sites] : group.keyed) {
                addEvent(extraction, eventFor(constant, storage), mergeSites(sites, CallbackOrigin::EventDispatch),
                         false);
            }
            for (const std::string& table : keyTables(storage, group, facts)) {
                fillTable(extraction, storage, table, group);
            }
        }
    }

    void checkProse(Extraction& extraction) {
        for (auto& [key, function] : extraction.functions) {
            applyProse(function.overloads, function.doc.lines);
        }
        for (auto& [key, usertype] : extraction.usertypes) {
            for (auto& [memberKey, member] : usertype.members) {
                std::vector<std::string> lines = member.doc.lines;
                lines.insert(lines.end(), member.declDoc.lines.begin(), member.declDoc.lines.end());
                applyProse(member.overloads, lines);
            }
        }
        const std::set<std::string> documented = tablesWithParams(extraction);
        for (auto& [key, event] : extraction.events) {
            if (documented.contains(event.table)) {
                checkEventProse(event);
            }
        }
    }

} // namespace dd
