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
inline constexpr bool is_default_serializible = is_any_of_v<T,
    std::string, bool, float, double, int, int64_t, unsigned, uint64_t>;

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
    // regular types
    void write(std::string_view name, std::string val);
    void write(std::string_view name, bool val);
    void write(std::string_view name, float val);
    void write(std::string_view name, double val);
    void write(std::string_view name, int val);
    void write(std::string_view name, int64_t val);
    void write(std::string_view name, unsigned val);
    void write(std::string_view name, uint64_t val);
    ProjectWriter nest(std::string_view name) {
        return ProjectWriter(*this, name);
    }
    void array(std::string_view name);
    ProjectWriter push_back(std::string_view name);
    template <typename T, typename U>
    void write(std::string_view name, const std::vector<T, U>& val)
        requires (!is_default_serializible<T>) 
    {
        array(name);
        for (T elem: val) {
            elem.serialize(push_back(name));
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
    std::optional<ValueT> read(std::string_view name) requires is_default_serializible<ValueT>;

    template <typename ValueT>
    ValueT read(std::string_view name, ValueT default_val) {
        std::optional<ValueT> opt_val = read<ValueT>(name);
        if (opt_val) {
            return std::move(*opt_val); 
        } else {
            return std::move(default_val);
        }
    }

    std::size_t arr_size(std::string_view name);
    // unchecked acess
    ProjectReader read_array(std::string_view name, std::size_t idx);

    std::optional<ProjectReader> nest(std::string_view name);

    template <typename VecT>
    std::optional<VecT> read(std::string_view name) requires IsVector<VecT> {
        std::size_t cnt = arr_size(name);
        if (cnt == 0) 
            return std::nullopt;
        VecT result;
        result.reserve(cnt);
        for (std::size_t idx = 0; idx < cnt; idx++) {
            typename VecT::value_type elem;
            elem.deserialize(read_array(name, idx));
            result.push_back(std::move(elem));
        }
        return result;
    }

    ProjectContext& getCtx() {
        return context_;
    }
private:
    std::reference_wrapper<const json> obj_;
    std::reference_wrapper<ProjectContext> context_;
};


#define DESERIALIZE_OPT(input, var) \
    do {                                    \
        using ValT = decltype(var);         \
        if (std::optional<ValT> opt_val = input.read<ValT>(#var)) \
            var = std::move(*opt_val); \
    } while(0)

#define DESERIALIZE_SIMPLE(input, var, dflt) \
    var = input.read<decltype(var)>(#var, dflt)


#define _GET_MACRO(_1, _2, _3, _4, NAME, ...) NAME

#define _FOR_EACH_1(MACRO, arg1) \
    MACRO(arg1);

#define _FOR_EACH_2(MACRO, arg1, arg2) \
    MACRO(arg1); \
    MACRO(arg2);

#define _FOR_EACH_3(MACRO, arg1, arg2, arg3) \
    MACRO(arg1); \
    MACRO(arg2); \
    MACRO(arg3);

#define _FOR_EACH_4(MACRO, arg1, arg2, arg3, arg4) \
    MACRO(arg1); \
    MACRO(arg2); \
    MACRO(arg3); \
    MACRO(arg4);
    
#define _GET_FOR_EACH_MACRO(...) \
    _GET_MACRO(__VA_ARGS__, _FOR_EACH_4, _FOR_EACH_3, _FOR_EACH_2, _FOR_EACH_1)

#define _SERIALIZE_MACRO(arg) SERIALIZE_SIMPLE(output, arg)
#define _DESERIALIZE_MACRO(arg) DESERIALIZE_OPT(input, arg)


#define DEFINE_SIMPLE_SERDE(...)    \
void serialize(ProjectWriter output) const {        \
    _GET_FOR_EACH_MACRO(__VA_ARGS__)(_SERIALIZE_MACRO, __VA_ARGS__)   \
}                                                   \
void deserialize(ProjectReader input) {             \
    _GET_FOR_EACH_MACRO(__VA_ARGS__)(_DESERIALIZE_MACRO, __VA_ARGS__) \
}


#define DECLARE_READ_FOR_T(T) \
extern template std::optional<T> ProjectReader::read(std::string_view name);

DECLARE_READ_FOR_T(std::string)
DECLARE_READ_FOR_T(int)
DECLARE_READ_FOR_T(int64_t)
DECLARE_READ_FOR_T(unsigned)
DECLARE_READ_FOR_T(uint64_t)
DECLARE_READ_FOR_T(float)
DECLARE_READ_FOR_T(double)
DECLARE_READ_FOR_T(bool)

#undef DECLARE_READ_FOR_T

}