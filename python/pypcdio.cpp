/*
 * Copyright 2026 Adobe. All rights reserved.
 * This file is licensed to you under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License. You may obtain a copy
 * of the License at http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software distributed under
 * the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR REPRESENTATIONS
 * OF ANY KIND, either express or implied. See the License for the specific language
 * governing permissions and limitations under the License.
 */
#include <pcdio/pcdio.h>

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/array.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

namespace nb = nanobind;

namespace {

// Create a (points, count) numpy array holding a copy of the field data.  `T` must match the
// C++ type implied by the field's `type`/`size`.  The spec's publicly mutable members can
// disagree with the field's byte length, so the exposed shape must be proven to match the
// backing allocation exactly before copying.
template <typename T>
nb::object field_to_array(const pcdio::PcdSpec& spec, const pcdio::PcdField& field)
{
    if (field.count < 1) {
        throw std::invalid_argument(
            "Field '" + field.name + "' has invalid count " + std::to_string(field.count));
    }
    const size_t count = static_cast<size_t>(field.count);
    if (spec.points != 0 && count > std::numeric_limits<size_t>::max() / spec.points) {
        throw std::invalid_argument("Field '" + field.name + "' element count overflows");
    }
    const size_t num_elements = spec.points * count;
    if (num_elements > std::numeric_limits<size_t>::max() / sizeof(T)) {
        throw std::invalid_argument("Field '" + field.name + "' byte count overflows");
    }
    const size_t expected_bytes = num_elements * sizeof(T);
    if (field.data.size() != expected_bytes) {
        throw std::invalid_argument("Field '" + field.name + "' has " +
                                    std::to_string(field.data.size()) +
                                    " bytes of data, expected " + std::to_string(expected_bytes) +
                                    " from points * count * size");
    }
    T* data = new T[num_elements]; // Owned by the capsule below.
    if (num_elements != 0) {
        std::memcpy(data, field.data.data(), field.data.size());
    }
    size_t shape[2] = {spec.points, count};
    nb::capsule owner(data, [](void* p) noexcept { delete[] static_cast<T*>(p); });
    nb::ndarray<nb::numpy, T> arr(data, 2, shape, owner);
    return nb::cast(arr);
}

nb::object get_array(const pcdio::PcdSpec& spec, const std::string& name)
{
    const pcdio::PcdField* field = spec.find_field(name);
    if (field == nullptr) throw nb::key_error(name.c_str());
    const char type = field->type;
    const int size = field->size;
    if (type == 'F' && size == 4) return field_to_array<float>(spec, *field);
    if (type == 'F' && size == 8) return field_to_array<double>(spec, *field);
    if (type == 'I' && size == 1) return field_to_array<int8_t>(spec, *field);
    if (type == 'I' && size == 2) return field_to_array<int16_t>(spec, *field);
    if (type == 'I' && size == 4) return field_to_array<int32_t>(spec, *field);
    if (type == 'I' && size == 8) return field_to_array<int64_t>(spec, *field);
    if (type == 'U' && size == 1) return field_to_array<uint8_t>(spec, *field);
    if (type == 'U' && size == 2) return field_to_array<uint16_t>(spec, *field);
    if (type == 'U' && size == 4) return field_to_array<uint32_t>(spec, *field);
    if (type == 'U' && size == 8) return field_to_array<uint64_t>(spec, *field);
    throw std::invalid_argument("Unsupported PCD field type/size combination");
}

template <typename T>
bool try_set_array(pcdio::PcdSpec& spec,
    const std::string& name,
    const nb::ndarray<>& arr,
    size_t points,
    int count)
{
    if (!(arr.dtype() == nb::dtype<T>())) return false;

    pcdio::PcdField field;
    field.name = name;
    field.type = std::is_floating_point_v<T> ? 'F' : std::is_signed_v<T> ? 'I' : 'U';
    field.size = static_cast<int>(sizeof(T));
    field.count = count;
    const auto* begin = static_cast<const uint8_t*>(arr.data());
    // Checked product: points comes from caller-controlled array shape metadata and the wrap
    // would otherwise produce an under-sized field.
    if (points != 0 && static_cast<size_t>(count) > std::numeric_limits<size_t>::max() / points) {
        throw std::invalid_argument("Field array shape overflows");
    }
    const size_t num_elements = points * static_cast<size_t>(count);
    if (num_elements > std::numeric_limits<size_t>::max() / sizeof(T)) {
        throw std::invalid_argument("Field array shape overflows");
    }
    const size_t num_bytes = num_elements * sizeof(T);
    field.data.assign(begin, begin + num_bytes);

    if (auto* existing = spec.find_field(name)) {
        *existing = std::move(field);
    } else {
        spec.fields.push_back(std::move(field));
    }
    return true;
}

void set_array(pcdio::PcdSpec& spec, const std::string& name, const nb::ndarray<>& arr)
{
    if (arr.device_type() != nb::device::cpu::value) {
        // The data pointer is memcpy'd directly, which is only valid for host memory.
        throw std::invalid_argument("Field array must be a CPU array");
    }
    if (arr.ndim() != 1 && arr.ndim() != 2) {
        throw std::invalid_argument("Field array must be 1D or 2D");
    }
    const size_t points = arr.shape(0);
    const size_t num_columns = arr.ndim() == 2 ? arr.shape(1) : 1;
    if (num_columns == 0 || num_columns > static_cast<size_t>(std::numeric_limits<int>::max())) {
        throw std::invalid_argument("Field array must have between 1 and 2^31 - 1 columns");
    }
    const int count = static_cast<int>(num_columns);

    // Only C-contiguous arrays are accepted (strides of empty arrays are arbitrary, so skip
    // the check when there are no elements).  The expected-stride product is computed with
    // overflow checks: shape values come from caller-controlled array metadata and a signed
    // int64 overflow would be UB.
    if (arr.size() != 0) {
        uint64_t expected_stride = 1;
        for (size_t d = arr.ndim(); d-- > 0;) {
            if (arr.stride(d) < 0 || static_cast<uint64_t>(arr.stride(d)) != expected_stride) {
                throw std::invalid_argument(
                    "Field array must be C-contiguous (see numpy.ascontiguousarray)");
            }
            const uint64_t dim = arr.shape(d);
            if (dim != 0 && expected_stride > std::numeric_limits<uint64_t>::max() / dim) {
                throw std::invalid_argument("Field array shape overflows");
            }
            expected_stride *= dim;
        }
    }

    // A spec with no fields and an unset point count is initialized by the first array.  Once
    // any field exists, the established point count is authoritative — including 0 for an
    // initialized-but-empty cloud — so a later non-empty array cannot make existing empty
    // fields inconsistent.
    const bool initializing = spec.fields.empty() && spec.points == 0;
    if (!initializing && points != spec.points) {
        throw std::invalid_argument("Field array row count does not match the number of points");
    }

    const bool ok = try_set_array<float>(spec, name, arr, points, count) ||
                    try_set_array<double>(spec, name, arr, points, count) ||
                    try_set_array<int8_t>(spec, name, arr, points, count) ||
                    try_set_array<int16_t>(spec, name, arr, points, count) ||
                    try_set_array<int32_t>(spec, name, arr, points, count) ||
                    try_set_array<int64_t>(spec, name, arr, points, count) ||
                    try_set_array<uint8_t>(spec, name, arr, points, count) ||
                    try_set_array<uint16_t>(spec, name, arr, points, count) ||
                    try_set_array<uint32_t>(spec, name, arr, points, count) ||
                    try_set_array<uint64_t>(spec, name, arr, points, count);
    if (!ok) throw std::invalid_argument("Unsupported field array dtype");

    if (initializing) {
        spec.width = points;
        spec.height = 1;
        spec.points = points;
    }
}

} // namespace

