#!/bin/bash

#######################
#    Configs Start    #
#######################

# Gateway address
GATEWAY="192.168.0.1"

# Admin password
PASSWORD="XXXXX"

#####################
#    Configs End    #
#####################

# Dependency check
if ! command -v jq &> /dev/null; then
    echo "Error: 'jq' is not installed. Please install it to parse JSON."
    exit 1
fi

# 1. Get Salt (zte_web_sault)
TIMESTAMP=$(date +%s%3N)
URL="http://$GATEWAY/ubus/?t=$TIMESTAMP"

SAULT_PAYLOAD='[{"jsonrpc":"2.0","id":1,"method":"call","params":["00000000000000000000000000000000","zwrt_web","web_login_info",{"":""}]}]'

echo "Fetching salt..."
SAULT_RESPONSE=$(curl -s -X POST "$URL" -H "Content-Type: application/json" -d "$SAULT_PAYLOAD")

SAULT=$(echo "$SAULT_RESPONSE" | jq -r '.[0].result[1].zte_web_sault // empty')

if [ -z "$SAULT" ]; then
    echo "Failed to fetch salt. Response:"
    echo "$SAULT_RESPONSE"
    exit 1
fi

# 2. Hash Password
# Equivalent to: sha256(sha256(Password).upper() + salt).upper()
HASH1=$(echo -n "$PASSWORD" | openssl dgst -sha256 | awk '{print toupper($2)}')
HASH2=$(echo -n "$HASH1$SAULT" | openssl dgst -sha256 | awk '{print toupper($2)}')

# 3. Login to get Session (ubus_rpc_session)
LOGIN_PAYLOAD=$(jq -n --arg hash "$HASH2" '[{"jsonrpc":"2.0","id":2,"method":"call","params":["00000000000000000000000000000000","zwrt_web","web_login",{"password":$hash}]}]')

echo "Logging in..."
SESSION_RESPONSE=$(curl -s -X POST "$URL" -H "Content-Type: application/json" -d "$LOGIN_PAYLOAD")

SESSION=$(echo "$SESSION_RESPONSE" | jq -r '.[0].result[1].ubus_rpc_session // empty')

if [ -z "$SESSION" ]; then
    echo "Failed to log in. Response:"
    echo "$SESSION_RESPONSE"
    exit 1
fi

# 4. Enable Debug Mode
TIMESTAMP_NEW=$(date +%s%3N)
URL_DEBUG="http://$GATEWAY/ubus/?t=$TIMESTAMP_NEW"

DEBUG_PAYLOAD=$(jq -n --arg sess "$SESSION" '[{"jsonrpc":"2.0","id":3,"method":"call","params":[$sess,"zwrt_bsp.usb","set",{"mode":"debug"}]}]')

echo "Enabling debug mode..."
ENABLE_RESPONSE=$(curl -s -X POST "$URL_DEBUG" -H "Content-Type: application/json" -d "$DEBUG_PAYLOAD")

echo "Result:"
echo "$ENABLE_RESPONSE" | jq .
