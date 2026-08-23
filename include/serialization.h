#pragma once

#include "core/audio_source.h"
#include "nlohmann/json_fwd.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <type_traits>
#include <concepts>

namespace waves {

using json = nlohmann::json;

// forward declaration
class PluginManager;

struct ProjectContext {
    std::map<std::string, AudioSourcePtr> media_map;
    PluginManager *plugin_manager;
};

// templated type magic

template <typename T, typename... Args>
inline constexpr bool is_any_of_v = (std::is_same_v<T, Args> || ...);

template <typename T>
concept is_default_serializible = is_any_of_v<T,
    std::string, bool, float, double, int, int64_t, unsigned, uint64_t>;

class ProjectWriter;
class ProjectReader;

template <typename T>
concept HasSerialize = requires (T obj, ProjectWriter output) {
    obj.serialize(output);
};

template <typename T>
concept HasDeserCtor = requires (ProjectReader input) {
    T(input);
};

template <typename T>
concept HasDeserialize = requires (T obj, ProjectReader input) {
    obj.deserialize(input);
};

template <typename T>
concept IsVector = requires {
    typename T::value_type;
    requires std::same_as<T, std::vector<typename T::value_type>>;
};


// Abstraction layer to not depend on json everywhere
class ProjectWriter {
public:
    ProjectWriter(json& obj): obj_(obj) {}
    // nested object
    ProjectWriter(ProjectWriter other, std::string_view name);

    // ==== WRITE DIRECTLY ====
    // regular types: string, int, float
    template<typename T>
    void write(T val) requires is_default_serializible<T>;

    // types with serialize method
    template<typename T>
    void write(const T& val) requires HasSerialize<T> {
        val.serialize(*this);
    }

    // Int cast for enums
    template<typename T>
    void write(T val) requires std::is_enum_v<T> {
        write(static_cast<int>(val));
    }

    // Create object with key name
    ProjectWriter nest(std::string_view name) {
        return ProjectWriter(*this, name);
    }

    // name : val
    template<typename T>
    void write(std::string_view name, const T& val) {
        if constexpr (IsVector<T>) {
            array(name).write(val);
        } else {
            nest(name).write(val);
        }
    }

    // Create array obj with key name
    ProjectWriter array(std::string_view name);
    ProjectWriter push_back();

    template <typename VecT>
    void write(const VecT& val) requires IsVector<VecT> {
        for (const typename VecT::value_type &elem: val) {
            push_back().write(elem);
        }
    }
    
private:
    std::reference_wrapper<json> obj_;
};

// write to output with same name as variable name
#define SERIALIZE_SIMPLE(output, var) output.write(#var, var)

class ProjectReader {
public:
    ProjectReader(const json& obj, ProjectContext& context): 
        obj_(obj), context_(context) {}

    template <typename ValueT>
    std::optional<ValueT> read() requires is_default_serializible<ValueT>;

    template <typename ValueT>
    std::optional<ValueT> read() requires std::is_enum_v<ValueT> {
        std::optional<int> v = read<int>();
        return (v) ? std::optional<ValueT>(static_cast<ValueT>(*v)) : std::nullopt;
    }

    template <typename ValueT>
    std::optional<ValueT> read(std::string_view name) {
        if (auto nested = nest(name)) {
            return nested->read<ValueT>();
        } else 
            return std::nullopt;
    }

    template <typename ValueT>
    ValueT read(std::string_view name, ValueT default_val) {
        std::optional<ValueT> opt_val = read<ValueT>(name);
        if (opt_val) {
            return std::move(*opt_val); 
        } else {
            return std::move(default_val);
        }
    }

