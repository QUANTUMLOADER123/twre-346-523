#!/usr/bin/env bash
# annotate.sh <title> <logfile> - print a log excerpt as a GitHub Actions error annotation (no-op outside Actions)
[ -n "${GITHUB_ACTIONS:-}" ] || exit 0
title="$1"; file="$2"
body=$( (grep -E "error|undefined|note:" "$file" || tail -30 "$file") | head -30 | sed -e 's/%/%25/g' -e 's/\r//g' | awk '{printf "%s%%0A", $0}')
echo "::error title=${title}::${body}"
