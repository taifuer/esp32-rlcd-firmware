#!/usr/bin/env python3
"""Validate and prepare monochrome images for ESP32 RLCD Firmware."""

from __future__ import annotations

import argparse
import hashlib
import os
from pathlib import Path
import struct
import sys
import tempfile
import warnings
from dataclasses import dataclass
from typing import Optional, Sequence


CANVAS_WIDTH = 400
CANVAS_HEIGHT = 300
IMAGE_AREA_HEIGHT = 250
PBM_ROW_BYTES = CANVAS_WIDTH // 8
PBM_RASTER_SIZE = PBM_ROW_BYTES * CANVAS_HEIGHT
PBM_CANONICAL_HEADER = b"P4\n400 300\n"

MAX_TARGET_BYTES = 16_384
MAX_PBM_HEADER_BYTES = 1_024
MAX_SOURCE_BYTES = 32 * 1024 * 1024
MAX_SOURCE_PIXELS = 40_000_000
FIRMWARE_FILENAME_MAX_BYTES = 63

_ASCII_WHITESPACE = b" \t\r\n\v\f"
_FIRMWARE_FILENAME_CHARACTERS = frozenset(
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_."
)
_FIRMWARE_FORMAT_BY_EXTENSION = {
    ".pbm": "PBM P4",
    ".bmp": "BMP 1-bit",
}
_BMP_FILE_SIZE = 15_662
_BMP_PIXEL_OFFSET = 62
_BMP_ROW_STRIDE = 52
_BMP_RASTER_SIZE = _BMP_ROW_STRIDE * CANVAS_HEIGHT


class ImageToolError(Exception):
    """A concise error that can be shown directly to a command-line user."""


@dataclass(frozen=True)
class ImageInfo:
    format_name: str
    width: int
    height: int
    file_size: int
    sha256: str


def _sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _skip_pbm_separators(data: bytes, offset: int) -> int:
    """Skip header whitespace and comments before the next PBM token."""

    while offset < len(data):
        if offset >= MAX_PBM_HEADER_BYTES:
            raise ImageToolError("PBM 头部超过 1024 字节")
        value = data[offset]
        if value in _ASCII_WHITESPACE:
            offset += 1
            continue
        if value == ord("#"):
            carriage_return = data.find(b"\r", offset + 1)
            line_feed = data.find(b"\n", offset + 1)
            endings = [value for value in (carriage_return, line_feed) if value >= 0]
            if not endings:
                raise ImageToolError("PBM 头部注释缺少换行符")
            ending = min(endings)
            offset = ending + 1
            if data[ending] == ord("\r") and offset < len(data):
                if data[offset] == ord("\n"):
                    offset += 1
            if offset > MAX_PBM_HEADER_BYTES:
                raise ImageToolError("PBM 头部超过 1024 字节")
            continue
        break
    return offset


def _read_pbm_token(data: bytes, offset: int, label: str) -> tuple[bytes, int]:
    offset = _skip_pbm_separators(data, offset)
    if offset >= len(data):
        raise ImageToolError(f"PBM 头部缺少{label}")
    if offset >= MAX_PBM_HEADER_BYTES:
        raise ImageToolError("PBM 头部超过 1024 字节")

    start = offset
    while offset < len(data):
        value = data[offset]
        if value in _ASCII_WHITESPACE:
            break
        if value == ord("#"):
            raise ImageToolError("PBM 注释前必须有 ASCII 空白")
        if value < 0x21 or value > 0x7E:
            raise ImageToolError(f"PBM {label}不是 ASCII 文本")
        offset += 1
        if offset > MAX_PBM_HEADER_BYTES:
            raise ImageToolError("PBM 头部超过 1024 字节")

    if start == offset:
        raise ImageToolError(f"PBM 头部缺少{label}")
    return data[start:offset], offset


def _parse_decimal_token(token: bytes, label: str) -> int:
    if not token.isdigit():
        raise ImageToolError(f"PBM {label}必须是十进制正整数")
    value = int(token)
    if value <= 0:
        raise ImageToolError(f"PBM {label}必须大于 0")
    return value