    template<typename ValueT>
    bool read_to(ValueT& val) {
        if constexpr (is_default_serializible<ValueT> || std::is_enum_v<ValueT>) {
            if (auto val_opt = read<ValueT>()) {
                val = std::move(*val_opt);
                return true;
            } else {
                return false;
            }
        } else if constexpr (HasDeserialize<ValueT>) {
            val.deserialize(*this);
            return true;
        } else if constexpr (IsVector<ValueT>) {
            int64_t cnt = arr_size();
            if (cnt < 0) return false;
            using vt = typename ValueT::value_type;
            if constexpr (HasDeserCtor<vt>) {
                val.clear();
                for (std::size_t idx = 0; idx < cnt; idx++) {
                    val.push_back(vt(read_array(idx)));
                }
            } else {
                val.resize(cnt);
                for (std::size_t idx = 0; idx < cnt; idx++) {
                    vt elem;
                    read_array(idx).read_to(elem);
                    val[idx] = std::move(elem);
                }
            }
            return true;
        }
        return false;
    }

    template<typename ValueT>
    void read_to(ValueT& val, ValueT dflt) {
        if (!read_to(val)) {
            val = std::move(dflt);
        }
    }

    template<typename ValueT>
    bool read_to(std::string_view name, ValueT& val) {
        if (auto nested = nest(name)) {
            return nested->read_to(val);
        } else {
            return false;
        }
    }

    template<typename ValueT>
    void read_to(std::string_view name, ValueT& val, ValueT dflt) {
        if (!read_to(name, val))
            val = std::move(dflt);
    }

    // @return array size or negative number if not array
    int64_t arr_size();
    // unchecked acess
    ProjectReader read_array(std::size_t idx);

    std::optional<ProjectReader> nest(std::string_view name);

    ProjectContext& getCtx() {
        return context_;
    }
private:
    std::reference_wrapper<const json> obj_;
    std::reference_wrapper<ProjectContext> context_;
};


#define DESERIALIZE_OPT(input, var) \
    input.read_to<decltype(var)>(#var, var)

#define DESERIALIZE_SIMPLE(input, var, dflt) \
    input.read_to<decltype(var)>(#var, var, dflt)


#define _GET_MACRO(_1, _2, _3, _4, _5, _6, _7, NAME, ...) NAME

#define FOR_EACH(MACRO, ...) \
    _GET_MACRO(__VA_ARGS__, _FOR_EACH_7, _FOR_EACH_6, _FOR_EACH_5, _FOR_EACH_4, _FOR_EACH_3, _FOR_EACH_2, _FOR_EACH_1)(MACRO, __VA_ARGS__)

#define _FOR_EACH_1(M, x)      M(x);
#define _FOR_EACH_2(M, x, ...) M(x); _FOR_EACH_1(M, __VA_ARGS__)
#define _FOR_EACH_3(M, x, ...) M(x); _FOR_EACH_2(M, __VA_ARGS__)
#define _FOR_EACH_4(M, x, ...) M(x); _FOR_EACH_3(M, __VA_ARGS__)
#define _FOR_EACH_5(M, x, ...) M(x); _FOR_EACH_4(M, __VA_ARGS__)
#define _FOR_EACH_6(M, x, ...) M(x); _FOR_EACH_5(M, __VA_ARGS__)
#define _FOR_EACH_7(M, x, ...) M(x); _FOR_EACH_6(M, __VA_ARGS__)


#define _SERIALIZE_MACRO(arg) SERIALIZE_SIMPLE(output, arg)
#define _DESERIALIZE_MACRO(arg) DESERIALIZE_OPT(input, arg)


#define DEFINE_SIMPLE_SERDE(...)    \
void serialize(ProjectWriter output) const {        \
    FOR_EACH(_SERIALIZE_MACRO, __VA_ARGS__)         \
}                                                   \
void deserialize(ProjectReader input) {             \
    FOR_EACH(_DESERIALIZE_MACRO, __VA_ARGS__)       \
}

#define DEFINE_SIMPLE_SERDE_OUTLINE(cls, ...) \
void cls::serialize(ProjectWriter output) const {   \
    FOR_EACH(_SERIALIZE_MACRO, __VA_ARGS__)         \
}                                                   \
void cls::deserialize(ProjectReader input) {        \
    FOR_EACH(_DESERIALIZE_MACRO, __VA_ARGS__)       \
}

}