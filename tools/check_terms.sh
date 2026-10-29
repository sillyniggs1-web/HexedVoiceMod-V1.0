#!/usr/bin/env bash
# HexedVoiceMod CI check: user-visible legacy branding must stay out of the project sources.
#
# Prohibited (case-insensitive):  "beta"  and  "VoiceFX v8" / "VoiceFX_v8" / "VoiceFX-v8"
#
# Allowlist (the only places these terms may appear):
#   * CHANGELOG.md              - historical release notes
#   * README.md                 - ONLY inside the "Upgrading from the VoiceFX v8 beta" section
#   * tools/check_terms.sh      - this script itself
# Everything else (jni/, menu/, test/, workflows, project files, other docs) must be clean.
set -u
cd "$(dirname "$0")/.."

fail=0
files=$(ls jni/*.h jni/*.cpp jni/Android.mk jni/Application.mk menu/*.lua test/*.cpp test/fake/rubberband/*.h \
          tools/make_icons.py .github/workflows/*.yml project.properties *.md 2>/dev/null)

for f in $files; do
  case "$f" in
    CHANGELOG.md)        continue ;;        # historical release notes are allowed
    tools/check_terms.sh) continue ;;       # the checker must be able to name the terms
  esac
  if [ "$f" = "README.md" ]; then
    # allow exactly the upgrade section: strip it (from its heading to the next same-or-higher heading)
    body=$(awk 'BEGIN{skip=0} /^#+[ ]*Upgrading from the VoiceFX v8 beta/{skip=1; next}
                skip==1 && /^#+[ ]/{skip=0} skip==0{print}' README.md 2>/dev/null)
  else
    body=$(cat "$f")
  fi
  hits=$(printf '%s\n' "$body" | grep -inE 'beta|voicefx[ _-]?v8' || true)
  if [ -n "$hits" ]; then
    echo "PROHIBITED TERM in $f:"
    echo "$hits"
    fail=1
  fi
done

if [ "$fail" -eq 0 ]; then
  echo "term check OK: no legacy branding outside the allowlist"
else
  echo "term check FAILED"
fi
exit $fail
