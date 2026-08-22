#!/usr/bin/env bash
# Push a new payload file (and version string) to an existing store.kde.org
# product over the OCS v1 API that api.kde-look.org still serves.
#
# The product must already exist -- creating one is a one-time manual step in
# the web UI (see packaging/pling/PUBLISHING.md). After that, releases update
# it from CI with three secrets:
#
#   PLING_USERNAME    your OpenDesktop/store.kde.org login
#   PLING_PASSWORD    its password (use an account dedicated to publishing)
#   PLING_CONTENT_ID  the number in the product URL, e.g. store.kde.org/p/123456
#
# Usage: publish-to-pling.sh <payload.tar.gz>
set -euo pipefail

payload="${1:?payload file required}"
[ -f "$payload" ] || { echo "No such file: $payload" >&2; exit 1; }

: "${PLING_USERNAME:?PLING_USERNAME not set}"
: "${PLING_PASSWORD:?PLING_PASSWORD not set}"
: "${PLING_CONTENT_ID:?PLING_CONTENT_ID not set}"

api="https://api.kde-look.org/ocs/v1"
version="$(basename "$payload" | sed -E 's/^.*-v([0-9][0-9.]*)\.tar\.gz$/\1/')"

ocs_status() {
    # OCS wraps everything in XML with <statuscode>100</statuscode> = ok.
    grep -o '<statuscode>[0-9]*</statuscode>' | grep -o '[0-9]*' | head -1
}

echo "Updating product $PLING_CONTENT_ID to version $version"
edit_response="$(curl -sS -u "$PLING_USERNAME:$PLING_PASSWORD" \
    --data-urlencode "version=$version" \
    "$api/content/edit/$PLING_CONTENT_ID")"
status="$(printf '%s' "$edit_response" | ocs_status)"
if [ "$status" != "100" ]; then
    echo "content/edit failed (statuscode=$status):" >&2
    printf '%s\n' "$edit_response" >&2
    exit 1
fi

echo "Uploading payload $(basename "$payload")"
upload_response="$(curl -sS -u "$PLING_USERNAME:$PLING_PASSWORD" \
    -F "localfile=@$payload" \
    "$api/content/uploaddownload/$PLING_CONTENT_ID")"
status="$(printf '%s' "$upload_response" | ocs_status)"
if [ "$status" != "100" ]; then
    echo "content/uploaddownload failed (statuscode=$status):" >&2
    printf '%s\n' "$upload_response" >&2
    exit 1
fi

echo "Published: https://store.kde.org/p/$PLING_CONTENT_ID (version $version)"
echo "It appears in Dolphin under Settings > Configure Dolphin > Context Menu"
echo "> Download New Services after the store cache refreshes."
