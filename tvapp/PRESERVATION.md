# Preservation record

## Origin and attribution

- Original project: https://github.com/blackbearreloaded/ps5-homebrew-ui
- Original author: BlackBearReloaded, with contributors credited in the source and third-party notices.
- Preserved by: JMUtechnologies.
- Preservation date: 2026-10-23.
- Recovered upstream commit: `dbf8286661224c7d8b01fdea62052251eec43eca`.
- Recovered upstream tree: `60f110e3be0e73714a2af464ac51450c35ada554`.
- Recovered tracked files: 883.

This is a source preservation repository. It is not an official continuation or a claim of authorship by the preserving account. The saved local clone was shallow; this repository starts a new import history and does not reconstruct earlier upstream commits, branches, tags, issues, releases, or pull requests. This snapshot is not claimed to be the original project's latest version.

## Verification

The saved Git pack passed verification, and the recovered upstream commit passed a full Git object integrity check with its missing earlier parent treated as a shallow boundary. Every tracked file was reconstructed from its Git blob, and its Git object hash was verified before writing. File modes were preserved.

No source code, original assets, copyright notices, license texts, or third-party notices were changed during preservation. Uncommitted local changes and later workstation-specific changes are outside this upstream snapshot.

## Preservation changes

- Added a preservation notice above the original README. The original README content follows the separator unchanged.
- Added this preservation record.
- Retained the original GitHub Actions workflows, unchanged, under `.github/preserved-workflows/` instead of `.github/workflows/`. They are inactive. Review their upstream dependencies, credentials, and release destinations before re-enabling them.

All other recovered tracked files retain their upstream paths and contents. Local caches, downloaded SDKs, build outputs, and workstation credentials were not imported.

## License

Original project code is GPL-3.0-or-later, as recorded in the preserved source headers and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). The original [LICENSE](LICENSE), copyright notices, and individual third-party licenses remain included.

## Build status and dependencies

Source preservation and file integrity were verified. A complete application build and PS5 hardware execution were not performed for this import. Historical build or hardware claims in the original documentation describe upstream's work, not new validation by JMUtechnologies.

Original upstream URLs and dependency pins are retained for provenance. Some repositories, release assets, and externally downloaded dependencies may no longer be available. In particular, `ps5-homebrew-ui` uses a separate `ps5-opengl` SDK; preserving these two repositories alone does not guarantee a fresh PS5 build.
