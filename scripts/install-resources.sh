#!/usr/bin/env bash
# Installs the Tinox syntax definition, MIME types and notification events for the current user.
# Kate, KDevelop, KTextEditor based editors and the IDE pick them up afterwards.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
data=${XDG_DATA_HOME:-$HOME/.local/share}

mkdir -p "$data/org.kde.syntax-highlighting/syntax" "$data/mime/packages" "$data/knotifications6"
install -m 644 "$here/resources/notifications/tinoxide.notifyrc" "$data/knotifications6/tinoxide.notifyrc"
install -m 644 "$here/resources/syntax/tinox.xml" "$data/org.kde.syntax-highlighting/syntax/tinox.xml"
install -m 644 "$here/resources/mime/tinox.xml" "$data/mime/packages/tinox.xml"
if command -v update-mime-database >/dev/null 2>&1; then
    update-mime-database "$data/mime"
else
    echo "note: update-mime-database not found; the MIME type is registered on the next login"
fi
echo "installed to $data (syntax: org.kde.syntax-highlighting/syntax/tinox.xml, mime: mime/packages/tinox.xml, notifications: knotifications6/tinoxide.notifyrc)"
