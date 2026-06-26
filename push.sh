#!/usr/bin/env bash
# One-command update:  ./push.sh "what I changed"
set -e
git add -A
git commit -m "${1:-Update}"
git push
