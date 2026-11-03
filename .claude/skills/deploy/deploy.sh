#!/usr/bin/env bash
# Publishes a built site directory to pupa (https://sp2.clau.se), run inside WSL.
# Usage: wsl -e bash .claude/skills/deploy/deploy.sh <site dir> [--dry-run]
# Mirrors the directory into /home/sp2docsdigger/www: files gone from the build are deleted there, everything ends up
# owned by sp2docsdigger and readable by nginx. Needs passwordless ssh to pupa and passwordless sudo there.
set -euo pipefail

HOST=pupa
DEST=/home/sp2docsdigger/www
OWNER=sp2docsdigger

src=${1:?usage: deploy.sh <site dir> [--dry-run]}
shift
src=$(realpath "$src")
if [ ! -f "$src/index.html" ]; then
    echo "deploy.sh: $src/index.html not found; build the site first" >&2
    exit 1
fi

# --delay-updates and --delete-after keep the old site whole until the new files are all in place.
rsync -rlt --checksum --delete-after --delay-updates \
    --rsync-path="sudo rsync" \
    --chown="$OWNER:$OWNER" --chmod=D755,F644 \
    --itemize-changes --stats "$@" \
    "$src/" "$HOST:$DEST/"
# The directory itself, in case it was recreated by hand.
ssh "$HOST" "sudo chown $OWNER:$OWNER $DEST && sudo chmod 755 $DEST"
