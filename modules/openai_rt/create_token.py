#!/usr/bin/env python3
"""
Create Google Gemini auth token script

This script creates an authentication token for the Google Gemini API.
The JSON request body is manually constructed for easy visibility.
"""

import os
import sys
from datetime import datetime, timedelta, timezone

try:
    import requests
    HAS_REQUESTS = True
except ImportError:
    import urllib.request
    import urllib.parse
    import json
    HAS_REQUESTS = False


# Configuration
API_KEY = os.environ.get("GOOGLE_API_KEY")
BASE_URL = "https://generativelanguage.googleapis.com"
API_VERSION = "v1alpha"
ENDPOINT = f"{BASE_URL}/{API_VERSION}/auth_tokens"


def main():
    # Check if API key is set
    if not API_KEY:
        print("Error: GOOGLE_API_KEY environment variable is required.", file=sys.stderr)
        print("Usage: export GOOGLE_API_KEY=\"your_api_key_here\" && python3 create_token.py", file=sys.stderr)
        sys.exit(1)

    # Calculate expireTime (60 minutes from now) and newSessionExpireTime (1 minute from now)
    # Format: YYYY-MM-DDTHH:MM:SS.ffffff+00:00 (with microseconds and timezone offset)
    now = datetime.now(timezone.utc)
    expire_time = (now + timedelta(minutes=60)).strftime('%Y-%m-%dT%H:%M:%S.%f+00:00')
    new_session_expire_time = (now + timedelta(minutes=1)).strftime('%Y-%m-%dT%H:%M:%S.%f+00:00')

    # Manually construct JSON request body for easy visibility
    # Request body matching working SDK format (NO bidiGenerateContentSetup field)
    # The setup configuration is sent when using the token, not when creating it
    request_body = (
        "{\n"
        '  "expireTime": "' + expire_time + '",\n'
        '  "newSessionExpireTime": "' + new_session_expire_time + '",\n'
        '  "uses": 1\n'
        "}"
    )

    # Display request information
    print("Creating auth token...")
    print()
    separator = "=" * 78
    print(separator)
    print("SENDING REQUEST:")
    print(separator)
    print(f"Endpoint: {ENDPOINT}")
    print("Headers:")
    print("  Content-Type: application/json")
    print(f"  x-goog-api-key: {API_KEY[:20]}...")
    print()
    print("Request Body (Raw JSON):")
    print(request_body)
    print()
    print("Request Body (Formatted JSON):")
    try:
        import json
        parsed = json.loads(request_body)
        print(json.dumps(parsed, indent=2))
    except Exception:
        print(request_body)
    print(separator)
    print()

    # Make the request
    if HAS_REQUESTS:
        try:
            response = requests.post(
                ENDPOINT,
                headers={
                    "Content-Type": "application/json",
                    "x-goog-api-key": API_KEY
                },
                data=request_body
            )
            http_code = response.status_code
            http_body = response.text
        except Exception as e:
            print(f"Error making request: {e}", file=sys.stderr)
            sys.exit(1)
    else:
        # Fallback to urllib
        try:
            req = urllib.request.Request(
                ENDPOINT,
                data=request_body.encode('utf-8'),
                headers={
                    "Content-Type": "application/json",
                    "x-goog-api-key": API_KEY
                }
            )
            with urllib.request.urlopen(req) as response:
                http_code = response.getcode()
                http_body = response.read().decode('utf-8')
        except urllib.error.HTTPError as e:
            http_code = e.code
            http_body = e.read().decode('utf-8')
        except Exception as e:
            print(f"Error making request: {e}", file=sys.stderr)
            sys.exit(1)

    # Display response
    print(separator)
    print("RECEIVED RESPONSE:")
    print(separator)
    print(f"HTTP Status Code: {http_code}")
    print()
    print("Response Body (Raw JSON):")
    print(http_body)
    print()
    print("Response Body (Formatted JSON):")
    try:
        import json
        parsed = json.loads(http_body)
        print(json.dumps(parsed, indent=2))
    except Exception:
        print("(Invalid JSON)")
    print(separator)
    print()

    # Check response and extract token
    if 200 <= http_code < 300:
        try:
            import json
            response_data = json.loads(http_body)
            # Extract the token (field name is 'name' or 'token')
            token = response_data.get('name') or response_data.get('token') or None
            
            if not token or token == "null":
                print("Error: Could not extract token.")
                print(http_body)
                sys.exit(1)

            print("Success! Token created.")
            print()
            print(f"Token: {token}")
            print()
            print("Run the python script with the token as command line argument:")
            print(f'  ./modules/openai_rt/gemini-test.py "{token}"')
        except json.JSONDecodeError:
            print("Error: Could not parse response as JSON.")
            print(http_body)
            sys.exit(1)
    else:
        print(f"Error! HTTP Status: {http_code}")
        print(http_body)
        sys.exit(1)


if __name__ == "__main__":
    main()


