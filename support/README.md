# Support feed schema

`targets-v3.json` keeps one entry for each shared exploit and KernelSU payload.
Automatic selection matches the exact device model and three-part kernel
version, such as `6.6.98`.

Each entry contains only:

- `payloadId` and `displayName`;
- one or more exact `Build.MODEL` values in `models`;
- one or more versions in `kernelVersions`;
- `url` and `size` for the exploit and KernelSU artifacts.

An entry may additionally set `requiresFreshP0Session` to `true` when slide
discovery and exploitation must run in the same payload process. The app then
disables its per-boot P0 cache for that profile and gives the single combined
attempt the target-specific long timeout. The field defaults to `false`, so
existing profiles retain the cached multi-attempt behavior.

The app extracts the leading numeric version from `uname -r`. Kernel suffixes,
Android build displays, fingerprints, and security-patch dates do not
participate in matching.

`targets-v2.json` remains unchanged for released 0.2.3 clients. New clients
read only schema version 3.
