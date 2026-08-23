#!/usr/bin/env python3

from contextlib import redirect_stderr, redirect_stdout
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock


PROJECT_DIR = Path(__file__).resolve().parents[1]
TOOL_PATH = PROJECT_DIR / "tools" / "rlcd-image.py"
SPEC = importlib.util.spec_from_file_location("rlcd_image_tool", TOOL_PATH)
assert SPEC is not None and SPEC.loader is not None
TOOL = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = TOOL
PREVIOUS_DONT_WRITE_BYTECODE = sys.dont_write_bytecode
sys.dont_write_bytecode = True
try:
    SPEC.loader.exec_module(TOOL)
finally:
    sys.dont_write_bytecode = PREVIOUS_DONT_WRITE_BYTECODE


def make_p4(*, header=b"P4\n400 300\n", first_byte=0x00):
    raster = bytes([first_byte]) + bytes(TOOL.PBM_RASTER_SIZE - 1)
    return header + raster


def make_bmp(*, top_down=False, reverse_palette=False):
    height = -TOOL.CANVAS_HEIGHT if top_down else TOOL.CANVAS_HEIGHT
    file_header = struct.pack(
        "<2sIHHI", b"BM", 15662, 0, 0, 62
    )
    dib_header = struct.pack(
        "<IiiHHIIiiII",
        40,
        TOOL.CANVAS_WIDTH,
        height,
        1,
        1,
        0,
        15600,
        2835,
        2835,
        2,
        2,
    )
    black = b"\x00\x00\x00\x00"
    white = b"\xff\xff\xff\x00"
    palette = white + black if reverse_palette else black + white
    return file_header + dib_header + palette + bytes(15600)


class ValidationTests(unittest.TestCase):
    def test_accepts_canonical_p4(self):
        data = make_p4()
        info = TOOL.validate_image_bytes(data)
        self.assertEqual(info.format_name, "PBM P4")
        self.assertEqual((info.width, info.height), (400, 300))
        self.assertEqual(info.file_size, len(data))
        self.assertEqual(info.sha256, hashlib.sha256(data).hexdigest())

    def test_accepts_p4_comments_before_dimensions(self):
        header = b"P4 # binary bitmap\n 400\t# width\n300\r\n"
        info = TOOL.validate_image_bytes(make_p4(header=header, first_byte=0x23))
        self.assertEqual(info.format_name, "PBM P4")

        carriage_return_comments = b"P4\r# format\r400\r# height\r300\n"
        info = TOOL.validate_image_bytes(make_p4(header=carriage_return_comments))
        self.assertEqual(info.format_name, "PBM P4")

    def test_rejects_comment_without_leading_whitespace(self):
        with self.assertRaisesRegex(TOOL.ImageToolError, "空白"):
            TOOL.validate_image_bytes(make_p4(header=b"P4# comment\n400 300\n"))

    def test_does_not_consume_raster_whitespace_or_comment_bytes(self):
        for first_byte in (0x09, 0x0A, 0x20, 0x23):
            with self.subTest(first_byte=first_byte):
                info = TOOL.validate_image_bytes(make_p4(first_byte=first_byte))
                self.assertEqual(info.file_size, len(make_p4(first_byte=first_byte)))

    def test_rejects_non_p4_and_wrong_dimensions(self):
        with self.assertRaisesRegex(TOOL.ImageToolError, "仅支持二进制 PBM P4"):
            TOOL.validate_image_bytes(make_p4(header=b"P1\n400 300\n"))
        with self.assertRaisesRegex(TOOL.ImageToolError, "400x300"):
            TOOL.validate_image_bytes(make_p4(header=b"P4\n399 300\n"))

    def test_rejects_truncated_trailing_and_extra_header_separator(self):
        canonical = make_p4()
        for invalid in (canonical[:-1], canonical + b"\0"):
            with self.subTest(length=len(invalid)):
                with self.assertRaisesRegex(TOOL.ImageToolError, "恰好"):
                    TOOL.validate_image_bytes(invalid)

        extra_newline = b"P4\n400 300\n\n" + bytes(TOOL.PBM_RASTER_SIZE)
        with self.assertRaisesRegex(TOOL.ImageToolError, "恰好"):
            TOOL.validate_image_bytes(extra_newline)

    def test_rejects_oversize_pbm_header(self):
        header = b"P4\n#" + (b"x" * 1020) + b"\n400 300\n"
        with self.assertRaisesRegex(TOOL.ImageToolError, "1024"):
            TOOL.validate_image_bytes(make_p4(header=header))

    def test_accepts_strict_bmp_in_both_row_orders(self):
        for top_down in (False, True):
            for reverse_palette in (False, True):
                with self.subTest(top_down=top_down, reverse=reverse_palette):
                    data = make_bmp(
                        top_down=top_down, reverse_palette=reverse_palette
                    )
                    info = TOOL.validate_image_bytes(data)
                    self.assertEqual(info.format_name, "BMP 1-bit")
                    self.assertEqual((info.width, info.height), (400, 300))

    def test_rejects_bmp_outside_project_subset(self):
        cases = {}
        wrong_bpp = bytearray(make_bmp())
        struct.pack_into("<H", wrong_bpp, 28, 8)
        cases["单平面 1-bit"] = bytes(wrong_bpp)

        compressed = bytearray(make_bmp())
        struct.pack_into("<I", compressed, 30, 1)
        cases["BI_RGB"] = bytes(compressed)

        wrong_offset = bytearray(make_bmp())
        struct.pack_into("<I", wrong_offset, 10, 64)
        cases["像素偏移"] = bytes(wrong_offset)

        wrong_palette = bytearray(make_bmp())
        wrong_palette[54:58] = b"\x01\x00\x00\x00"
        cases["黑色和白色"] = bytes(wrong_palette)

        for message, data in cases.items():
            with self.subTest(message=message):
                with self.assertRaisesRegex(TOOL.ImageToolError, message):
                    TOOL.validate_image_bytes(data)

    def test_rejects_unknown_and_oversized_target(self):
        with self.assertRaisesRegex(TOOL.ImageToolError, "仅支持"):
            TOOL.validate_image_bytes(b"not an image")
        with self.assertRaisesRegex(TOOL.ImageToolError, "16384"):
            TOOL.validate_image_bytes(b"P" * (TOOL.MAX_TARGET_BYTES + 1))


