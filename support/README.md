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

An entry may also list the exact full kernel release in `kernelVersions`
(for example `6.6.98-android15-8-pd6ff1cd-abogkiS9360ZHSCCZG1-4k`). Clients
that know the full release prefer an exact match before falling back to the
three-part version; that is what disambiguates regional builds sharing a model
and a three-part version (see `pa2q-S9360ZCSCCZG1` vs `pa2q-S9360ZHSCCZG1`).
Keeping the three-part string in the list preserves compatibility with older
clients.

`targets-v2.json` remains unchanged for released 0.2.3 clients. New clients
read only schema version 3.
