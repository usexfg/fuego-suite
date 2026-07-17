# Epoch Epics — Fuego → Discord Relay

Posts commit summaries from USEXFG GitHub repos to Discord in real time
and publishes **Epoch Epics** network reports at every Fuego epoch boundary
(~900 blocks / 5 days). Zero infrastructure — runs entirely on GitHub Actions
free tier.

## Files

```
epoch-relay/
├── workflows/
│   ├── push-notify.yml   # copy to each watched repo
│   └── epoch-watch.yml   # copy to hub repo (fuego-suite)
├── scripts/
│   ├── post-push.sh       # (unused — push-notify is self-contained)
│   └── check-epoch.sh     # epoch boundary logic for epoch-watch
├── state/
│   └── epoch-state.json   # persisted epoch cursor + repo SHAs
└── .env.example           # reference for required secrets
```

## Setup

### 1. Per-repo real-time push notifications

Copy `workflows/push-notify.yml` into each repository you want to monitor
(e.g. `usexfg/digm-platform`, `usexfg/fuego-suite`) at
`.github/workflows/push-notify.yml`.

In each repository, create **Actions secrets**:

| Name | Required | Value |
|------|----------|-------|
| `DISCORD_WEBHOOK_URL` | yes | Discord channel webhook (Channel Settings → Integrations → Webhooks) |
| `GEMINI_API_KEY` | no | Google Gemini API key (free at https://aistudio.google.com/apikey) — enables AI-generated commit summaries |

Every `git push` will now post a commit summary to that channel. If `GEMINI_API_KEY` is set, the embed description is an AI-generated summary with raw commits in a collapsed field; otherwise the raw listing is shown directly.

### 2. Epoch boundary reports (hub repo)

Copy `workflows/epoch-watch.yml` into `fuego-suite/.github/workflows/`.
Also copy the `scripts/` and `state/` directories so the workflow can
find `scripts/check-epoch.sh` and `state/epoch-state.json`.

In the hub repo, create **Actions secrets**:

| Name | Required | Value |
|------|----------|-------|
| `FUEGO_RPC_URL` | yes | Fuego daemon JSON-RPC URL (default `http://127.0.0.1:28180/json_rpc`) |
| `MAINNET_WEBHOOK_URL` | yes | Discord webhook for the network report channel |
| `REPO_WEBHOOKS` | yes | JSON map — e.g. `{"usexfg/digm-platform":"https://discord.com/api/webhooks/...","usexfg/fuego-suite":"https://discord.com/api/webhooks/..."}` |
| `GEMINI_API_KEY` | no | Google Gemini API key — enables AI summaries in per-repo epoch reports |

The workflow runs every 4 hours via `schedule` and can also be triggered
manually from the Actions tab (`workflow_dispatch`).

### 3. First run

1. Push to any watched repo — the real-time embed should appear in its channel.
2. Go to the hub repo → Actions → **Epoch Watch** → **Run workflow**.
   This primes `state/epoch-state.json` with the current epoch.
   Subsequent scheduled runs will detect the next boundary automatically.

## How it works

- **push-notify**: Inline bash reads the `push` event payload via `${{ toJSON(github.event) }}`. If `GEMINI_API_KEY` is set, commit messages are fed to Gemini 1.5 Flash for a 1-2 sentence summary; the raw listing goes into an expandable field. Without the key, the raw listing is used directly. Posts to `DISCORD_WEBHOOK_URL`.
- **epoch-watch**: Polls `FUEGO_RPC_URL` (`getblockcount`), derives epoch = `(count-1)/900`. When the epoch advances, calls the GitHub compare API for each repo to collect commits since the last known SHA. If `GEMINI_API_KEY` is set, each repo's commits are summarized by Gemini. Posts per-repo epoch summary embeds (yellow), then a network report embed (orange) titled **Epoch Epics** to the mainnet channel. State is persisted by committing `state/epoch-state.json` back to the repo.
