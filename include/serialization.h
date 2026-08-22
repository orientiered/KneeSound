#pragma once

#include "core/audio_source.h"
#include "nlohmann/json_fwd.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace waves {

using json = nlohmann::json;

// forward declaration
class PluginManager;

struct ProjectContext {
    std::map<std::string, AudioSourcePtr> media_map;
    PluginManager *plugin_manager;
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
    std::optional<ValueT> read(std::string_view name);

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