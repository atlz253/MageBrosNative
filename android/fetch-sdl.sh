#!/bin/sh
# Fetches the pinned SDL3 source into app/jni/SDL, where the Gradle build takes SDL3's Java shim
# from and CMake builds libSDL3.so from. The version lives in gradle/libs.versions.toml.
set -eu

cd "$(dirname "$0")"
version=$(sed -n 's/^sdl = "\(.*\)"/\1/p' gradle/libs.versions.toml)
[ -n "$version" ] || { echo "fetch-sdl: no sdl version in gradle/libs.versions.toml" >&2; exit 1; }

dest=app/jni/SDL
[ -f "$dest/CMakeLists.txt" ] && exit 0

# SHA-256 of the GitHub source archive for the pinned release.
sha256=c4b08b950bd29d83caae0ab8d884298d27046a9d30700fb42841c50992665c90
url="https://github.com/libsdl-org/SDL/archive/refs/tags/release-${version}.tar.gz"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/mbn-sdl.XXXXXX")
trap 'rm -rf "$tmp"' EXIT

echo "fetch-sdl: downloading SDL3 ${version}"
curl -L --fail --silent --show-error -o "$tmp/SDL.tar.gz" "$url"
if command -v shasum >/dev/null 2>&1; then
    echo "$sha256  $tmp/SDL.tar.gz" | shasum -a 256 -c - >/dev/null
else
    echo "$sha256  $tmp/SDL.tar.gz" | sha256sum -c - >/dev/null
fi

rm -rf "$dest"
mkdir -p "$dest"
tar -xzf "$tmp/SDL.tar.gz" -C "$dest" --strip-components=1
[ -f "$dest/CMakeLists.txt" ] || { echo "fetch-sdl: SDL3 archive is incomplete" >&2; exit 1; }
echo "fetch-sdl: SDL3 ${version} is in $dest"
