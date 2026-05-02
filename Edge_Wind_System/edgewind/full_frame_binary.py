from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass

EWFULL_MAGIC = 0x31465745
EWFULL_VERSION_V1 = 1
EWFULL_VERSION_V2 = 2
EWFULL_PROTOS = {'ewfull/1', 'ewfull/2'}
MAX_CHANNELS = 4
MAX_WAVEFORM_COUNT = 4096
MAX_FFT_COUNT = 2048

_HEADER_STRUCT = struct.Struct('<IHHIQI64s8sBBBBIIIIIIII')
_CHANNEL_META_STRUCT = struct.Struct('<BBHHHii')

_CHANNEL_LABELS = {
    0: ('直流母线(+)', 'V'),
    1: ('直流母线(-)', 'V'),
    2: ('负载电流', 'A'),
    3: ('漏电流', 'mA'),
}


class FullFrameBinaryError(ValueError):
    pass


@dataclass(slots=True)
class FullFrameBinaryDecodeResult:
    payload: dict
    rx_bytes: int
    channel_count: int
    waveform_max: int
    fft_max: int


def _decode_text(raw: bytes) -> str:
    return raw.split(b'\x00', 1)[0].decode('utf-8', errors='ignore').strip()


def _downsample_numeric_view(view, limit: int | None, scale: float = 1.0) -> list:
    count = len(view)
    if count <= 0:
        return []
    if limit is None or limit <= 0 or count <= limit:
        if scale == 1.0:
            return [int(v) for v in view]
        return [float(v) / scale for v in view]

    step = max(1, count // limit)
    sampled = view[::step]
    if len(sampled) > limit:
        sampled = sampled[:limit]
    if scale == 1.0:
        return [int(v) for v in sampled]
    return [float(v) / scale for v in sampled]


def decode_full_frame_binary(body: bytes,
                             *,
                             waveform_limit: int | None = None,
                             fft_limit: int | None = None) -> FullFrameBinaryDecodeResult:
    if not isinstance(body, (bytes, bytearray, memoryview)):
        raise FullFrameBinaryError('body must be bytes')

    raw = bytes(body)
    if len(raw) < _HEADER_STRUCT.size:
        raise FullFrameBinaryError(f'body too short: {len(raw)}')

    (
        magic,
        version,
        header_len,
        frame_id,
        timestamp_ms,
        flags,
        node_id_raw,
        fault_code_raw,
        report_mode,
        status_code,
        channel_count,
        _reserved0,
        downsample_step,
        upload_points,
        heartbeat_ms,
        min_interval_ms,
        http_timeout_ms,
        chunk_kb,
        chunk_delay_ms,
        data_crc32,
    ) = _HEADER_STRUCT.unpack_from(raw, 0)

    if magic != EWFULL_MAGIC:
        raise FullFrameBinaryError(f'bad magic: 0x{magic:08x}')
    if version not in {EWFULL_VERSION_V1, EWFULL_VERSION_V2}:
        raise FullFrameBinaryError(f'bad version: {version}')
    if header_len != _HEADER_STRUCT.size:
        raise FullFrameBinaryError(f'bad header_len: {header_len}')
    if channel_count < 1 or channel_count > MAX_CHANNELS:
        raise FullFrameBinaryError(f'bad channel_count: {channel_count}')

    meta_offset = header_len
    payload_offset = meta_offset + channel_count * _CHANNEL_META_STRUCT.size
    if len(raw) < payload_offset:
        raise FullFrameBinaryError('truncated channel meta')

    crc_blob = raw[meta_offset:]
    actual_crc32 = zlib.crc32(crc_blob) & 0xFFFFFFFF
    if actual_crc32 != data_crc32:
        alt_seed_ff = zlib.crc32(crc_blob, 0xFFFFFFFF) & 0xFFFFFFFF
        alt_seed_ff_xor = alt_seed_ff ^ 0xFFFFFFFF
        alt_xor = actual_crc32 ^ 0xFFFFFFFF
        raise FullFrameBinaryError(
            f'crc mismatch: expected=0x{data_crc32:08x} actual=0x{actual_crc32:08x} '
            f'alt_seed_ff=0x{alt_seed_ff:08x} alt_seed_ff_xor=0x{alt_seed_ff_xor:08x} alt_xor=0x{alt_xor:08x}'
        )

    node_id = _decode_text(node_id_raw)
    fault_code = _decode_text(fault_code_raw) or 'E00'
    channels = []
    waveform_max = 0
    fft_max = 0
    cursor = payload_offset

    for index in range(channel_count):
        ch_offset = meta_offset + index * _CHANNEL_META_STRUCT.size
        (
            channel_id,
            _meta_reserved0,
            waveform_count,
            fft_count,
            _meta_reserved1,
            value_scaled,
            current_value_scaled,
        ) = _CHANNEL_META_STRUCT.unpack_from(raw, ch_offset)

        if waveform_count > MAX_WAVEFORM_COUNT:
            raise FullFrameBinaryError(f'bad waveform_count: {waveform_count}')
        if fft_count > MAX_FFT_COUNT:
            raise FullFrameBinaryError(f'bad fft_count: {fft_count}')

        waveform_item_size = 4 if version == EWFULL_VERSION_V1 else 2
        waveform_bytes = waveform_count * waveform_item_size
        fft_bytes = fft_count * 2
        next_cursor = cursor + waveform_bytes + fft_bytes
        if next_cursor > len(raw):
            raise FullFrameBinaryError('body length mismatch')

        if waveform_count > 0:
            try:
                waveform_view = memoryview(raw[cursor:cursor + waveform_bytes]).cast(
                    'i' if version == EWFULL_VERSION_V1 else 'h'
                )
                waveform = _downsample_numeric_view(waveform_view, waveform_limit, 1.0)
            except Exception:
                fmt = f'<{waveform_count}i' if version == EWFULL_VERSION_V1 else f'<{waveform_count}h'
                waveform = list(struct.unpack_from(fmt, raw, cursor))
                waveform = _downsample_numeric_view(waveform, waveform_limit, 1.0)
        else:
            waveform = []
        cursor += waveform_bytes

        if fft_count > 0:
            try:
                fft_view = memoryview(raw[cursor:cursor + fft_bytes]).cast('h')
                fft = _downsample_numeric_view(fft_view, fft_limit, 10.0)
            except Exception:
                fft = [value / 10.0 for value in struct.unpack_from(f'<{fft_count}h', raw, cursor)]
                fft = _downsample_numeric_view(fft, fft_limit, 1.0)
        else:
            fft = []
        cursor += fft_bytes

        label, unit = _CHANNEL_LABELS.get(channel_id, (f'通道{channel_id}', ''))
        waveform_max = max(waveform_max, waveform_count)
        fft_max = max(fft_max, fft_count)
        channels.append({
            'id': int(channel_id),
            'channel_id': int(channel_id),
            'label': label,
            'name': label,
            'unit': unit,
            'value': int(value_scaled),
            'current_value': int(current_value_scaled),
            'waveform_count_raw': int(waveform_count),
            'fft_count_raw': int(fft_count),
            'waveform': waveform,
            'fft_spectrum': fft,
        })

    if cursor != len(raw):
        raise FullFrameBinaryError(f'trailing bytes: {len(raw) - cursor}')

    status = 'online' if int(status_code) == 1 else 'offline'
    report_mode_text = 'full' if int(report_mode) == 1 else 'summary'
    payload = {
        'node_id': node_id,
        'device_id': node_id,
        'status': status,
        'fault_code': fault_code,
        'seq': int(frame_id),
        'timestamp_ms': int(timestamp_ms),
        'flags': int(flags),
        'report_mode': report_mode_text,
        'downsample_step': int(downsample_step),
        'upload_points': int(upload_points),
        'heartbeat_ms': int(heartbeat_ms),
        'min_interval_ms': int(min_interval_ms),
        'http_timeout_ms': int(http_timeout_ms),
        'chunk_kb': int(chunk_kb),
        'chunk_delay_ms': int(chunk_delay_ms),
        'channels': channels,
    }

    return FullFrameBinaryDecodeResult(
        payload=payload,
        rx_bytes=len(raw),
        channel_count=int(channel_count),
        waveform_max=waveform_max,
        fft_max=fft_max,
    )
