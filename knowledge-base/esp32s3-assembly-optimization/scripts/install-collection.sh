#!/usr/bin/env bash
# Register knowledge-base/esp32s3-assembly-optimization/ as a qmd collection named
# "esp32s3-assembly-optimization-kb" and build/refresh its lexical index + vector
# embeddings.
#
# Run from anywhere: the script resolves the topic dir relative to itself
# (symlink-safe via readlink -f). Re-runnable: if the collection is already
# registered at the same path, just refreshes the indices; if registered at a
# different path (e.g. after a checkout move), removes and re-adds it.

set -euo pipefail

COLLECTION_NAME="esp32s3-assembly-optimization-kb"

# Symlink-safe self-path resolution so $0 via a symlinked checkout still finds
# the topic dir adjacent to scripts/.
SELF_REAL="$(readlink -f "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(cd "$(dirname "$SELF_REAL")" && pwd)"
KB_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# On WSL the qmd on PATH is usually the Windows-native build behind a
# wrapper, and it resolves paths on the Windows side. A WSL path such as
# /mnt/c/work/... is then taken relative to the Windows home and the
# collection indexes nothing (seen 2026-09-06). Register the Windows form.
KB_DIR_REG="$KB_DIR"
if [[ -r /proc/version ]] && grep -qi microsoft /proc/version && command -v wslpath >/dev/null 2>&1; then
  KB_DIR_REG="$(wslpath -w "$KB_DIR")"
fi

if ! command -v qmd >/dev/null 2>&1; then
  echo "error: qmd not on PATH." >&2
  echo >&2
  echo "  Install upstream qmd: npm i -g @tobilu/qmd" >&2
  echo "  Or see https://github.com/tobilu/qmd for platform-specific notes." >&2
  exit 1
fi

echo "==> registering qmd collection: $COLLECTION_NAME"
echo "    path: $KB_DIR_REG"

# Probe the current registration. `qmd collection show <name>` exits non-zero
# when the collection does not exist; on success it prints "  Path: <abs>".
existing_path=""
if show_out=$(qmd collection show "$COLLECTION_NAME" 2>/dev/null); then
  existing_path=$(printf '%s\n' "$show_out" \
    | awk '/^[[:space:]]*Path:/ {sub(/^[[:space:]]*Path:[[:space:]]*/,""); print; exit}')
fi

if [[ -z "$existing_path" ]]; then
  echo "    (registering new)"
  qmd collection add "$KB_DIR_REG" --name "$COLLECTION_NAME"
elif [[ "${existing_path//\\//}" != "${KB_DIR_REG//\\//}" ]]; then
  echo "    (path drift: registered at $existing_path; re-registering at $KB_DIR_REG)"
  qmd collection remove "$COLLECTION_NAME"
  qmd collection add "$KB_DIR_REG" --name "$COLLECTION_NAME"
else
  echo "    (already registered at correct path, skipping add)"
fi

# `qmd update` re-indexes every registered collection; it does not take a
# per-collection flag. That is fine for our workflow but be aware that this
# also refreshes any sibling collections the user has registered.
echo "==> indexing (lexical, all collections)"
qmd update

echo "==> building embeddings"
qmd embed --collection "$COLLECTION_NAME"

echo "==> done. try: qmd query 'why does ee.vld.128.ip mask the low address bits' --collection $COLLECTION_NAME"
