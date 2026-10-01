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
#include <pcdio/exception.h>
#include <pcdio/pcdio.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using pcdio::PcdField;
using pcdio::PcdSpec;

template <typename T>
PcdField make_field(const std::string& name, int count, const std::vector<T>& values)
{
    PcdField field;
    field.name = name;
    if constexpr (std::is_floating_point_v<T>) {
        field.type = 'F';
    } else if constexpr (std::is_signed_v<T>) {
        field.type = 'I';
    } else {
        field.type = 'U';
    }
    field.size = static_cast<int>(sizeof(T));
    field.count = count;
    const auto* begin = reinterpret_cast<const uint8_t*>(values.data());
    field.data.assign(begin, begin + values.size() * sizeof(T));
    return field;
}

PcdSpec make_point_cloud()
{
    constexpr size_t n = 6;
    PcdSpec spec;
    spec.width = n;
    spec.height = 1;
    spec.points = n;

    std::vector<double> xs(n), ys(n), zs(n);
    std::vector<float> intensity(n);
    std::vector<float> normal(n * 3);
    std::vector<uint32_t> rgb(n);
    for (size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<double>(i) * 1.5;
        ys[i] = static_cast<double>(i) * -2.25;
        zs[i] = static_cast<double>(i) + 0.5;
        intensity[i] = static_cast<float>(i) * 0.5f;
        normal[i * 3 + 0] = 1.0f;
        normal[i * 3 + 1] = 0.0f;
        normal[i * 3 + 2] = static_cast<float>(i) * 0.1f;
        // PCL packs RGB into a single uint32 as (r << 16) | (g << 8) | b.
        rgb[i] = (static_cast<uint32_t>(10 * i) << 16) | (static_cast<uint32_t>(20 * i) << 8) |
                 static_cast<uint32_t>(30 * i);
    }

    spec.fields.push_back(make_field("x", 1, xs));
    spec.fields.push_back(make_field("y", 1, ys));
    spec.fields.push_back(make_field("z", 1, zs));
    spec.fields.push_back(make_field("intensity", 1, intensity));
    spec.fields.push_back(make_field("normal", 3, normal));
    spec.fields.push_back(make_field("rgb", 1, rgb));
    return spec;
}

void check_same(const PcdSpec& spec1, const PcdSpec& spec2)
{
    REQUIRE(spec2.version == spec1.version);
    REQUIRE(spec2.width == spec1.width);
    REQUIRE(spec2.height == spec1.height);
    REQUIRE(spec2.points == spec1.points);
    REQUIRE(spec2.viewpoint == spec1.viewpoint);
    REQUIRE(spec2.data == spec1.data);

    REQUIRE(spec2.fields.size() == spec1.fields.size());
    for (size_t i = 0; i < spec1.fields.size(); ++i) {
        const auto& f1 = spec1.fields[i];
        const auto& f2 = spec2.fields[i];
        CAPTURE(f1.name);
        REQUIRE(f2.name == f1.name);
        REQUIRE(f2.type == f1.type);
        REQUIRE(f2.size == f1.size);
        REQUIRE(f2.count == f1.count);
        // All encodings round-trip exactly: binary encodings copy raw bytes, and ascii values
        // are printed with max_digits10 precision.
        REQUIRE(f2.data == f1.data);
    }
}

void check_roundtrip(const std::string& encoding)
{
    PcdSpec spec = make_point_cloud();
    spec.data = encoding;

    std::stringstream buffer;
    REQUIRE_NOTHROW(pcdio::save_pcd(buffer, spec));
    PcdSpec spec2 = pcdio::load_pcd(buffer);
    check_same(spec, spec2);
}

} // namespace

TEST_CASE("2D points roundtrip", "[io]")
{
    // PCD imposes no spatial dimensionality; a cloud with only x/y must round-trip.
    constexpr size_t n = 5;
    std::vector<float> xs(n), ys(n);
    for (size_t i = 0; i < n; ++i) {
        xs[i] = static_cast<float>(i) * 1.5f;
        ys[i] = static_cast<float>(i) * -2.0f;
    }
    PcdSpec spec;
    spec.width = n;
    spec.height = 1;
    spec.points = n;
    spec.fields.push_back(make_field("x", 1, xs));
    spec.fields.push_back(make_field("y", 1, ys));

    SECTION("ascii")
    {
        spec.data = "ascii";
    }
    SECTION("binary")
    {
        spec.data = "binary";
    }
    SECTION("binary_compressed")
    {
        spec.data = "binary_compressed";
    }

    std::stringstream buffer;
    REQUIRE_NOTHROW(pcdio::save_pcd(buffer, spec));
    PcdSpec spec2 = pcdio::load_pcd(buffer);
    check_same(spec, spec2);
    REQUIRE(spec2.find_field("z") == nullptr);
}

