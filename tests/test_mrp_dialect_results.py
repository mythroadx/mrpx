import csv
import json
import tempfile
import unittest
from pathlib import Path

from tools import mrp_dialect_results


class DialectResultsTests(unittest.TestCase):
    def test_records_compile_runtime_and_event_results(self):
        report = mrp_dialect_results.parse_log(
            "runtime log\nDIALECT|started\n"
            "DIALECT|supported|ok|7\n"
            "DIALECT|unsupported|compile_error|near `-'\n"
            "DIALECT|runtime|runtime_error|bad argument\n"
            "DIALECT|complete|3\nDIALECT_EVENT|0|18|0\n"
        )
        self.assertEqual(report["status_counts"],
                         {"ok": 1, "compile_error": 1, "runtime_error": 1})
        self.assertEqual(report["events"], [{"code": "0", "p0": "18", "p1": "0"}])
        self.assertEqual(report["cases"][1]["result"], "near `-'")

    def test_rejects_incomplete_ambiguous_or_malformed_logs(self):
        logs = (
            "DIALECT|started\nDIALECT|one|ok|1\n",
            "DIALECT|started\nDIALECT|one|ok|1\nDIALECT|complete|2\n",
            "DIALECT|started\nDIALECT|one|ok|1\nDIALECT|one|ok|2\nDIALECT|complete|2\n",
            "DIALECT|started\nDIALECT|one|unknown|1\nDIALECT|complete|1\n",
            "DIALECT|started\nDIALECT|one|ok|1\nDIALECT|complete|1\nDIALECT|started\n",
            "DIALECT|started\nDIALECT|one|ok|1\nDIALECT|complete|1\nDIALECT_EVENT|0|18\n",
            "DIALECT_EVENT|0|18|0\nDIALECT|started\nDIALECT|one|ok|1\nDIALECT|complete|1\n",
        )
        for log in logs:
            with self.subTest(log=log), self.assertRaises(ValueError):
                mrp_dialect_results.parse_log(log)

    def test_writes_json_csv_without_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            log = root / "probe.log"
            log.write_text("DIALECT|started\nDIALECT|one|ok|1\nDIALECT|complete|1\n",
                           encoding="utf-8")
            output = root / "results"
            mrp_dialect_results.write_results(log, output)
            data = json.loads((output / "dialect-results.json").read_text(encoding="utf-8"))
            with (output / "dialect-results.csv").open(encoding="utf-8-sig", newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertEqual(rows, [{"name": "one", "status": "ok", "result": "1"}])
            self.assertEqual(data["case_count"], 1)
            self.assertEqual(data["status_counts"],
                             {"ok": 1, "compile_error": 0, "runtime_error": 0})
            before = (output / "dialect-results.json").read_bytes()
            with self.assertRaises(ValueError):
                mrp_dialect_results.write_results(log, output)
            self.assertEqual((output / "dialect-results.json").read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
