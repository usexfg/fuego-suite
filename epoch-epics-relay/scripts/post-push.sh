#!/usr/bin/env bash
set -euo pipefail
# Called by push-notify.yml in each repo.
# Reads the push event from $GITHUB_EVENT_PATH, sends a Discord embed via webhook.

if [[ -z "${DISCORD_WEBHOOK:-}" ]]; then
  echo "DISCORD_WEBHOOK not set" >&2
  exit 1
fi

EVENT=$(cat "$GITHUB_EVENT_PATH")

REPO=$(jq -r '.repository.full_name' <<<"$EVENT")
REF=$(jq -r '.ref' <<<"$EVENT")
BRANCH=${REF#refs/heads/}
COMMITS=$(jq -c '.commits[]' <<<"$EVENT")
NUM_COMMITS=$(jq '.commits | length' <<<"$EVENT")

# Build embed description (list of commits)
DESC=""
while IFS= read -r c; do
  SHA=$(jq -r '.id[:7]' <<<"$c")
  MSG=$(jq -r '.message' <<<"$c" | head -1)
  NAME=$(jq -r '.author.name // .author.username // "unknown"' <<<"$c")
  [[ ${#MSG} -gt 80 ]] && MSG="${MSG:0:79}…"
  LINE=$(printf "\`%s\` %s — *%s*" "$SHA" "$MSG" "$NAME")
  DESC="$DESC\n$LINE"
done <<<"$COMMITS"

[[ ${#DESC} -gt 2000 ]] && DESC="${DESC:0:1999}…"

TIMESTAMP=$(date -u +%Y-%m-%dT%H:%M:%SZ)

PAYLOAD=$(jq -c -n \
  --arg title "$REPO · $NUM_COMMITS new commit(s)" \
  --arg desc "$DESC" \
  --arg url "https://github.com/$REPO/commits/$BRANCH" \
  --argjson color 3066993 \
  --arg ts "$TIMESTAMP" \
  --arg footer "$BRANCH" \
  '{
    embeds: [{
      title: $title,
      description: $desc,
      url: $url,
      color: $color,
      footer: { text: $footer },
      timestamp: $ts
    }]
  }')

curl -sf -X POST "$DISCORD_WEBHOOK" \
  -H "Content-Type: application/json" \
  -d "$PAYLOAD" >/dev/null
