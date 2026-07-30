#!/usr/bin/env bash
set -euo pipefail
# Epoch boundary check — runs as a scheduled GitHub Action.
# Detects when the Fuego epoch advances, collects commits per repo via the
# GitHub compare API, and posts per-repo + network summaries to Discord webhooks.

STATE_FILE="state/epoch-state.json"
EPOCH_BLOCKS="${EPOCH_BLOCKS:-900}"
AUTH_HEADER="Authorization: token $GITHUB_TOKEN"

require() { [[ -n "${!1:-}" ]] || { echo "$1 required" >&2; exit 1; }; }
require FUEGO_RPC_URL
require MAINNET_WEBHOOK
require REPO_WEBHOOKS
require GITHUB_TOKEN

GHGET() { curl -sf -H "$AUTH_HEADER" -H "Accept: application/vnd.github.v3+json" "$1"; }
GHPOST() { curl -sf -X POST "$@" -H "Content-Type: application/json"; }

# ── 1. Poll current block height & compute epoch ──────────────────────────
COUNT=$(GHPOST "$FUEGO_RPC_URL" \
  -d '{"jsonrpc":"2.0","id":"epoch-relay","method":"getblockcount","params":{}}' \
  | jq -r '.result.count // empty')
[[ -n "$COUNT" ]] || { echo "RPC no block count" >&2; exit 1; }
HEIGHT=$((COUNT - 1))
EPOCH=$((HEIGHT / EPOCH_BLOCKS))
START=$((EPOCH * EPOCH_BLOCKS))
END=$((START + EPOCH_BLOCKS - 1))

# ── 2. Read state ──────────────────────────────────────────────────────────
mkdir -p "$(dirname "$STATE_FILE")"
if [[ -f "$STATE_FILE" ]]; then
  LAST_EPOCH=$(jq -r '.last_epoch // 0' "$STATE_FILE")
else
  LAST_EPOCH=0; echo '{"last_epoch":0,"repos":{}}' > "$STATE_FILE"
fi

(( EPOCH > LAST_EPOCH )) || { echo "epoch $EPOCH already reported (last=$LAST_EPOCH)"; exit 0; }
GAP=$((EPOCH - LAST_EPOCH - 1))
echo "epoch advanced: $LAST_EPOCH → $EPOCH (gap=$GAP)"
NOTE=""; (( GAP > 0 )) && NOTE="missed ${GAP} epoch boundary(ies)"

# ── 3. Init per-repo state (last_sha) if missing ──────────────────────────
for REPO in $(jq -r 'keys[]' <<<"$REPO_WEBHOOKS"); do
  jq -e ".repos[\"$REPO\"]" "$STATE_FILE" >/dev/null 2>&1 && continue
  DEF=$(GHGET "https://api.github.com/repos/$REPO" | jq -r '.default_branch // "main"')
  SHA=$(GHGET "https://api.github.com/repos/$REPO/git/refs/heads/$DEF" | jq -r '.object.sha // empty')
  STATE=$(jq --arg r "$REPO" --arg sha "${SHA:-}" '.repos[$r] = (.repos[$r] // {"last_sha": $sha})' "$STATE_FILE")
  echo "$STATE" > "$STATE_FILE"
done

# ── 4. Per-repo epoch summaries ────────────────────────────────────────────
STATS=/tmp/epoch-stats-$$.json; echo '{}' > "$STATS"
trap "rm -f $STATS" EXIT

