#!/bin/bash

# --- Konfiguration ---
GITHUB_TOKEN="REPLACE WITH TOKEN"
REPO="RobBotzz/MatrixOS"
DEST_FOLDER="/home/robin/MatrixOS/build"
TEMP_ZIP="/tmp/latest_build.zip"
# ---------------------

echo "🔍 Search for newest release of $REPO..."

# 1. Find out URL of newest Build
ASSET_URL=$(curl -s -H "Authorization: Bearer $GITHUB_TOKEN" \
  "https://api.github.com/repos/$REPO/releases/latest" \
  | jq -r '.assets[0].url')

# Check if URL found
if [ "$ASSET_URL" = "null" ] || [ -z "$ASSET_URL" ]; then
  echo "❌ Error: No release found or missing permission."
  exit 1
fi


echo "⬇️ Download newest build..."

curl -L -s \
  -H "Authorization: Bearer $GITHUB_TOKEN" \
  -H "Accept: application/octet-stream" \
  "$ASSET_URL" \
  -o "$TEMP_ZIP"


echo "📦 Unzip data and save to $DEST_FOLDER..."

unzip -o "$TEMP_ZIP" -d "$DEST_FOLDER"

rm "$TEMP_ZIP"

echo "✅ Update successful!"