TEST_CASE("2D ascii fixture", "[io]")
{
    // A 2D cloud as another tool would emit it (no z field).
    const std::string payload = "VERSION 0.7\n"
                                "FIELDS x y\n"
                                "SIZE 4 4\n"
                                "TYPE F F\n"
                                "COUNT 1 1\n"
                                "WIDTH 3\n"
                                "HEIGHT 1\n"
                                "VIEWPOINT 0 0 0 1 0 0 0\n"
                                "POINTS 3\n"
                                "DATA ascii\n"
                                "0 0\n"
                                "1 2\n"
                                "-1 -2\n";
    std::stringstream ss(payload);
    PcdSpec spec = pcdio::load_pcd(ss);
    REQUIRE(spec.points == 3);
    REQUIRE(spec.fields.size() == 2);
    REQUIRE(spec.find_field("z") == nullptr);
    REQUIRE(spec.find_field("x")->get_data<float>()[1] == Catch::Approx(1.0f));
    REQUIRE(spec.find_field("y")->get_data<float>()[2] == Catch::Approx(-2.0f));
}

TEST_CASE("roundtrip", "[io]")
{
    SECTION("ascii")
    {
        check_roundtrip("ascii");
    }
    SECTION("binary")
    {
        check_roundtrip("binary");
    }
    SECTION("binary_compressed")
    {
        check_roundtrip("binary_compressed");
    }
}

TEST_CASE("empty cloud roundtrip", "[io]")
{
    PcdSpec spec;
    spec.width = 0;
    spec.height = 1;
    spec.points = 0;
    spec.fields.push_back(make_field("x", 1, std::vector<float>{}));
    spec.fields.push_back(make_field("y", 1, std::vector<float>{}));
    spec.fields.push_back(make_field("z", 1, std::vector<float>{}));

    SECTION("ascii")
    {
        spec.data = "ascii";
    }
    SECTION("binary")
    {
        spec.data = "binary";
    }
    SECTION("binary_compressed")
    {
        spec.data = "binary_compressed";
    }

    std::stringstream buffer;
    REQUIRE_NOTHROW(pcdio::save_pcd(buffer, spec));
    PcdSpec spec2 = pcdio::load_pcd(buffer);
    check_same(spec, spec2);
}

TEST_CASE("binary_compressed many small fields roundtrip", "[io]")
{
    // Each field compresses as an independent LZF stream with its own control overhead, so the
    // total compressed size can exceed a single-stream plausibility bound; the loader must
    // accept its own writer's output.
    PcdSpec spec;
    spec.width = 1;
    spec.height = 1;
    spec.points = 1;
    spec.data = "binary_compressed";
    for (int i = 0; i < 100; ++i) {
        PcdField field;
        field.name = "f" + std::to_string(i);
        field.type = 'U';
        field.size = 1;
        field.count = 1;
        field.data = {static_cast<uint8_t>(i)};
        spec.fields.push_back(std::move(field));
    }

    std::stringstream buffer;
    pcdio::save_pcd(buffer, spec);
    PcdSpec spec2 = pcdio::load_pcd(buffer);
    check_same(spec, spec2);
}

TEST_CASE("file roundtrip", "[io]")
{
    PcdSpec spec = make_point_cloud();
    // Unique name avoids cross-talk between concurrent test runs sharing the temp directory.
    const auto filename =
        (std::filesystem::temp_directory_path() /
            ("pcdio_test_file_roundtrip_" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                ".pcd"))
            .string();
    SECTION("ascii")
    {
        spec.data = "ascii";
    }
    SECTION("binary")
    {
        spec.data = "binary";
    }
    SECTION("binary_compressed")
    {
        spec.data = "binary_compressed";
    }

    pcdio::save_pcd(filename, spec);
    PcdSpec spec2 = pcdio::load_pcd(filename);
    // Remove before the checks so a failed assertion cannot leak the file.
    std::filesystem::remove(filename);
    check_same(spec, spec2);
}