def _validate_pbm(data: bytes) -> ImageInfo:
    magic, offset = _read_pbm_token(data, 0, "格式标识")
    if magic != b"P4":
        raise ImageToolError("仅支持二进制 PBM P4，不支持当前 PBM 格式")

    width_token, offset = _read_pbm_token(data, offset, "宽度")
    height_token, offset = _read_pbm_token(data, offset, "高度")
    width = _parse_decimal_token(width_token, "宽度")
    height = _parse_decimal_token(height_token, "高度")
    if (width, height) != (CANVAS_WIDTH, CANVAS_HEIGHT):
        raise ImageToolError(
            f"图片尺寸必须是 {CANVAS_WIDTH}x{CANVAS_HEIGHT}，"
            f"当前是 {width}x{height}"
        )

    if offset >= len(data) or data[offset] not in _ASCII_WHITESPACE:
        raise ImageToolError("PBM 高度后缺少栅格分隔符")
    if data[offset : offset + 2] == b"\r\n":
        raster_offset = offset + 2
    else:
        raster_offset = offset + 1
    if raster_offset > MAX_PBM_HEADER_BYTES:
        raise ImageToolError("PBM 头部超过 1024 字节")

    raster_size = len(data) - raster_offset
    if raster_size != PBM_RASTER_SIZE:
        raise ImageToolError(
            f"PBM 栅格必须恰好是 {PBM_RASTER_SIZE} 字节，"
            f"当前是 {raster_size} 字节"
        )

    return ImageInfo("PBM P4", width, height, len(data), _sha256(data))


def _validate_bmp(data: bytes) -> ImageInfo:
    if len(data) != _BMP_FILE_SIZE:
        raise ImageToolError(
            f"1-bit BMP 必须恰好是 {_BMP_FILE_SIZE} 字节，当前是 {len(data)} 字节"
        )

    signature, declared_size, reserved_1, reserved_2, pixel_offset = (
        struct.unpack_from("<2sIHHI", data, 0)
    )
    if signature != b"BM":
        raise ImageToolError("BMP 文件标识无效")
    if declared_size != len(data):
        raise ImageToolError("BMP 文件头中的大小与实际大小不一致")
    if reserved_1 != 0 or reserved_2 != 0:
        raise ImageToolError("BMP 保留字段必须为 0")
    if pixel_offset != _BMP_PIXEL_OFFSET:
        raise ImageToolError(f"BMP 像素偏移必须是 {_BMP_PIXEL_OFFSET}")

    (
        dib_size,
        width,
        signed_height,
        planes,
        bits_per_pixel,
        compression,
        image_size,
        _x_pixels_per_meter,
        _y_pixels_per_meter,
        colors_used,
        colors_important,
    ) = struct.unpack_from("<IiiHHIIiiII", data, 14)

    if dib_size != 40:
        raise ImageToolError("BMP 仅支持 40 字节 BITMAPINFOHEADER")
    if width != CANVAS_WIDTH or abs(signed_height) != CANVAS_HEIGHT:
        raise ImageToolError(
            f"图片尺寸必须是 {CANVAS_WIDTH}x{CANVAS_HEIGHT}，"
            f"当前是 {width}x{abs(signed_height)}"
        )
    if planes != 1 or bits_per_pixel != 1:
        raise ImageToolError("BMP 必须是单平面 1-bit 图片")
    if compression != 0:
        raise ImageToolError("BMP 必须使用未压缩 BI_RGB 格式")
    if image_size not in (0, _BMP_RASTER_SIZE):
        raise ImageToolError(
            f"BMP 像素数据大小必须是 0 或 {_BMP_RASTER_SIZE}"
        )
    if colors_used not in (0, 2):
        raise ImageToolError("BMP 调色板颜色数必须是 0 或 2")
    if colors_important not in (0, 2):
        raise ImageToolError("BMP 重要颜色数必须是 0 或 2")

    palette = (data[54:58], data[58:62])
    black = b"\x00\x00\x00\x00"
    white = b"\xff\xff\xff\x00"
    if set(palette) != {black, white}:
        raise ImageToolError("BMP 调色板必须恰好包含黑色和白色")

    return ImageInfo(
        "BMP 1-bit",
        width,
        abs(signed_height),
        len(data),
        _sha256(data),
    )


