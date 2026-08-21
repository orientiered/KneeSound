#include "serialization.h"
#include <cstdint>
#include <optional>
#include <string_view>
#include "nlohmann/json.hpp"

namespace waves {

// =================== PROJECT WRITER ==============

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

// =================== PROJECT READER ==============

template <typename ValueT>
std::optional<ValueT> ProjectReader::read(std::string_view name) {
    if (!obj_.get().contains(name)) {
        return std::nullopt;
    }

    return obj_.get().at(name).get<ValueT>();
}

#define DEFINE_READ_FOR_T(T) \
template std::optional<T> ProjectReader::read(std::string_view name);

DEFINE_READ_FOR_T(std::string)
DEFINE_READ_FOR_T(int)
DEFINE_READ_FOR_T(int64_t)
DEFINE_READ_FOR_T(unsigned)
DEFINE_READ_FOR_T(uint64_t)
DEFINE_READ_FOR_T(float)
DEFINE_READ_FOR_T(double)
DEFINE_READ_FOR_T(bool)

std::optional<ProjectReader> ProjectReader::nest(std::string_view name) {
    if (!obj_.get().contains(name)) {
        return std::nullopt;
    }

    return ProjectReader(obj_.get().at(name), context_);
}

std::size_t ProjectReader::arr_size(std::string_view name) {
    if (!obj_.get().contains(name)) {
        return 0;
    }

    if (!obj_.get().at(name).is_array()) {
        return 0;
    }

    
    return obj_.get().at(name).size();
}

ProjectReader ProjectReader::read_array(std::string_view name, std::size_t idx) {
    return ProjectReader(obj_.get()[name][idx], context_);
}
}