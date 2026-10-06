# Contributing to PS5 Cooling & System Center - Pro

Thank you for contributing.

## Development setup

1. Clone the repository and open the project folder.
1. Ensure the PS5 payload SDK is available locally.
1. Set the SDK environment variable before building:

```powershell
$env:PS5_PAYLOAD_SDK="C:/path/to/PS5_PAYLOAD_SDK"
```

1. Build the project:

```powershell
make
```

If `PS5_PAYLOAD_SDK` is not set, the build will fail.

## Branching and pull requests

1. Create a feature branch from `main`.
2. Keep commits focused and descriptive.
3. Open a pull request with:
   - short problem statement
   - summary of changes
   - test notes (what was tested on device and what was not)

## Coding guidelines

- Keep behavior changes explicit and easy to review.
- Prefer small, isolated commits.
- Avoid unrelated refactors in the same pull request.
- Preserve existing API behavior unless the change is intentional and documented.

## Testing guidance

- Run local checks where possible.
- If PS5 hardware testing was not possible, clearly note this in the PR.
- For API changes, include request/response examples in the PR description.
- Before release-related PRs, verify release docs are synced:

```powershell
.\tools\check-release-liesmich-sync.ps1
```

## Security and safety

- Do not commit secrets, private tokens, or local credentials.
- Do not include proprietary SDK files or binaries in pull requests.
- Do not add game data, firmware, system libraries from the console, keys or
  anything that circumvents copy protection, and do not ask for help with
  pirated content in issues or pull requests. See the legal section of the README.
- Report security problems privately, as described in [SECURITY.md](SECURITY.md).

## License

By contributing, you agree that your contributions are licensed under the repository license.