TEST_CASE("viewpoint precision roundtrip", "[io]")
{
    // The header VIEWPOINT is the only floating-point-precision-sensitive header line; integer
    // viewpoints would round-trip even with lossy formatting.
    PcdSpec spec = make_point_cloud();
    spec.viewpoint = {1.25, -2.5, 0.70710678118654752, 0.1, 0.2, 0.3, 0.9};
    SECTION("ascii")
    {
        spec.data = "ascii";
    }
    SECTION("binary_compressed")
    {
        spec.data = "binary_compressed";
    }

    std::stringstream buffer;
    pcdio::save_pcd(buffer, spec);
    PcdSpec spec2 = pcdio::load_pcd(buffer);
    REQUIRE(spec2.viewpoint == spec.viewpoint);
}

TEST_CASE("ascii fixture", "[io]")
{
    // Minimal PCD ascii payload with x y z and an intensity field.
    const std::string payload = "# .PCD v0.7 - Point Cloud Data file format\n"
                                "VERSION 0.7\n"
                                "FIELDS x y z intensity\n"
                                "SIZE 4 4 4 4\n"
                                "TYPE F F F F\n"
                                "COUNT 1 1 1 1\n"
                                "WIDTH 3\n"
                                "HEIGHT 1\n"
                                "VIEWPOINT 0 0 0 1 0 0 0\n"
                                "POINTS 3\n"
                                "DATA ascii\n"
                                "0 0 0 1\n"
                                "1 2 3 2\n"
                                "-1 -2 -3 3\n";

    std::stringstream ss(payload);
    PcdSpec spec = pcdio::load_pcd(ss);
    REQUIRE(spec.points == 3);
    REQUIRE(spec.fields.size() == 4);
    REQUIRE(spec.data == "ascii");

    const PcdField* fx = spec.find_field("x");
    const PcdField* fz = spec.find_field("z");
    const PcdField* intensity = spec.find_field("intensity");
    REQUIRE(fx != nullptr);
    REQUIRE(fz != nullptr);
    REQUIRE(intensity != nullptr);
    REQUIRE(fx->get_data<float>()[1] == Catch::Approx(1.0f));
    REQUIRE(fz->get_data<float>()[2] == Catch::Approx(-3.0f));
    REQUIRE(intensity->get_data<float>()[2] == Catch::Approx(3.0f));
    REQUIRE(spec.find_field("missing") == nullptr);
}

TEST_CASE("uncompressed binary fixture", "[io]")
{
    // Our writer only emits uncompressed `binary` on request, so this exercises the read path
    // for files produced by other tools.
    std::string payload = "# .PCD v0.7 - Point Cloud Data file format\n"
                          "VERSION 0.7\n"
                          "FIELDS x y z\n"
                          "SIZE 4 4 4\n"
                          "TYPE F F F\n"
                          "COUNT 1 1 1\n"
                          "WIDTH 2\n"
                          "HEIGHT 1\n"
                          "VIEWPOINT 0 0 0 1 0 0 0\n"
                          "POINTS 2\n"
                          "DATA binary\n";
    const float values[6] = {0.f, 1.f, 2.f, 10.f, 11.f, 12.f};
    payload.append(reinterpret_cast<const char*>(values), sizeof(values));
    // Trailing bytes after the binary payload are tolerated and must not shift records.
    payload.append("XYZ");

    std::stringstream ss(payload);
    PcdSpec spec = pcdio::load_pcd(ss);
    REQUIRE(spec.points == 2);
    REQUIRE(spec.fields.size() == 3);

    const PcdField* fx = spec.find_field("x");
    const PcdField* fz = spec.find_field("z");
    REQUIRE(fx != nullptr);
    REQUIRE(fz != nullptr);
    REQUIRE(fx->get_data<float>()[0] == Catch::Approx(0.f));
    REQUIRE(fx->get_data<float>()[1] == Catch::Approx(10.f));
    REQUIRE(fz->get_data<float>()[0] == Catch::Approx(2.f));
    REQUIRE(fz->get_data<float>()[1] == Catch::Approx(12.f));
}