NB_MODULE(pypcdio, m)
{
    m.doc() = "A tiny library to read/write ASCII/binary/compressed PCD format files";

    nb::class_<pcdio::PcdField>(m, "PcdField")
        .def(nb::init<>())
        .def_rw("name", &pcdio::PcdField::name)
        .def_rw("type", &pcdio::PcdField::type)
        .def_rw("size", &pcdio::PcdField::size)
        .def_rw("count", &pcdio::PcdField::count)
        .def_prop_rw(
            "data",
            [](const pcdio::PcdField& self) {
                return nb::bytes(reinterpret_cast<const char*>(self.data.data()), self.data.size());
            },
            [](pcdio::PcdField& self, nb::bytes value) {
                const char* p = value.c_str();
                self.data.assign(p, p + value.size());
            })
        .def("__repr__", [](const pcdio::PcdField& self) {
            return "PcdField(name=" + self.name + ", type=" + std::string(1, self.type) +
                   ", size=" + std::to_string(self.size) + ", count=" + std::to_string(self.count) +
                   ", data=" + std::to_string(self.data.size()) + " bytes)";
        });

    nb::class_<pcdio::PcdSpec>(m, "PcdSpec")
        .def(nb::init<>())
        .def_rw("version", &pcdio::PcdSpec::version)
        .def_rw("fields", &pcdio::PcdSpec::fields)
        .def_rw("width", &pcdio::PcdSpec::width)
        .def_rw("height", &pcdio::PcdSpec::height)
        .def_rw("points", &pcdio::PcdSpec::points)
        .def_rw("viewpoint", &pcdio::PcdSpec::viewpoint)
        .def_rw("data", &pcdio::PcdSpec::data)
        .def("get_array",
            &get_array,
            nb::arg("name"),
            "Return a copy of the named field as a (points, count) numpy array")
        .def("set_array",
            &set_array,
            nb::arg("name"),
            nb::arg("arr"),
            "Create or replace a field from a C-contiguous 1D or 2D numpy array.  The array "
            "must be genuinely allocated: the DLPack/buffer protocols carry shape and strides "
            "but no backing-store length, so views built from untrusted layout metadata "
            "(numpy.lib.stride_tricks.as_strided, custom __array_interface__/__dlpack__ "
            "producers) are out of scope")
        .def("__repr__", [](const pcdio::PcdSpec& self) {
            return "PcdSpec(version=" + self.version + ", width=" + std::to_string(self.width) +
                   ", height=" + std::to_string(self.height) +
                   ", points=" + std::to_string(self.points) + ", data=" + self.data +
                   ", fields=" + std::to_string(self.fields.size()) + ")";
        });

    m.def("load_pcd",
        nb::overload_cast<const std::string&>(&pcdio::load_pcd),
        nb::arg("filename"),
        "Load a point cloud from a PCD file");
    m.def("save_pcd",
        nb::overload_cast<const std::string&, const pcdio::PcdSpec&>(&pcdio::save_pcd),
        nb::arg("filename"),
        nb::arg("spec"),
        "Save a point cloud to a PCD file");
    m.def("validate_spec",
        &pcdio::validate_spec,
        nb::arg("spec"),
        "Validate a PcdSpec, raising an exception on errors");
}
