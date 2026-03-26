#!/usr/bin/env bash
# ralph.sh — spec-driven development loop
#
# Feeds spec.md to claude each iteration, starting a fresh session every time.
# Sessions are saved to disk by claude automatically.
#
# Usage:
#   ./ralph.sh [spec.md] [options]
#
# Options:
#   --auto, -a                    Skip the between-iteration pause (run until ctrl+c)
#   --dangerously-skip-permissions  Bypass all claude tool permission prompts
#   --model <model>               Claude model to use (e.g. opus, sonnet)
#   --help, -h                    Show this help

set -euo pipefail

# ── defaults ──────────────────────────────────────────────────────────────────
SPEC="progress.md"
AUTO=false
SKIP_PERMISSIONS=true
MODEL=""

# ── argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --auto|-a)
            AUTO=true
            shift
            ;;
        --dangerously-skip-permissions)
            SKIP_PERMISSIONS=true
            shift
            ;;
        --model)
            if [[ -z "${2:-}" ]]; then
                echo "error: --model requires an argument" >&2
                exit 1
            fi
            MODEL="$2"
            shift 2
            ;;
        --help|-h)
            sed -n '2,/^[^#]/{ /^#/{ s/^# \{0,1\}//; p }; /^[^#]/q }' "$0"
            exit 0
            ;;
        -*)
            echo "error: unknown option '$1'" >&2
            echo "run './ralph.sh --help' for usage" >&2
            exit 1
            ;;
        *)
            SPEC="$1"
            shift
            ;;
    esac
done

# ── validate ──────────────────────────────────────────────────────────────────
if [[ ! -f "$SPEC" ]]; then
    echo "error: spec file '$SPEC' not found" >&2
    exit 1
fi

if ! command -v claude &>/dev/null; then
    echo "error: 'claude' not found in PATH" >&2
    exit 1
fi

# ── build claude flags ────────────────────────────────────────────────────────
CLAUDE_FLAGS=("-p")
$SKIP_PERMISSIONS && CLAUDE_FLAGS+=("--dangerously-skip-permissions")
[[ -n "$MODEL" ]] && CLAUDE_FLAGS+=("--model" "$MODEL")

# ── signal handling ───────────────────────────────────────────────────────────
trap 'echo ""; echo "ralph stopped after $iteration iteration(s)."; exit 0' INT TERM

# ── loop ──────────────────────────────────────────────────────────────────────
iteration=0

echo "ralph"
echo "  spec : $SPEC"
echo "  mode : $( $AUTO && echo "auto (ctrl+c to stop)" || echo "manual (enter to advance, ctrl+c to stop)" )"
$SKIP_PERMISSIONS && echo "  perms: bypassed"
[[ -n "$MODEL" ]] && echo "  model: $MODEL"
echo ""

while true; do
    iteration=$((iteration + 1))

    echo "┌─ iteration $iteration  $(date '+%Y-%m-%d %H:%M:%S') ──────────────────────────────"
    echo ""

    claude "${CLAUDE_FLAGS[@]}" < "$SPEC"

    echo ""
    echo "└─ iteration $iteration complete ────────────────────────────────────────────"
    echo ""

    printf "  commit work to github? [Y/n] "
    read -r COMMIT_REPLY
    if [[ "${COMMIT_REPLY,,}" != "n" ]]; then
        git add -A
        git commit -m "ralph: iteration $iteration  $(date '+%Y-%m-%d %H:%M:%S')" || echo "  (nothing to commit)"
        git push
        echo ""
    fi

    if ! $AUTO; then
        printf "  press enter for iteration $((iteration + 1)), ctrl+c to stop... "
        read -r
        echo ""
    fi
done
