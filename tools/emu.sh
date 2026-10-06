#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Run a firmware in the FM-1 emulator (github.com/simonjohansson/fm1-emulator and our fork).
#
#   tools/emu.sh                      pick a firmware and CPU clock interactively
#   tools/emu.sh FIRMWARE [options]   FIRMWARE: a path, or part of a listed name
#   tools/emu.sh --list               list the firmware found, then exit
#   tools/emu.sh --update             fetch and rebuild the emulator, then exit
#
# Options:
#   --cpu MHZ     emulated CPU clock, 1..1000 (default: the firmware's own clock;
#                 96 = correct sound and faster than real time for our firmware)
#   --bg          start in the background (log in .emu/logs/<name>.log)
#   --rebuild     rebuild the emulator first
#
# Firmware is looked for in:
#   build/    packages built here (the builder, build.sh)
#   firmwares/ firmware you downloaded (any .fwsc: stock, Felucca, SLOOP, X0X...); git-ignored
#
# The emulator is cloned into .emu/fm1-emulator (git-ignored) on the first run:
#   EMU_REPO    where to clone from (default: our private fork github.com/hdavid/fm1-emulator;
#               upstream: https://github.com/simonjohansson/fm1-emulator.git)
#   EMU_BRANCH  the branch (default: feat/upstream-merge for our fork, main for upstream)
#   EMU_DIR     use an existing rust-emulator directory instead (no clone, no fetch)
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGES="${IMAGES:-$ROOT/firmwares}"
UPSTREAM_URL="https://github.com/simonjohansson/fm1-emulator.git"
FORK_URL="git@github.com:hdavid/fm1-emulator.git"
CLONE="$ROOT/.emu/fm1-emulator"

die() { echo "emu: $*" >&2; exit 1; }
usage() { sed -n '3,22p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

emu_source() {  # -> "repo branch"
    local repo="${EMU_REPO:-}" branch="${EMU_BRANCH:-}"
    repo="${repo:-$FORK_URL}"
    if [ -z "$branch" ]; then
        case "$repo" in *simonjohansson*) branch=main ;; *) branch=feat/upstream-merge ;; esac
    fi
    echo "$repo $branch"
}

ensure_clone() {  # clone once; --update fetches again
    local repo branch
    read -r repo branch <<<"$(emu_source)"
    if [ ! -d "$CLONE/.git" ]; then
        command -v git >/dev/null || die "git is needed to fetch the emulator"
        echo "emu: cloning $repo ($branch) into .emu/fm1-emulator ..."
        mkdir -p "$ROOT/.emu"
        git clone --branch "$branch" "$repo" "$CLONE" || die "clone failed"
        REBUILD=1
    elif [ "$UPDATE" = 1 ]; then
        echo "emu: fetching $branch from $repo ..."
        git -C "$CLONE" fetch "$repo" "$branch" && git -C "$CLONE" checkout -q --detach FETCH_HEAD ||
            die "fetch failed"
        REBUILD=1
    fi
}

ensure_emulator() {
    if [ -n "${EMU_DIR:-}" ]; then DIR="$EMU_DIR"; else ensure_clone; DIR="$CLONE/rust-emulator"; fi
    [ -d "$DIR" ] || die "no emulator at $DIR"
    EMULATOR="$DIR/target/release/fm1-ui"
    if [ "$REBUILD" = 1 ] || [ ! -x "$EMULATOR" ]; then
        command -v cargo >/dev/null || die "Rust (cargo) is needed to build the emulator: https://rustup.rs"
        echo "emu: building the emulator (a few minutes the first time) ..."
        (cd "$DIR" && cargo build --release --features gui --bin fm1-ui) || die "build failed"
    fi
}

find_firmware() {  # "path<TAB>origin", newest first within each origin
    local dir origin
    for dir in "$ROOT/build" "$IMAGES"; do
        [ -d "$dir" ] || continue
        if [ "$dir" = "$IMAGES" ]; then origin="$(basename "$IMAGES")/"; else origin="build/"; fi
        if [ "$dir" = "$ROOT/build" ]; then pat='optimist-*.fwsc' depth=1; else pat='*.fwsc' depth=2; fi; find "$dir" -maxdepth $depth -type f -name "$pat" -print0 2>/dev/null | xargs -0 ls -t 2>/dev/null |
            while IFS= read -r path; do printf '%s\t%s\n' "$path" "$origin"; done
    done
}

