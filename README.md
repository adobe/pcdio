# PcdIO

PcdIO is a minimal library written with modern C++.  It supports reading and
writing [PCD (Point Cloud Data)][pcd format] v0.7 files in all three data
encodings — `ascii`, `binary` and `binary_compressed` (LZF) — and is
compatible with files produced by the [Point Cloud Library (PCL)][PCL].

[![build and test](https://github.com/adobe/pcdio/actions/workflows/build.yml/badge.svg)](https://github.com/adobe/pcdio/actions/workflows/build.yml)

## Build

```sh
# Build C++ library
mkdir build
cd build
cmake ..
make

# Build python
pip install git+https://github.com/Research-Adobe/pcdio.git
```

Ordinary CMake builds install the C++ library, headers, and package config with
`cmake --install build`, including when `-DPCDIO_PYTHON=ON` also builds the
Python extension. Consumers can use `find_package(pcdio CONFIG REQUIRED)` and
link `pcdio::pcdio`; set `CMAKE_PREFIX_PATH` to the installation prefix when
needed. Scikit-build wheels omit the C++ development artifacts.

## Example

A small CLI that loads a PCD file, prints a summary, and writes a copy is
included as an example.  It is disabled by default; build it with
`-DPCDIO_BUILD_EXAMPLES=On`:

```sh
mkdir build && cd build
cmake .. -DPCDIO_BUILD_EXAMPLES=On
make
./pcd_inspect input.pcd   # writes a copy to ./tmp.pcd
```

## Usage

In C++:
```c++
#include <pcdio/pcdio.h>

pcdio::PcdSpec spec = pcdio::load_pcd("input.pcd");

pcdio::save_pcd("output.pcd", spec);
```

or in Python
```python
import pcdio

spec = pcdio.load_pcd("input.pcd")
pcdio.save_pcd("output.pcd", spec)
```

## `PcdSpec` data structure

`PcdSpec` ([code](include/pcdio/PcdSpec.h)) is a data structure that maps
almost verbatim to the information stored in a PCD file header:

|  PcdSpec member | PCD header entry          | Description                                  |
| --------------: | :------------------------ | :------------------------------------------- |
|       `version` | VERSION                   | Format version.  Only `"0.7"` is supported.  |
|        `fields` | FIELDS, SIZE, TYPE, COUNT | Per-point fields (see below).                |
|         `width` | WIDTH                     | Width (number of points for unorganized clouds). |
|        `height` | HEIGHT                    | Height (1 for unorganized clouds).           |
|        `points` | POINTS                    | Number of points; must equal `width * height`. |
|     `viewpoint` | VIEWPOINT                 | Sensor pose (tx ty tz qw qx qy qz).          |
|          `data` | DATA                      | `"ascii"`, `"binary"` or `"binary_compressed"`. |

All fields are populated by `pcdio::load_pcd()`, and all fields should be set
up correctly before calling `pcdio::save_pcd()`.  The helper function
`pcdio::validate_spec(spec)` can be used to check whether a given `spec` is
valid.

### Point fields

Each point field is stored as raw bytes in point-major order: point `i`,
channel `c` occupies the byte range
`[(i * count + c) * size, (i * count + c + 1) * size)` of `field.data`.

```c++
pcdio::PcdField field;
field.name = "intensity";  // No whitespace allowed.
field.type = 'F';          // 'I': signed int, 'U': unsigned int, 'F': float.
field.size = 4;            // Bytes per value: 1, 2, 4 or 8 (4 or 8 for 'F').
field.count = 1;           // Number of values per point.
field.data = {...};        // points * count * size raw bytes.
```

The combination of `type` and `size` determines the C++ value type:

| type | size | C++ type   |
| :--: | :--: | :--------: |
| 'F'  | 4    | `float`    |
| 'F'  | 8    | `double`   |
| 'I'  | 1    | `int8_t`   |
| 'I'  | 2    | `int16_t`  |
| 'I'  | 4    | `int32_t`  |
| 'I'  | 8    | `int64_t`  |
| 'U'  | 1    | `uint8_t`  |
| 'U'  | 2    | `uint16_t` |
| 'U'  | 4    | `uint32_t` |
| 'U'  | 8    | `uint64_t` |

Use `field.get_value<T>(index)` and `field.set_value<T>(index, value)` for
C++17-safe typed reads and writes. Both copy one scalar with `memcpy`, without
allocating or exposing a typed pointer into byte storage. The flat value index
for point `i`, channel `c` is `i * field.count + c`.

```c++
field.data.resize(3 * sizeof(float)); // Allocate storage before writing values.
field.set_value<float>(0, 1.5f);
const float intensity = field.get_value<float>(0);
```

A type/size mismatch throws `std::invalid_argument`; an index without a
complete value in `field.data` throws `std::out_of_range`. Writes do not resize
the buffer. These accessors replace the former `get_data<T>()` pointer API:
read `get_data<T>()[index]` as `get_value<T>(index)`, and replace assignments
through that pointer with `set_value<T>(index, value)`.

PCL's packed `rgb` (float) / `rgba` (uint32) color fields are preserved
verbatim like any other field; no channel conversion is applied.

### Example: creating a point cloud from scratch

```c++
#include <pcdio/pcdio.h>
#include <vector>

int main()
{
    std::vector<float> xs = {0, 1, 2};
    std::vector<float> ys = {0, 0, 0};
    std::vector<float> zs = {0, 0, 0};

    auto make_field = [](const std::string& name, const std::vector<float>& values) {
        pcdio::PcdField field;
        field.name = name;
        field.type = 'F';
        field.size = sizeof(float);
        field.count = 1;
        const auto* begin = reinterpret_cast<const uint8_t*>(values.data());
        field.data.assign(begin, begin + values.size() * sizeof(float));
        return field;
    };

    pcdio::PcdSpec spec;
    spec.width = 3;
    spec.height = 1;
    spec.points = 3;
    spec.data = "binary_compressed"; // or "ascii", "binary"
    spec.fields.push_back(make_field("x", xs));
    spec.fields.push_back(make_field("y", ys));
    spec.fields.push_back(make_field("z", zs));

    pcdio::validate_spec(spec);
    pcdio::save_pcd("cloud.pcd", spec);
    return 0;
}
```

and in Python, with numpy.  The first `set_array()` call on a fresh `PcdSpec`
establishes the cloud dimensions (`width`/`points` = row count, `height` = 1);
every later array must have the same row count, including 0 for an empty cloud:

```python
import numpy as np
import pcdio

spec = pcdio.PcdSpec()
rng = np.random.default_rng()
xyz = rng.random((100, 3), dtype=np.float32)
spec.set_array("x", xyz[:, 0].copy())  # arrays must be C-contiguous
spec.set_array("y", xyz[:, 1].copy())
spec.set_array("z", xyz[:, 2].copy())
spec.set_array("intensity", rng.random(100, dtype=np.float32))
spec.data = "binary_compressed"
pcdio.save_pcd("cloud.pcd", spec)

spec2 = pcdio.load_pcd("cloud.pcd")
xyz2 = np.stack([spec2.get_array("x")[:, 0],
                 spec2.get_array("y")[:, 0],
                 spec2.get_array("z")[:, 0]], axis=1)
assert np.allclose(xyz, xyz2)
```

## Tests

```sh
# C++ unit tests
mkdir build
cd build
cmake .. -DPCDIO_BUILD_TESTS=On
make
ctest

# Python unit tests
pip install .[test]
python -m pytest tests/test_pcdio.py
```

[pcd format]: https://pointclouds.org/documentation/tutorials/pcd_file_format.html
[PCL]: https://pointclouds.org/
