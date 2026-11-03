#include "codec.hpp"

#include <llvm/Support/FormatVariadic.h>
#include <llvm/Support/JSON.h>

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace dd {

    namespace {

        // One member of a model struct and its JSON key.
        template <typename Owner, typename Type>
        struct Field {
            // JSON key.
            const char* name;
            // The member.
            Type Owner::* member;
        };

        // The fields of a model struct, in declaration order (specialized per struct below). A field missing here
        // is lost in the cache; the golden tests run every fixture through the cache to catch that.
        template <typename T>
        struct Fields;

        // Model structs with a field list.
        template <typename T>
        concept Described = requires { Fields<T>::list; };

        // Shorthand for the field lists.
        template <typename Owner, typename Type>
        constexpr Field<Owner, Type> field(const char* name, Type Owner::* member) {
            return {name, member};
        }

        template <>
        struct Fields<SourceLoc> {
            static constexpr auto list = std::make_tuple(
                field("file", &SourceLoc::file), field("line", &SourceLoc::line), field("column", &SourceLoc::column));
        };
        template <>
        struct Fields<Doc> {
            static constexpr auto list =
                std::make_tuple(field("lines", &Doc::lines), field("nodoc", &Doc::nodoc),
                                field("deprecated", &Doc::deprecated), field("group", &Doc::group));
        };
        template <>
        struct Fields<TypeInfo> {
            static constexpr auto list =
                std::make_tuple(field("spelling", &TypeInfo::spelling), field("canonical", &TypeInfo::canonical),
                                field("kind", &TypeInfo::kind), field("inner", &TypeInfo::inner),
                                field("returns", &TypeInfo::returns), field("nullable", &TypeInfo::nullable),
                                field("hidden", &TypeInfo::hidden), field("lua", &TypeInfo::lua));
        };
        template <>
        struct Fields<Param> {
            static constexpr auto list = std::make_tuple(
                field("name", &Param::name), field("type", &Param::type), field("omittable", &Param::omittable),
                field("binder", &Param::binder), field("callback", &Param::callback), field("key", &Param::key));
        };
        template <>
        struct Fields<CallbackSig> {
            static constexpr auto list = std::make_tuple(
                field("params", &CallbackSig::params), field("returns", &CallbackSig::returns),
                field("nilAllowed", &CallbackSig::nilAllowed), field("nilMeaning", &CallbackSig::nilMeaning),
                field("origin", &CallbackSig::origin), field("storedIn", &CallbackSig::storedIn),
                field("selector", &CallbackSig::selector), field("unresolved", &CallbackSig::unresolved),
                field("prose", &CallbackSig::prose), field("conflicts", &CallbackSig::conflicts),
                field("loc", &CallbackSig::loc), field("forwardedTo", &CallbackSig::forwardedTo),
                field("forwardedParam", &CallbackSig::forwardedParam));
        };
        template <>
        struct Fields<FunctionSig> {
            static constexpr auto list =
                std::make_tuple(field("params", &FunctionSig::params), field("returns", &FunctionSig::returns),
                                field("cppTarget", &FunctionSig::cppTarget), field("doc", &FunctionSig::doc),
                                field("self", &FunctionSig::self));
        };
        template <>
        struct Fields<Function> {
            static constexpr auto list = std::make_tuple(
                field("owner", &Function::owner), field("luaName", &Function::luaName), field("root", &Function::root),
                field("loc", &Function::loc), field("doc", &Function::doc), field("overloads", &Function::overloads),
                field("docOnly", &Function::docOnly), field("definedInLua", &Function::definedInLua),
                field("aliasOf", &Function::aliasOf), field("group", &Function::group));
        };
        template <>
        struct Fields<Value> {
            static constexpr auto list =
                std::make_tuple(field("owner", &Value::owner), field("luaName", &Value::luaName),
                                field("root", &Value::root), field("loc", &Value::loc), field("doc", &Value::doc),
                                field("type", &Value::type), field("docOnly", &Value::docOnly));
        };
        template <>
        struct Fields<Member> {
            static constexpr auto list = std::make_tuple(
                field("luaName", &Member::luaName), field("kind", &Member::kind), field("meta", &Member::meta),
                field("loc", &Member::loc), field("doc", &Member::doc), field("declDoc", &Member::declDoc),
                field("type", &Member::type), field("overloads", &Member::overloads), field("getter", &Member::getter),
                field("setter", &Member::setter));
        };
        template <>
        struct Fields<MemberKey> {
            static constexpr auto list =
                std::make_tuple(field("luaName", &MemberKey::luaName), field("loc", &MemberKey::loc));
        };
        template <>
        struct Fields<Usertype> {
            static constexpr auto list = std::make_tuple(
                field("luaName", &Usertype::luaName), field("cppType", &Usertype::cppType),
                field("loc", &Usertype::loc), field("doc", &Usertype::doc), field("declDoc", &Usertype::declDoc),
                field("bases", &Usertype::bases), field("constructors", &Usertype::constructors),
                field("noConstructor", &Usertype::noConstructor), field("members", &Usertype::members),
                field("group", &Usertype::group), field("section", &Usertype::section));
        };
        template <>
        struct Fields<EnumValue> {
            static constexpr auto list = std::make_tuple(
                field("luaName", &EnumValue::luaName), field("text", &EnumValue::text),
                field("number", &EnumValue::number), field("string", &EnumValue::string), field("doc", &EnumValue::doc),
                field("origin", &EnumValue::origin), field("loc", &EnumValue::loc));
        };
        template <>
        struct Fields<Enum> {
            static constexpr auto list =
                std::make_tuple(field("luaName", &Enum::luaName), field("loc", &Enum::loc), field("doc", &Enum::doc),
                                field("values", &Enum::values), field("filledAtRuntime", &Enum::filledAtRuntime));
        };
        template <>
        struct Fields<Alias> {
            static constexpr auto list = std::make_tuple(field("name", &Alias::name), field("type", &Alias::type),
                                                         field("loc", &Alias::loc), field("doc", &Alias::doc));
        };
        template <>
        struct Fields<LibraryCall> {
            static constexpr auto list =
                std::make_tuple(field("names", &LibraryCall::names), field("function", &LibraryCall::function),
                                field("loc", &LibraryCall::loc));
        };
        template <>
        struct Fields<ScriptCallback> {
            static constexpr auto list =
                std::make_tuple(field("luaName", &ScriptCallback::luaName), field("type", &ScriptCallback::type),
                                field("loc", &ScriptCallback::loc), field("doc", &ScriptCallback::doc));
        };
        template <>
        struct Fields<BlockDoc> {
            static constexpr auto list =
                std::make_tuple(field("target", &BlockDoc::target), field("lines", &BlockDoc::lines));
        };
        template <>
        struct Fields<Event> {
            static constexpr auto list = std::make_tuple(
                field("table", &Event::table), field("luaName", &Event::luaName), field("storedIn", &Event::storedIn),
                field("callback", &Event::callback), field("generic", &Event::generic), field("doc", &Event::doc));
        };
        template <>
        struct Fields<DispatchSite> {
            static constexpr auto list = std::make_tuple(
                field("storage", &DispatchSite::storage), field("signature", &DispatchSite::signature),
                field("constant", &DispatchSite::constant), field("selectorFunction", &DispatchSite::selectorFunction),
                field("selectorParam", &DispatchSite::selectorParam), field("container", &DispatchSite::container),
                field("loop", &DispatchSite::loop));
        };
        template <>
        struct Fields<SelectorEdge> {
            static constexpr auto list =
                std::make_tuple(field("callee", &SelectorEdge::callee), field("param", &SelectorEdge::param),
                                field("constant", &SelectorEdge::constant), field("caller", &SelectorEdge::caller),
                                field("callerParam", &SelectorEdge::callerParam));
        };
        template <>
        struct Fields<CallbackFacts> {
            static constexpr auto list = std::make_tuple(
                field("flows", &CallbackFacts::flows), field("sites", &CallbackFacts::sites),
                field("edges", &CallbackFacts::edges), field("containerKeys", &CallbackFacts::containerKeys),
                field("storageSelectors", &CallbackFacts::storageSelectors),
                field("vtableHooks", &CallbackFacts::vtableHooks),
                field("consumedBindings", &CallbackFacts::consumedBindings));
        };
        template <>
        struct Fields<BindingKey> {
            static constexpr auto list =
                std::make_tuple(field("owner", &BindingKey::owner), field("luaName", &BindingKey::luaName),
                                field("loc", &BindingKey::loc));
        };
        template <>
        struct Fields<Extraction> {
            static constexpr auto list = std::make_tuple(
                field("functions", &Extraction::functions), field("values", &Extraction::values),
                field("usertypes", &Extraction::usertypes), field("enums", &Extraction::enums),
                field("aliases", &Extraction::aliases), field("libraries", &Extraction::libraries),
                field("scriptCallbacks", &Extraction::scriptCallbacks),
                field("globalVariables", &Extraction::globalVariables), field("events", &Extraction::events),
                field("callbackFacts", &Extraction::callbackFacts), field("blockDocs", &Extraction::blockDocs));
        };

        // Integers other than bool (bool has its own overload).
        template <typename T>
        concept Integer = std::integral<T> && !std::same_as<T, bool>;

        // ---- encode: declarations first, so the templates find each other for std containers of model types ----

        llvm::json::Value encode(const std::string& value);
        llvm::json::Value encode(bool value);
        template <Integer T>
        llvm::json::Value encode(T value);
        template <typename T>
            requires std::is_enum_v<T>
        llvm::json::Value encode(T value);
        template <typename T>
        llvm::json::Value encode(const std::optional<T>& value);
        template <typename T>
        llvm::json::Value encode(const std::vector<T>& values);
        template <typename T>
        llvm::json::Value encode(const std::set<T>& values);
        template <typename K, typename V>
        llvm::json::Value encode(const std::map<K, V>& values);
        template <typename A, typename B>
        llvm::json::Value encode(const std::pair<A, B>& value);
        template <typename... T>
        llvm::json::Value encode(const std::tuple<T...>& value);
        template <Described T>
        llvm::json::Value encode(const T& value);

        llvm::json::Value encode(const std::string& value) { return value; }

        llvm::json::Value encode(bool value) { return value; }

        template <Integer T>
        llvm::json::Value encode(T value) {
            return static_cast<std::int64_t>(value);
        }

        template <typename T>
            requires std::is_enum_v<T>
        llvm::json::Value encode(T value) {
            return static_cast<std::int64_t>(value);
        }

        template <typename T>
        llvm::json::Value encode(const std::optional<T>& value) {
            return value ? encode(*value) : llvm::json::Value(nullptr);
        }

        // An array of the encoded elements of a range.
        template <typename Range>
        llvm::json::Value encodeRange(const Range& values) {
            llvm::json::Array array;
            for (const auto& value : values) {
                array.push_back(encode(value));
            }
            return array;
        }

        template <typename T>
        llvm::json::Value encode(const std::vector<T>& values) {
            return encodeRange(values);
        }

        template <typename T>
        llvm::json::Value encode(const std::set<T>& values) {
            return encodeRange(values);
        }

        template <typename K, typename V>
        llvm::json::Value encode(const std::map<K, V>& values) {
            return encodeRange(values); // [key, value] pairs, so keys need not be strings
        }

        template <typename A, typename B>
        llvm::json::Value encode(const std::pair<A, B>& value) {
            return llvm::json::Array{encode(value.first), encode(value.second)};
        }

        template <typename... T>
        llvm::json::Value encode(const std::tuple<T...>& value) {
            return std::apply([](const auto&... elements) { return llvm::json::Array{encode(elements)...}; }, value);
        }

        template <Described T>
        llvm::json::Value encode(const T& value) {
            llvm::json::Object object;
            std::apply([&](const auto&... fields) { ((object[fields.name] = encode(value.*(fields.member))), ...); },
                       Fields<T>::list);
            return object;
        }

        // ---- decode: false when the JSON does not have the expected shape ----

        bool decode(const llvm::json::Value& json, std::string& out);
        bool decode(const llvm::json::Value& json, bool& out);
        template <Integer T>
        bool decode(const llvm::json::Value& json, T& out);
        template <typename T>
            requires std::is_enum_v<T>
        bool decode(const llvm::json::Value& json, T& out);
        template <typename T>
        bool decode(const llvm::json::Value& json, std::optional<T>& out);
        template <typename T>
        bool decode(const llvm::json::Value& json, std::vector<T>& out);
        template <typename T>
        bool decode(const llvm::json::Value& json, std::set<T>& out);
        template <typename K, typename V>
        bool decode(const llvm::json::Value& json, std::map<K, V>& out);
        template <typename A, typename B>
        bool decode(const llvm::json::Value& json, std::pair<A, B>& out);
        template <typename... T>
        bool decode(const llvm::json::Value& json, std::tuple<T...>& out);
        template <Described T>
        bool decode(const llvm::json::Value& json, T& out);

        bool decode(const llvm::json::Value& json, std::string& out) {
            const std::optional<llvm::StringRef> text = json.getAsString();
            if (text) {
                out = text->str();
            }
            return text.has_value();
        }

        bool decode(const llvm::json::Value& json, bool& out) {
            const std::optional<bool> value = json.getAsBoolean();
            out = value.value_or(false);
            return value.has_value();
        }

        template <Integer T>
        bool decode(const llvm::json::Value& json, T& out) {
            const std::optional<std::int64_t> value = json.getAsInteger();
            out = static_cast<T>(value.value_or(0));
            return value.has_value();
        }

        template <typename T>
            requires std::is_enum_v<T>
        bool decode(const llvm::json::Value& json, T& out) {
            const std::optional<std::int64_t> value = json.getAsInteger();
            out = static_cast<T>(value.value_or(0));
            return value.has_value();
        }

        template <typename T>
        bool decode(const llvm::json::Value& json, std::optional<T>& out) {
            if (json.kind() == llvm::json::Value::Null) {
                out.reset();
                return true;
            }
            return decode(json, out.emplace());
        }

        template <typename T>
        bool decode(const llvm::json::Value& json, std::vector<T>& out) {
            const llvm::json::Array* array = json.getAsArray();
            if (array == nullptr) {
                return false;
            }
            out.resize(array->size());
            for (std::size_t i = 0; i < array->size(); ++i) {
                if (!decode((*array)[i], out[i])) {
                    return false;
                }
            }
            return true;
        }

        // Decodes the elements of an array and inserts them into a set or map.
        template <typename Element, typename Container>
        bool decodeInto(const llvm::json::Value& json, Container& out) {
            std::vector<Element> elements;
            if (!decode(json, elements)) {
                return false;
            }
            for (Element& element : elements) {
                out.insert(std::move(element));
            }
            return true;
        }

        template <typename T>
        bool decode(const llvm::json::Value& json, std::set<T>& out) {
            return decodeInto<T>(json, out);
        }

        template <typename K, typename V>
        bool decode(const llvm::json::Value& json, std::map<K, V>& out) {
            return decodeInto<std::pair<K, V>>(json, out);
        }

        template <typename A, typename B>
        bool decode(const llvm::json::Value& json, std::pair<A, B>& out) {
            const llvm::json::Array* array = json.getAsArray();
            return array != nullptr && array->size() == 2 && decode((*array)[0], out.first) &&
                decode((*array)[1], out.second);
        }

        template <typename... T>
        bool decode(const llvm::json::Value& json, std::tuple<T...>& out) {
            const llvm::json::Array* array = json.getAsArray();
            if (array == nullptr || array->size() != sizeof...(T)) {
                return false;
            }
            return std::apply(
                [&](auto&... elements) {
                    std::size_t i = 0;
                    return (decode((*array)[i++], elements) && ...);
                },
                out);
        }

        template <Described T>
        bool decode(const llvm::json::Value& json, T& out) {
            const llvm::json::Object* object = json.getAsObject();
            if (object == nullptr) {
                return false;
            }
            return std::apply(
                [&](const auto&... fields) {
                    return ([&](const auto& f) {
                        const llvm::json::Value* value = object->get(f.name);
                        return value != nullptr && decode(*value, out.*(f.member));
                    }(fields) &&
                            ...);
                },
                Fields<T>::list);
        }

    } // namespace

    std::string encodeExtraction(const Extraction& extraction) {
        return llvm::formatv("{0}", encode(extraction)).str();
    }

    bool decodeExtraction(llvm::StringRef text, Extraction& out) {
        llvm::Expected<llvm::json::Value> json = llvm::json::parse(text);
        if (!json) {
            llvm::consumeError(json.takeError());
            return false;
        }
        return decode(*json, out);
    }

} // namespace dd
