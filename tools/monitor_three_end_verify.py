#!/usr/bin/env python3
import argparse
import json
import os
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

import serial


FULL_HTTP_DONE_RE = re.compile(r"full http done frame=(\d+).*?elapsed=(\d+)ms http=(\d+) result=(-?\d+)")
NACK_REASON_RE = re.compile(r"NACK ref_seq=\d+ reason=(\d+)")
STM_RX_INVALID_RE = re.compile(r"RX invalid:\s+([a-zA-Z0-9_]+)")
STM_SPI_STATS_RE = re.compile(r"\[ESP32SPI\]\[spi_stats\]\s+(.*)")
ESP_REPORT_START_RE = re.compile(r"report start frame=(\d+).*?\blen=(\d+)")
ESP_REPORT_START_OPTIONS_RE = re.compile(
    r"chunk_kb=(\d+).*?chunk_delay=(\d+).*?effective_delay=(\d+).*?write_chunk=(\d+)"
)
ESP_REPORT_READ_RE = re.compile(
    r"report stage=read frame=(\d+).*?\blen=(\d+).*?err=([A-Z_]+).*?http=(\d+).*?total=(\d+).*?open=(\d+).*?stream=(\d+).*?fetch=(\d+).*?read=(\d+)"
)
ESP_DROPPED_INVALID_RE = re.compile(r"Dropped invalid RX packet,\s+reason=(\d+)")
ESP_SPI_STATS_RE = re.compile(r"\bspi_stats\s+(.*)")
KV_INT_RE = re.compile(r"([A-Za-z0-9_]+)=(-?\d+)")
JOURNAL_SHORT_UNIX_RE = re.compile(r"^(\d+(?:\.\d+)?)\s")
CLOUD_RAW_FULL_SIG = "raw_lens=[(0, 4096, 2048)"
CLOUD_EMIT_LIMITED_SIG = "emit_lens=[(0, 1024, 512)"
CLOUD_EMIT_FULL_SIG = "emit_lens=[(0, 4096, 2048)"
CLOUD_EXPECTED_FULL_BODY_BYTES = 49348

NACK_REASON_NAMES = {
    "1": "CRC_FAIL",
    "2": "BAD_LENGTH",
    "3": "UNSUPPORTED_VERSION",
    "4": "QUEUE_FULL",
    "5": "BUSY",
    "6": "SESSION_MISMATCH",
    "7": "INVALID_STATE",
    "8": "INVALID_PAYLOAD",
}

SPI_BASELINE_10M = {
    "stm_nack_lines": 437,
    "stm_spi_invalid": 154,
    "stm_hal_spi_fail": 37,
    "esp_spi_invalid": 270,
}


def is_cloud_error_line(line):
    lower = line.lower().replace("error=none", "")
    return any(k in lower for k in ["node_timeout", "offline", "bad-frame", "traceback", "error", " 500 ", " 502 ", "timeout"])


def classify_cloud_series_lens(line):
    return {
        "raw_full": CLOUD_RAW_FULL_SIG in line,
        "emit_limited": CLOUD_EMIT_LIMITED_SIG in line,
        "emit_full_regression": CLOUD_EMIT_FULL_SIG in line,
    }


def parse_int_kv(text):
    return {key: int(value) for key, value in KV_INT_RE.findall(text)}


def named_counter(counter, names):
    return {
        names.get(str(key), str(key)): value
        for key, value in sorted(counter.items(), key=lambda item: int(item[0]) if str(item[0]).isdigit() else str(item[0]))
    }


