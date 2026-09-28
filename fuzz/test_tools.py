#!/usr/bin/env python3
"""Regression tests for dictionary validation and campaign failure reporting."""

import json
import os
import pathlib
import select
import subprocess
import sys
import tempfile
import unittest

from generate_vocabulary import generate
from run_campaign import run_process


class VocabularyTest(unittest.TestCase):
    def test_preserves_binary_tokens(self):
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / "input.dict"
            output = pathlib.Path(directory) / "output.h"
            source.write_text('# comment\nnul="\\x00"\nhi="\\xff\\x80"\n')
            generate(source, output)
            self.assertIn("{0}", output.read_text())
            self.assertIn("{255,128}", output.read_text())

    def test_rejects_empty_malformed_and_duplicate_vocabularies(self):
        for content in ("# empty\n", 'bad="\\x0g"\n', 'one="\\x00"\ntwo="\\x00"\n'):
            with (
                self.subTest(content=content),
                tempfile.TemporaryDirectory() as directory,
            ):
                source = pathlib.Path(directory) / "input.dict"
                source.write_text(content)
                with self.assertRaises(ValueError):
                    generate(source, pathlib.Path(directory) / "output.h")


class CampaignTest(unittest.TestCase):
    def test_timeout_terminates_worker_processes(self):
        read_fd, write_fd = os.pipe()
        try:
            with os.fdopen(write_fd, "w") as log:
                code = run_process(
                    [
                        sys.executable,
                        "-c",
                        (
                            "import os, signal\n"
                            "if os.fork() == 0:\n"
                            " print('worker started', flush=True)\n"
                            " signal.pause()\n"
                            "else:\n"
                            " os.wait()\n"
                        ),
                    ],
                    os.environ.copy(),
                    log,
                    timeout=5,
                )
            self.assertEqual(code, 124)

            # EOF proves the worker also closed its inherited stdout pipe.
            output = b""
            while True:
                ready, _, _ = select.select([read_fd], [], [], 5)
                self.assertTrue(ready, "a worker survived the campaign timeout")
                chunk = os.read(read_fd, 4096)
                if not chunk:
                    break
                output += chunk
            self.assertIn(b"worker started", output)
        finally:
            os.close(read_fd)

    def test_records_every_property_and_propagates_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            fake = root / "fake_fuzzer"
            fake.write_text(
                f"#!{sys.executable}\n"
                "import os, pathlib, sys\n"
                "if '--list_fuzz_tests=1' in sys.argv:\n"
                " print('[*] Fuzz test: Example.pass')\n"
                " print('[*] Fuzz test: Example.fail')\n"
                " sys.exit(0)\n"
                "pathlib.Path(os.environ['FUZZTEST_TESTSUITE_OUT_DIR'], 'sample').write_text('saved')\n"
                "print('property output')\n"
                "sys.exit(7 if '--fuzz=Example.fail' in sys.argv else 0)\n"
            )
            fake.chmod(0o700)
            output = root / "evidence"
            command = [
                sys.executable,
                str(pathlib.Path(__file__).with_name("run_campaign.py")),
                str(fake),
                "--seconds",
                "1",
                "--jobs",
                "2",
                "--output",
                str(output),
                "--corpus",
                str(root / "corpus"),
            ]
            result = subprocess.run(
                command, capture_output=True, text=True, check=False
            )
            self.assertEqual(result.returncode, 1, result.stderr)
            summary = json.loads((output / "summary.json").read_text())
            self.assertEqual(
                {r["test"]: r["returncode"] for r in summary["results"]},
                {"Example.pass": 0, "Example.fail": 7},
            )
            for name in ("Example.pass", "Example.fail"):
                self.assertIn("property output", (output / f"{name}.log").read_text())
                self.assertEqual(
                    (root / "corpus" / name / "sample").read_text(), "saved"
                )
            # Existing evidence must survive an accidental reuse of --output.
            original = (output / "summary.json").read_bytes()
            rerun = subprocess.run(command, capture_output=True, text=True, check=False)
            self.assertNotEqual(rerun.returncode, 0)
            self.assertEqual((output / "summary.json").read_bytes(), original)


if __name__ == "__main__":
    unittest.main()
