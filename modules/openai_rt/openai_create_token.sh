#!/bin/bash

# Configuration
# Only use OPENAI_API_KEY, no fallback to avoid env clashing
API_KEY="${OPENAI_API_KEY}"
BASE_URL="https://api.openai.com"
ENDPOINT="${BASE_URL}/v1/realtime/client_secrets"

# Default values (can be overridden via environment variables)
PROMPT="${OPENAI_PROMPT:-Be a friendly assistant}"
VOICE="${OPENAI_VOICE:-sage}"
TURN_DETECTION="${OPENAI_TURN_DETECTION:-server_vad}"

# Check if API key is set
if [ -z "$API_KEY" ]; then
    echo "Error: OPENAI_API_KEY environment variable is required." >&2
    echo "Usage: export OPENAI_API_KEY=\"your_api_key_here\" && ./openai_create_token.sh" >&2
    exit 1
fi

# Escape prompt for JSON (escape quotes and backslashes)
# Use Python if available for proper JSON escaping, otherwise basic escaping
if command -v python3 &> /dev/null; then
    ESCAPED_PROMPT=$(python3 -c "import json, sys; print(json.dumps(sys.argv[1]))" "$PROMPT")
    # Remove surrounding quotes from json.dumps output
    ESCAPED_PROMPT="${ESCAPED_PROMPT#\"}"
    ESCAPED_PROMPT="${ESCAPED_PROMPT%\"}"
else
    # Basic escaping: escape backslashes first, then quotes
    ESCAPED_PROMPT=$(echo "$PROMPT" | sed 's/\\/\\\\/g' | sed 's/"/\\"/g')
fi

# Request body based on Perl implementation
# Note: The session configuration is embedded in the token creation request
REQUEST_BODY=$(cat <<EOF
{
  "expires_after": {
    "anchor": "created_at",
    "seconds": 3600
  },
  "session": {
    "type": "realtime",
    "model": "gpt-realtime",
    "tool_choice": "none",
    "instructions": "${ESCAPED_PROMPT}",
    "output_modalities": ["audio"],
    "audio": {
      "input": {
        "format": {
          "type": "audio/pcm",
          "rate": 24000
        },
        "noise_reduction": {
          "type": "near_field"
        },
        "turn_detection": {
          "type": "server_vad",
          "threshold": 0.5,
          "prefix_padding_ms": 300,
          "silence_duration_ms": 500,
          "idle_timeout_ms": 6000,
          "create_response": true,
          "interrupt_response": true
        }
      },
      "output": {
        "format": {
          "type": "audio/pcm",
          "rate": 24000
        },
        "voice": "${VOICE}"
      }
    }
  }
}
EOF
)

# Make the request
echo "Creating OpenAI ephemeral client secret..."
echo ""
SEPARATOR="=================================================================================="
echo "$SEPARATOR"
echo "SENDING REQUEST:"
echo "$SEPARATOR"
echo "Endpoint: ${ENDPOINT}"
echo "Headers:"
echo "  Content-Type: application/json"
echo "  Accept: application/json"
echo "  Authorization: Bearer ${API_KEY:0:20}..."
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
  -H "Accept: application/json" \
  -H "Authorization: Bearer ${API_KEY}" \
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
    
    TOKEN=$(echo "$HTTP_BODY" | jq -r '.value // empty')
    
    if [ -z "$TOKEN" ] || [ "$TOKEN" = "null" ]; then
        echo "Error: Could not extract ephemeral key from response."
        echo "Expected field 'value' in response."
        echo "$HTTP_BODY"
        exit 1
    fi

    echo "Success! Ephemeral client secret created."
    echo ""
    echo "Ephemeral Key: $TOKEN"
    echo ""
    echo "You can use this ephemeral key as your OPENAI_API_KEY for the WebSocket connection."
    echo "The key expires 3600 seconds (1 hour) after creation."
else
    echo "Error! HTTP Status: ${HTTP_CODE}"
    echo "$HTTP_BODY"
    exit 1
fi