def validate_image_bytes(data: bytes) -> ImageInfo:
    """Validate the firmware's strict PBM/BMP subset."""

    if not data:
        raise ImageToolError("图片文件为空")
    if len(data) > MAX_TARGET_BYTES:
        raise ImageToolError(f"目标图片不能超过 {MAX_TARGET_BYTES} 字节")
    if data.startswith(b"P"):
        return _validate_pbm(data)
    if data.startswith(b"BM"):
        return _validate_bmp(data)
    raise ImageToolError("仅支持本项目约束的 PBM P4 或 1-bit BMP")


def validate_firmware_filename(
    path: Path,
    *,
    image_info: Optional[ImageInfo] = None,
    require_pbm: bool = False,
) -> None:
    """Validate the filename rules used by the firmware's card scanner."""

    name = path.name
    try:
        encoded_name = name.encode("ascii")
    except UnicodeEncodeError as error:
        raise ImageToolError("图片文件名只能使用 ASCII 字符") from error

    if not encoded_name:
        raise ImageToolError("图片文件名不能为空")
    if len(encoded_name) > FIRMWARE_FILENAME_MAX_BYTES:
        raise ImageToolError(
            f"图片文件名不能超过 {FIRMWARE_FILENAME_MAX_BYTES} 个 ASCII 字节"
        )
    if name.startswith("."):
        raise ImageToolError("图片文件名不能以句点开头")
    if any(character not in _FIRMWARE_FILENAME_CHARACTERS for character in name):
        raise ImageToolError(
            "图片文件名只能使用英文字母、数字、连字号、下划线和句点"
        )

    extension = path.suffix.lower()
    expected_format = _FIRMWARE_FORMAT_BY_EXTENSION.get(extension)
    if expected_format is None:
        raise ImageToolError("图片文件扩展名必须是 .pbm 或 .bmp")
    if require_pbm and extension != ".pbm":
        raise ImageToolError("转换输出必须使用 .pbm 扩展名")
    if image_info is not None and image_info.format_name != expected_format:
        raise ImageToolError(
            f"文件扩展名 {extension} 与实际格式 {image_info.format_name} 不一致"
        )


def read_and_validate(path: Path) -> tuple[bytes, ImageInfo]:
    try:
        stat = path.stat()
    except OSError as error:
        raise ImageToolError(f"无法读取图片：{error}") from error
    if not path.is_file():
        raise ImageToolError(f"不是普通文件：{path}")
    if stat.st_size > MAX_TARGET_BYTES:
        raise ImageToolError(f"目标图片不能超过 {MAX_TARGET_BYTES} 字节")
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ImageToolError(f"无法读取图片：{error}") from error
    return data, validate_image_bytes(data)


def encode_p4_luma(luma: bytes, threshold: int = 128) -> bytes:
    """Encode a 400x300 grayscale canvas using standard PBM bit semantics."""

    expected = CANVAS_WIDTH * CANVAS_HEIGHT
    if len(luma) != expected:
        raise ImageToolError(
            f"灰度画布必须包含 {expected} 个像素，当前是 {len(luma)}"
        )
    if not 0 <= threshold <= 255:
        raise ImageToolError("二值化阈值必须在 0 到 255 之间")

    raster = bytearray(PBM_RASTER_SIZE)
    output_offset = 0
    input_offset = 0
    for _y in range(CANVAS_HEIGHT):
        for _byte_x in range(PBM_ROW_BYTES):
            packed = 0
            for bit in range(8):
                if luma[input_offset] < threshold:
                    packed |= 0x80 >> bit
                input_offset += 1
            raster[output_offset] = packed
            output_offset += 1
    return PBM_CANONICAL_HEADER + bytes(raster)


def _pillow_constants(image_module):
    try:
        resample = image_module.Resampling.LANCZOS
    except AttributeError:  # Pillow < 9.1
        resample = image_module.LANCZOS
    try:
        floyd_steinberg = image_module.Dither.FLOYDSTEINBERG
    except AttributeError:  # Pillow < 9.1
        floyd_steinberg = image_module.FLOYDSTEINBERG
    return resample, floyd_steinberg


