#!/usr/bin/env bash
# sign_app_bundle.sh — ad-hoc sign every Mach-O in the .app, then the .app itself.
#
# Usage: sign_app_bundle.sh <APP_CONTENTS_MACOS_DIR | APP_BUNDLE>
#
# Must run as the LAST bundling step: install_name_tool and file copies
# invalidate signatures. Final pass signs the .app and runs a deep verify.
#
# Order: dylibs/shared libraries first, then executables, then .framework
# bundles. Signatures are (re)created with --force because install_name_tool
# and copying invalidate existing ones. Verifies the result and fails on any
# invalid signature. No-op outside macOS.

set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  exit 0
fi

root="${1:?usage: sign_app_bundle.sh <DIR>}"
case "$root" in
  */Contents/MacOS) root="$(cd "$root/../.." && pwd -P)" ;;
esac
[[ -d "$root" ]] || { echo "not a directory: $root" >&2; exit 1; }

libs=()
execs=()
while IFS= read -r -d '' f; do
  desc="$(file -b "$f")"
  case "$desc" in
    *"Mach-O"*"dynamically linked shared library"*|*"Mach-O"*"bundle"*) libs+=("$f") ;;
    *"Mach-O"*) execs+=("$f") ;;
  esac
done < <(find "$root" -type f -not -path '*/.git/*' -print0)

sign() { codesign --force -s - "$1" >/dev/null 2>&1 || { echo "codesign failed: $1" >&2; exit 1; }; }

for f in ${libs[@]+"${libs[@]}"}; do sign "$f"; done
for f in ${execs[@]+"${execs[@]}"}; do sign "$f"; done

while IFS= read -r -d '' fw; do
  codesign --force -s - "$fw" >/dev/null 2>&1 || echo "warning: framework not signed as bundle: $fw" >&2
done < <(find "$root" -type d -name '*.framework' -print0)

# The main executable is sealed together with the bundle, so only the final
# deep verify below is meaningful.
codesign --force -s - "$root" >/dev/null 2>&1 || { echo "codesign failed: $root" >&2; exit 1; }
codesign --verify --deep --strict "$root" || { echo "deep verify failed: $root" >&2; exit 1; }

printf 'signed %d Mach-O files under %s\n' "$(( ${#libs[@]} + ${#execs[@]} ))" "$root"
