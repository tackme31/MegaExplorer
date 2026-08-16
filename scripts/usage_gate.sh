#!/usr/bin/env bash
# Decides whether an /evolve cycle may start, by reading how much of the
# Claude subscription's 5-hour and 7-day windows is already spent. A cycle
# costs ~100k tokens, so one started near the limit dies mid-way and leaves a
# half-finished branch; skipping and waiting for the next cron fire is cheaper.
#
# The only surface that reports those percentages is Claude Code's own /usage
# command: hooks don't receive them, OpenTelemetry has no such metric, and the
# Admin Usage & Cost API is org-scoped (unavailable for individual accounts)
# and reports tokens and dollars, not window consumption. Run headless, /usage
# resolves locally -- no API turn, no tokens, ~4s.
#
# MSYS_NO_PATHCONV=1 is load-bearing, not tidiness: Git Bash rewrites the
# leading slash and "/usage" reaches the CLI as C:/Program Files/Git/usage,
# which is not a command, so it goes to the model as an ordinary prompt --
# measured at $0.22 and 21k tokens of the very quota this is protecting. The
# zero-cost assertion below is what catches that if the variable is ever lost.
# Do not add --bare: it ignores OAuth entirely and only reads an API key.
#
# Windows/Git Bash only.
#
# Exit 0: under both thresholds, start the cycle. Exit 1: skip this cycle.
# Anything unreadable exits 1 as well -- see the parse-failure note below.
set -u

max_5h=${EVOLVE_USAGE_MAX_5H:-70}
max_7d=${EVOLVE_USAGE_MAX_7D:-90}

out=$(MSYS_NO_PATHCONV=1 timeout 60 claude -p "/usage" --model haiku \
    --output-format json 2>/dev/null) || {
    echo "usage_gate: skip -- 'claude -p /usage' failed or timed out"
    exit 1
}

tmp=$(mktemp) || {
    echo "usage_gate: skip -- mktemp failed"
    exit 1
}
trap 'rm -f "$tmp"' EXIT
printf '%s' "$out" >"$tmp"

# --model haiku never applies to a resolved slash command; it only caps the
# damage if /usage stops resolving and the text reaches a model after all.
python - "$tmp" "$max_5h" "$max_7d" <<'PY'
import json
import re
import sys

path, max_5h, max_7d = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])


def skip(msg):
    # Unreadable output is treated as "over the limit", not as "assume fine".
    # This runs unattended for hours at a time, and a wrong skip costs one
    # cycle that the next cron fire retries, while a wrong start burns the
    # window and strands a branch.
    print("usage_gate: skip -- " + msg)
    raise SystemExit(1)


try:
    with open(path, encoding="utf-8") as f:
        report = json.load(f)
except Exception as exc:
    skip("could not parse the JSON from claude -p (%s)" % exc)

if report.get("num_turns") or report.get("total_cost_usd"):
    skip("/usage was answered by a model (num_turns=%r, cost=%r) instead of "
         "running locally -- MSYS_NO_PATHCONV=1 lost, or the command was "
         "renamed" % (report.get("num_turns"), report.get("total_cost_usd")))

text = report.get("result") or ""
five = re.search(r"^Current session:\s*(\d+)%\s*used\s*(.*)$", text, re.M)
seven = re.search(r"^Current week \(all models\):\s*(\d+)%\s*used\s*(.*)$",
                  text, re.M)
if not five or not seven:
    skip("no percentage lines in the /usage output (format changed?)")


def resets(match):
    return match.group(2).lstrip("\u00b7 ").strip() or "reset time unknown"


pct_5h, pct_7d = int(five.group(1)), int(seven.group(1))
over = []
if pct_5h >= max_5h:
    over.append("5h %d%% >= %d%% (%s)" % (pct_5h, max_5h, resets(five)))
if pct_7d >= max_7d:
    over.append("7d %d%% >= %d%% (%s)" % (pct_7d, max_7d, resets(seven)))
if over:
    skip("; ".join(over))

print("usage_gate: ok -- 5h %d%% (< %d%%), 7d %d%% (< %d%%)"
      % (pct_5h, max_5h, pct_7d, max_7d))
PY