def convert_source_to_p4(source: Path, *, threshold: int, dither: bool) -> bytes:
    """Load a common image with Pillow and return a canonical display PBM."""

    try:
        source_size = source.stat().st_size
    except OSError as error:
        raise ImageToolError(f"无法读取源图片：{error}") from error
    if not source.is_file():
        raise ImageToolError(f"不是普通文件：{source}")
    if source_size <= 0:
        raise ImageToolError("源图片为空")
    if source_size > MAX_SOURCE_BYTES:
        raise ImageToolError(
            f"源图片不能超过 {MAX_SOURCE_BYTES // (1024 * 1024)} MiB"
        )

    try:
        from PIL import Image, ImageOps
    except ImportError as error:
        raise ImageToolError(
            "转换常见图片需要 Pillow；请先执行 python3 -m pip install Pillow"
        ) from error

    Image.MAX_IMAGE_PIXELS = MAX_SOURCE_PIXELS
    try:
        bomb_warning = Image.DecompressionBombWarning
        bomb_error = Image.DecompressionBombError
    except AttributeError:  # pragma: no cover - compatibility with old Pillow
        bomb_warning = RuntimeWarning
        bomb_error = RuntimeError

    try:
        with warnings.catch_warnings():
            warnings.simplefilter("error", bomb_warning)
            with Image.open(source) as opened:
                width, height = opened.size
                if width <= 0 or height <= 0:
                    raise ImageToolError("源图片尺寸无效")
                if width * height > MAX_SOURCE_PIXELS:
                    raise ImageToolError(
                        f"源图片不能超过 {MAX_SOURCE_PIXELS:,} 像素"
                    )
                transposed = ImageOps.exif_transpose(opened)
                transposed.load()
                working = transposed.copy()
    except ImageToolError:
        raise
    except (OSError, ValueError, SyntaxError, bomb_warning, bomb_error) as error:
        raise ImageToolError(f"无法解码源图片：{error}") from error

    if working.width * working.height > MAX_SOURCE_PIXELS:
        raise ImageToolError(f"源图片不能超过 {MAX_SOURCE_PIXELS:,} 像素")

    if "A" in working.getbands() or "transparency" in working.info:
        rgba = working.convert("RGBA")
        black = Image.new("RGBA", rgba.size, (0, 0, 0, 255))
        black.alpha_composite(rgba)
        grayscale = black.convert("L")
    else:
        grayscale = working.convert("L")
    working.close()

    scale = min(
        CANVAS_WIDTH / grayscale.width,
        IMAGE_AREA_HEIGHT / grayscale.height,
    )
    fitted_width = max(1, min(CANVAS_WIDTH, round(grayscale.width * scale)))
    fitted_height = max(
        1, min(IMAGE_AREA_HEIGHT, round(grayscale.height * scale))
    )
    resample, floyd_steinberg = _pillow_constants(Image)
    fitted = grayscale.resize((fitted_width, fitted_height), resample=resample)
    grayscale.close()

    x = (CANVAS_WIDTH - fitted_width) // 2
    y = (IMAGE_AREA_HEIGHT - fitted_height) // 2
    if dither:
        monochrome = fitted.convert("1", dither=floyd_steinberg)
        canvas = Image.new("1", (CANVAS_WIDTH, CANVAS_HEIGHT), 0)
        canvas.paste(monochrome, (x, y))
        luma = canvas.convert("L").tobytes()
        monochrome.close()
    else:
        canvas = Image.new("L", (CANVAS_WIDTH, CANVAS_HEIGHT), 0)
        canvas.paste(fitted, (x, y))
        luma = canvas.tobytes()
    fitted.close()
    canvas.close()

    encoding_threshold = 128 if dither else threshold
    result = encode_p4_luma(luma, threshold=encoding_threshold)
    validate_image_bytes(result)
    return result


def _target_exists(path: Path) -> bool:
    return path.exists() or path.is_symlink()


