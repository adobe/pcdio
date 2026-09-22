"""Unit tests for the pcdio python bindings."""

import struct

import numpy as np
import pcdio
import pytest


def make_point_cloud():
    """A 6-point cloud exercising float64, float32, multi-channel, packed uint32 and uint64
    fields."""
    n = 6
    spec = pcdio.PcdSpec()
    i = np.arange(n)
    spec.set_array("x", (i * 1.5).astype(np.float64))
    spec.set_array("y", (i * -2.25).astype(np.float64))
    spec.set_array("z", (i + 0.5).astype(np.float64))
    spec.set_array("intensity", (i * 0.5).astype(np.float32))
    normal = np.zeros((n, 3), dtype=np.float32)
    normal[:, 0] = 1.0
    normal[:, 2] = i * 0.1
    spec.set_array("normal", normal)
    # PCL packs RGB into a single uint32 as (r << 16) | (g << 8) | b.
    rgb = (
        ((10 * i) << 16).astype(np.uint32)
        | ((20 * i) << 8).astype(np.uint32)
        | (30 * i).astype(np.uint32)
    )
    spec.set_array("rgb", rgb)
    spec.set_array("id", i.astype(np.uint64) + np.uint64(2**63 + 100))
    return spec


def assert_same_cloud(spec1, spec2):
    assert spec2.version == spec1.version
    assert spec2.width == spec1.width
    assert spec2.height == spec1.height
    assert spec2.points == spec1.points
    assert list(spec2.viewpoint) == list(spec1.viewpoint)
    assert spec2.data == spec1.data

    assert len(spec2.fields) == len(spec1.fields)
    for f1, f2 in zip(spec1.fields, spec2.fields):
        assert f2.name == f1.name
        assert f2.type == f1.type
        assert f2.size == f1.size
        assert f2.count == f1.count
        # All encodings round-trip exactly: binary encodings copy raw bytes, and ascii values
        # are printed with max_digits10 precision.
        assert np.array_equal(spec2.get_array(f2.name), spec1.get_array(f1.name))


@pytest.mark.parametrize("encoding", ["ascii", "binary", "binary_compressed"])
def test_roundtrip(tmp_path, encoding):
    spec = make_point_cloud()
    spec.data = encoding
    filename = tmp_path / "cloud.pcd"
    pcdio.save_pcd(str(filename), spec)
    spec2 = pcdio.load_pcd(str(filename))
    assert spec2.data == encoding
    assert_same_cloud(spec, spec2)


@pytest.mark.parametrize("encoding", ["ascii", "binary", "binary_compressed"])
def test_2d_points_roundtrip(tmp_path, encoding):
    # PCD is dimension-agnostic; a cloud with only x/y must round-trip.
    n = 5
    i = np.arange(n)
    spec = pcdio.PcdSpec()
    spec.set_array("x", (i * 1.5).astype(np.float32))
    spec.set_array("y", (i * -2.0).astype(np.float32))
    assert spec.points == n
    assert [f.name for f in spec.fields] == ["x", "y"]
    spec.data = encoding
    filename = tmp_path / "cloud2d.pcd"
    pcdio.save_pcd(str(filename), spec)
    spec2 = pcdio.load_pcd(str(filename))
    assert spec2.points == n
    assert [f.name for f in spec2.fields] == ["x", "y"]
    assert np.array_equal(spec2.get_array("x")[:, 0], spec.get_array("x")[:, 0])
    assert np.array_equal(spec2.get_array("y")[:, 0], spec.get_array("y")[:, 0])


@pytest.mark.parametrize("encoding", ["ascii", "binary", "binary_compressed"])
def test_empty_cloud_roundtrip(tmp_path, encoding):
    spec = pcdio.PcdSpec()
    spec.set_array("x", np.empty(0, dtype=np.float32))
    spec.set_array("y", np.empty(0, dtype=np.float32))
    spec.set_array("z", np.empty(0, dtype=np.float32))
    assert spec.points == 0
    spec.data = encoding
    filename = tmp_path / "empty.pcd"
    pcdio.save_pcd(str(filename), spec)
    assert_same_cloud(spec, pcdio.load_pcd(str(filename)))