class Monitor:
    def __init__(self, args):
        self.args = args
        self.stop = threading.Event()
        self.lock = threading.Lock()
        self.stats = {
            "stm_lines": 0,
            "esp_lines": 0,
            "cloud_lines": 0,
            "stm_full_starts": 0,
            "stm_full_ends": 0,
            "stm_http_done": 0,
            "stm_http_fail": 0,
            "stm_full_timeout": 0,
            "stm_nack_lines": 0,
            "stm_ready_timeout_lines": 0,
            "stm_holdoff": 0,
            "esp_report_starts": 0,
            "esp_report_read_ok": 0,
            "esp_report_read_fail": 0,
            "cloud_full_series": 0,
            "cloud_raw_full_series": 0,
            "cloud_emit_limited_series": 0,
            "cloud_emit_full_regression": 0,
            "cloud_reconnects": 0,
            "cloud_errors": 0,
            "serial_errors": [],
            "cloud_errors_text": [],
            "stm_nack_reasons": {},
            "stm_spi_invalid": {},
            "stm_hal_spi_fail": 0,
            "esp_spi_invalid_reasons": {},
        }
        self.start_time = None
        self.stm_begin_times = []
        self.stm_http_elapsed_ms = []
        self.stm_http_elapsed_timed = []
        self.esp_http_total_ms = []
        self.esp_http_total_timed = []
        self.esp_http_open_ms = []
        self.esp_http_stream_ms = []
        self.esp_http_fetch_ms = []
        self.esp_http_read_ms = []
        self.esp_payload_lengths = set()
        self.esp_start_chunk_kb = set()
        self.esp_start_chunk_delay_ms = set()
        self.esp_start_effective_delay_ms = set()
        self.esp_start_write_chunk_bytes = set()
        self.cloud_series_times = []
        self.stm_spi_stats_first = {}
        self.stm_spi_stats_last = {}
        self.esp_spi_stats_first = {}
        self.esp_spi_stats_last = {}
        self.cloud_proc = None

    @staticmethod
    def inc_counter(mapping, key):
        key = str(key)
        mapping[key] = int(mapping.get(key, 0)) + 1

    @staticmethod
    def success_rate(ok, fail):
        total = ok + fail
        return None if total <= 0 else ok / total

    def log_line(self, path, source, line):
        now = time.time()
        clean = line.rstrip("\r\n")
        with open(path, "a", encoding="utf-8", errors="replace") as f:
            f.write(f"{now:.3f} {clean}\n")
        self.handle_line(source, now, clean)

    def handle_line(self, source, now, line):
        with self.lock:
            self.stats[f"{source}_lines"] += 1
            if source == "stm":
                if "TX REPORT_FULL_BEGIN" in line:
                    self.stats["stm_full_starts"] += 1
                    self.stm_begin_times.append(now)
                if "TX REPORT_FULL_END" in line:
                    self.stats["stm_full_ends"] += 1
                m_stats = STM_SPI_STATS_RE.search(line)
                if m_stats:
                    parsed = parse_int_kv(m_stats.group(1))
                    if not self.stm_spi_stats_first:
                        self.stm_spi_stats_first = dict(parsed)
                    self.stm_spi_stats_last = parsed
                m = FULL_HTTP_DONE_RE.search(line)
                if m:
                    self.stats["stm_http_done"] += 1
                    elapsed_ms = int(m.group(2))
                    self.stm_http_elapsed_ms.append(elapsed_ms)
                    self.stm_http_elapsed_timed.append((now, elapsed_ms))
                    if m.group(3) != "200" or m.group(4) != "0":
                        self.stats["stm_http_fail"] += 1
                lower = line.lower()
                if "full wait timeout" in lower or "full result timeout" in lower:
                    self.stats["stm_full_timeout"] += 1
                if "NACK" in line:
                    self.stats["stm_nack_lines"] += 1
                    m_nack = NACK_REASON_RE.search(line)
                    if m_nack:
                        self.inc_counter(self.stats["stm_nack_reasons"], m_nack.group(1))
                if "READY timeout" in line:
                    self.stats["stm_ready_timeout_lines"] += 1
                if "holdoff" in lower:
                    self.stats["stm_holdoff"] += 1
                m_invalid = STM_RX_INVALID_RE.search(line)
                if m_invalid:
                    self.inc_counter(self.stats["stm_spi_invalid"], m_invalid.group(1))
                if "HAL_SPI_TransmitReceive failed" in line:
                    self.stats["stm_hal_spi_fail"] += 1
            elif source == "esp":
                m = ESP_REPORT_START_RE.search(line)
                if m:
                    self.stats["esp_report_starts"] += 1
                    self.esp_payload_lengths.add(int(m.group(2)))
                    m_start_options = ESP_REPORT_START_OPTIONS_RE.search(line)
                    if m_start_options:
                        self.esp_start_chunk_kb.add(int(m_start_options.group(1)))
                        self.esp_start_chunk_delay_ms.add(int(m_start_options.group(2)))
                        self.esp_start_effective_delay_ms.add(int(m_start_options.group(3)))
                        self.esp_start_write_chunk_bytes.add(int(m_start_options.group(4)))
                m = ESP_REPORT_READ_RE.search(line)
                if m:
                    self.esp_payload_lengths.add(int(m.group(2)))
                    total_ms = int(m.group(5))
                    self.esp_http_total_ms.append(total_ms)
                    self.esp_http_total_timed.append((now, total_ms))
                    self.esp_http_open_ms.append(int(m.group(6)))
                    self.esp_http_stream_ms.append(int(m.group(7)))
                    self.esp_http_fetch_ms.append(int(m.group(8)))
                    self.esp_http_read_ms.append(int(m.group(9)))
                    if m.group(3) == "ESP_OK" and m.group(4) == "200":
                        self.stats["esp_report_read_ok"] += 1
                    else:
                        self.stats["esp_report_read_fail"] += 1
                elif (
                    "report stage=" in line
                    and "err=ESP_OK" not in line
                    and any(k in line.lower() for k in ["fail", "timeout", "error", "esp_err"])
                ):
                    self.stats["esp_report_read_fail"] += 1
                m_invalid = ESP_DROPPED_INVALID_RE.search(line)
                if m_invalid:
                    self.inc_counter(self.stats["esp_spi_invalid_reasons"], m_invalid.group(1))
                m_stats = ESP_SPI_STATS_RE.search(line)
                if m_stats:
                    parsed = parse_int_kv(m_stats.group(1))
                    if not self.esp_spi_stats_first:
                        self.esp_spi_stats_first = dict(parsed)
                    self.esp_spi_stats_last = parsed
            elif source == "cloud":
                if "[/api/node/full_frame_bin][series]" in line:
                    self.stats["cloud_full_series"] += 1
                    m_ts = JOURNAL_SHORT_UNIX_RE.match(line)
                    self.cloud_series_times.append(float(m_ts.group(1)) if m_ts else now)
                lens = classify_cloud_series_lens(line)
                if lens["raw_full"]:
                    self.stats["cloud_raw_full_series"] += 1
                if lens["emit_limited"]:
                    self.stats["cloud_emit_limited_series"] += 1
                if lens["emit_full_regression"]:
                    self.stats["cloud_emit_full_regression"] += 1
                if is_cloud_error_line(line):
                    self.stats["cloud_errors"] += 1
                    if len(self.stats["cloud_errors_text"]) < 20:
                        self.stats["cloud_errors_text"].append(line)

    def serial_thread(self, port, baud, path, source):
        try:
            ser = serial.Serial()
            ser.port = port
            ser.baudrate = baud
            ser.timeout = 0.2
            ser.rtscts = False
            ser.dsrdtr = False
            # Avoid toggling ESP32 EN/BOOT through USB-UART auto-reset while
            # starting a passive monitor.
            ser.dtr = False
            ser.rts = False
            with ser:
                try:
                    ser.reset_input_buffer()
                except Exception:
                    pass
                while not self.stop.is_set():
                    raw = ser.readline()
                    if not raw:
                        continue
                    self.log_line(path, source, raw.decode("utf-8", errors="replace"))
        except Exception as exc:
            with self.lock:
                self.stats["serial_errors"].append(f"{source}:{port}:{exc}")

    def cloud_thread(self, path):
        reconnect_delay_s = 2.0
        while not self.stop.is_set():
            cmd = [
                "ssh",
                "-o",
                "ConnectTimeout=8",
                "-o",
                "ServerAliveInterval=15",
                "-o",
                "ServerAliveCountMax=2",
                "-F",
                self.args.ssh_config,
                "aliyun-ubuntu",
                "journalctl -u edge_wind.service -n 0 -f -o short-unix --no-pager",
            ]
            try:
                self.cloud_proc = subprocess.Popen(
                    cmd,
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                )
                while not self.stop.is_set():
                    if self.cloud_proc.stdout is None:
                        break
                    line = self.cloud_proc.stdout.readline()
                    if line:
                        self.log_line(path, "cloud", line)
                        if "[/api/node/full_frame_bin][series]" in line:
                            reconnect_delay_s = 2.0
                    elif self.cloud_proc.poll() is not None:
                        break
                    else:
                        time.sleep(0.1)
            except Exception as exc:
                with self.lock:
                    self.stats["cloud_errors_text"].append(f"cloud_ssh_error:{exc}")
                    self.stats["cloud_errors"] += 1
            finally:
                if self.cloud_proc and self.cloud_proc.poll() is None:
                    self.cloud_proc.terminate()
            if not self.stop.is_set():
                with self.lock:
                    self.stats["cloud_reconnects"] += 1
                time.sleep(reconnect_delay_s)
                reconnect_delay_s = min(30.0, reconnect_delay_s * 2.0)

    @staticmethod
    def percentiles(values):
        if not values:
            return {}
        vals = sorted(values)
        def pct(p):
            idx = min(len(vals) - 1, max(0, int(round((len(vals) - 1) * p))))
            return vals[idx]
        return {
            "count": len(vals),
            "avg": sum(vals) / len(vals),
            "p50": pct(0.50),
            "p95": pct(0.95),
            "p99": pct(0.99),
            "max": vals[-1],
        }

    def timed_percentiles(self, items, max_elapsed_s=None):
        if self.start_time is None:
            return {}
        values = []
        for ts, value in items:
            if max_elapsed_s is None or (ts - self.start_time) <= max_elapsed_s:
                values.append(value)
        return self.percentiles(values)

    def interval_percentiles(self, times, max_elapsed_s=None):
        if self.start_time is None:
            return {}
        intervals = []
        for prev, cur in zip(times, times[1:]):
            if max_elapsed_s is None or (cur - self.start_time) <= max_elapsed_s:
                intervals.append((cur - prev) * 1000.0)
        return self.percentiles(intervals)

    @staticmethod
    def counter_delta(first, last):
        if not first or not last:
            return {}
        keys = set(first) | set(last)
        return {
            key: max(0, int(last.get(key, 0)) - int(first.get(key, 0)))
            for key in keys
        }

    def fetch_cloud_journal_counts(self, since_epoch):
        cmd = [
            "ssh",
            "-F",
            self.args.ssh_config,
            "aliyun-ubuntu",
            f"journalctl -u edge_wind.service --since @{int(since_epoch)} -o short-unix --no-pager",
        ]
        result = {
            "cloud_journal_full_series": 0,
            "cloud_journal_raw_full_series": 0,
            "cloud_journal_emit_limited_series": 0,
            "cloud_journal_emit_full_regression": 0,
            "cloud_journal_series_interval_ms": {},
            "cloud_journal_errors": 0,
            "cloud_journal_runtime_errors": 0,
            "cloud_journal_error_text": "",
        }
        try:
            proc = subprocess.run(
                cmd,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                timeout=30,
            )
            text = proc.stdout or ""
            journal_series_times = []
            for line in text.splitlines():
                if "[/api/node/full_frame_bin][series]" in line:
                    result["cloud_journal_full_series"] += 1
                    m_ts = JOURNAL_SHORT_UNIX_RE.match(line)
                    if m_ts:
                        journal_series_times.append(float(m_ts.group(1)))
                lens = classify_cloud_series_lens(line)
                if lens["raw_full"]:
                    result["cloud_journal_raw_full_series"] += 1
                if lens["emit_limited"]:
                    result["cloud_journal_emit_limited_series"] += 1
                if lens["emit_full_regression"]:
                    result["cloud_journal_emit_full_regression"] += 1
                if is_cloud_error_line(line):
                    result["cloud_journal_runtime_errors"] += 1
                    if not result["cloud_journal_error_text"]:
                        result["cloud_journal_error_text"] = line[:500]
            result["cloud_journal_series_interval_ms"] = self.percentiles([
                (cur - prev) * 1000.0 for prev, cur in zip(journal_series_times, journal_series_times[1:])
            ])
            if proc.returncode != 0:
                result["cloud_journal_errors"] = 1
                result["cloud_journal_error_text"] = (proc.stderr or proc.stdout or "").strip()[:500]
        except Exception as exc:
            result["cloud_journal_errors"] = 1
            result["cloud_journal_error_text"] = str(exc)
        return result

    def build_common_metrics(self, elapsed=None):
        with self.lock:
            payload = dict(self.stats)
            times = list(self.stm_begin_times)
            stm_http_timed = list(self.stm_http_elapsed_timed)
            esp_http_timed = list(self.esp_http_total_timed)
            cloud_series_times = list(self.cloud_series_times)
            stm_spi_stats_first = dict(self.stm_spi_stats_first)
            stm_spi_stats_last = dict(self.stm_spi_stats_last)
            esp_spi_stats_first = dict(self.esp_spi_stats_first)
            esp_spi_stats_last = dict(self.esp_spi_stats_last)
        payload["elapsed_s"] = round(elapsed, 1) if elapsed is not None else None
        elapsed_min = ((elapsed if elapsed is not None else 0) / 60.0) or None
        stm_spi_stats_delta = self.counter_delta(stm_spi_stats_first, stm_spi_stats_last)
        esp_spi_stats_delta = self.counter_delta(esp_spi_stats_first, esp_spi_stats_last)
        payload["esp_payload_lengths"] = sorted(self.esp_payload_lengths)
        payload["esp_start_chunk_kb_values"] = sorted(self.esp_start_chunk_kb)
        payload["esp_start_chunk_delay_ms_values"] = sorted(self.esp_start_chunk_delay_ms)
        payload["esp_start_effective_delay_ms_values"] = sorted(self.esp_start_effective_delay_ms)
        payload["esp_start_write_chunk_bytes_values"] = sorted(self.esp_start_write_chunk_bytes)
        payload["stm_frame_interval_ms"] = self.interval_percentiles(times)
        payload["stm_frame_interval_ms_first60"] = self.interval_percentiles(times, 60.0)
        payload["stm_long_frame_intervals_gt5s"] = sum(1 for prev, cur in zip(times, times[1:]) if (cur - prev) * 1000.0 > 5000.0)
        payload["stm_long_frame_intervals_gt8s"] = sum(1 for prev, cur in zip(times, times[1:]) if (cur - prev) * 1000.0 > 8000.0)
        payload["stm_http_elapsed_ms"] = self.percentiles(self.stm_http_elapsed_ms)
        payload["stm_http_elapsed_ms_first60"] = self.timed_percentiles(stm_http_timed, 60.0)
        payload["esp_http_total_ms"] = self.percentiles(self.esp_http_total_ms)
        payload["esp_http_total_ms_first60"] = self.timed_percentiles(esp_http_timed, 60.0)
        payload["esp_http_open_ms"] = self.percentiles(self.esp_http_open_ms)
        payload["esp_http_stream_ms"] = self.percentiles(self.esp_http_stream_ms)
        payload["esp_http_fetch_ms"] = self.percentiles(self.esp_http_fetch_ms)
        payload["esp_http_read_ms"] = self.percentiles(self.esp_http_read_ms)
        payload["cloud_series_interval_ms"] = self.interval_percentiles(cloud_series_times)
        payload["cloud_series_interval_ms_first60"] = self.interval_percentiles(cloud_series_times, 60.0)
        payload["esp_http_success_rate"] = self.success_rate(payload["esp_report_read_ok"], payload["esp_report_read_fail"])
        payload["stm_nack_reasons_named"] = named_counter(payload["stm_nack_reasons"], NACK_REASON_NAMES)
        payload["esp_spi_invalid_reasons_named"] = named_counter(payload["esp_spi_invalid_reasons"], NACK_REASON_NAMES)
        payload["stm_spi_stats_first"] = stm_spi_stats_first
        payload["stm_spi_stats_last"] = stm_spi_stats_last
        payload["stm_spi_stats_delta"] = stm_spi_stats_delta
        payload["esp_spi_stats_first"] = esp_spi_stats_first
        payload["esp_spi_stats_last"] = esp_spi_stats_last
        payload["esp_spi_stats_delta"] = esp_spi_stats_delta
        frame_count = max(1, payload["stm_full_starts"])
        stm_rx_invalid_lines = sum(int(v) for v in payload["stm_spi_invalid"].values())
        esp_invalid_lines = sum(int(v) for v in payload["esp_spi_invalid_reasons"].values())
        stm_rx_invalid = max(stm_spi_stats_delta.get("rx_invalid", 0), stm_rx_invalid_lines)
        stm_nack_total = max(stm_spi_stats_delta.get("nack_total", 0), payload["stm_nack_lines"])
        stm_hal_fail = max(stm_spi_stats_delta.get("hal_fail", 0), payload["stm_hal_spi_fail"])
        esp_invalid_total = max(esp_spi_stats_delta.get("invalid_total", 0), esp_invalid_lines)
        payload["spi_retry_per_frame"] = round(stm_nack_total / frame_count, 4)
        payload["spi_rx_invalid_per_frame"] = round(stm_rx_invalid / frame_count, 4)
        payload["spi_hal_fail_per_frame"] = round(stm_hal_fail / frame_count, 4)
        payload["spi_baseline_10m"] = SPI_BASELINE_10M
        if elapsed_min:
            payload["spi_rates_per_min"] = {
                "stm_nack": stm_nack_total / elapsed_min,
                "stm_rx_invalid": stm_rx_invalid / elapsed_min,
                "stm_hal_fail": stm_hal_fail / elapsed_min,
                "esp_invalid": esp_invalid_total / elapsed_min,
            }
        return payload

    @staticmethod
    def validate_release_web_smooth(summary):
        failures = []

        def require(condition, message):
            if not condition:
                failures.append(message)

        require(summary.get("stm_full_starts", 0) > 0, "no STM32 full-frame starts observed")
        require(summary.get("stm_full_timeout", 0) == 0, "STM32 full-frame timeout observed")
        require(summary.get("stm_http_fail", 0) == 0, "STM32 HTTP failure observed")
        require(summary.get("stm_long_frame_intervals_gt5s", 0) == 0, "STM32 full-frame interval exceeded 5s")
        require(summary.get("esp_report_read_ok", 0) > 0, "no ESP32 successful full-frame HTTP read observed")
        success_rate = summary.get("esp_http_success_rate")
        require(success_rate is not None and success_rate >= 0.995, "ESP32 HTTP success rate below 99.5%")
        require(CLOUD_EXPECTED_FULL_BODY_BYTES in summary.get("esp_payload_lengths", []), "ESP32 full-frame body length 49348 not observed")
        require(summary.get("cloud_effective_full_series", 0) > 0, "no cloud full-frame series log observed")
        require(summary.get("cloud_effective_raw_full_series", 0) > 0, "cloud raw full 4096/2048 evidence missing")
        require(summary.get("cloud_effective_emit_limited_series", 0) > 0, "cloud emit limited 1024/512 evidence missing")
        require(summary.get("cloud_effective_emit_full_regression", 0) == 0, "cloud emitted full 4096/2048 data to UI")
        require(summary.get("cloud_errors", 0) == 0, "cloud live log errors observed")
        require(summary.get("cloud_journal_errors", 0) == 0, "cloud journal query failed")
        require(summary.get("cloud_journal_runtime_errors", 0) == 0, "cloud journal runtime errors observed")
        require(not summary.get("serial_errors"), "serial monitor errors observed")
        return failures

    def write_progress(self, path, elapsed):
        payload = self.build_common_metrics(elapsed)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(payload, f, ensure_ascii=False, indent=2)

    def run(self):
        out_dir = Path(self.args.out_dir)
        out_dir.mkdir(parents=True, exist_ok=True)
        prefix = self.args.prefix
        stm_log = out_dir / f"{prefix}_stm.log"
        esp_log = out_dir / f"{prefix}_esp.log"
        cloud_log = out_dir / f"{prefix}_cloud.log"
        progress_json = out_dir / f"{prefix}_progress.json"
        summary_json = out_dir / f"{prefix}_summary.json"

        for path in [stm_log, esp_log, cloud_log, progress_json, summary_json]:
            try:
                path.unlink()
            except FileNotFoundError:
                pass

        threads = [
            threading.Thread(target=self.serial_thread, args=(self.args.stm_port, self.args.stm_baud, stm_log, "stm"), daemon=True),
            threading.Thread(target=self.serial_thread, args=(self.args.esp_port, self.args.esp_baud, esp_log, "esp"), daemon=True),
            threading.Thread(target=self.cloud_thread, args=(cloud_log,), daemon=True),
        ]
        for thread in threads:
            thread.start()

        start = time.time()
        self.start_time = start
        next_progress = 0.0
        while True:
            elapsed = time.time() - start
            if elapsed >= self.args.duration:
                break
            if elapsed >= next_progress:
                self.write_progress(progress_json, elapsed)
                next_progress += self.args.progress_interval
            time.sleep(0.5)

        self.stop.set()
        if self.cloud_proc and self.cloud_proc.poll() is None:
            self.cloud_proc.terminate()
        for thread in threads:
            thread.join(timeout=3)

        summary = self.build_common_metrics(self.args.duration)
        summary.update({
                "duration_s": self.args.duration,
                "logs": {
                    "stm": str(stm_log),
                    "esp": str(esp_log),
                    "cloud": str(cloud_log),
                    "progress": str(progress_json),
                    "summary": str(summary_json),
                },
            })
        journal_counts = self.fetch_cloud_journal_counts(start - 5.0)
        summary.update(journal_counts)
        summary["cloud_effective_full_series"] = max(
            summary.get("cloud_full_series", 0),
            summary.get("cloud_journal_full_series", 0),
        )
        summary["cloud_effective_raw_full_series"] = max(
            summary.get("cloud_raw_full_series", 0),
            summary.get("cloud_journal_raw_full_series", 0),
        )
        summary["cloud_effective_emit_limited_series"] = max(
            summary.get("cloud_emit_limited_series", 0),
            summary.get("cloud_journal_emit_limited_series", 0),
        )
        summary["cloud_effective_emit_full_regression"] = max(
            summary.get("cloud_emit_full_regression", 0),
            summary.get("cloud_journal_emit_full_regression", 0),
        )
        assertion_failures = self.validate_release_web_smooth(summary) if self.args.assert_release_web_smooth else []
        summary["release_web_smooth_assert"] = {
            "enabled": bool(self.args.assert_release_web_smooth),
            "ok": not assertion_failures,
            "failures": assertion_failures,
        }
        with open(summary_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
        self.write_progress(progress_json, self.args.duration)
        print(json.dumps(summary, ensure_ascii=False, indent=2))
        if assertion_failures:
            print("release web smooth assertion failed:", file=sys.stderr)
            for failure in assertion_failures:
                print(f"- {failure}", file=sys.stderr)
            raise SystemExit(1)


def main():
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    parser = argparse.ArgumentParser()
    parser.add_argument("--duration", type=int, default=300)
    parser.add_argument("--out-dir", default="test_logs")
    parser.add_argument("--prefix", default=time.strftime("verify5m_%Y%m%d_%H%M%S"))
    parser.add_argument("--stm-port", default="COM7")
    parser.add_argument("--stm-baud", type=int, default=921600)
    parser.add_argument("--esp-port", default="COM4")
    parser.add_argument("--esp-baud", type=int, default=115200)
    parser.add_argument("--ssh-config", default=os.path.abspath("ALiYunFuWuQi/ssh_config"))
    parser.add_argument("--progress-interval", type=int, default=10)
    parser.add_argument("--assert-release-web-smooth", action="store_true")
    Monitor(parser.parse_args()).run()


if __name__ == "__main__":
    main()