TEST_CASE("binary_compressed liblzf interop", "[io]")
{
    // This payload's compressed body was produced by the *reference* liblzf 3.6 `lzf_compress`
    // (the codec PCL uses for `binary_compressed` PCD), NOT by our own encoder.  Decoding it
    // verifies real liblzf/PCL interoperability rather than merely proving our compressor and
    // decompressor agree with each other.
    //
    // The 16-point cloud has fields x y z intensity (all float32).  Column-major (SoA) source:
    //   x = 0,1,...,15   y = 0   z = 2   intensity = 100
    // The repeated y/z/intensity columns force liblzf to emit back-references, exercising the
    // decoder's copy path in addition to literal runs.
    static const unsigned char kBlob[] = {
        0x01,
        0x00,
        0x00,
        0x40,
        0x00,
        0x01,
        0x80,
        0x3f,
        0x20,
        0x05,
        0x00,
        0x40,
        0x20,
        0x02,
        0x20,
        0x03,
        0x00,
        0x80,
        0x20,
        0x03,
        0x00,
        0xa0,
        0x20,
        0x03,
        0x00,
        0xc0,
        0x20,
        0x03,
        0x00,
        0xe0,
        0x20,
        0x03,
        0x04,
        0x00,
        0x41,
        0x00,
        0x00,
        0x10,
        0x20,
        0x03,
        0x00,
        0x20,
        0x20,
        0x03,
        0x00,
        0x30,
        0x20,
        0x03,
        0x00,
        0x40,
        0x20,
        0x03,
        0x00,
        0x50,
        0x20,
        0x03,
        0x00,
        0x60,
        0x20,
        0x03,
        0x00,
        0x70,
        0x20,
        0x03,
        0xe0,
        0x38,
        0x00,
        0x40,
        0x63,
        0xe0,
        0x32,
        0x03,
        0x01,
        0xc8,
        0x42,
        0xe0,
        0x31,
        0x03,
        0x01,
        0xc8,
        0x42,
    };
    const uint32_t compressed_size = static_cast<uint32_t>(sizeof(kBlob)); // 81
    const uint32_t uncompressed_size = 16u * 4u * sizeof(float);           // 256

    std::string payload = "# .PCD v0.7 - Point Cloud Data file format\n"
                          "VERSION 0.7\n"
                          "FIELDS x y z intensity\n"
                          "SIZE 4 4 4 4\n"
                          "TYPE F F F F\n"
                          "COUNT 1 1 1 1\n"
                          "WIDTH 16\n"
                          "HEIGHT 1\n"
                          "VIEWPOINT 0 0 0 1 0 0 0\n"
                          "POINTS 16\n"
                          "DATA binary_compressed\n";
    payload.append(reinterpret_cast<const char*>(&compressed_size), sizeof(compressed_size));
    payload.append(reinterpret_cast<const char*>(&uncompressed_size), sizeof(uncompressed_size));
    payload.append(reinterpret_cast<const char*>(kBlob), sizeof(kBlob));

    std::stringstream ss(payload);
    PcdSpec spec = pcdio::load_pcd(ss);
    REQUIRE(spec.points == 16);

    const PcdField* fx = spec.find_field("x");
    const PcdField* fy = spec.find_field("y");
    const PcdField* fz = spec.find_field("z");
    const PcdField* intensity = spec.find_field("intensity");
    REQUIRE(fx != nullptr);
    REQUIRE(fy != nullptr);
    REQUIRE(fz != nullptr);
    REQUIRE(intensity != nullptr);
    for (size_t i = 0; i < 16; ++i) {
        REQUIRE(fx->get_data<float>()[i] == Catch::Approx(static_cast<float>(i)));
        REQUIRE(fy->get_data<float>()[i] == Catch::Approx(0.0f));
        REQUIRE(fz->get_data<float>()[i] == Catch::Approx(2.0f));
        REQUIRE(intensity->get_data<float>()[i] == Catch::Approx(100.0f));
    }
}