def test_ascii_fixture(tmp_path):
    # Minimal PCD ascii payload with x y z and an intensity field.
    payload = (
        "# .PCD v0.7 - Point Cloud Data file format\n"
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
        "-1 -2 -3 3\n"
    )
    filename = tmp_path / "fixture.pcd"
    filename.write_text(payload)

    spec = pcdio.load_pcd(str(filename))
    assert spec.points == 3
    assert len(spec.fields) == 4
    assert spec.data == "ascii"
    assert spec.get_array("x")[1, 0] == pytest.approx(1.0)
    assert spec.get_array("z")[2, 0] == pytest.approx(-3.0)
    assert spec.get_array("intensity")[2, 0] == pytest.approx(3.0)


def test_uncompressed_binary_fixture(tmp_path):
    # Exercises the uncompressed `binary` read path (files produced by other tools).
    payload = (
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "VERSION 0.7\n"
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "COUNT 1 1 1\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS 2\n"
        "DATA binary\n"
    ).encode() + np.array([0, 1, 2, 10, 11, 12], dtype=np.float32).tobytes()
    filename = tmp_path / "fixture.pcd"
    filename.write_bytes(payload)

    spec = pcdio.load_pcd(str(filename))
    assert spec.points == 2
    assert np.array_equal(spec.get_array("x")[:, 0], [0.0, 10.0])
    assert np.array_equal(spec.get_array("z")[:, 0], [2.0, 12.0])


def test_binary_compressed_liblzf_interop(tmp_path):
    # The compressed body below was produced by the *reference* liblzf 3.6 `lzf_compress` (the
    # codec PCL uses for `binary_compressed` PCD), not by our own encoder, verifying real
    # liblzf/PCL interoperability of the decoder.
    #
    # The 16-point cloud has fields x y z intensity (all float32).  Column-major (SoA) source:
    #   x = 0,1,...,15   y = 0   z = 2   intensity = 100
    blob = bytes(
        [
            0x01, 0x00, 0x00, 0x40, 0x00, 0x01, 0x80, 0x3F, 0x20, 0x05, 0x00, 0x40, 0x20, 0x02,
            0x20, 0x03, 0x00, 0x80, 0x20, 0x03, 0x00, 0xA0, 0x20, 0x03, 0x00, 0xC0, 0x20, 0x03,
            0x00, 0xE0, 0x20, 0x03, 0x04, 0x00, 0x41, 0x00, 0x00, 0x10, 0x20, 0x03, 0x00, 0x20,
            0x20, 0x03, 0x00, 0x30, 0x20, 0x03, 0x00, 0x40, 0x20, 0x03, 0x00, 0x50, 0x20, 0x03,
            0x00, 0x60, 0x20, 0x03, 0x00, 0x70, 0x20, 0x03, 0xE0, 0x38, 0x00, 0x40, 0x63, 0xE0,
            0x32, 0x03, 0x01, 0xC8, 0x42, 0xE0, 0x31, 0x03, 0x01, 0xC8, 0x42,
        ]
    )
    payload = (
        "# .PCD v0.7 - Point Cloud Data file format\n"
        "VERSION 0.7\n"
        "FIELDS x y z intensity\n"
        "SIZE 4 4 4 4\n"
        "TYPE F F F F\n"
        "COUNT 1 1 1 1\n"
        "WIDTH 16\n"
        "HEIGHT 1\n"
        "VIEWPOINT 0 0 0 1 0 0 0\n"
        "POINTS 16\n"
        "DATA binary_compressed\n"
    ).encode() + struct.pack("<II", len(blob), 16 * 4 * 4) + blob
    filename = tmp_path / "fixture.pcd"
    filename.write_bytes(payload)

    spec = pcdio.load_pcd(str(filename))
    assert spec.points == 16
    assert np.array_equal(spec.get_array("x")[:, 0], np.arange(16, dtype=np.float32))
    assert np.array_equal(spec.get_array("y")[:, 0], np.zeros(16, dtype=np.float32))
    assert np.array_equal(spec.get_array("z")[:, 0], np.full(16, 2.0, dtype=np.float32))
    assert np.array_equal(spec.get_array("intensity")[:, 0], np.full(16, 100.0, dtype=np.float32))


def test_uint64_ascii_roundtrip(tmp_path):
    # Values above INT64_MAX must survive the ascii round-trip.
    big = np.uint64(2**63 + 100)
    spec = pcdio.PcdSpec()
    spec.set_array("x", np.array([0.0, 1.0]))
    spec.set_array("y", np.array([0.0, 1.0]))
    spec.set_array("z", np.array([0.0, 1.0]))
    spec.set_array("id", np.array([big, big - 1], dtype=np.uint64))
    spec.data = "ascii"

    filename = tmp_path / "cloud.pcd"
    pcdio.save_pcd(str(filename), spec)
    spec2 = pcdio.load_pcd(str(filename))
    assert np.array_equal(spec2.get_array("id")[:, 0], [big, big - 1])


