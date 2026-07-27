#!/bin/bash
set -euo pipefail

# --- Configuration ---
REPO="RobBotzz/MatrixOS"
WORKFLOW="pi-zero-ci.yml"
ARTIFACT_NAME="pi-zero-executable"
DEST_FOLDER="${MATRIXOS_DEST:-/home/robin/MatrixOS/build}"
# ---------------------

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# GITHUB_TOKEN needs read access to Actions. Kept out of the repository.
if [ ! -f "$SCRIPT_DIR/.env" ]; then
  echo "❌ Error: $SCRIPT_DIR/.env is missing (expected GITHUB_TOKEN=...)."
  exit 1
fi
source "$SCRIPT_DIR/.env"

if [ -z "${GITHUB_TOKEN:-}" ]; then
  echo "❌ Error: GITHUB_TOKEN is not set in $SCRIPT_DIR/.env."
  exit 1
fi

for tool in curl jq unzip; do
  if ! command -v "$tool" >/dev/null; then
    echo "❌ Error: '$tool' is not installed."
    exit 1
  fi
done

# JSON GET against the GitHub API.
api() {
  curl -fsSL \
    -H "Authorization: Bearer $GITHUB_TOKEN" \
    -H "Accept: application/vnd.github+json" \
    "$1"
}

# Deliberately not "the newest artifact of any run": that would also pick up
# pull-request builds. Take the newest successful run on main instead.
echo "🔍 Search for newest successful main build of $REPO..."

RUN_ID=$(api "https://api.github.com/repos/$REPO/actions/workflows/$WORKFLOW/runs?branch=main&status=success&per_page=1" \
  | jq -r '.workflow_runs[0].id // empty')

if [ -z "$RUN_ID" ]; then
  echo "❌ Error: no successful main build found (or missing permission)."
  exit 1
fi

ASSET_URL=$(api "https://api.github.com/repos/$REPO/actions/runs/$RUN_ID/artifacts" \
  | jq -r --arg name "$ARTIFACT_NAME" \
      '.artifacts[] | select(.name == $name) | .archive_download_url' \
  | head -1)

if [ -z "$ASSET_URL" ]; then
  echo "❌ Error: run $RUN_ID has no artifact named '$ARTIFACT_NAME'."
  echo "   Artifacts expire after 90 days — trigger a new build if this one is old."
  exit 1
fi

echo "⬇️ Download build from run $RUN_ID..."

TEMP_ZIP=$(mktemp -t matrixos-build-XXXXXX.zip)
trap 'rm -f "$TEMP_ZIP"' EXIT

curl -fsSL \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  "$ASSET_URL" -o "$TEMP_ZIP"

echo "📦 Unzip data and save to $DEST_FOLDER..."

mkdir -p "$DEST_FOLDER"
unzip -o -q "$TEMP_ZIP" -d "$DEST_FOLDER"

# Artifact zips do not carry the executable bit.
if [ -f "$DEST_FOLDER/MatrixOS" ]; then
  chmod +x "$DEST_FOLDER/MatrixOS"
else
  echo "⚠️  Warning: expected $DEST_FOLDER/MatrixOS in the artifact."
fi

echo "✅ Update successful! ($DEST_FOLDER/MatrixOS)"
