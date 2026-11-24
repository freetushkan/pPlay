#!/usr/bin/env bash
set -euo pipefail
title="$1"
log_file="$2"
summary="$GITHUB_STEP_SUMMARY"
tmp="$(mktemp)"
trap 'rm -f "$tmp"' EXIT
normalise() {
  sed -E -e 's/^[0-9]{4}-[0-9TZ:.-]+Z //' -e 's#^.*/(third_party|src|compat|android|include|libcross2d|libsmb2)/#\1/#'
}
emit_group() {
  local label="$1" pattern="$2"
  : > "$tmp"
  if [[ -f "$log_file" ]]; then
    { grep -E "$pattern" "$log_file" || true; } | normalise | sort | uniq -c | sort -nr > "$tmp"
  fi
  local count
  count="$(wc -l < "$tmp" | tr -d ' ')"
  printf '#### %s — %s unique\n\n' "$label" "$count" >> "$summary"
  if [[ "$count" -gt 0 ]]; then
    sed 's/^/    /' "$tmp" >> "$summary"
    printf '\n\n' >> "$summary"
  else
    printf '_None found._\n\n' >> "$summary"
  fi
}
{
  printf '### %s: build diagnostics\n\n' "$title"
  if [[ ! -f "$log_file" ]]; then
    printf '_No captured build log (%s)._ \n\n' "$log_file"
  else
    emit_group "Warnings" 'warning:|##[[]warning[]]|DeprecationWarning:|(^|[[:space:]])WARNING:'
    emit_group "Errors" 'error:|##[[]error[]]|FAILURE: Build failed|What went wrong:|(^|[[:space:]])FAILED([[:space:]]|$)|Traceback [(]most recent call last[)]|RuntimeError:|TimeoutError:|(^|[[:space:]])FAIL([[:space:]:]|$)'
  fi
} >> "$summary"