def test_viewpoint_roundtrip(tmp_path):
    spec = make_point_cloud()
    spec.viewpoint = [1.5, -2.25, 3.125, 1.0, 0.0, 0.0, 0.0]
    filename = tmp_path / "cloud.pcd"
    for encoding in ["ascii", "binary_compressed"]:
        spec.data = encoding
        pcdio.save_pcd(str(filename), spec)
        spec2 = pcdio.load_pcd(str(filename))
        assert list(spec2.viewpoint) == [1.5, -2.25, 3.125, 1.0, 0.0, 0.0, 0.0]


@pytest.mark.parametrize(
    "payload, match",
    [
        # Negative SIZE must be rejected.
        (
            "VERSION 0.7\n"
            "FIELDS x y z\n"
            "SIZE 4 4 -4\n"
            "TYPE F F F\n"
            "COUNT 1 1 1\n"
            "WIDTH 1\n"
            "HEIGHT 1\n"
            "POINTS 1\n"
            "DATA ascii\n"
            "0 0 0\n",
            "invalid SIZE",
        ),
        # Missing FIELDS.
        ("VERSION 0.7\nWIDTH 1\nHEIGHT 1\nDATA ascii\n", "no FIELDS"),
        # Missing DATA.
        ("VERSION 0.7\nFIELDS x\nSIZE 4\nTYPE F\nWIDTH 1\nHEIGHT 1\n", "no DATA"),
        # Unknown DATA encoding.
        (
            "VERSION 0.7\nFIELDS x\nSIZE 4\nTYPE F\nWIDTH 1\nHEIGHT 1\nDATA made_up\n",
            "Unknown PCD DATA format",
        ),
        # Ascii row with too few values.
        (
            "VERSION 0.7\n"
            "FIELDS x y z\n"
            "SIZE 4 4 4\n"
            "TYPE F F F\n"
            "WIDTH 1\n"
            "HEIGHT 1\n"
            "POINTS 1\n"
            "DATA ascii\n"
            "0 0\n",
            "too few values",
        ),
        # Missing VERSION.
        (
            "FIELDS x\nSIZE 4\nTYPE F\nWIDTH 1\nHEIGHT 1\nPOINTS 1\nDATA ascii\n0\n",
            "no VERSION",
        ),
        # Missing HEIGHT.
        (
            "VERSION 0.7\nFIELDS x\nSIZE 4\nTYPE F\nWIDTH 1\nPOINTS 1\nDATA ascii\n0\n",
            "no HEIGHT",
        ),
        # Finite float token outside the field's range.
        (
            "VERSION 0.7\n"
            "FIELDS x\n"
            "SIZE 4\n"
            "TYPE F\n"
            "WIDTH 1\n"
            "HEIGHT 1\n"
            "POINTS 1\n"
            "DATA ascii\n"
            "1e100\n",
            "out of range",
        ),
    ],
)
def test_malformed_input_is_rejected(tmp_path, payload, match):
    filename = tmp_path / "bad.pcd"
    filename.write_text(payload)
    with pytest.raises(RuntimeError, match=match):
        pcdio.load_pcd(str(filename))


def test_truncated_binary_payload(tmp_path):
    payload = (
        "VERSION 0.7\n"
        "FIELDS x y z\n"
        "SIZE 4 4 4\n"
        "TYPE F F F\n"
        "WIDTH 2\n"
        "HEIGHT 1\n"
        "POINTS 2\n"
        "DATA binary\n"
    ).encode() + np.array([0, 1, 2], dtype=np.float32).tobytes()  # a quarter of the payload
    filename = tmp_path / "bad.pcd"
    filename.write_bytes(payload)
    with pytest.raises(RuntimeError, match="truncated"):
        pcdio.load_pcd(str(filename))


def test_missing_file():
    with pytest.raises(RuntimeError, match="Unable to open input file"):
        pcdio.load_pcd("/nonexistent/cloud.pcd")


