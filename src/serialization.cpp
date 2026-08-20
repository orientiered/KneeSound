#include "serialization.h"
#include <cstdint>
#include <string_view>
#include "nlohmann/json.hpp"

namespace waves {

ProjectWriter::ProjectWriter(ProjectWriter other, std::string_view name): 
    obj_(other.obj_.get()[name]) {}

#define DEFINE_WRITE_FOR_T(T) \
void ProjectWriter::write(std::string_view name, T val) { \
    obj_.get()[name] = std::move(val); \
}

DEFINE_WRITE_FOR_T(std::string)
DEFINE_WRITE_FOR_T(int)
DEFINE_WRITE_FOR_T(int64_t)
DEFINE_WRITE_FOR_T(unsigned)
DEFINE_WRITE_FOR_T(uint64_t)
DEFINE_WRITE_FOR_T(float)
DEFINE_WRITE_FOR_T(double)
DEFINE_WRITE_FOR_T(bool)


void ProjectWriter::array(std::string_view name) {
    obj_.get()[name] = json::array();
}

ProjectWriter ProjectWriter::push_back(std::string_view name) {
    if (!obj_.get()[name].is_array()) 
        array(name);
    obj_.get()[name].push_back(json());
    return ProjectWriter(obj_.get()[name].back());
}

}