for REPO in $(jq -r 'keys[]' <<<"$REPO_WEBHOOKS"); do
  LAST_SHA=$(jq -r ".repos[\"$REPO\"].last_sha // \"\"" "$STATE_FILE")
  WEBHOOK=$(jq -r ".[\"$REPO\"]" <<<"$REPO_WEBHOOKS")

  DEF=$(GHGET "https://api.github.com/repos/$REPO" | jq -r '.default_branch')
  CURR_SHA=$(GHGET "https://api.github.com/repos/$REPO/git/refs/heads/$DEF" | jq -r '.object.sha')

  COMMITS='[]'
  if [[ -n "$LAST_SHA" && "$LAST_SHA" != "null" && "$LAST_SHA" != "$CURR_SHA" ]]; then
    COMMITS=$(GHGET "https://api.github.com/repos/$REPO/compare/$LAST_SHA...$CURR_SHA" \
      | jq '.commits // []')
  fi
  CNT=$(jq 'length' <<<"$COMMITS")

  STATE=$(jq --arg r "$REPO" --arg sha "$CURR_SHA" '.repos[$r].last_sha = $sha' "$STATE_FILE")
  echo "$STATE" > "$STATE_FILE"
  jq --arg r "$REPO" --argjson n "$CNT" '. + {($r): $n}' "$STATS" > "$STATS.tmp" 2>/dev/null && mv "$STATS.tmp" "$STATS" || true

  (( CNT > 0 )) || { echo "no commits for $REPO this epoch"; continue; }

  AUTHORS=$(jq '[.[].commit.author.name] | unique | length' <<<"$COMMITS")
  FILES=$(jq '[.[].files // [] | length] | add // 0' <<<"$COMMITS")
  ADD=$(jq '[.[].stats.additions // 0] | add // 0' <<<"$COMMITS")
  DEL=$(jq '[.[].stats.deletions // 0] | add // 0' <<<"$COMMITS")

  # Commit listing (max 25)
  LIST=$(jq -r '.[:25][] |
    "[" + .sha[:7] + "](" + .html_url + ") " +
    (.commit.message | split("\n")[0])[:70] + " — *" +
    (.commit.author.name // "unknown") + "*"' <<<"$COMMITS")
  (( CNT > 25 )) && LIST+="\n… and $((CNT-25)) more"

  TOP=$(jq -r '[.[].commit.author.name] | group_by(.) | map({name:.[0],n:length}) | sort_by(-.n) | .[:8] | .[] | "\(.name) (\(.n))" ' <<<"$COMMITS" | paste -sd, -)
  [[ -z "$TOP" ]] && TOP="_none_"

  # AI summarization
  AI_SUM=""
  if (( CNT > 0 )) && [[ -n "${GEMINI_KEY:-}" ]]; then
    jq -r '.[] | "\(.sha[:7]) \(.commit.message | split("\n")[0])"' <<<"$COMMITS" > /tmp/epoch-ai-input.txt
    PROMPT="Summarize the following commits from the $REPO repo into 1-2 sentences for a dev Discord channel. Focus on the main theme:\n\n$(cat /tmp/epoch-ai-input.txt)"
    AI_SUM=$(jq -Rs --arg p "$PROMPT" '{contents:[{parts:[{text: ($p)}]}]}' \
      | curl -sf --max-time 15 -X POST \
        "https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=$GEMINI_KEY" \
        -H "Content-Type: application/json" -d @- \
      | jq -r '.candidates[0].content.parts[0].text // ""' 2>/dev/null || true)
  fi

  if [[ -n "$AI_SUM" ]]; then
    [[ ${#AI_SUM} -gt 1800 ]] && AI_SUM="${AI_SUM:0:1799}…"
    PAYLOAD=$(jq -c -n \
      --arg title "📜 Epoch #$EPOCH — $REPO" \
      --arg desc "$AI_SUM" \
      --arg raw "$LIST" \
      --argjson color 15844367 \
      --argjson c $CNT \
      --argjson a "$AUTHORS" \
      --arg blocks "$START–$END" \
      --arg delta "+$ADD/-$DEL ($FILES files)" \
      --arg top "$TOP" \
      --arg note "$NOTE" \
      '{
        embeds: [{
          title: $title, description: $desc, color: $color,
          fields: [
            {name:"Commits", value:($c|tostring), inline:true},
            {name:"Authors", value:($a|tostring), inline:true},
            {name:"Blocks", value:$blocks, inline:true},
            {name:"Files Δ", value:$delta, inline:true},
            {name:"Contributors", value:$top, inline:false},
            {name:"Raw Commits", value:$raw, inline:false}
          ],
          footer: (if $note != "" then {text:$note} else null end),
          timestamp: (now|strflocaltime("%Y-%m-%dT%H:%M:%SZ"))
        }]
      }')
  else
    PAYLOAD=$(jq -c -n \
      --arg title "📜 Epoch #$EPOCH — $REPO" \
      --arg desc "$LIST" \
      --argjson color 15844367 \
      --argjson c $CNT \
      --argjson a "$AUTHORS" \
      --arg blocks "$START–$END" \
      --arg delta "+$ADD/-$DEL ($FILES files)" \
      --arg top "$TOP" \
      --arg note "$NOTE" \
      '{
        embeds: [{
          title: $title, description: $desc, color: $color,
          fields: [
            {name:"Commits", value:($c|tostring), inline:true},
            {name:"Authors", value:($a|tostring), inline:true},
            {name:"Blocks", value:$blocks, inline:true},
            {name:"Files Δ", value:$delta, inline:true},
            {name:"Contributors", value:$top, inline:false}
          ],
          footer: (if $note != "" then {text:$note} else null end),
          timestamp: (now|strflocaltime("%Y-%m-%dT%H:%M:%SZ"))
        }]
      }')
  fi
  curl -sf -X POST "$WEBHOOK" -H "Content-Type: application/json" -d "$PAYLOAD" >/dev/null
  echo "epoch #$EPOCH summary for $REPO ($CNT commits)"
done

# ── 5. Network report (Epoch Epics) ────────────────────────────────────────
TOTAL=$(jq '[.[] | numbers] | add // 0' "$STATS")
ACTIVE=$(jq '[.[] | select(. > 0)] | length' "$STATS")

ALL_AUTHORS='[]'
for REPO in $(jq -r 'keys[]' <<<"$REPO_WEBHOOKS"); do
  LAST_SHA=$(jq -r ".repos[\"$REPO\"].last_sha // \"\"" "$STATE_FILE")
  [[ -z "$LAST_SHA" || "$LAST_SHA" == "null" ]] && continue
  DEF=$(GHGET "https://api.github.com/repos/$REPO" | jq -r '.default_branch')
  CURR_SHA=$(GHGET "https://api.github.com/repos/$REPO/git/refs/heads/$DEF" | jq -r '.object.sha')
  REPO_AUTHORS=$(GHGET "https://api.github.com/repos/$REPO/compare/$LAST_SHA...$CURR_SHA" \
    | jq '[.commits[].commit.author.name] // []')
  ALL_AUTHORS=$(jq -c -n --argjson a1 "$ALL_AUTHORS" --argjson a2 "$REPO_AUTHORS" '$a1 + $a2')
done
TOP_NET=$(jq -r 'group_by(.) | map({name:.[0],n:length}) | sort_by(-.n) | .[:8] | .[] | "\(.name) (\(.n))" ' <<<"$ALL_AUTHORS" 2>/dev/null | paste -sd, -)
[[ -z "$TOP_NET" ]] && TOP_NET="_none_"

REPO_LINES=$(jq -r 'to_entries | sort_by(.key) | .[] | "**\(.key)** — \(.value) commit(s)"' "$STATS")
[[ -z "$REPO_LINES" ]] && REPO_LINES="_No repo activity_"

NET_PAYLOAD=$(jq -c -n \
  --arg title "📜 Epoch Epics — Epoch #$EPOCH Closed" \
  --arg desc "$REPO_LINES" \
  --argjson color 15105570 \
  --arg blocks "$START–$END" \
  --argjson total "$TOTAL" \
  --argjson active "$ACTIVE" \
  --arg top "$TOP_NET" \
  --arg note "$NOTE" \
  '{
    embeds: [{
      title: $title, description: $desc, color: $color,
      fields: [
        {name:"Block Range", value:$blocks, inline:true},
        {name:"Active Repos", value:($active|tostring), inline:true},
        {name:"Total Commits", value:($total|tostring), inline:true},
        {name:"Top Contributors", value:$top, inline:false}
      ],
      footer: (if $note != "" then {text:$note} else null end),
      timestamp: (now|strflocaltime("%Y-%m-%dT%H:%M:%SZ"))
    }]
  }')
curl -sf -X POST "$MAINNET_WEBHOOK" -H "Content-Type: application/json" -d "$NET_PAYLOAD" >/dev/null
echo "Epoch Epics #$EPOCH posted"

# ── 6. Update epoch & commit state ─────────────────────────────────────────
STATE=$(jq --argjson e "$EPOCH" '.last_epoch = $e' "$STATE_FILE")
echo "$STATE" > "$STATE_FILE"

git config user.name "epoch-relay[bot]"
git config user.email "epoch-relay[bot]@users.noreply.github.com"
git add "$STATE_FILE"
if git diff --cached --quiet; then
  echo "state unchanged"
else
  git commit -m "epoch state: advance to #$EPOCH"
  git push
fi