TEST_CASE("uint64 ascii roundtrip", "[io]")
{
    // Values above INT64_MAX must survive the ascii round-trip.
    const uint64_t big = static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 100u;
    PcdSpec spec;
    spec.width = 2;
    spec.height = 1;
    spec.points = 2;
    spec.data = "ascii";
    spec.fields.push_back(make_field("x", 1, std::vector<double>{0.0, 1.0}));
    spec.fields.push_back(make_field("y", 1, std::vector<double>{0.0, 1.0}));
    spec.fields.push_back(make_field("z", 1, std::vector<double>{0.0, 1.0}));
    spec.fields.push_back(make_field("id", 1, std::vector<uint64_t>{big, big - 1u}));

    std::stringstream buffer;
    pcdio::save_pcd(buffer, spec);
    PcdSpec spec2 = pcdio::load_pcd(buffer);

    const PcdField* id = spec2.find_field("id");
    REQUIRE(id != nullptr);
    REQUIRE(id->get_data<uint64_t>()[0] == big);
    REQUIRE(id->get_data<uint64_t>()[1] == big - 1u);
}

TEST_CASE("malformed binary_compressed data is rejected", "[io]")
{
    PcdSpec spec = make_point_cloud();
    spec.data = "binary_compressed";
    std::stringstream buffer;
    pcdio::save_pcd(buffer, spec);
    const std::string valid = buffer.str();

    const std::string marker = "DATA binary_compressed\n";
    const size_t pos = valid.find(marker);
    REQUIRE(pos != std::string::npos);
    const size_t sizes_offset = pos + marker.size();

    SECTION("truncated size fields")
    {
        std::string truncated = valid.substr(0, sizes_offset + 2);
        std::stringstream ss(truncated);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("truncated compressed blob")
    {
        std::string truncated = valid.substr(0, valid.size() - 3);
        std::stringstream ss(truncated);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("uncompressed size mismatch")
    {
        std::string mutated = valid;
        // Corrupt the uncompressed_size field (second uint32 after the DATA line).
        const uint32_t wrong = 4;
        std::memcpy(mutated.data() + sizes_offset + 4, &wrong, sizeof(wrong));
        std::stringstream ss(mutated);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
}

TEST_CASE("binary payload reads across chunk boundaries", "[io]")
{
    constexpr size_t kChunkSize = 64 * 1024;
    constexpr size_t kPointCount = kChunkSize / sizeof(float) + 3;
    std::string header = "VERSION 0.7\n"
                         "FIELDS x\n"
                         "SIZE 4\n"
                         "TYPE F\n";
    header += "WIDTH " + std::to_string(kPointCount) + "\n";
    header += "HEIGHT 1\nPOINTS " + std::to_string(kPointCount) + "\n";
    header += "DATA binary\n";
    std::vector<float> values(kPointCount);
    for (size_t i = 0; i < values.size(); ++i) values[i] = static_cast<float>(i);

    std::string payload = header;
    payload.append(reinterpret_cast<const char*>(values.data()), values.size() * sizeof(float));

    SECTION("reads a payload larger than one chunk")
    {
        std::stringstream ss(payload);
        const PcdSpec spec = pcdio::load_pcd(ss);
        const PcdField* field = spec.find_field("x");
        REQUIRE(field != nullptr);
        REQUIRE(field->data.size() == values.size() * sizeof(float));
        REQUIRE(std::memcmp(field->data.data(), values.data(), field->data.size()) == 0);
    }
    SECTION("rejects truncation after a full chunk and a partial read")
    {
        payload.resize(header.size() + kChunkSize + sizeof(float));
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
}

TEST_CASE("malformed input is rejected", "[io]")
{
    SECTION("negative SIZE")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x y z\n"
                                    "SIZE 4 4 -4\n" // negative size must be rejected
                                    "TYPE F F F\n"
                                    "COUNT 1 1 1\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0 0 0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("COUNT cardinality mismatch")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x y z\n"
                                    "SIZE 4 4 4\n"
                                    "TYPE F F F\n"
                                    "COUNT 1 1\n" // two values for three fields
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0 0 0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("trailing value after WIDTH")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1 999\n" // fixed-arity entry with a trailing token
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("trailing value after VIEWPOINT")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "VIEWPOINT 0 0 0 1 0 0 0 extra\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("trailing value after VERSION")
    {
        const std::string payload = "VERSION 0.7 junk\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("trailing value after DATA")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii junk\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("missing FIELDS")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "DATA ascii\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("missing DATA")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("unknown DATA encoding")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "DATA made_up\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::UnsupportedFeature);
    }
    SECTION("truncated binary payload")
    {
        std::string payload = "VERSION 0.7\n"
                              "FIELDS x y z\n"
                              "SIZE 4 4 4\n"
                              "TYPE F F F\n"
                              "WIDTH 2\n"
                              "HEIGHT 1\n"
                              "POINTS 2\n"
                              "DATA binary\n";
        const float values[3] = {0.f, 1.f, 2.f}; // only one quarter of the expected payload
        payload.append(reinterpret_cast<const char*>(values), sizeof(values));
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("ascii row with too few values")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x y z\n"
                                    "SIZE 4 4 4\n"
                                    "TYPE F F F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0 0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("missing VERSION")
    {
        const std::string payload = "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("missing WIDTH")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("missing HEIGHT")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "0\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::InvalidFormat);
    }
    SECTION("ascii token with junk suffix")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "1junk\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("ascii integer out of range for the field type")
    {
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS label\n"
                                    "SIZE 1\n"
                                    "TYPE U\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "256\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
    SECTION("ascii float out of range for the field type")
    {
        // Finite and representable as double, but outside the float range: narrowing would be
        // undefined behavior, so it must be rejected rather than saturated to infinity.
        const std::string payload = "VERSION 0.7\n"
                                    "FIELDS x\n"
                                    "SIZE 4\n"
                                    "TYPE F\n"
                                    "WIDTH 1\n"
                                    "HEIGHT 1\n"
                                    "POINTS 1\n"
                                    "DATA ascii\n"
                                    "1e100\n";
        std::stringstream ss(payload);
        REQUIRE_THROWS_AS(pcdio::load_pcd(ss), pcdio::CorruptData);
    }
}

TEST_CASE("ascii special float values", "[io]")
{
    // PCL emits "nan"/"inf" tokens for invalid measurements; they must keep loading.
    const std::string payload = "VERSION 0.7\n"
                                "FIELDS x\n"
                                "SIZE 4\n"
                                "TYPE F\n"
                                "WIDTH 6\n"
                                "HEIGHT 1\n"
                                "POINTS 6\n"
                                "DATA ascii\n"
                                "nan\ninf\n-inf\nNAN\nINF\n-INF\n";
    std::stringstream ss(payload);
    PcdSpec spec = pcdio::load_pcd(ss);
    const float* xs = spec.find_field("x")->get_data<float>();
    REQUIRE(std::isnan(xs[0]));
    REQUIRE(std::isinf(xs[1]));
    REQUIRE(xs[1] > 0);
    REQUIRE(std::isinf(xs[2]));
    REQUIRE(xs[2] < 0);
    REQUIRE(std::isnan(xs[3]));
    REQUIRE(std::isinf(xs[4]));
    REQUIRE(xs[4] > 0);
    REQUIRE(std::isinf(xs[5]));
    REQUIRE(xs[5] < 0);
}

TEST_CASE("get_data checks the field type", "[io]")
{
    PcdField field; // Defaults to type 'F', size 4.
    field.name = "x";
    field.data.resize(sizeof(float), 0);
    REQUIRE_NOTHROW(field.get_data<float>());
    REQUIRE_THROWS_AS(field.get_data<double>(), std::invalid_argument);
    REQUIRE_THROWS_AS(field.get_data<int32_t>(), std::invalid_argument);
}

TEST_CASE("validate_spec", "[io]")
{
    PcdSpec spec = make_point_cloud();
    REQUIRE_NOTHROW(pcdio::validate_spec(spec));

    SECTION("no fields")
    {
        spec.fields.clear();
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("points mismatch")
    {
        spec.points = spec.width + 1;
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("duplicate field names")
    {
        spec.fields.push_back(spec.fields[0]);
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("field data size mismatch")
    {
        spec.fields[0].data.pop_back();
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("invalid field type")
    {
        spec.fields[0].type = 'X';
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("zero field count")
    {
        spec.fields[0].count = 0;
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("invalid encoding")
    {
        spec.data = "made_up";
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("unsupported version")
    {
        spec.version = "0.6";
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
    SECTION("field name with whitespace")
    {
        spec.fields[0].name = "bad name";
        REQUIRE_THROWS_AS(pcdio::validate_spec(spec), pcdio::InvalidFormat);
    }
}
