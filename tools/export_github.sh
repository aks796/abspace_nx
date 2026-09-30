#!/bin/sh
# export_github.sh -- github_repo/: the files to put on GitHub, taken from
# what git tracks here (and new files not ignored yet): the source, the build
# files, tools/, README.md, NOTES.md, LICENSE and the icon. Never the game's
# files, build outputs, logs from a console or backups (.gitignore keeps
# them out of git), and not PLAN.md (working notes). github_repo/.git, if
# there is one, is kept. Nothing is pushed.
#
#   tools/export_github.sh
set -e
HERE="$(cd "$(dirname "$0")/.." && pwd)"
cd "$HERE"
OUT=github_repo
mkdir -p "$OUT"
find "$OUT" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
git ls-files --cached --others --exclude-standard |
  grep -v -e '^PLAN\.md$' -e "^$OUT/" |
  while IFS= read -r f; do [ -f "$f" ] && printf '%s\n' "$f"; done |
  tar -cf - -T - | tar -xf - -C "$OUT"
echo "$OUT/: $(find "$OUT" -type f -not -path "$OUT/.git/*" | wc -l | tr -d ' ') files, $(du -sh "$OUT" | cut -f1)"
