#!/bin/bash
source .env

# --- Configuration ---
REPO="RobBotzz/MatrixOS"
DEST_FOLDER="/home/robin/MatrixOS/build"
# ---------------------

echo "🔍 Search for newest artifact of $REPO..."

# 1. Find out URL of newest Build
ASSET_URL=$(curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
  "https://api.github.com/repos/$REPO/actions/artifacts" \
  | jq -r '.artifacts[0].archive_download_url')

# Check if URL found
if [ "$ASSET_URL" = "null" ] || [ -z "$ASSET_URL" ]; then
  echo "❌ Error: No artifact found or missing permission."
  exit 1
fi


echo "⬇️ Download newest build..."

curl -L -s \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  "$ASSET_URL" \
  -o "$TEMP_ZIP"


echo "📦 Unzip data and save to $DEST_FOLDER..."

unzip -o "$TEMP_ZIP" -d "$DEST_FOLDER"

rm "$TEMP_ZIP"

echo "✅ Update successful!"