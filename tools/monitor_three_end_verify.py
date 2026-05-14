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
ESP_REPORT_START_RE = re.compile(r"report start frame=(\d+).*?\blen=(\d+)")
ESP_REPORT_START_OPTIONS_RE = re.compile(
    r"chunk_kb=(\d+).*?chunk_delay=(\d+).*?effective_delay=(\d+).*?write_chunk=(\d+)"
)
ESP_REPORT_READ_RE = re.compile(
    r"report stage=read frame=(\d+).*?\blen=(\d+).*?err=([A-Z_]+).*?http=(\d+).*?total=(\d+).*?open=(\d+).*?stream=(\d+).*?fetch=(\d+).*?read=(\d+)"
)
ESP_DROPPED_INVALID_RE = re.compile(r"Dropped invalid RX packet,\s+reason=(\d+)")


def is_cloud_error_line(line):
    lower = line.lower().replace("error=none", "")
    return any(k in lower for k in ["node_timeout", "offline", "bad-frame", "traceback", "error", " 500 ", " 502 ", "timeout"])


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
            "cloud_raw_4096_2048": 0,
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
            elif source == "cloud":
                if "[/api/node/full_frame_bin][series]" in line:
                    self.stats["cloud_full_series"] += 1
                if "raw_lens=[(0, 4096, 2048)" in line and "emit_lens=[(0, 4096, 2048)" in line:
                    self.stats["cloud_raw_4096_2048"] += 1
                if is_cloud_error_line(line):
                    self.stats["cloud_errors"] += 1
                    if len(self.stats["cloud_errors_text"]) < 20:
                        self.stats["cloud_errors_text"].append(line)

    def serial_thread(self, port, baud, path, source):
        try:
            with serial.Serial(port, baudrate=baud, timeout=0.2) as ser:
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
        while not self.stop.is_set():
            cmd = [
                "ssh",
                "-F",
                self.args.ssh_config,
                "aliyun-ubuntu",
                "journalctl -u edge_wind.service -n 0 -f --no-pager",
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
                time.sleep(2.0)

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

    def fetch_cloud_journal_counts(self, since_epoch):
        cmd = [
            "ssh",
            "-F",
            self.args.ssh_config,
            "aliyun-ubuntu",
            f"journalctl -u edge_wind.service --since @{int(since_epoch)} --no-pager",
        ]
        result = {
            "cloud_journal_full_series": 0,
            "cloud_journal_raw_4096_2048": 0,
            "cloud_journal_errors": 0,
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
            for line in text.splitlines():
                if "[/api/node/full_frame_bin][series]" in line:
                    result["cloud_journal_full_series"] += 1
                if "raw_lens=[(0, 4096, 2048)" in line and "emit_lens=[(0, 4096, 2048)" in line:
                    result["cloud_journal_raw_4096_2048"] += 1
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
        payload["elapsed_s"] = round(elapsed, 1) if elapsed is not None else None
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
        payload["esp_http_success_rate"] = self.success_rate(payload["esp_report_read_ok"], payload["esp_report_read_fail"])
        return payload

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
        summary["cloud_effective_raw_4096_2048"] = max(
            summary.get("cloud_raw_4096_2048", 0),
            summary.get("cloud_journal_raw_4096_2048", 0),
        )
        with open(summary_json, "w", encoding="utf-8") as f:
            json.dump(summary, f, ensure_ascii=False, indent=2)
        self.write_progress(progress_json, self.args.duration)
        print(json.dumps(summary, ensure_ascii=False, indent=2))


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
    Monitor(parser.parse_args()).run()


if __name__ == "__main__":
    main()
