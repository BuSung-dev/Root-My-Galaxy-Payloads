# Support feed schema

`targets-v4.json` is the current app feed. It preserves the existing installable
profiles and can also describe an exact, non-installable compatibility record.
`targets-v3.json` remains unchanged for older clients.

Every entry contains:

- `payloadId` and `displayName`;
- one or more exact `Build.MODEL` values in `models`;
- one or more versions in `kernelVersions`;
- an optional `availability` value (`installable` by default or
  `metadata-only`).

An installable entry contains `url` and `size` for both artifacts. A
metadata-only entry contains neither artifact and is never eligible for
download or installation.

Version 4 may additionally constrain matching with `manufacturers`, `devices`,
`products`, `kernelReleases`, `buildIds`, `buildIncrements`, `buildFingerprints`,
and `pageSizes`. Every
non-empty constraint must match. This allows an exact firmware record without
broadening compatibility to another device or build.

An entry may retain `requiresFreshP0Session` as compatibility metadata. Stage 2A
parses the field but intentionally does not change payload execution behavior.

The app still extracts the leading numeric version from `uname -r`. For entries
without version-4 constraints, matching remains model plus numeric kernel
version. Exact records may additionally require the full kernel release,
Android build display, build fingerprint, product device, manufacturer, and
runtime page size.

`targets-v2.json` remains unchanged for released 0.2.3 clients. Version 3 clients
continue to read `targets-v3.json`; updated clients read only schema version 4.
