#!/bin/bash
#
# Docs https://zulip.com/api/upload-file
#
# Must set env vars:
#
#   * ZULIP_EMAIL
#   * ZULIP_API_KEY
#
# Optionally set env vars:
#
#   * ZULIP_DOMAIN - default: raku.zulipchat.com
#
# Pass argument:
#
#   * path to file
#
# Example usage uploading a file, extracting the path component
# from the response, and posting a message with the fully-qualified
# URL to download the file
#
#     $ url_path_component=$(./tools/zulip-file-upload.sh Artistic2.txt | jq -r '.url')
#
#     $ echo $url_path_component
#     /user_uploads/56534/N8QnWz2lDP8-1daEkLDKpXSh/Artistic2.txt
#
#     $ ./tools/zulip-message-post.sh "check out this file: https://raku.zulipchat.com${url_path_component}"
#     {"result":"success","msg":"","id":470682252}
#


if [ -z $ZULIP_DOMAIN ]; then
  ZULIP_DOMAIN="raku.zulipchat.com"
fi

set -eu

UPLOAD_FILE=$1

curl -sSX POST "https://${ZULIP_DOMAIN}/api/v1/user_uploads" \
    -u "${ZULIP_EMAIL}:${ZULIP_API_KEY}" \
    -F "filename=@${UPLOAD_FILE}"

