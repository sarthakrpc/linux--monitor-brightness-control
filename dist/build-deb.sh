#!/bin/bash
# Assemble build/brightness-control_<version>_amd64.deb from the release binary.
# Needs no root: dpkg-deb records root ownership itself.
set -euo pipefail

SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PKG=brightness-control

if [ ! -x "$SRC/build/$PKG" ]; then
    echo "build/$PKG is missing - run 'make' first" >&2
    exit 1
fi

# The package version is whatever the binary says it is.
VERSION="$("$SRC/build/$PKG" --version | awk '{print $2}')"
STAGE="$SRC/build/deb-stage"
OUT="$SRC/build/${PKG}_${VERSION}_amd64.deb"

rm -rf "$STAGE"
umask 022
put() { # put <mode> <source> <path inside the package>
    install -D -m "$1" "$2" "$STAGE/$3"
}

put 0755 "$SRC/build/$PKG"                        usr/bin/$PKG
put 0644 "$SRC/dist/$PKG.desktop"                 usr/share/applications/$PKG.desktop
# Starts the panel icon at every login, for every user.
put 0644 "$SRC/dist/$PKG-autostart.desktop"       etc/xdg/autostart/$PKG.desktop

put 0644 "$SRC/README.md"                         usr/share/doc/$PKG/README.md
# Kept in the same relative place, so the README's links and pictures work
# from the installed copy too.
for doc in "$SRC"/docs/*; do
    put 0644 "$doc"                               "usr/share/doc/$PKG/docs/$(basename "$doc")"
done

mkdir -p "$STAGE/DEBIAN"
for script in postinst prerm; do
    install -m 0755 "$SRC/dist/deb/$script" "$STAGE/DEBIAN/$script"
done
install -m 0644 "$SRC/dist/deb/conffiles" "$STAGE/DEBIAN/conffiles"

SIZE="$(du -sk --exclude=DEBIAN "$STAGE" | cut -f1)"
sed -e "s/@VERSION@/$VERSION/" -e "s/@INSTALLED_SIZE@/$SIZE/" \
    "$SRC/dist/deb/control.in" > "$STAGE/DEBIAN/control"

(cd "$STAGE" && find . -path ./DEBIAN -prune -o -type f -printf '%P\0' | sort -z | xargs -0 md5sum) \
    > "$STAGE/DEBIAN/md5sums"

rm -f "$OUT"
dpkg-deb --root-owner-group -Zxz --build "$STAGE" "$OUT" >/dev/null
rm -rf "$STAGE"

echo "built $OUT"
dpkg-deb --info "$OUT" | sed -n '/^ Package:/,$p'
