#!/usr/bin/env bash
# ralph.sh — spec-driven development loop
#
# Always tries Claude CLI first, falls back to local LLM only when token limit is reached.
# Each iteration retries the standard Claude endpoint.
#
# Usage:
#   ./ralph.sh [LOOP.md] [options]
#
# Options:
#   --auto, -a                    Skip the between-iteration pause
#   --dangerously-skip-permissions  Bypass tool permission prompts
#   --model <model>               Claude model to use
#   --offline-url <url>           Local LLM endpoint (default: http://localhost:8088)
#   --help, -h                    Show this help

set -euo pipefail

# ── defaults ──────────────────────────────────────────────────────────────────
LOOP="../spec/LOOP.md"
AUTO=false
SKIP_PERMISSIONS=true
MODEL=""
OFFLINE_LLM_URL="http://localhost:8088"

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
        --offline-url)
            if [[ -z "${2:-}" ]]; then
                echo "error: --offline-url requires an argument" >&2
                exit 1
            fi
            OFFLINE_LLM_URL="$2"
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
            LOOP="$1"
            shift
            ;;
    esac
done

# ── validate ──────────────────────────────────────────────────────────────────
if [[ ! -f "$LOOP" ]]; then
    echo "error: loop file '$LOOP' not found" >&2
    exit 1
fi

if ! command -v claude &>/dev/null; then
    echo "error: 'claude' not found in PATH" >&2
    exit 1
fi

# ── build claude flags ────────────────────────────────────────────────────────
CLAUDE_FLAGS=("-p" "--verbose")
$SKIP_PERMISSIONS && CLAUDE_FLAGS+=("--dangerously-skip-permissions")
[[ -n "$MODEL" ]] && CLAUDE_FLAGS+=("--model" "$MODEL")

# ── signal handling ───────────────────────────────────────────────────────────
trap 'echo ""; echo "ralph stopped after $iteration iteration(s)."; exit 0' INT TERM

# ── helpers ───────────────────────────────────────────────────────────────────
run_local_llm() {
    export ANTHROPIC_BASE_URL="$OFFLINE_LLM_URL"
	claude "${CLAUDE_FLAGS[@]}" < "$LOOP" 2>&1
	unset ANTHROPIC_BASE_URL
}

is_token_limit_error() {
    local output="$1"
    echo "$output" | grep -qi "token\|rate.*limit\|too.*many.*requests\|429"
}

commit_work() {
    local iteration="$1"
    git add -A
    local commit_msg
    local timestamp
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    commit_msg=$(git diff --cached --quiet && echo "" || git diff --cached --stat | head -20 | claude -p "Generate a concise commit message. Output only the message, no quotes.")
    if [[ -n "$commit_msg" ]]; then
        git commit -m "$commit_msg" -m "ralph: $timestamp" || echo "  (nothing to commit)"
    else
        git commit -m "ralph: $timestamp" || echo "  (nothing to commit)"
    fi
    git push
}

# ── main loop ─────────────────────────────────────────────────────────────────
iteration=0

echo "ralph"
echo "  loop : $LOOP"
echo "  mode : $( $AUTO && echo 'auto (ctrl+c to stop)' || echo 'manual (enter to advance)' )"
$SKIP_PERMISSIONS && echo "  perms: bypassed"
echo "  local llm: $OFFLINE_LLM_URL (fallback only)"
echo ""

while true; do
    iteration=$((iteration + 1))

    echo "┌─ iteration $iteration  $(date '+%Y-%m-%d %H:%M:%S') ──────────────────────────────"
    echo ""

    # Always try Claude first
    set +e
	(cat "$LOOP" | claude "${CLAUDE_FLAGS[@]}") & 
	CLAUDE_PID=$!
	wait $CLAUDE_PID
	exit_code=$?
    set -e

    if [[ $exit_code -ne 0 ]]; then
        echo "⚠ Token limit hit, using local LLM for this iteration" >&2
        echo ""
        run_local_llm
    fi

    echo ""
    echo "└─ iteration $iteration complete ────────────────────────────────────────────"
    echo ""

    if ! $AUTO; then
        printf "  commit work to github? [Y/n] "
        read -r COMMIT_REPLY
    fi
    if [[ ! -v COMMIT_REPLY || "${COMMIT_REPLY,,}" != "n" ]]; then
        commit_work "$iteration"
        echo ""
    fi
	unset COMMIT_REPLY
	
    if ! $AUTO; then
        printf "  press enter for iteration $((iteration + 1)), ctrl+c to stop... "
        read -r
        echo ""
    fi
done