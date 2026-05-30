#!/usr/bin/env bash
# wBMS pull-deploy: run from cron every 5 min. Fast no-op when main is unchanged;
# on a new commit it fast-forwards and rebuilds the containers — but only when
# cloud files (backend/frontend/compose/mosquitto) actually changed, so a
# firmware-only commit doesn't needlessly restart the stack.
#
# Deployed copy lives at /root/deploy.sh (decoupled from the pulled tree so a
# pull never rewrites the script mid-run). cron: */5 * * * * /root/deploy.sh
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

REPO=/root/Wireless-Battery-Management-System
LOG=/var/log/wbms-deploy.log
cd "$REPO"

exec 9>/tmp/wbms-deploy.lock
flock -n 9 || exit 0   # single-flight: skip if a previous build is still running

git fetch --quiet origin main
OLD=$(git rev-parse HEAD)
NEW=$(git rev-parse origin/main)
[ "$OLD" = "$NEW" ] && exit 0   # nothing new -> silent no-op

{
  echo "=== $(date -u '+%Y-%m-%d %H:%M:%SZ') deploy ${OLD:0:7} -> ${NEW:0:7} ==="
  git merge --ff-only origin/main
  if git diff --name-only "$OLD" "$NEW" | grep -qE '^(backend/|frontend/|docker-compose|mosquitto/)'; then
    echo "cloud files changed -> docker compose up -d --build"
    docker compose up -d --build
  else
    echo "no cloud changes (firmware-only) -> skipping container rebuild"
  fi
  echo "=== done $(date -u '+%Y-%m-%d %H:%M:%SZ') ==="
} >> "$LOG" 2>&1
