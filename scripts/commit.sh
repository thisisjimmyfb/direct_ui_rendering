#!/usr/bin/env bash
# commit.sh — generate a commit message from pending changes and push to GitHub
set -euo pipefail

cd "$(git -C "$(dirname "$0")" rev-parse --show-toplevel)"

if ! command -v claude &>/dev/null; then
    echo "error: 'claude' not found in PATH" >&2
    exit 1
fi

OFFLINE_LLM_URL="http://localhost:8088"

# Collect pending changes
STATUS=$(git status --short)
if [[ -z "$STATUS" ]]; then
    echo "nothing to commit, working tree clean"
    exit 0
fi

DIFF=$(git diff HEAD)

generate_commit_msg() {
	export ANTHROPIC_BASE_URL="$OFFLINE_LLM_URL"
    printf '%s\n\n%s' "$STATUS" "$DIFF" | claude -p "You are writing a git commit message. Based on the following git status and diff, write a concise commit message (imperative mood, 72 chars or fewer for the subject). Output only the commit message text, no quotes, no explanation."
	unset ANTHROPIC_BASE_URL
}

is_token_limit_error() {
    local output="$1"
    echo "$output" | grep -qi "token\|rate.*limit\|too.*many.*requests\|429"
}

# Generate commit message
COMMIT_MSG=$(generate_commit_msg)

if is_token_limit_error "$COMMIT_MSG"; then
	export ANTHROPIC_BASE_URL="$OFFLINE_LLM_URL"
	COMMIT_MSG=$(generate_commit_msg)
	unset ANTHROPIC_BASE_URL
fi

if [[ -z "$COMMIT_MSG" ]]; then
    echo "error: claude returned an empty commit message" >&2
    exit 1
fi

echo "commit message: $COMMIT_MSG"
echo ""

git add -A
git commit -m "$COMMIT_MSG"
git push

echo ""
echo "done."