describe() {
    local when
    when="$(date -r "$1" '+%Y-%m-%d %H:%M' 2>/dev/null || echo '?')"
    printf '%-48s %-11s %s' "$(basename "$1")" "$2" "$when"
}

FIRMWARE="" CPU="" BACKGROUND=0 REBUILD=0 LIST=0 UPDATE=0
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help) usage ;;
        --list) LIST=1 ;;
        --update) UPDATE=1 ;;
        --bg) BACKGROUND=1 ;;
        --rebuild) REBUILD=1 ;;
        --cpu) shift; [ $# -gt 0 ] || die "--cpu needs a value"; CPU="$1" ;;
        --cpu=*) CPU="${1#--cpu=}" ;;
        -*) echo "emu: unknown option $1" >&2; usage 2 ;;
        *) [ -z "$FIRMWARE" ] || die "one firmware at a time"; FIRMWARE="$1" ;;
    esac
    shift
done

if [ "$UPDATE" = 1 ]; then ensure_emulator; exit 0; fi

ENTRIES=()
while IFS= read -r line; do ENTRIES+=("$line"); done < <(find_firmware)
none="no .fwsc in build/ or firmwares/ (build one with 'make builder', or put downloaded firmware in firmwares/)"

if [ "$LIST" = 1 ]; then
    [ ${#ENTRIES[@]} -gt 0 ] || die "$none"
    i=1
    for e in "${ENTRIES[@]}"; do printf '%2d) %s\n' "$i" "$(describe "${e%%$'\t'*}" "${e#*$'\t'}")"; i=$((i + 1)); done
    exit 0
fi

if [ -z "$FIRMWARE" ]; then
    [ -t 0 ] || die "no firmware given and no terminal to ask (try --list)"
    [ ${#ENTRIES[@]} -gt 0 ] || die "$none"
    echo "Firmware:"
    i=1
    for e in "${ENTRIES[@]}"; do printf '  %2d) %s\n' "$i" "$(describe "${e%%$'\t'*}" "${e#*$'\t'}")"; i=$((i + 1)); done
    while :; do
        read -r -p "Pick 1-${#ENTRIES[@]}: " pick
        if [[ "$pick" =~ ^[0-9]+$ ]] && [ "$pick" -ge 1 ] && [ "$pick" -le ${#ENTRIES[@]} ]; then
            FIRMWARE="${ENTRIES[$((pick - 1))]%%$'\t'*}"
            break
        fi
    done
    if [ -z "$CPU" ]; then
        echo "CPU clock: empty = the firmware's own clock (realistic, slowest),"
        echo "           96 = correct sound, faster (our firmware runs above real time)"
        read -r -p "CPU MHz [own]: " CPU
    fi
elif [ ! -f "$FIRMWARE" ]; then
    matches=()
    for e in "${ENTRIES[@]}"; do  # an exact name wins over partial matches
        path="${e%%$'\t'*}" name="$(basename "${e%%$'\t'*}")"
        if [ "$name" = "$FIRMWARE" ] || [ "$name" = "$FIRMWARE.fwsc" ]; then matches=("$path"); break; fi
        case "$name" in *"$FIRMWARE"*) matches+=("$path") ;; esac
    done
    [ ${#matches[@]} -gt 0 ] || die "no firmware matching '$FIRMWARE' (see --list)"
    [ ${#matches[@]} -eq 1 ] || die "'$FIRMWARE' matches ${#matches[@]} files: $(printf '%s ' "${matches[@]##*/}")"
    FIRMWARE="${matches[0]}"
fi

ensure_emulator
ARGS=("$FIRMWARE")
if [ -n "$CPU" ]; then
    [[ "$CPU" =~ ^[0-9]+$ ]] && [ "$CPU" -ge 1 ] && [ "$CPU" -le 1000 ] || die "--cpu takes 1..1000 MHz"
    ARGS+=("--cpu-mhz=$CPU")
fi

echo "emu: $(basename "$FIRMWARE") at ${CPU:-its own clock}${CPU:+ MHz}"
if [ "$BACKGROUND" = 1 ]; then
    mkdir -p "$ROOT/.emu/logs"
    log="$ROOT/.emu/logs/$(basename "$FIRMWARE" .fwsc).log"
    (nohup "$EMULATOR" "${ARGS[@]}" </dev/null >"$log" 2>&1 & echo $! >"$log.pid"; disown) </dev/null
    sleep 0.2
    echo "emu: started (pid $(cat "$log.pid" 2>/dev/null || echo '?')), log $log"
else
    exec "$EMULATOR" "${ARGS[@]}"
fi
