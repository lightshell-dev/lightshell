# Security Policy

## Supported Versions

| Version | Supported          |
| ------- | ------------------ |
| latest  | Yes                |

## Reporting a Vulnerability

If you discover a security vulnerability in LightShell, please report it responsibly.

**Do not open a public GitHub issue for security vulnerabilities.**

Instead, please send an email to **security@lightshell.dev** with:

1. A description of the vulnerability
2. Steps to reproduce the issue
3. The potential impact
4. Any suggested fixes (optional)

### What to expect

- **Acknowledgment**: We will acknowledge receipt of your report within 48 hours.
- **Assessment**: We will assess the vulnerability and determine its severity within 5 business days.
- **Fix timeline**: Critical vulnerabilities will be patched within 7 days. Other issues will be addressed in the next scheduled release.
- **Disclosure**: We will coordinate with you on public disclosure timing. We ask that you allow us a reasonable window to release a fix before any public disclosure.

## Security Practices

- All GitHub Actions workflows use pinned SHA references to prevent supply-chain attacks.
- Dependencies are regularly audited with `govulncheck` and GitHub's Dependabot.
- CodeQL static analysis runs on every push and pull request.
- The OpenSSF Scorecard is monitored to maintain supply-chain security best practices.

## Scope

This security policy applies to the LightShell project repository and its official npm packages (`lightshell`, `create-lightshell`, and `@lightshell/*` platform packages).
