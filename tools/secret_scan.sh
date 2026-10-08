#!/usr/bin/env bash
# Refuse to push credentials. Scans every tracked + staged file.
# Literal values to hunt for live OUTSIDE the repo in ~/.config/shiv/secret-values.txt (one per line, mode 600).
set -u
cd "$(git rev-parse --show-toplevel)"
LIST="${SHIV_SECRET_LIST:-$HOME/.config/shiv/secret-values.txt}"
files=$(git ls-files -co --exclude-standard | grep -vE '\.(png|jpg|bin|zip|elf|xapk)$|^docs/toy/')
hits=0
pat='(eyJ[A-Za-z0-9_-]{20,}\.[A-Za-z0-9_-]{20,}\.)|(sk-ant-[A-Za-z0-9_-]{20,})|(gsk_[A-Za-z0-9]{20,})|(ghp_[A-Za-z0-9]{30,})|(github_pat_[A-Za-z0-9_]{30,})|(-----BEGIN [A-Z ]*PRIVATE KEY-----)|(xox[baprs]-[A-Za-z0-9-]{10,})|(shpat_[a-f0-9]{20,})|(pk_[a-f0-9]{30,})'
if [ -n "$files" ]; then
  out=$(echo "$files" | xargs -d '\n' /usr/bin/grep -nIE "$pat" 2>/dev/null)
  if [ -n "$out" ]; then echo "SECRET-LIKE PATTERN:"; echo "$out" | cut -c1-160; hits=1; fi
  if [ -r "$LIST" ]; then
    while IFS= read -r v; do
      [ ${#v} -lt 8 ] && continue
      m=$(echo "$files" | xargs -d '\n' /usr/bin/grep -nIF -- "$v" 2>/dev/null | cut -d: -f1,2)
      if [ -n "$m" ]; then echo "KNOWN SECRET VALUE (${v:0:3}...) in: $m"; hits=1; fi
    done < "$LIST"
  else
    echo "note: no $LIST, pattern scan only"
  fi
fi
if [ $hits -ne 0 ]; then echo "secret_scan: FAIL"; exit 1; fi
echo "secret_scan: clean ($(echo "$files" | wc -l) files)"
