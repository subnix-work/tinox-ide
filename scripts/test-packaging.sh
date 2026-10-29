#!/usr/bin/env bash
# Validate real staged installation, relocatable loader path and desktop file opening.
set -euo pipefail
cd "$(dirname "$0")/.."
here=$PWD
sandbox=$(mktemp -d)
trap 'rm -rf "$sandbox"' EXIT
build_rpath=$(patchelf --print-rpath build/tinox-ide)
make install PREFIX=/usr LIBDIR=lib DESTDIR="$sandbox/root"
prefix="$sandbox/root/usr"
[[ "$(patchelf --print-rpath build/tinox-ide)" == "$build_rpath" ]]
[[ "$(patchelf --print-rpath "$prefix/bin/tinox-ide")" == '$ORIGIN/../lib' ]]
[[ "$(ldd "$prefix/bin/tinox-ide" | grep libtinoxkde.so.1)" == *"$prefix/bin/../lib/"* ]]
for path in share/applications/org.subnix.tinoxide.desktop \
    share/metainfo/org.subnix.tinoxide.metainfo.xml \
    share/icons/hicolor/scalable/apps/org.subnix.tinoxide.svg \
    share/org.kde.syntax-highlighting/syntax/tinox.xml \
    share/mime/packages/org.subnix.tinoxide.xml share/knotifications6/tinoxide.notifyrc; do
    test -s "$prefix/$path"
done
test ! -e "$prefix/lib/libtinoxkde-mock.so"
desktop-file-validate "$prefix/share/applications/org.subnix.tinoxide.desktop"
appstreamcli validate --no-net "$prefix/share/metainfo/org.subnix.tinoxide.metainfo.xml"
update-mime-database "$prefix/share/mime"
mkdir -p "$sandbox/project with spaces"
printf 'class Main {}\n' > "$sandbox/project with spaces/Example.tnx"
printf '[package]\nname = "packaging-smoke"\n' > "$sandbox/project with spaces/tinox.toml"
export XDG_DATA_DIRS="$prefix/share:/usr/local/share:/usr/share"
[[ "$(xdg-mime query filetype "$sandbox/project with spaces/Example.tnx")" == text/x-tinox ]]
[[ "$(xdg-mime query filetype "$sandbox/project with spaces/tinox.toml")" == application/x-tinox-project ]]
# A clean cwd proves no build-tree path is needed. Relative file arguments must work too.
cd "$sandbox"
for argument in 'project with spaces/Example.tnx' 'project with spaces/tinox.toml'; do
    config="$sandbox/config-$(basename "$argument")"
    env QT_QPA_PLATFORM=offscreen TKDE_NO_KCRASH=1 TINOXIDE_SMOKE=1 \
        XDG_CONFIG_HOME="$config" XDG_DATA_HOME="$sandbox/data" XDG_CACHE_HOME="$sandbox/cache" \
        "$here/scripts/test-gui-smoke.sh" 'installed IDE' timeout 30 "$prefix/bin/tinox-ide" "$argument"
    grep -Fq "$sandbox/project with spaces" "$config/tinoxiderc"
    if [[ "$argument" == *.tnx ]]; then
        grep -Fq 'Example.tnx' "$config/tinoxiderc"
    fi
done
echo 'packaging: staged binary resolves its installed bridge; desktop, AppStream, MIME and file/project startup pass'
