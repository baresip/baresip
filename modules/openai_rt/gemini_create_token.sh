#!/bin/bash

# Configuration
# Only use GOOGLE_API_KEY, no fallback to GEMINI_API_KEY to avoid env clashing
API_KEY="${GOOGLE_API_KEY}"
BASE_URL="https://generativelanguage.googleapis.com"
API_VERSION="v1alpha"
ENDPOINT="${BASE_URL}/${API_VERSION}/auth_tokens"

# Check if API key is set
if [ -z "$API_KEY" ]; then
    echo "Error: GOOGLE_API_KEY environment variable is required." >&2
    echo "Usage: export GOOGLE_API_KEY=\"your_api_key_here\" && ./create_token.sh" >&2
    exit 1
fi


# Calculate expireTime (60 minutes from now) and newSessionExpireTime (1 minute from now)
# Format: YYYY-MM-DDTHH:MM:SS.ffffff+00:00 (with microseconds and timezone offset, matching working SDK)
# The SDK uses 60 minutes for expire time and 1 minute for new session expire time

# Try Python for precise timestamp formatting with microseconds
if command -v python3 &> /dev/null; then
    # Use Python to get timestamp with microseconds and timezone offset
    EXPIRE_TIME=$(python3 -c "from datetime import datetime, timedelta, timezone; print((datetime.now(timezone.utc) + timedelta(minutes=60)).strftime('%Y-%m-%dT%H:%M:%S.%f+00:00'))")
    NEW_SESSION_EXPIRE_TIME=$(python3 -c "from datetime import datetime, timedelta, timezone; print((datetime.now(timezone.utc) + timedelta(minutes=1)).strftime('%Y-%m-%dT%H:%M:%S.%f+00:00'))")
elif date -u -v+60M +"%Y-%m-%dT%H:%M:%S.000000+00:00" &>/dev/null 2>&1; then
    # macOS format (approximation without microseconds)
    EXPIRE_TIME=$(date -u -v+60M +"%Y-%m-%dT%H:%M:%S.000000+00:00")
    NEW_SESSION_EXPIRE_TIME=$(date -u -v+1M +"%Y-%m-%dT%H:%M:%S.000000+00:00")
elif date -u -d "+60 minutes" +"%Y-%m-%dT%H:%M:%S.000000+00:00" &>/dev/null 2>&1; then
    # Linux format (GNU date, approximation without microseconds)
    EXPIRE_TIME=$(date -u -d "+60 minutes" +"%Y-%m-%dT%H:%M:%S.000000+00:00")
    NEW_SESSION_EXPIRE_TIME=$(date -u -d "+1 minute" +"%Y-%m-%dT%H:%M:%S.000000+00:00")
else
    echo "Error: Need python3 or compatible date command for timestamp generation." >&2
    exit 1
fi

# Validate expire times were calculated
if [ -z "$EXPIRE_TIME" ] || [ -z "$NEW_SESSION_EXPIRE_TIME" ]; then
    echo "Error: Failed to calculate expireTime or newSessionExpireTime. Please check your date command." >&2
    exit 1
fi

# Request body matching working SDK format (NO bidiGenerateContentSetup field)
# The setup configuration is sent when using the token, not when creating it
REQUEST_BODY=$(cat <<EOF
{
  "expireTime": "${EXPIRE_TIME}",
  "newSessionExpireTime": "${NEW_SESSION_EXPIRE_TIME}",
  "uses": 1
}
EOF
)

# Make the request
echo "Creating auth token..."
echo ""
SEPARATOR="=================================================================================="
echo "$SEPARATOR"
echo "SENDING REQUEST:"
echo "$SEPARATOR"
echo "Endpoint: ${ENDPOINT}"
echo "Headers:"
echo "  Content-Type: application/json"
echo "  x-goog-api-key: ${API_KEY:0:20}..."
echo ""
echo "Request Body (Raw JSON):"
echo "${REQUEST_BODY}"
echo ""
if command -v jq &> /dev/null; then
    echo "Request Body (Formatted JSON):"
    echo "${REQUEST_BODY}" | jq '.'
fi
echo "$SEPARATOR"
echo ""

RESPONSE=$(curl -s -w "\n%{http_code}" \
  -X POST \
  -H "Content-Type: application/json" \
  -H "x-goog-api-key: ${API_KEY}" \
  -d "${REQUEST_BODY}" \
  "${ENDPOINT}")

# Extract Body and Code
HTTP_BODY=$(echo "$RESPONSE" | sed '$d')
HTTP_CODE=$(echo "$RESPONSE" | tail -n 1)

echo "$SEPARATOR"
echo "RECEIVED RESPONSE:"
echo "$SEPARATOR"
echo "HTTP Status Code: ${HTTP_CODE}"
echo ""
echo "Response Body (Raw JSON):"
echo "${HTTP_BODY}"
echo ""
if command -v jq &> /dev/null; then
    echo "Response Body (Formatted JSON):"
    echo "${HTTP_BODY}" | jq '.' 2>/dev/null || echo "(Invalid JSON)"
fi
echo "$SEPARATOR"
echo ""

if [ "$HTTP_CODE" -ge 200 ] && [ "$HTTP_CODE" -lt 300 ]; then
    # Extract the token (field name is 'name' or 'token')
    TOKEN=$(echo "$HTTP_BODY" | jq -r '.name // .token // empty')
    
    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        echo "Error: Could not extract token."
        echo "$HTTP_BODY"
        exit 1
    fi

    echo "Success! Token created."
    echo ""
    echo "Token: $TOKEN"
    echo ""
    echo "Run the python script with the token as command line argument:"
    echo "  ./modules/openai_rt/gemini-test.py \"$TOKEN\""
else
    echo "Error! HTTP Status: ${HTTP_CODE}"
    echo "$HTTP_BODY"
    exit 1
fi