def write_validated_atomic(path: Path, data: bytes, *, force: bool) -> ImageInfo:
    """Validate, durably write, re-read, and atomically install an image."""

    info = validate_image_bytes(data)
    validate_firmware_filename(path, image_info=info)
    parent = path.parent
    if not parent.is_dir():
        raise ImageToolError(f"输出目录不存在：{parent}")
    if _target_exists(path) and not force:
        raise ImageToolError(f"输出文件已存在；如需覆盖请添加 --force：{path}")

    temporary: Optional[Path] = None
    try:
        descriptor, temporary_name = tempfile.mkstemp(
            prefix=f".{path.name}.", suffix=".tmp", dir=parent
        )
        temporary = Path(temporary_name)
        with os.fdopen(descriptor, "wb") as output:
            output.write(data)
            output.flush()
            os.fsync(output.fileno())
        os.chmod(temporary, 0o644)

        _written, written_info = read_and_validate(temporary)
        if written_info.sha256 != info.sha256:
            raise ImageToolError("临时文件写入后校验不一致")
        if _target_exists(path) and not force:
            raise ImageToolError(f"输出文件已存在；如需覆盖请添加 --force：{path}")
        os.replace(temporary, path)
        temporary = None

        try:
            directory_fd = os.open(parent, os.O_RDONLY)
        except OSError:
            directory_fd = None
        if directory_fd is not None:
            try:
                os.fsync(directory_fd)
            except OSError:
                pass
            finally:
                os.close(directory_fd)

        _installed, installed_info = read_and_validate(path)
        if installed_info.sha256 != info.sha256:
            raise ImageToolError("输出文件安装后校验不一致")
        return installed_info
    except OSError as error:
        raise ImageToolError(f"无法写入输出图片：{error}") from error
    finally:
        if temporary is not None:
            try:
                temporary.unlink()
            except FileNotFoundError:
                pass


def _print_info(info: ImageInfo) -> None:
    print(f"格式: {info.format_name}")
    print(f"尺寸: {info.width}x{info.height}")
    print(f"文件大小: {info.file_size} bytes")
    print(f"SHA256: {info.sha256}")


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="检查或生成 ESP32-S3-RLCD-4.2 使用的单色图片"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    check_parser = subparsers.add_parser(
        "check", help="严格检查 400x300 PBM P4 或 1-bit BMP"
    )
    check_parser.add_argument("input", type=Path, metavar="INPUT")

    convert_parser = subparsers.add_parser(
        "convert", help="将常见图片转换为规范的 400x300 PBM P4"
    )
    convert_parser.add_argument("input", type=Path, metavar="INPUT")
    convert_parser.add_argument("output", type=Path, metavar="OUTPUT")
    convert_parser.add_argument(
        "--threshold",
        type=int,
        default=128,
        metavar="0..255",
        help="无抖动二值化阈值（默认：128）",
    )
    convert_parser.add_argument(
        "--dither",
        action="store_true",
        help="启用 Floyd-Steinberg 抖动（默认关闭）",
    )
    convert_parser.add_argument(
        "--force", action="store_true", help="允许覆盖已有输出文件"
    )
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = _build_parser()
    arguments = parser.parse_args(argv)
    try:
        if arguments.command == "check":
            validate_firmware_filename(arguments.input)
            _data, info = read_and_validate(arguments.input)
            validate_firmware_filename(arguments.input, image_info=info)
            _print_info(info)
            return 0

        if not 0 <= arguments.threshold <= 255:
            raise ImageToolError("二值化阈值必须在 0 到 255 之间")
        validate_firmware_filename(arguments.output, require_pbm=True)
        if _target_exists(arguments.output) and not arguments.force:
            raise ImageToolError(
                f"输出文件已存在；如需覆盖请添加 --force：{arguments.output}"
            )
        result = convert_source_to_p4(
            arguments.input,
            threshold=arguments.threshold,
            dither=arguments.dither,
        )
        info = write_validated_atomic(
            arguments.output, result, force=arguments.force
        )
        print(f"已写入: {arguments.output}")
        _print_info(info)
        return 0
    except ImageToolError as error:
        print(f"错误: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
