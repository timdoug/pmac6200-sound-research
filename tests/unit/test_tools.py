# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Tim Douglas
"""Publication tooling checks, with synthetic data and no external dependencies."""

from contextlib import redirect_stdout
import hashlib
import importlib.util
import io
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
import wave


ROOT = Path(__file__).resolve().parents[2]


def load_module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


evidence = load_module("evidence", "tools/verify_evidence.py")
headers = load_module("headers", "tools/prepare_headers.py")
runner = load_module("runner", "tests/mame/run.py")


class EvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.manifest = self.root / "test.sha256"
        self.content = b"synthetic evidence\n"
        self.digest = hashlib.sha256(self.content).hexdigest()
        self.manifest.write_text(f"{self.digest}  report.txt\n")

    def verify(self, optional=False):
        with redirect_stdout(io.StringIO()):
            return evidence.verify_manifest(self.manifest, self.root, optional)

    def test_valid(self):
        (self.root / "report.txt").write_bytes(self.content)
        self.assertEqual(self.verify(), (1, 0, 0))

    def test_required_missing_fails(self):
        self.assertEqual(self.verify(), (0, 1, 1))

    def test_optional_missing_is_reported(self):
        self.assertEqual(self.verify(optional=True), (0, 1, 0))

    def test_mismatch_is_never_optional(self):
        (self.root / "report.txt").write_bytes(b"changed")
        self.assertEqual(self.verify(optional=True), (0, 0, 1))

    def test_invalid_and_duplicate_entries_fail(self):
        entries = ("", "nonsense\n", f"{'x' * 64}  report.txt\n",
                   f"{self.digest}  report.txt\n" * 2)
        for entry in entries:
            with self.subTest(entry=entry):
                self.manifest.write_text(entry)
                self.assertEqual(self.verify(), (0, 0, 1))

    def test_unsafe_paths_fail(self):
        for name in ("../outside", "/absolute", "a/../b", "C:/outside", "a\\b", "./a"):
            with self.subTest(name=name):
                self.manifest.write_text(f"{self.digest}  {name}\n")
                self.assertEqual(self.verify(), (0, 0, 1))

    def test_symlink_is_rejected(self):
        (self.root / "real.txt").write_bytes(self.content)
        (self.root / "report.txt").symlink_to(self.root / "real.txt")
        self.assertEqual(self.verify(), (0, 0, 1))

    def test_parent_symlink_cannot_escape_root(self):
        base = self.root / "base"
        base.mkdir()
        (self.root / "outside.txt").write_bytes(self.content)
        (base / "link").symlink_to(self.root, target_is_directory=True)
        self.manifest.write_text(f"{self.digest}  link/outside.txt\n")
        with redirect_stdout(io.StringIO()):
            self.assertEqual(evidence.verify_manifest(self.manifest, base), (0, 0, 1))

    def test_missing_manifest_is_an_error(self):
        self.manifest.unlink()
        self.assertEqual(self.verify(), (0, 0, 1))


class HeaderTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in headers.PROBES:
            (self.root / name).mkdir()

    def test_header_hash_and_insertion(self):
        # Synthetic input, not a vendored ASCTester header.
        original = b"prefix\n// Reads a VIA2 register\nsuffix\n"
        expected = original.replace(b"// Reads a VIA2 register\n",
                                    headers.WORD_HELPERS + b"// Reads a VIA2 register\n")
        with patch.object(headers, "BASE_SHA256", hashlib.sha256(original).hexdigest()), \
                patch.object(headers, "RESULT_SHA256", hashlib.sha256(expected).hexdigest()):
            self.assertEqual(headers.build_header(original), expected)

    def test_wrong_original_is_rejected(self):
        with self.assertRaises(ValueError):
            headers.build_header(b"wrong revision")

    def test_install_is_idempotent(self):
        with redirect_stdout(io.StringIO()):
            headers.install_headers(b"example", self.root)
            headers.install_headers(b"example", self.root)
        for name in headers.PROBES:
            self.assertEqual((self.root / name / "asctester.h").read_bytes(), b"example")

    def test_conflict_prevents_all_writes(self):
        conflict = self.root / headers.PROBES[-1] / "asctester.h"
        conflict.write_bytes(b"local edit")
        with self.assertRaises(ValueError):
            headers.install_headers(b"example", self.root)
        self.assertFalse((self.root / headers.PROBES[0] / "asctester.h").exists())
        self.assertEqual(conflict.read_bytes(), b"local edit")

    def test_symlinked_directory_is_rejected(self):
        target = self.root / headers.PROBES[0]
        target.rmdir()
        target.symlink_to(self.root / headers.PROBES[1], target_is_directory=True)
        with self.assertRaises(ValueError):
            headers.install_headers(b"example", self.root)


class StartupAudioTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / "synthetic.wav"

    def write_wave(self, channels, samples, width=2):
        with wave.open(str(self.path), "wb") as audio:
            audio.setnchannels(channels)
            audio.setsampwidth(width)
            audio.setframerate(48000)
            audio.writeframes(struct.pack(f"<{len(samples)}h", *samples) if width == 2
                              else bytes(samples))

    def test_stereo_passes(self):
        self.write_wave(2, [200, -300, -400, 500])
        self.assertEqual(runner.startup_peaks(self.path), [400, 500])

    def test_floppy_channel_is_excluded(self):
        self.write_wave(3, [200, -300, 0])
        self.assertEqual(runner.startup_peaks(self.path), [200, 300])

    def test_mono_must_not_pass_a_two_channel_check(self):
        self.write_wave(1, [200, 300])
        with self.assertRaisesRegex(RuntimeError, "two speaker channels"):
            runner.startup_peaks(self.path)

    def test_silent_channel_and_empty_capture_fail(self):
        for samples in ([200, 0], []):
            with self.subTest(samples=samples):
                self.write_wave(2, samples)
                with self.assertRaisesRegex(RuntimeError, "missing startup audio"):
                    runner.startup_peaks(self.path)

    def test_non_16_bit_capture_fails(self):
        self.write_wave(2, [128, 128], width=1)
        with self.assertRaisesRegex(RuntimeError, "16-bit"):
            runner.startup_peaks(self.path)


if __name__ == "__main__":
    unittest.main()