class EncodingAndIoTests(unittest.TestCase):
    def test_p4_encoding_uses_standard_black_bit_semantics(self):
        black = bytes(TOOL.CANVAS_WIDTH * TOOL.CANVAS_HEIGHT)
        black_p4 = TOOL.encode_p4_luma(black)
        self.assertEqual(
            black_p4[len(TOOL.PBM_CANONICAL_HEADER) :],
            b"\xff" * TOOL.PBM_RASTER_SIZE,
        )

        white = b"\xff" * (TOOL.CANVAS_WIDTH * TOOL.CANVAS_HEIGHT)
        white_p4 = TOOL.encode_p4_luma(white)
        self.assertEqual(
            white_p4[len(TOOL.PBM_CANONICAL_HEADER) :],
            b"\x00" * TOOL.PBM_RASTER_SIZE,
        )

    def test_atomic_writer_refuses_overwrite_and_force_replaces(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "SCREEN.PBM"
            output.write_bytes(b"keep")
            data = make_p4(first_byte=0x55)
            with self.assertRaisesRegex(TOOL.ImageToolError, "--force"):
                TOOL.write_validated_atomic(output, data, force=False)
            self.assertEqual(output.read_bytes(), b"keep")

            info = TOOL.write_validated_atomic(output, data, force=True)
            self.assertEqual(output.read_bytes(), data)
            self.assertEqual(info.sha256, hashlib.sha256(data).hexdigest())
            self.assertEqual(list(Path(directory).glob("*.tmp")), [])

    def test_atomic_writer_self_checks_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "bad.pbm"
            with self.assertRaises(TOOL.ImageToolError):
                TOOL.write_validated_atomic(output, b"bad", force=False)
            self.assertFalse(output.exists())

    def test_atomic_writer_rejects_extension_content_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "screen.bmp"
            with self.assertRaisesRegex(TOOL.ImageToolError, "与实际格式"):
                TOOL.write_validated_atomic(output, make_p4(), force=False)
            self.assertFalse(output.exists())


class CommandTests(unittest.TestCase):
    def test_check_prints_required_metadata(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "SCREEN.PBM"
            data = make_p4()
            source.write_bytes(data)
            stdout = io.StringIO()
            with redirect_stdout(stdout):
                result = TOOL.main(["check", str(source)])
            self.assertEqual(result, 0)
            report = stdout.getvalue()
            self.assertIn("格式: PBM P4", report)
            self.assertIn("尺寸: 400x300", report)
            self.assertIn(f"文件大小: {len(data)} bytes", report)
            self.assertIn(hashlib.sha256(data).hexdigest(), report)

    def test_check_rejects_extension_content_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            cases = (
                ("screen.bmp", make_p4()),
                ("screen.pbm", make_bmp()),
            )
            for filename, data in cases:
                with self.subTest(filename=filename):
                    source = Path(directory) / filename
                    source.write_bytes(data)
                    stderr = io.StringIO()
                    with redirect_stderr(stderr):
                        result = TOOL.main(["check", str(source)])
                    self.assertEqual(result, 1)
                    self.assertIn("与实际格式", stderr.getvalue())

    def test_check_rejects_names_the_firmware_would_ignore(self):
        with tempfile.TemporaryDirectory() as directory:
            invalid_names = (
                ".hidden.pbm",
                "bad name.pbm",
                "中文.pbm",
                f"{'a' * 60}.pbm",
                "screen.png",
            )
            for filename in invalid_names:
                with self.subTest(filename=filename):
                    source = Path(directory) / filename
                    source.write_bytes(make_p4())
                    stderr = io.StringIO()
                    with redirect_stderr(stderr):
                        result = TOOL.main(["check", str(source)])
                    self.assertEqual(result, 1)
                    self.assertTrue(stderr.getvalue().startswith("错误:"))

    def test_convert_command_can_be_tested_without_pillow(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.png"
            output = Path(directory) / "SCREEN.PBM"
            source.write_bytes(b"mock source")
            converted = make_p4(first_byte=0xA5)
            stdout = io.StringIO()
            with mock.patch.object(
                TOOL, "convert_source_to_p4", return_value=converted
            ) as converter, redirect_stdout(stdout):
                result = TOOL.main(["convert", str(source), str(output)])
            self.assertEqual(result, 0)
            self.assertEqual(output.read_bytes(), converted)
            converter.assert_called_once_with(source, threshold=128, dither=False)
            self.assertIn(f"已写入: {output}", stdout.getvalue())

    def test_convert_refuses_existing_output_before_loading_pillow(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.png"
            output = Path(directory) / "SCREEN.PBM"
            source.write_bytes(b"mock source")
            output.write_bytes(b"keep")
            stderr = io.StringIO()
            with mock.patch.object(TOOL, "convert_source_to_p4") as converter:
                with redirect_stderr(stderr):
                    result = TOOL.main(["convert", str(source), str(output)])
            self.assertEqual(result, 1)
            converter.assert_not_called()
            self.assertEqual(output.read_bytes(), b"keep")
            self.assertIn("--force", stderr.getvalue())

    def test_convert_rejects_non_pbm_and_unsupported_output_names(self):
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "source.png"
            source.write_bytes(b"mock source")
            invalid_outputs = (
                Path(directory) / "screen.bmp",
                Path(directory) / "bad name.pbm",
                Path(directory) / "中文.pbm",
                Path(directory) / f"{'a' * 60}.pbm",
            )
            for output in invalid_outputs:
                with self.subTest(output=output.name):
                    stderr = io.StringIO()
                    with mock.patch.object(
                        TOOL, "convert_source_to_p4"
                    ) as converter, redirect_stderr(stderr):
                        result = TOOL.main(
                            ["convert", str(source), str(output)]
                        )
                    self.assertEqual(result, 1)
                    converter.assert_not_called()
                    self.assertFalse(output.exists())
                    self.assertTrue(stderr.getvalue().startswith("错误:"))


if __name__ == "__main__":
    unittest.main()
