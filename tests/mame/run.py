#!/usr/bin/env python3
# license:BSD-3-Clause
# copyright-holders:Tim Douglas
"""Run diskless Apple sound checks with isolated writable state (no third-party modules)."""

import argparse
from array import array
from concurrent.futures import ThreadPoolExecutor
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import wave


def startup_peaks(path):
    with wave.open(str(path), "rb") as audio:
        if audio.getsampwidth() != 2:
            raise RuntimeError("expected 16-bit PCM capture")
        if audio.getnchannels() < 2:
            raise RuntimeError("expected at least two speaker channels")
        values = array("h", audio.readframes(audio.getnframes()))
        if sys.byteorder != "little":
            values.byteswap()
        # First two channels are the main speakers; exclude floppy motor audio.
        peaks = [max((abs(v) for v in values[ch::audio.getnchannels()]), default=0)
                 for ch in range(2)]
        if min(peaks) < 100:
            raise RuntimeError(f"missing startup audio: channel peaks {peaks}")
        return peaks


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mame", type=Path)
    parser.add_argument("--rompath", required=True, help="MAME ROM search path; no ROMs are distributed here")
    parser.add_argument("--smoke", action="store_true", help="also test ROM startup audio on related Macs")
    parser.add_argument("--output", type=Path, help="parent for a new, uniquely named results directory")
    args = parser.parse_args()
    binary = str(args.mame.resolve())
    if not args.mame.is_file() or not os.access(binary, os.X_OK):
        parser.error("mame must name an existing executable")
    if args.output is not None and not args.output.is_dir():
        parser.error("--output must name an existing parent directory")
    script = str(Path(__file__).with_name("primetime2.lua").resolve())
    result_dir = Path(tempfile.mkdtemp(prefix="pmac-sound-", dir=args.output))
    print(f"Results: {result_dir}", flush=True)
    env = dict(os.environ, SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")

    def run(label, system, extra, fixture):
        dest = result_dir / label
        dest.mkdir()
        cmd = [binary, system, "-noreadconfig", "-video", "none", "-sound", "none",
               "-nothrottle", "-skip_gameinfo", "-seconds_to_run", "5" if fixture else "15",
               "-rompath", args.rompath, "-nvram_directory", str(dest / "nvram"),
               "-cfg_directory", str(dest / "cfg"), "-state_directory", str(dest / "sta"),
               "-snapshot_directory", str(dest / "snap")]
        if fixture:
            cmd += ["-autoboot_script", script, "-autoboot_delay", "2"]
        else:
            cmd += ["-wavwrite", str(dest / "startup.wav")]
        cmd += extra
        try:
            result = subprocess.run(cmd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                    text=True, timeout=180)
            (dest / "output.log").write_text(result.stdout, encoding="utf-8")
            if result.returncode:
                raise RuntimeError(f"MAME exit status {result.returncode}; see output.log")
            if fixture:
                if "PMAC_SOUND_TEST_PASS" not in result.stdout or "[LUA ERROR]" in result.stdout:
                    raise RuntimeError("Lua fixture did not pass; see output.log")
                detail = "device assertions passed"
            else:
                peaks = startup_peaks(dest / "startup.wav")
                detail = f"startup audio peaks {peaks} (not an OS boot/fidelity test)"
            print(f"PASS {label}: {detail}", flush=True)
            return True
        except (OSError, RuntimeError, subprocess.TimeoutExpired, wave.Error) as error:
            print(f"FAIL {label}: {error}", flush=True)
            return False

    jobs = [(f"fixture-{rate}", "pmac6200", ["-samplerate", str(rate)], True)
            for rate in (4000, 22050, 48000, 96000)]
    if args.smoke:
        jobs += [(system, system, [], False)
                 for system in ("pmac6200", "pmac5200", "macqd630", "macqd605", "maclc520")]
        jobs += [(f"maclc580-{bios}", "maclc580", ["-bios", bios], False)
                 for bios in ("older", "later")]
    with ThreadPoolExecutor(max_workers=2) as pool:
        results = list(pool.map(lambda job: run(*job), jobs))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
