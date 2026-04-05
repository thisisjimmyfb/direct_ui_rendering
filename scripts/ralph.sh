#!/usr/bin/env bash
# ralph.sh — spec-driven development loop
#
# Uses local LLM during peak hours (5am-11am, 1pm-7pm) to avoid double token pricing.
# Falls back to standard Claude endpoint during off-peak hours.
# Peak hours are re-evaluated at the start of each iteration.
#
# Usage:
#   ./ralph.sh [LOOP.md] [options]
#
# Options:
#   --auto, -a                    Skip the between-iteration pause
#   --dangerously-skip-permissions  Bypass tool permission prompts
#   --llm <local|claude|auto>     LLM selection: force local, force Claude, or peak-hour routing (default: auto)
#   --model <model>               Claude model to use
#   --offline-url <url>           Local LLM endpoint (default: http://localhost:8088)
#   --help, -h                    Show this help

set -euo pipefail

# ── defaults ──────────────────────────────────────────────────────────────────
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
LOOP="$ROOT/spec/LOOP.md"
AUTO=false
SKIP_PERMISSIONS=true
LLM_MODE="auto"
MODEL="claude-haiku-4-5"
OFFLINE_LLM_URL="http://localhost:8088"

# ── argument parsing ──────────────────────────────────────────────────────────
while [[ $# -gt 0 ]]; do
    case "$1" in
        --auto|-a)
            AUTO=true
            shift
            ;;
        --llm)
            if [[ -z "${2:-}" ]]; then
                echo "error: --llm requires an argument (local|claude|auto)" >&2
                exit 1
            fi
            case "$2" in
                local|claude|auto) LLM_MODE="$2" ;;
                *)
                    echo "error: --llm must be one of: local, claude, auto" >&2
                    exit 1
                    ;;
            esac
            shift 2
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

# Peak hours: 5am-11am and 1pm-7pm (13:00-19:00) — double token pricing
is_peak_hours() {
    local hour
    hour=$(date +%H)
    hour=$((10#$hour))
    if [[ $hour -ge 5 && $hour -lt 11 ]] || [[ $hour -ge 13 && $hour -lt 19 ]]; then
        return 0
    fi
    return 1
}

# ── helpers ───────────────────────────────────────────────────────────────────
run_local_llm() {
	cat "$LOOP" | ANTHROPIC_BASE_URL="$OFFLINE_LLM_URL" claude "${CLAUDE_FLAGS[@]}" 2>&1
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
    commit_msg=$(git diff --cached --quiet && echo "" || git diff --cached --stat | head -20 | ANTHROPIC_BASE_URL="$OFFLINE_LLM_URL" claude -p "Generate a concise commit message. Output only the message, no quotes." 2>/dev/null)
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
case "$LLM_MODE" in
    local)  echo "  llm  : local (forced, $OFFLINE_LLM_URL)" ;;
    claude) echo "  llm  : Claude (forced, ignoring peak hours)" ;;
    auto)   echo "  llm  : peak-hour routing (local=$OFFLINE_LLM_URL, checked each iteration)" ;;
esac
echo ""

while true; do
    iteration=$((iteration + 1))

    iter_start=$(date +%s)

    # Re-evaluate LLM mode each iteration
    if [[ "$LLM_MODE" == "local" ]] || { [[ "$LLM_MODE" == "auto" ]] && is_peak_hours; }; then
        USE_LOCAL=true
    else
        USE_LOCAL=false
    fi

    if $USE_LOCAL; then
        echo "┌─ iteration $iteration  $(date '+%Y-%m-%d %H:%M:%S')  [local LLM$( [[ "$LLM_MODE" == "local" ]] && echo ' (forced)' || echo ' (peak hours)' )] ──────────────────────────────"
    else
        echo "┌─ iteration $iteration  $(date '+%Y-%m-%d %H:%M:%S')  [Claude$( [[ "$LLM_MODE" == "claude" ]] && echo ' (forced)' || true )] ──────────────────────────────"
    fi
    echo ""

    if $USE_LOCAL; then
        run_local_llm
    else
        set +e
        output=$(cat "$LOOP" | claude "${CLAUDE_FLAGS[@]}")
        exit_code=$?
        set -e

        if [[ $exit_code -ne 0 ]] || is_token_limit_error "$output"; then
            echo "⚠ Token limit hit, falling back to local LLM"
            echo ""
            run_local_llm
        fi
    fi

    iter_end=$(date +%s)
    iter_duration=$((iter_end - iter_start))
    echo ""
    echo "└─ iteration $iteration complete ($(printf '%02d:%02d:%02d' $((iter_duration / 3600)) $(((iter_duration % 3600) / 60)) $((iter_duration % 60)))) ─────────────────────────────────────"
    echo ""

    if [[ -z "$(git status --porcelain)" ]]; then
        echo "  no file changes produced — stopping."
        echo ""
        exit 0
    fi

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
