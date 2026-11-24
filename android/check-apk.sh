#!/bin/sh
# Checks that an APK carries the licence texts and the bundled General MIDI bank, and no game
# content. Used by the Android CI job and by hand before publishing.
set -eu

apk=${1:?usage: check-apk.sh <apk>}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/mbn-apk.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

unzip -l "$apk" > "$tmp/list"

fail() {
    echo "check-apk: $*" >&2
    exit 1
}

for required in \
    lib/arm64-v8a/libmain.so \
    lib/arm64-v8a/libSDL3.so \
    assets/TimGM6mb.sf2 \
    assets/licenses/LICENSE \
    assets/licenses/THIRD_PARTY_NOTICES.md \
    assets/licenses/TimGM6mb-LICENSE.txt \
    assets/licenses/coldet-LGPL.txt; do
    grep -q " $required$" "$tmp/list" || fail "missing $required"
done

for forbidden in MageBros.exe Music01.mid Music02.mid Music03.mid Music04.mid Music05.mid Music06.mid; do
    if grep -qi "$forbidden" "$tmp/list"; then fail "game content in the APK: $forbidden"; fi
done
if grep -qE ' saves/|\.bmp$' "$tmp/list"; then fail "a save or screenshot is in the APK"; fi

echo "check-apk: contents OK ($apk)"