def test_validate_spec():
    spec = make_point_cloud()
    pcdio.validate_spec(spec)  # Should not raise.

    # Points mismatch.
    bad = make_point_cloud()
    bad.points = bad.width + 1
    with pytest.raises(RuntimeError, match="WIDTH"):
        pcdio.validate_spec(bad)

    # Duplicate field names (the fields getter returns a copy; reassign to mutate).
    bad = make_point_cloud()
    bad_fields = list(bad.fields)
    bad_fields.append(bad_fields[0])
    bad.fields = bad_fields
    with pytest.raises(RuntimeError, match="duplicated"):
        pcdio.validate_spec(bad)

    # Field data size mismatch.
    bad = make_point_cloud()
    field = bad.fields[0]
    field.data = field.data[:-1]
    bad_fields = list(bad.fields)
    bad_fields[0] = field
    bad.fields = bad_fields
    with pytest.raises(RuntimeError, match="bytes of data"):
        pcdio.validate_spec(bad)

    # Unsupported version.
    bad = make_point_cloud()
    bad.version = "0.6"
    with pytest.raises(RuntimeError, match="version"):
        pcdio.validate_spec(bad)

    # Unknown encoding.
    bad = make_point_cloud()
    bad.data = "made_up"
    with pytest.raises(RuntimeError, match="Unknown PCD DATA format"):
        pcdio.validate_spec(bad)


def test_get_array_missing_field():
    spec = make_point_cloud()
    with pytest.raises(KeyError):
        spec.get_array("nope")


def test_set_array_replaces_and_preserves_order():
    spec = make_point_cloud()
    replacement = np.zeros(spec.points, dtype=np.float64)
    spec.set_array("x", replacement)
    assert [f.name for f in spec.fields] == [
        "x",
        "y",
        "z",
        "intensity",
        "normal",
        "rgb",
        "id",
    ]
    assert np.array_equal(spec.get_array("x")[:, 0], replacement)


def test_set_array_errors():
    spec = make_point_cloud()

    # Non-contiguous input (1D strided slice and Fortran-ordered 2D).
    with pytest.raises(ValueError, match="C-contiguous"):
        spec.set_array("bad", np.zeros((10, 3), dtype=np.float32)[:, 0])
    with pytest.raises(ValueError, match="C-contiguous"):
        spec.set_array("bad", np.asfortranarray(np.zeros((spec.points, 3), dtype=np.float32)))

    # Unsupported dtype.
    with pytest.raises(ValueError, match="dtype"):
        spec.set_array("bad", np.zeros(spec.points, dtype=np.complex64))

    # Wrong dimensionality.
    with pytest.raises(ValueError, match="1D or 2D"):
        spec.set_array("bad", np.zeros((spec.points, 2, 2), dtype=np.float32))

    # Row count mismatch.
    with pytest.raises(ValueError, match="row count"):
        spec.set_array("bad", np.zeros(spec.points + 1, dtype=np.float32))


def test_get_array_rejects_inconsistent_field():
    # The byte-length guard in get_array (not just validate_spec) must reject a field whose
    # data disagrees with points * count * size.
    spec = make_point_cloud()
    fields = list(spec.fields)
    fields[0].data = fields[0].data[:-1]
    spec.fields = fields
    with pytest.raises(ValueError, match="bytes of data"):
        spec.get_array("x")


def test_get_array_returns_a_copy():
    spec = make_point_cloud()
    arr = spec.get_array("x")
    arr[0, 0] = 999.0
    assert spec.get_array("x")[0, 0] != 999.0


def test_set_array_established_empty_cloud():
    # Once any field exists, the established point count (0 here) is authoritative.
    spec = pcdio.PcdSpec()
    spec.set_array("x", np.empty(0, dtype=np.float32))
    with pytest.raises(ValueError, match="row count"):
        spec.set_array("y", np.zeros(3, dtype=np.float32))


def test_nan_inf_ascii_roundtrip(tmp_path):
    spec = pcdio.PcdSpec()
    spec.set_array("x", np.array([np.nan, np.inf, -np.inf, 1.0], dtype=np.float32))
    spec.set_array("y", np.zeros(4, dtype=np.float32))
    spec.set_array("z", np.zeros(4, dtype=np.float32))
    spec.data = "ascii"
    filename = tmp_path / "cloud.pcd"
    pcdio.save_pcd(str(filename), spec)
    spec2 = pcdio.load_pcd(str(filename))
    assert np.array_equal(spec2.get_array("x"), spec.get_array("x"), equal_nan=True)


def test_field_data_bytes_property():
    spec = make_point_cloud()
    field = spec.fields[0]
    raw = field.data
    assert isinstance(raw, bytes)
    assert len(raw) == spec.points * field.size * field.count
    assert np.array_equal(np.frombuffer(raw, dtype=np.float64), spec.get_array("x")[:, 0])

    field.data = raw  # Bytes round-trip through the setter.
    assert field.data == raw
