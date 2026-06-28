import struct
import unittest
import zlib

from edgewind.full_frame_binary import (
    EWFULL_MAGIC,
    EWFULL_VERSION_V2,
    _CHANNEL_META_STRUCT,
    _HEADER_STRUCT,
    decode_full_frame_binary,
)
from edgewind.routes import api


def _fixed_text(text, size):
    return text.encode("utf-8")[:size].ljust(size, b"\x00")


def _build_frame(*, fft_enabled):
    waveform = [2500, 2501]
    spectrum = [10, 20] if fft_enabled else []
    meta = _CHANNEL_META_STRUCT.pack(
        0,
        1,
        len(waveform),
        len(spectrum),
        0,
        2500,
        2500,
    )
    body = struct.pack(f"<{len(waveform)}h", *waveform)
    if spectrum:
        body += struct.pack(f"<{len(spectrum)}h", *spectrum)
    crc = zlib.crc32(meta + body) & 0xFFFFFFFF
    header = _HEADER_STRUCT.pack(
        EWFULL_MAGIC,
        EWFULL_VERSION_V2,
        _HEADER_STRUCT.size,
        1,
        0,
        0,
        _fixed_text("NODE_TEST", 64),
        _fixed_text("E00", 8),
        1,
        1,
        1,
        0,
        1,
        4096,
        5000,
        200,
        15000,
        0,
        0,
        crc,
    )
    return header + meta + body


class FftEnabledTest(unittest.TestCase):
    def test_command_value_normalization(self):
        self.assertEqual(api._normalize_command_value("fft_enabled", 0), 0)
        self.assertEqual(api._normalize_command_value("fft_enabled", "0"), 0)
        self.assertEqual(api._normalize_command_value("fft_enabled", False), 0)
        self.assertEqual(api._normalize_command_value("fft_enabled", 1), 1)
        self.assertEqual(api._normalize_command_value("fft_enabled", "on"), 1)
        self.assertEqual(api._normalize_command_value("fft_enabled", True), 1)
        self.assertIsNone(api._normalize_command_value("fft_enabled", "maybe"))

    def test_full_frame_fft_count_drives_fft_enabled_flag(self):
        enabled = decode_full_frame_binary(_build_frame(fft_enabled=True))
        disabled = decode_full_frame_binary(_build_frame(fft_enabled=False))

        self.assertEqual(enabled.payload["fft_enabled"], 1)
        self.assertEqual(enabled.fft_max, 2)
        self.assertEqual(enabled.payload["channels"][0]["fft_count_raw"], 2)
        self.assertEqual(disabled.payload["fft_enabled"], 0)
        self.assertEqual(disabled.fft_max, 0)
        self.assertEqual(disabled.payload["channels"][0]["fft_count_raw"], 0)


if __name__ == "__main__":
    unittest.main()
