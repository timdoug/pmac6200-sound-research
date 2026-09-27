# Working with this research

Preserve original measurements, reports and probe versions. Add new dated
artifacts rather than replacing old evidence or recomputing an old hash to
make a changed file appear original. Clearly separate hardware observations,
ROM-derived inferences, emulator results and provisional modelling choices.

For new hardware runs, record model/board revision, ROM and OS, source revision,
toolchain, physical connections, interface settings and the procedure. If a
value is unknown, say so. Attribute dependencies and do not add ROMs, OS images,
borrowed unlicensed source, third-party recordings or private machine state.

For tooling changes, run from the repository root:

```sh
python3 -m unittest discover -s tests/unit -v
python3 tools/verify_evidence.py
git diff --check
```

These lightweight checks run in CI without hardware, ROMs, captures, NumPy or
an external ASCTester checkout. They test repository/tooling integrity, not
hardware correctness. MAME device/smoke checks and hardware probes remain
separate, opt-in procedures. Historical notes may retain original whitespace.

BSD-3-Clause applies to the owner's original work; preserve other contributors'
notices and the exceptions in `THIRD_PARTY.md`. Explain AI assistance where used.
