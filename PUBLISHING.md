# Publishing this research

The owner created
[timdoug/pmac6200-sound-research](https://github.com/timdoug/pmac6200-sound-research).
Its branch references were empty when checked on 2026-09-27. Local `origin`
is configured for that repository. Preparation does not push, publish a
release, or open a PR.

From this checkout, review and run:

```sh
git status --short
git remote -v
python3 -m unittest discover -s tests/unit -v
python3 tools/verify_evidence.py
git push -u origin main
```

The last command is the owner's publication step, not something run by the
preparation checks. It pushes `main`, not local backup branches or ignored files.

Before publishing:

1. Review `git ls-files` and `THIRD_PARTY.md`. ASCTester and its borrowed
   headers are deliberately not vendored. Your original work is BSD-3-Clause;
   preserve the exceptions and others' notices and permissions.
2. Publish a reviewed audio attachment separately. Four non-CD capture
   candidates are packaged locally in
   `local-only/pmac6200-sound-tone-captures-2026-09-24.tar.gz`. The matching
   paths/hashes are in `manifests/tone-captures.sha256`. Inspect the recordings
   before release; a filename or probe description alone cannot certify rights.
   Extract that attachment from this repository's root to restore `probes/...`.
3. Do not publish the unfiltered original bundle in `local-only/`: it includes
   borrowed headers and a CD-content capture. The two JackProbe `*-cd.wav`
   files are also local-only. Keep their hashes and reports for provenance;
   publish audio only after permission or with a clearly labelled replacement
   measurement using material you can redistribute.
4. After creating the release, add its actual URL to `docs/captures.md`.
   No attachment is currently claimed available. Pin the MAME PR's reference
   to a research commit or release, not only
   the changing default branch.
5. In the MAME PR, summarize the improvements, validation and limitations.
   Include AI assistance and the actual model/version in the initial PR
   description, as MAME requires; do not infer that identifier from this repo.

Publishing the source repository does not require publishing the capture
attachment at the same time. The README explicitly describes what is missing.
The CI workflow only runs lightweight Python tests and manifest checks, with
read-only repository permissions; it neither fetches ROMs nor runs hardware probes.
See `docs/publication-checks.md` for the checks completed before the first push.

## Local-only preservation

`.gitignore` protects captures, `probes/ASCTester/`, generated borrowed headers,
archives, guest media and build outputs. These files are not uploaded by a
normal Git push. Do not use `git clean -xfd`: it would delete this local evidence.

The original bundle is retained as
`local-only/pmac6200-sound-evidence-final.tar.gz`, SHA-256
`d010e7486e6421ec1ed58815a4a83f48f1db01d21c82589ad422863a500887f1`.
All original workspace files are also left in place; packaging did not delete
or alter the source probes, captures, disk images or the ASCTester checkout.

Unlabelled root-level WAV/M4A recordings are retained under
`local-only/unlabelled-recordings/`, not included in the tone attachment.
