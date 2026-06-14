#!/usr/bin/env bash
# wBMS pull-deploy: run from cron every 5 min. Fast no-op when main is unchanged;
# on a new commit it fast-forwards and rebuilds the containers — but only when
# cloud files (backend/frontend/compose/mosquitto) actually changed, so a
# firmware-only commit doesn't needlessly restart the stack.
#
# Robustness (so a push "just works"):
#   * Build failure -> HEAD is rolled back to the previous commit, so the NEXT
#     run RETRIES instead of advancing HEAD and silently never rebuilding. The
#     old (working) containers keep running while a build is broken.
#   * Self-update: the live $REPO/deploy.sh atomically syncs itself from the
#     version-controlled ops/deploy.sh (syntax-checked first), so a pushed
#     script change propagates on the next run — no manual copy needed.
#   * Dangling images are pruned after a successful rebuild to keep disk in check.
#
# Usage:
#   ops/deploy.sh /path/to/repo
#
# First run copies ops/deploy.sh -> $REPO/deploy.sh. Cron should invoke the
# live copy so a pull never rewrites the script mid-run:
#   */5 * * * * /path/to/repo/deploy.sh /path/to/repo
# Log: $REPO/wbms-deploy.log
set -euo pipefail
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

if [ $# -lt 1 ] || [ -z "${1:-}" ]; then
  echo "Usage: $0 /path/to/repo" >&2
  exit 1
fi

REPO=$(cd "$1" && pwd)
LOG="$REPO/wbms-deploy.log"
LIVE="$REPO/deploy.sh"

if [ ! -d "$REPO/.git" ]; then
  echo "ERROR: $REPO is not a git repository" >&2
  exit 1
fi

cd "$REPO"

if [ -f "$REPO/.env" ]; then
  set -a
  # shellcheck disable=SC1091
  source "$REPO/.env"
  set +a
fi

exec 9>"$REPO/.deploy.lock"
flock -n 9 || exit 0   # single-flight: skip if a previous build is still running

# Keep the live script in sync with the repo's canonical ops/deploy.sh. Atomic
# (mv) so the in-flight run is never corrupted; syntax-checked (bash -n) so a
# broken script can't propagate and brick all future deploys. Effective next run.
sync_self() {
  local src="$REPO/ops/deploy.sh"
  [ -f "$src" ] || return 0
  cmp -s "$src" "$LIVE" && return 0
  if bash -n "$src"; then
    install -m 755 "$src" "$LIVE.new" && mv -f "$LIVE.new" "$LIVE"
    echo "live deploy script self-updated from ops/deploy.sh"
  else
    echo "WARNING: ops/deploy.sh failed syntax check; NOT self-updating"
  fi
}

# The broker runs with `allow_anonymous false` + a gitignored password_file, so a
# fresh/wiped deploy would have NO passwd file and mosquitto would silently reject
# BOTH the master (publisher) and backend (subscriber) -> zero telemetry.
# Provision it idempotently with both users. Password mirrors MQTT_PASSWORD.
ensure_mqtt_passwd() {
  local pf="$REPO/mosquitto/config/passwd"
  local pw="${MQTT_PASSWORD:-change-me}"
  if [ -f "$pf" ] && grep -q '^wbms-master:' "$pf" && grep -q '^wbms-backend:' "$pf"; then
    chmod 644 "$pf" 2>/dev/null || true
    return 0
  fi
  echo "provisioning mosquitto passwd (wbms-master, wbms-backend)"
  docker run --rm -v "$REPO/mosquitto/config:/mosquitto/config" eclipse-mosquitto:2 sh -c \
    "mosquitto_passwd -c -b /mosquitto/config/passwd wbms-master '$pw' && \
     mosquitto_passwd -b /mosquitto/config/passwd wbms-backend '$pw'"
  chmod 644 "$pf"
}

git fetch --quiet origin main
OLD=$(git rev-parse HEAD)
NEW=$(git rev-parse origin/main)
[ "$OLD" = "$NEW" ] && exit 0   # nothing new -> silent no-op

{
  echo "=== $(date -u '+%Y-%m-%d %H:%M:%SZ') deploy ${OLD:0:7} -> ${NEW:0:7} ==="
  git merge --ff-only origin/main

  if git diff --name-only "$OLD" "$NEW" | grep -qE '^(backend/|frontend/|docker-compose|mosquitto/)'; then
    echo "cloud files changed -> docker compose up -d --build"
    ensure_mqtt_passwd
    if docker compose up -d --build; then
      docker image prune -f >/dev/null 2>&1 || true
      sync_self
      echo "=== done $(date -u '+%Y-%m-%d %H:%M:%SZ') (rebuilt) ==="
    else
      echo "BUILD FAILED -> rolling HEAD back to ${OLD:0:7} so the next run retries"
      git reset --hard "$OLD"
      echo "=== FAILED $(date -u '+%Y-%m-%d %H:%M:%SZ') ==="
      exit 1
    fi
  else
    echo "no cloud changes (firmware-only) -> skipping container rebuild"
    sync_self
    echo "=== done $(date -u '+%Y-%m-%d %H:%M:%SZ') (no rebuild) ==="
  fi
} >> "$LOG" 2>&1
