#!/usr/bin/env bash
# generate_changelog.sh — Build a changelog from conventional commits.
#
# Usage:
#   ./scripts/generate_changelog.sh              # commits since last tag
#   ./scripts/generate_changelog.sh v1.0.0       # commits since v1.0.0
#   ./scripts/generate_changelog.sh v1.0.0 v2.0.0  # commits between two tags

set -euo pipefail

from_ref="${1:-}"
to_ref="${2:-HEAD}"

# If no from_ref supplied, use the most recent tag before $to_ref.
if [ -z "$from_ref" ]; then
    from_ref=$(git describe --tags --abbrev=0 "$to_ref^" 2>/dev/null || true)
fi

if [ -n "$from_ref" ]; then
    range="${from_ref}..${to_ref}"
    header="## Changes since ${from_ref}"
else
    range="$to_ref"
    header="## All changes"
fi

# Collect conventional-commit log lines.
log=$(git log --pretty=format:"%s" "$range" 2>/dev/null || true)

if [ -z "$log" ]; then
    echo "$header"
    echo ""
    echo "_No conventional commits found._"
    exit 0
fi

# Section helpers — order matches the table in README.
declare -a SECTIONS=(
    "feat:Features"
    "fix:Bug Fixes"
    "refactor:Refactoring"
    "perf:Performance"
    "test:Tests"
    "docs:Documentation"
    "style:Style"
    "chore:Chores"
    "ci:CI"
    "revert:Reverts"
)

echo "$header"
echo ""

found_any=false

for entry in "${SECTIONS[@]}"; do
    type="${entry%%:*}"
    title="${entry#*:}"

    # Match lines starting with "type:" or "type(scope):"
    matches=$(echo "$log" | grep -E "^${type}(\(.*\))?:" || true)

    if [ -n "$matches" ]; then
        found_any=true
        echo "### ${title}"
        echo ""
        while IFS= read -r line; do
            # Strip the type prefix to get the description.
            desc=$(echo "$line" | sed -E "s/^${type}(\([^)]*\))?:[[:space:]]*//" )
            scope=$(echo "$line" | sed -nE "s/^${type}\(([^)]*)\):.*/\1/p")
            if [ -n "$scope" ]; then
                echo "- **${scope}**: ${desc}"
            else
                echo "- ${desc}"
            fi
        done <<< "$matches"
        echo ""
    fi
done

if [ "$found_any" = false ]; then
    echo "_No conventional commits found._"
fi
