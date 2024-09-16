#!/bin/bash
#
# See api docs: https://zulip.com/api/send-message
#
# Must set env vars:
#
#   * ZULIP_EMAIL
#   * ZULIP_API_KEY
#
# Optionally set env vars:
#
#   * ZULIP_DOMAIN - default: raku.zulipchat.com
#   * ZULIP_TO - default: dev
#   * ZULIP_TOPIC - default: "API Test"
#
# Pass argument:
#
#   * string content to post into the message
#


if [ -z $ZULIP_DOMAIN ]; then
  ZULIP_DOMAIN="raku.zulipchat.com"
fi

if [ -z $ZULIP_TO ]; then
  ZULIP_TO="dev"
fi

if [ -z $ZULIP_TOPIC ]; then
  ZULIP_TOPIC="API Test"
fi

set -eu

message_content=$1


curl -sSX POST "https://${ZULIP_DOMAIN}/api/v1/messages" \
    -u "${ZULIP_EMAIL}:${ZULIP_API_KEY}" \
    --data-urlencode type=stream \
    --data-urlencode "to=${ZULIP_TO}" \
    --data-urlencode "topic=${ZULIP_TOPIC}" \
    --data-urlencode content="${message_content}"

