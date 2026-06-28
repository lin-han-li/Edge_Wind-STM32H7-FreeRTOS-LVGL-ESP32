import unittest

from edgewind.routes import api


def _candidate(fault_code, marker, seq, *, endpoint="/api/node/full_frame_bin", mode="full", has_wave=True):
    processed_channels = []
    raw_lens = []
    for ch_id in range(4):
        wave = [float(marker + ch_id), float(marker + ch_id + 0.1)] if has_wave else []
        spec = [float(marker + ch_id + 0.2)] if has_wave else []
        processed_channels.append({
            "id": ch_id,
            "label": f"ch{ch_id}",
            "type": "test",
            "unit": "V",
            "value": float(marker + ch_id),
            "current_value": float(marker + ch_id),
            "waveform": wave,
            "fft_spectrum": spec,
        })
        raw_lens.append((ch_id, 4096 if has_wave else 0, 2048 if has_wave else 0))
    data = {
        "node_id": "NODE_TEST",
        "fault_code": fault_code,
        "seq": seq,
        "report_mode": mode,
        "channels": processed_channels,
    }
    return api._build_snapshot_candidate(
        node_id="NODE_TEST",
        fault_code=fault_code,
        data=data,
        processed_channels=processed_channels,
        raw_series_lens=raw_lens,
        request_tag=endpoint,
        report_mode=mode,
        current_timestamp=float(seq),
        is_bad_frame=not has_wave,
        is_empty_keepalive=False,
    )


class SnapshotStateTest(unittest.TestCase):
    def setUp(self):
        api._reset_snapshot_state_for_tests()
        api.node_snapshot_saved.clear()
        self.saved = []
        self._orig_submit = api._submit_snapshot_save

        def fake_submit(node_id, fault_code, snapshot_type, frame, event_timestamp):
            self.saved.append((node_id, fault_code, snapshot_type, frame.get("data") if frame else None, event_timestamp))
            return frame is not None

        api._submit_snapshot_save = fake_submit

    def tearDown(self):
        api._submit_snapshot_save = self._orig_submit
        api._reset_snapshot_state_for_tests()
        api.node_snapshot_saved.clear()

    def test_fault_snapshot_uses_last_normal_and_second_fault_frame(self):
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 10, 1))
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 20, 2))
        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 30, 3))
        self.assertEqual(self.saved, [])

        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 40, 4))

        self.assertEqual([row[2] for row in self.saved], ["before", "after"])
        before_frame = self.saved[0][3]
        after_frame = self.saved[1][3]
        self.assertEqual(before_frame["channels"][0]["waveform"][0], 20.0)
        self.assertEqual(after_frame["channels"][0]["waveform"][0], 40.0)
        self.assertEqual(self.saved[0][4], self.saved[1][4])

    def test_recovery_snapshot_uses_last_fault_and_second_normal_frame(self):
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 10, 1))
        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 20, 2))
        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 30, 3))
        self.saved.clear()

        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 40, 4))
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 50, 5))
        self.assertEqual(self.saved, [])

        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 60, 6))

        self.assertEqual([row[2] for row in self.saved], ["before_recovery", "after_recovery"])
        before_frame = self.saved[0][3]
        after_frame = self.saved[1][3]
        self.assertEqual(before_frame["channels"][0]["waveform"][0], 40.0)
        self.assertEqual(after_frame["channels"][0]["waveform"][0], 60.0)
        self.assertEqual(self.saved[0][4], self.saved[1][4])

    def test_summary_heartbeat_and_empty_waveforms_do_not_drive_snapshots(self):
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 10, 1))
        api._process_snapshot_candidate("NODE_TEST", "E00", _candidate("E00", 20, 2))

        summary = _candidate("E01", 30, 3, mode="summary")
        heartbeat = _candidate("E01", 40, 4, endpoint="/api/node/heartbeat")
        empty = _candidate("E01", 50, 5, has_wave=False)
        self.assertIsNone(summary)
        self.assertIsNone(heartbeat)
        self.assertIsNone(empty)
        api._process_snapshot_candidate("NODE_TEST", "E01", summary)
        api._process_snapshot_candidate("NODE_TEST", "E01", heartbeat)
        api._process_snapshot_candidate("NODE_TEST", "E01", empty)
        self.assertEqual(self.saved, [])

        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 60, 6))
        self.assertEqual(self.saved, [])
        api._process_snapshot_candidate("NODE_TEST", "E01", _candidate("E01", 70, 7))
        self.assertEqual([row[2] for row in self.saved], ["before", "after"])
        self.assertEqual(self.saved[1][3]["channels"][0]["waveform"][0], 70.0)


if __name__ == "__main__":
    unittest.main()
