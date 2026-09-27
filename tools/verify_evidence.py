#!/usr/bin/env python3
"""Verify the original 62-file evidence manifest without hiding missing files."""

import argparse
import hashlib
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-all", action="store_true",
                        help="fail if any separately supplied header/capture is missing")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifest = root / "manifests" / "original-evidence.sha256"
    checked = missing = failed = 0
    for line in manifest.read_text(encoding="utf-8").splitlines():
        expected, name = line.split("  ", 1)
        relative = Path(name)
        if relative.is_absolute() or ".." in relative.parts:
            parser.error(f"unsafe manifest path: {name}")
        source = root / "probes" / relative
        if not source.is_file():
            print(f"MISSING {name}")
            missing += 1
            continue
        digest = hashlib.sha256()
        with source.open("rb") as stream:
            for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(chunk)
        if digest.hexdigest() != expected:
            print(f"MISMATCH {name}")
            failed += 1
        else:
            checked += 1
    print(f"{checked} verified; {missing} missing; {failed} mismatched")
    return int(bool(failed or (args.require_all and missing) or not checked))


if __name__ == "__main__":
    raise SystemExit(main())
