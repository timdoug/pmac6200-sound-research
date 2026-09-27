#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, Tim Douglas
"""Verify tracked evidence strictly and report separately supplied originals."""

import argparse
import hashlib
from pathlib import Path, PurePosixPath
import re


def read_manifest(manifest):
    entries = []
    seen = set()
    for number, line in enumerate(manifest.read_text(encoding="utf-8").splitlines(), 1):
        match = re.fullmatch(r"([0-9a-fA-F]{64})  (.+)", line)
        if not match:
            raise ValueError(f"invalid manifest entry on line {number}")
        expected, name = match.groups()
        relative = PurePosixPath(name)
        if (relative.is_absolute() or ".." in relative.parts or not relative.parts
                or str(relative) != name or "\\" in name or ":" in name):
            raise ValueError(f"unsafe manifest path on line {number}: {name}")
        if name in seen:
            raise ValueError(f"duplicate manifest path: {name}")
        seen.add(name)
        entries.append((expected.lower(), relative))
    if not entries:
        raise ValueError("empty manifest")
    return entries


def verify_manifest(manifest, base, allow_missing=False):
    """Return (verified, missing, failed); optional means missing, never mismatched."""
    try:
        entries = read_manifest(manifest)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"FAILED {manifest.name}: {error}")
        return 0, 0, 1
    base = base.resolve()
    checked = missing = failed = 0
    for expected, relative in entries:
        source = base / relative
        try:
            source.resolve().relative_to(base)
            if source.is_symlink():
                raise ValueError("symlink is not an original evidence file")
            if not source.exists():
                print(f"MISSING {relative}")
                missing += 1
                failed += int(not allow_missing)
                continue
            digest = hashlib.sha256()
            with source.open("rb") as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b""):
                    digest.update(chunk)
            if digest.hexdigest() != expected:
                raise ValueError("SHA-256 mismatch")
            checked += 1
        except (OSError, ValueError) as error:
            print(f"FAILED {relative}: {error}")
            failed += 1
    print(f"{manifest.name}: {checked} verified; {missing} missing; {failed} failed")
    return checked, missing, failed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--require-all", action="store_true",
                        help="also require all 62 originals, including the withheld CD capture")
    parser.add_argument("--captures", action="store_true",
                        help="also require the four separately supplied tone captures")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    manifests = root / "manifests"
    checks = [verify_manifest(manifests / "preserved-files.sha256", root)]
    checks.append(verify_manifest(manifests / "publication-checks.sha256", root))
    checks.append(verify_manifest(manifests / "original-evidence.sha256",
                                  root / "probes", allow_missing=not args.require_all))
    if args.captures:
        checks.append(verify_manifest(manifests / "tone-captures.sha256", root))
    return int(any(failed for _, _, failed in checks))


if __name__ == "__main__":
    raise SystemExit(main())
