#!/usr/bin/env bash
# Preserve each source repository's Git ancestry while importing its entire tree.
set -euo pipefail
if [[ ${1:-} != "--push" && ${1:-} != "--dry-run" ]]; then
  echo "Usage: bash scripts/import-device-history.sh --dry-run|--push" >&2
  exit 2
fi
mode="$1"
[[ -z "$(git status --porcelain)" ]] || { echo "Working tree must be clean" >&2; exit 1; }
git config user.name "${GIT_AUTHOR_NAME:-Root My Galaxy Migration}"
git config user.email "${GIT_AUTHOR_EMAIL:-41898282+github-actions[bot]@users.noreply.github.com}"
for model in f731u t870; do
  branch="device/$model"
  source_repo="rmg-$model"
  remote="migration-$model"
  prefix="legacy-source/$model/"
  git fetch origin "$branch"
  if ! git remote get-url "$remote" >/dev/null 2>&1; then
    git remote add "$remote" "https://github.com/youyoudezhuzhu/$source_repo.git"
  fi
  git fetch "$remote" main
  source_sha="$(git rev-parse "$remote/main")"
  git checkout -B "$branch" "origin/$branch"
  if git merge-base --is-ancestor "$source_sha" HEAD; then
    echo "SKIP $branch: source $source_sha already in ancestry"
    continue
  fi
  if git ls-tree -r --name-only HEAD | grep -q "^$prefix"; then
    echo "STOP $branch: destination $prefix already exists" >&2
    exit 1
  fi
  echo "Import $source_repo @ $source_sha into $branch:$prefix"
  if [[ "$mode" == "--dry-run" ]]; then
    continue
  fi
  # Merge ancestry without overwriting the existing payload repository tree.
  git merge --allow-unrelated-histories -s ours --no-commit "$source_sha"
  # Stage the full source tree in a separate namespace, preserving all files.
  git read-tree --prefix="$prefix" -u "$source_sha"
  git commit -m "migration: preserve $source_repo history and import source under $prefix"
  git push origin "HEAD:refs/heads/$branch"
done
echo "Completed $mode. No source repositories, tags or releases were deleted."
