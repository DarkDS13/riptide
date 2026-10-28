#!/usr/bin/env bash
# Download the Silesia corpus and build the gzip test set used in our experiments:
#   pilot/silesia/gz/<file>.gz    each of the 12 files, gzip -6
#   pilot/silesia/silesia.tar.gz  tarball of all 12 files, gzip -6 (as in the base paper)
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DIR="$ROOT/pilot/silesia"
URL="https://sun.aei.polsl.pl/~sdeor/corpus/silesia.zip"
FILES="dickens mozilla mr nci ooffice osdb reymont samba sao webster x-ray xml"

mkdir -p "$DIR/files" "$DIR/gz"
if [ ! -f "$DIR/silesia.zip" ]; then
    echo "Downloading Silesia corpus (about 68 MB)..."
    curl -L --fail -o "$DIR/silesia.zip" "$URL"
fi
unzip -tq "$DIR/silesia.zip"
unzip -oq "$DIR/silesia.zip" -d "$DIR/files"

for f in $FILES; do
    gzip -6 -c "$DIR/files/$f" > "$DIR/gz/$f.gz"
done

# Keep macOS extended attributes out of the tarball so it is identical across platforms.
TAR_OPTS=""
if tar --version 2>/dev/null | grep -qi bsdtar; then
    TAR_OPTS="--no-mac-metadata"
fi
COPYFILE_DISABLE=1 tar $TAR_OPTS -cf "$DIR/silesia.tar" -C "$DIR/files" $FILES
gzip -6 -c "$DIR/silesia.tar" > "$DIR/silesia.tar.gz"
rm "$DIR/silesia.tar"

for f in "$DIR"/gz/*.gz "$DIR/silesia.tar.gz"; do
    gzip -t "$f"
done
echo "Done: $(ls "$DIR"/gz | wc -l | tr -d ' ') files and silesia.tar.gz in $DIR"
