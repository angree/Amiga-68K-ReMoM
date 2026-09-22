#!/bin/sh
#
# pobierz-upstream.sh <plik.tar.gz> - sciaga przypiety snapshot jbalcomb/ReMoM
# z GitHuba i odtwarza z niego upstream/ReMoM-a9cc082.tar.gz BAJT W BAJT:
# tylko pliki z build/upstream-pliki.txt, konce linii CRLF (jak w kopii,
# na ktorej powstaly latki). Sprawdzone 2026-09-22: 398/398 plikow identycznych.
# Kodu ReMoM nie ma w repozytorium publicznym - nalezy do jego autora.
#
set -e
CEL="$1"
LISTA="$(cd "$(dirname "$0")" && pwd)/upstream-pliki.txt"
SHA=a9cc0826c42c28962d926d2243ca30c864fc493c
TMP=$(mktemp -d)
echo "== pobieram jbalcomb/ReMoM @ $SHA"
curl -fL -o "$TMP/r.tar.gz" "https://github.com/jbalcomb/ReMoM/archive/$SHA.tar.gz"
mkdir "$TMP/x"
tar xzf "$TMP/r.tar.gz" -C "$TMP/x"
mv "$TMP/x/ReMoM-$SHA" "$TMP/ReMoM"
cd "$TMP"
while read f; do perl -pi -e 's/\r?\n/\r\n/' "$f"; done < "$LISTA"
mkdir -p "$(dirname "$CEL")"
tar czf "$CEL" -T "$LISTA"
cd /
rm -rf "$TMP"
