# Device repository migration

## State
- `device/f731u` and `device/t870` exist but are **not yet imported** until the workflow succeeds.
- Source repositories remain intact: `rmg-f731u` and `rmg-t870`.
- `main` remains the authoritative multi-device payload feed.
- No source repository deletion is authorized by this workflow.

## Run
Open **Actions → Import device repository histories → Run workflow**, enter `IMPORT`.
The job fetches each public source repository and imports its complete working tree into
`legacy-source/f731u/` or `legacy-source/t870/` on the corresponding device branch.
Each destination commit has the source tip as a merge parent, retaining source commit ancestry.
The import stops rather than overwriting an existing destination directory.

## After import
1. Verify both branch histories include their source tip SHA.
2. Compare source tree against the prefixed destination tree byte-for-byte.
3. Audit all tags, GitHub Releases and release assets independently (Git does not transfer GitHub Releases).
4. Audit updater URLs, support feed URLs, workflow triggers and release naming.
5. Build and verify F731U APK; never label T870 successful without hardware proof.
6. Only then propose archive/delete operations for separate confirmation.

**Important:** this is lossless staging, not final layout normalization. Do not delete legacy-source trees or old repositories before review.
