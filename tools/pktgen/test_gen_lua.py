#!/usr/bin/env python3
from __future__ import annotations

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(DIR))

import gen_lua  # noqa: E402


class GenLuaTest(unittest.TestCase):
    def test_frame_size(self) -> None:
        self.assertEqual(gen_lua.frame_size(4), 46)
        self.assertEqual(gen_lua.frame_size(1400), 1442)

    def test_hot_fixed_sport(self) -> None:
        block = gen_lua.src_port_block("hot", 1)
        self.assertIn('"start", 4000', block)
        self.assertIn('"inc", 0', block)

    def test_flows_range(self) -> None:
        block = gen_lua.src_port_block("flows", 1000)
        self.assertIn('"start", 4000', block)
        self.assertIn('"inc", 1', block)
        self.assertIn('"max", 4999', block)

    def test_flows_10000_in_range(self) -> None:
        block = gen_lua.src_port_block("flows", 10000)
        self.assertIn('"max", 13999', block)

    def test_unknown_pattern_raises(self) -> None:
        with self.assertRaises(ValueError):
            gen_lua.src_port_block("bogus", 1000)

    def test_cli_writes_summary_fields(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "m01.lua"
            subprocess.check_call(
                [
                    sys.executable,
                    str(DIR / "gen_lua.py"),
                    "--case",
                    "M01",
                    "--pattern",
                    "hot",
                    "--flows",
                    "1",
                    "--payload",
                    "4",
                    "-o",
                    str(out),
                ],
                cwd=str(DIR),
            )
            text = out.read_text()
            self.assertIn("PKTGEN_SUMMARY case=M01 pattern=hot", text)
            self.assertIn("payload=4", text)
            self.assertIn("flows=1", text)
            self.assertNotIn("mode=", text)
            self.assertNotIn("sut=", text)
            head = text.split("pktgen.screen", 1)[0]
            self.assertNotRegex(head, r'(?m)^\s*require\s*[("\']Pktgen')
            self.assertIn("pktgen.screen", text)
            self.assertIn("portStats(p)", text)
            self.assertIn("row.curr", text)


if __name__ == "__main__":
    unittest.main()
