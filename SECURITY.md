English | [简体中文](SECURITY.zh-CN.md)

# Security

YUVRaw parses external image files. Out-of-bounds access, integer overflow, crashes caused by malformed files, and reachable vulnerabilities in dependency decoders are within the scope of security reports.

## Private reporting

Report vulnerabilities through [GitHub private vulnerability reporting](https://github.com/bobcatkay/yuvraw-viewer/security/advisories/new), avoiding exploit details in public Issues. Use Issues for ordinary bugs.

## Helpful information

- Application version/commit, Windows version, architecture, and relevant decoder versions.
- Reproduction steps, format, dimensions, stride, bit depth, and the first point of failure.
- A sanitized minimal log excerpt or crash stack.
- A publishable synthetic minimal-reproduction generator; original camera files are not required.

Share only content you are authorized to share. Remove or replace real test images, usernames, absolute paths, and camera identifiers.

## Maintenance scope

There is currently no long-term-support branch or guaranteed response time. After the first release, maintenance prioritizes the latest Windows x64 version. Dependency fixes must update locks and complete corresponding source, then rerun affected CPU/GPU format regressions. Release notes should record affected versions, fixed versions, and public explanations of confirmed issues.
