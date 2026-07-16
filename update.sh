#!/usr/bin/env bash

#   ./update.sh

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

log()  { echo "[update] $*"; }
warn() { echo "[update] WARNING: $*" >&2; }
die()  { echo "[update] ERROR: $*" >&2; exit 1; }

pop_stash_quiet() {
    local log_file
    log_file="$(mktemp)"
    if git stash pop >"$log_file" 2>&1; then
        rm -f "$log_file"
        return 0
    else
        cat "$log_file"
        rm -f "$log_file"
        return 1
    fi
}

[ -d .git ] || die "$SCRIPT_DIR is not a git checkout - cannot auto-update. Did you install wsrx via 'git clone'?"
command -v git >/dev/null 2>&1 || die "git is not installed."
command -v make >/dev/null 2>&1 || die "make is not installed."

# --- 1. Stop wsrx (and the web interface) before touching any files ---
if [ -x ./wsrx.sh ]; then
    log "Stopping wsrx..."
    ./wsrx.sh stop || warn "wsrx.sh stop reported an error (was it already stopped?)"
else
    warn "wsrx.sh not found or not executable - skipping stop."
fi

# --- 2. Back up local, machine-specific files before pulling ---
BACKUP_DIR="$SCRIPT_DIR/backups/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$BACKUP_DIR"
for f in config.ini offset_cache.txt; do
    if [ -f "$f" ]; then
        cp "$f" "$BACKUP_DIR/"
        log "Backed up $f -> $BACKUP_DIR/"
    fi
done

OLD_COMMIT="$(git rev-parse --short HEAD)"
STASHED=0
if ! git diff --quiet || ! git diff --cached --quiet; then
    log "Local changes detected, stashing them before pulling..."
    git stash push -m "wsrx update.sh auto-stash $(date +%s)" >/dev/null
    STASHED=1
fi

# --- 4. Pull the latest changes (fast-forward only - never auto-merges) ---
log "Fetching latest changes from GitHub..."
git fetch --tags origin
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"

attempt_pull() {
    git pull --ff-only origin "$CURRENT_BRANCH"
}

PULL_LOG="$(mktemp)"
trap 'rm -f "$PULL_LOG"' EXIT

if ! attempt_pull >"$PULL_LOG" 2>&1; then
    cat "$PULL_LOG"

    if grep -qE "would be overwritten by (merge|checkout)" "$PULL_LOG"; then
        mapfile -t BLOCKING_FILES < <(sed -n '/would be overwritten by \(merge\|checkout\)/,/^Please/p' "$PULL_LOG" | sed -n 's/^\t//p')

        if [ "${#BLOCKING_FILES[@]}" -gt 0 ]; then
            log "Untracked local file(s) are blocking the update - moving them aside and retrying:"
            for f in "${BLOCKING_FILES[@]}"; do
                if [ -e "$f" ]; then
                    mkdir -p "$BACKUP_DIR/$(dirname "$f")"
                    mv "$f" "$BACKUP_DIR/$f"
                    log "  moved '$f' -> $BACKUP_DIR/$f (the version from the update takes its place)"
                fi
            done

            if ! attempt_pull >"$PULL_LOG" 2>&1; then
                cat "$PULL_LOG"
                warn "Retry still failed after moving the conflicting file(s) aside."
                if [ "$STASHED" -eq 1 ]; then
                    pop_stash_quiet || warn "Could not restore stashed changes, check 'git stash list'."
                fi
                die "Update aborted, nothing was rebuilt. Resolve the git state manually and re-run."
            fi
        else
            warn "Fast-forward pull failed, and the blocking file(s) could not be parsed from git's output."
            if [ "$STASHED" -eq 1 ]; then
                pop_stash_quiet || warn "Could not restore stashed changes, check 'git stash list'."
            fi
            die "Update aborted, nothing was rebuilt. Resolve the git state manually and re-run."
        fi
    else
        warn "Fast-forward pull failed (local commits diverged from origin?)."
        if [ "$STASHED" -eq 1 ]; then
            pop_stash_quiet || warn "Could not restore stashed changes, check 'git stash list'."
        fi
        die "Update aborted, nothing was rebuilt. Resolve the git state manually and re-run."
    fi
fi

if [ "$STASHED" -eq 1 ]; then
    if pop_stash_quiet; then
        log "Local changes restored."
    else
        warn "Could not automatically restore your local changes (conflict with the update)."
        warn "Your changes are still safe in 'git stash list' - resolve manually, e.g.:"
        warn "  git stash show -p | less"
        warn "  git stash pop"
    fi

    UNMERGED="$(git diff --name-only --diff-filter=U || true)"
    if [ -n "$UNMERGED" ]; then
        warn "The following files still have unresolved conflict markers:"
        echo "$UNMERGED" | sed 's/^/[update]   /' >&2
        warn "Not rebuilding or restarting wsrx with a broken file."
        die "Resolve the conflict(s) above (e.g. edit the file, remove the <<<<<<< markers, then 'git add <file>'), then re-run ./update.sh."
    fi
fi

NEW_COMMIT="$(git rev-parse --short HEAD)"
if [ "$OLD_COMMIT" = "$NEW_COMMIT" ]; then
    log "Already up to date ($NEW_COMMIT)."
else
    log "Updated: $OLD_COMMIT -> $NEW_COMMIT"
    log "Changes:"
    git log --oneline "$OLD_COMMIT..$NEW_COMMIT" | sed 's/^/[update]   /'
fi

if [ ! -f config.ini ] && [ -f "$BACKUP_DIR/config.ini" ]; then
    warn "config.ini is missing after the update, restoring from backup."
    cp "$BACKUP_DIR/config.ini" config.ini
fi

# --- 6. Rebuild everything ---
log "Rebuilding wsrx and wsrx-web..."
make clean
make

log "Rebuilding decoders..."
(cd decoder && make clean && make)

chmod +x wsrx.sh 2>/dev/null || true

# --- 7. Restart ---
if [ -x ./wsrx.sh ]; then
    log "Starting wsrx..."
    ./wsrx.sh start
else
    warn "wsrx.sh not found - start wsrx manually."
fi

log "Update complete (now at $NEW_COMMIT)."