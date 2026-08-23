#include "serialization.h"
#include <cstdint>
#include <optional>
#include <string_view>
#include "nlohmann/json.hpp"

namespace waves {

// =================== PROJECT WRITER ==============

template<typename T> 
void ProjectWriter::write(T val) requires is_default_serializible<T> {
    obj_.get() = std::move(val);
}

#define DEFINE_WRITE_FOR_T(T) \
template void ProjectWriter::write<T>(T val);

DEFINE_WRITE_FOR_T(std::string)
DEFINE_WRITE_FOR_T(int)
DEFINE_WRITE_FOR_T(int64_t)
DEFINE_WRITE_FOR_T(unsigned)
DEFINE_WRITE_FOR_T(uint64_t)
DEFINE_WRITE_FOR_T(float)
DEFINE_WRITE_FOR_T(double)
DEFINE_WRITE_FOR_T(bool)

ProjectWriter::ProjectWriter(ProjectWriter other, std::string_view name): 
    obj_(other.obj_.get()[name]) {}
    

ProjectWriter ProjectWriter::array(std::string_view name) {
    return ProjectWriter(obj_.get()[name] = json::array());
}

ProjectWriter ProjectWriter::push_back() {
    if (!obj_.get().is_array())
        obj_.get() = json::array();

    obj_.get().push_back(json());
    return ProjectWriter(obj_.get().back());
}

// =================== PROJECT READER ==============

std::optional<ProjectReader> ProjectReader::nest(std::string_view name) {
    if (!obj_.get().contains(name)) {
        return std::nullopt;
    }

    return ProjectReader(obj_.get().at(name), context_);
}

template <typename ValueT>
std::optional<ValueT> ProjectReader::read() requires is_default_serializible<ValueT> {
    if (obj_.get().is_primitive() && !obj_.get().is_null()) {
        return obj_.get().get<ValueT>();
    } else {
        return std::nullopt;
    }
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

int64_t ProjectReader::arr_size() {
    return obj_.get().is_array() ? obj_.get().size() : -1;
}

ProjectReader ProjectReader::read_array(std::size_t idx) {
    return ProjectReader(obj_.get()[idx], context_);
}

}