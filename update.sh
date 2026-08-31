
#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

UPDATE_PATHS=(src websrc decoder web)

log()  { echo "[update] $*"; }
warn() { echo "[update] WARNING: $*" >&2; }
die()  { echo "[update] ERROR: $*" >&2; exit 1; }

[ -d .git ] || die "$SCRIPT_DIR is not a git checkout - cannot auto-update. Did you install wsrx via 'git clone'?"
command -v git >/dev/null 2>&1 || die "git is not installed."
command -v make >/dev/null 2>&1 || die "make is not installed."

mirror_dir() {
    local src="$1" dest="$2"
    mkdir -p "$dest"
    if [ -n "$(find "$dest" -mindepth 1 -print -quit 2>/dev/null)" ]; then
        while IFS= read -r rel; do
            if [ ! -e "$src/$rel" ]; then
                rm -rf "$dest/$rel"
            fi
        done < <(cd "$dest" && find . -mindepth 1 | sed 's#^\./##')
    fi
    while IFS= read -r rel; do
        if [ -d "$src/$rel" ]; then
            mkdir -p "$dest/$rel"
        else
            mkdir -p "$dest/$(dirname "$rel")"
            cp -p "$src/$rel" "$dest/$rel"
        fi
    done < <(cd "$src" && find . -mindepth 1 | sed 's#^\./##')
}

# --- 1. Stop wsrx (and the web interface) before touching any files ---
if [ -x ./wsrx.sh ]; then
    log "Stopping wsrx..."
    ./wsrx.sh stop || warn "wsrx.sh stop reported an error (was it already stopped?)"
else
    warn "wsrx.sh not found or not executable - skipping stop."
fi

# --- 2. Fetch the latest changes ---
log "Fetching latest changes from GitHub..."
git fetch --tags origin
CURRENT_BRANCH="$(git rev-parse --abbrev-ref HEAD)"
REMOTE_REF="origin/$CURRENT_BRANCH"
NEW_REMOTE_SHA="$(git rev-parse "$REMOTE_REF")"

WORKTREE_DIR="$(mktemp -d)"
cleanup_worktree() {
    git worktree remove --force "$WORKTREE_DIR" >/dev/null 2>&1 || rm -rf "$WORKTREE_DIR"
}
trap cleanup_worktree EXIT

log "Preparing update from $REMOTE_REF ($NEW_REMOTE_SHA)..."
git worktree add --detach --force "$WORKTREE_DIR" "$REMOTE_REF" >/dev/null

for p in "${UPDATE_PATHS[@]}"; do
    if [ -d "$WORKTREE_DIR/$p" ]; then
        log "Replacing $p/ with the version from $REMOTE_REF ..."
        mirror_dir "$WORKTREE_DIR/$p" "$SCRIPT_DIR/$p"
    else
        warn "'$p' does not exist upstream anymore - leaving your local copy untouched."
    fi
done

cleanup_worktree
trap - EXIT

# --- 4. Rebuild everything ---
log "Rebuilding wsrx and wsrx-web..."
make clean
make

log "Rebuilding decoders..."
(cd decoder && make clean && make)

chmod +x wsrx.sh 2>/dev/null || true

# --- 5. Restart ---
if [ -x ./wsrx.sh ]; then
    log "Starting wsrx..."
    ./wsrx.sh start
else
    warn "wsrx.sh not found - start wsrx manually."
fi

log "Update complete (${UPDATE_PATHS[*]} replaced with $REMOTE_REF@$NEW_REMOTE_SHA)."
