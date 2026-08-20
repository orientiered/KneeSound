#pragma once

#include "nlohmann/json_fwd.hpp"
#include <functional>
#include <string_view>

namespace waves {

using json = nlohmann::json;

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

}