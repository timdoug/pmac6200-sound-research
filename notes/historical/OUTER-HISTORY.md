# Outer investigation repo — archived

The original work happened in an outer git repo at `~/mame0289s` (a checked-in
MAME 0.289 tarball with the early Piltdown/Cordyceps investigation committed on
top). Once everything moved into this upstream clone (`mame/`), that outer repo
was redundant, so its full history was archived here and the working tree
deleted.

`outer-investigation.bundle` is a git bundle of every ref (branch `main`, tip
`38ed7cad268 mklinux: add the scripts that drove the install and the login
check`). It records the complete history — verified with `git bundle verify`.

## Restore it

```sh
git clone outer-investigation.bundle outer-investigation
# or inspect without a full checkout:
git bundle list-heads outer-investigation.bundle
```

The commit messages are the running log of how the findings were made; the code
in them is the old 0.289 driver text (`macpiltdown.cpp`), superseded by the
upstream `maccordyceps.cpp` work in this tree.
