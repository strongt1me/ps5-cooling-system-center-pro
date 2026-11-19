# Security Policy

**English** · [Deutsch](SECURITY.de.md)

## Supported version

Security fixes are made for the **latest release** only. Older versions are not
maintained; please update first.

## Reporting a vulnerability

Please do **not** report vulnerabilities in public (no issue, no discussion).

- Preferably through **Security → "Report a vulnerability"** in this repository
  (a private report), if GitHub offers the feature here.
- Otherwise by a message to the maintainer on GitHub: [@strongt1me](https://github.com/strongt1me).

Please include in your report:

- the affected version (the "System" page or the file name of the ELF),
- console model and firmware,
- what you did, what happened and what you expected,
- if possible, a small reproducible description (a request to the interface,
  browser behavior).

The maintainer replies within a few days if possible, confirms receipt and gets
back to you once the cause is clear. There are no bounties. Whoever reports a
vulnerability responsibly is named in the release notes if they wish.

## What counts as a vulnerability

For example:

- a foreign web page or a foreign device can control the console through the
  interface, although the server's protections (checking `Host` and `Origin`,
  Content-Security-Policy) should prevent it,
- file paths that are built from requests lead out of the intended area (path
  traversal),
- a request crashes the program or makes the interface freeze for good,
- injected characters are executed as code in the interface (XSS).

## What is not a bug of this project

- **The interface has no login.** That is intentional: it is meant for your own
  home network, and every device in that network may read **and** change things.
  The console does not belong on the open internet: no port forwarding to port
  8086. If you want the program to answer only on the console itself, set
  `bind_address` to `127.0.0.1`.
- **Kraken Mount (port 8097) and Dump Forge (port 8095) are programs by other
  authors.** Since 17.11.2026 Kraken Mount starts the app by itself, about five
  seconds after its own start, unless it was switched off on the FPKGMount tab
  (the marker `kraken/aus.txt` in the data folder); Dump Forge starts it only
  for the duration of a conversion. Both listen on the whole home network and
  check neither `Host` nor `Origin`; the setting `bind_address` and this app's
  browser protection do not apply to them. If you do not want that, switch Kraken
  Mount off on the FPKGMount tab (the switch is remembered across restarts).
  Such things are reported to the authors of those programs. The consequence for
  Kraken Mount 0.3.0 (checked on 18.11.2026, read from the program's source
  code, not reproduced): a foreign web page in the browser of a device on the
  home network can change the settings there without a login (search folders,
  debug mode) and delete a game whose exact path it knows; it reads files under
  `/data` only with the debug mode switched on and by a detour (DNS rebinding).
  The FPKGMount tab shows differing search folders; checking `Host` and `Origin`
  can only be added by the author of Kraken Mount.
- Vulnerabilities in the console itself, in the jailbreak, in kstuff, in the
  payload loader or in other homebrew programs. Please report them there.
- Bypassing the manufacturer's protections. This project contains no code for
  that and gives no help with it.

## Legal

As described in the [README](README.md#legal-notice-and-disclaimer): the program
is provided without any warranty.
