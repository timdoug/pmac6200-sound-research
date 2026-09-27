#!/usr/bin/env python3
"""Prepare ignored probe headers from a separately obtained ASCTester checkout."""

import argparse
import hashlib
from pathlib import Path
import subprocess


REVISION = "ea57fffb4f72a7d10839b9ca7d21d68b439d0d32"
BASE_SHA256 = "01eeddc14de62b63ed102d3f136aae086acb0c89654f497b3251dd59ee5a7a50"
RESULT_SHA256 = "a4fc2f47b50e2aa1cca7d0dbd160b26fd5a6c65ee2add43e2cb4462bec50031c"
PROBES = (
    "CDPlay", "ClockProbe", "DFACProbe", "FIFOProbe", "FilterProbe", "I2CProbe",
    "JackProbe", "RampProbe", "RecProbe", "ResetProbe", "StateProbe", "VolPath",
    "XAProbe",
)
WORD_HELPERS = b"""// Reads an ASC register as a 16-bit word.  The 16-bit sample FIFO window lives above the
// byte-wide registers, so offsets here can exceed $FFF.
static inline uint16_t ascReadReg16(uint16_t offset)
{
\treturn *(volatile uint16_t *)((*(volatile uint8_t **)ASCBase) + offset);
}

// Writes an ASC register as a 16-bit word
static inline void ascWriteReg16(uint16_t offset, uint16_t value)
{
\t*(volatile uint16_t *)((*(volatile uint8_t **)ASCBase) + offset) = value;
}

"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("asctester", type=Path, help="separate ASCTester Git checkout")
    args = parser.parse_args()
    try:
        original = subprocess.check_output([
            "git", "-C", str(args.asctester.resolve()), "show",
            f"{REVISION}:asctester.h",
        ])
    except (OSError, subprocess.CalledProcessError) as error:
        parser.error(f"cannot read pinned ASCTester header: {error}")
    if hashlib.sha256(original).hexdigest() != BASE_SHA256:
        parser.error("unexpected upstream header; no files changed")
    marker = b"// Reads a VIA2 register\n"
    if original.count(marker) != 1:
        parser.error("unexpected upstream layout; no files changed")
    result = original.replace(marker, WORD_HELPERS + marker, 1)
    if hashlib.sha256(result).hexdigest() != RESULT_SHA256:
        parser.error("prepared header does not match the measured version")

    root = Path(__file__).resolve().parents[1] / "probes"
    targets = [root / name / "asctester.h" for name in PROBES]
    for target in targets:
        if not target.parent.is_dir() or target.is_symlink():
            parser.error(f"unsafe or missing destination: {target}")
        if target.exists() and target.read_bytes() != result:
            parser.error(f"refusing to overwrite different header: {target}")
    for target in targets:
        if not target.exists():
            with target.open("xb") as output:
                output.write(result)
        print(f"OK {target.relative_to(root.parent)}")
    print("Headers match the original measurements; they remain ignored by Git.")


if __name__ == "__main__":
    main()
