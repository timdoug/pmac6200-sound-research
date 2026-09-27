# Historical automation: not supported entry points

`workspace-scripts/` contains byte-preserved scripts from the original MAME
workspace, renamed with `.txt` and made non-executable. Some forcibly terminate
emulators, delete/recreate disk or NVRAM files, use fixed temporary paths, or
depend on local baseline disks. Do not rename and run them on a working setup.
They are archived to explain earlier experiments, not recommended for reuse.

Use `tests/mame/run.py` for current diskless emulator checks.

`rom-analysis-tools/` preserves the original analysis helpers. Some embed the
original owner's filesystem paths. Supply your own ROMs and adjust a working
copy if using them; no extracted ROM/OS binaries or full disassembly listings
are included. The original `cordyceps-notes/dis/` and Git bundle are excluded.
