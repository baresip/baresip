#!/usr/bin/env python3

import asyncio
import websockets
import json
import sys
import os

# The Endpoint for Ephemeral Tokens is strictly "Constrained"
URI = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContentConstrained"

async def connect_and_run(token):
    # FIX: Use 'Token' schema, NOT 'Bearer'. 
    # 'Bearer' triggers OAuth checks which fail for these tokens.
    headers = {
        "Authorization": f"Token {token}", 
        "Content-Type": "application/json"
    }

    print(f"Connecting to: {URI}")
    print(f"Using Header: Authorization: Token {token[:15]}...")
    print()
    print("=" * 80)
    print("CONNECTION DETAILS:")
    print("=" * 80)
    print(f"URI: {URI}")
    print(f"Headers:")
    for key, value in headers.items():
        if key == "Authorization":
            print(f"  {key}: Token {token[:20]}...")
        else:
            print(f"  {key}: {value}")
    print("=" * 80)
    print()
    
    try:
        async with websockets.connect(URI, additional_headers=headers) as websocket:
            print("✅ Connected!")
            print()

            # Setup message based on working SDK example
            # Note: Even with ephemeral tokens, the SDK sends a full setup message
            # with model (with "models/" prefix) and systemInstruction (with "role": "user")
            setup_msg = {
                "setup": {
                    "model": "models/gemini-2.5-flash-native-audio-preview-09-2025",
                    "generationConfig": {
                        "responseModalities": ["AUDIO"],
                        "temperature": 0.7
                    },
                    "systemInstruction": {
                        "parts": [
                            {
                                "text": "You are a helpful assistant and answer in a friendly tone."
                            }
                        ],
                        "role": "user"
                    }
                }
            }
            setup_msg_json = json.dumps(setup_msg)

            print("=" * 80)
            print("SENDING JSON MESSAGE:")
            print("=" * 80)
            print("Raw JSON (exact bytes being sent):")
            print(setup_msg_json)
            print()
            print("Formatted JSON (for readability):")
            print(json.dumps(setup_msg, indent=2))
            print("=" * 80)
            print()
            
            await websocket.send(setup_msg_json)
            print("Setup message sent. Waiting for responses...")
            print()

            # Loop to listen for server response
            message_count = 0
            while True:
                try:
                    response_raw = await asyncio.wait_for(websocket.recv(), timeout=10.0)
                    message_count += 1
                    
                    print("=" * 80)
                    print(f"RECEIVED JSON MESSAGE #{message_count}:")
                    print("=" * 80)
                    print("Raw JSON (exact bytes received):")
                    print(response_raw)
                    print()
                    
                    try:
                        msg = json.loads(response_raw)
                        print("Formatted JSON (for readability):")
                        print(json.dumps(msg, indent=2))
                        print()
                        
                        if "serverContent" in msg:
                            content = msg["serverContent"]
                            if "turnComplete" in content:
                                print("📝 Server: Turn Complete")
                            elif "modelTurn" in content:
                                print("🎵 Server: Audio/Text Data Received (Session is working!)")
                                # We can exit the test successfully here if we want
                                print("=" * 80)
                                print()
                                return
                        
                        elif "setupComplete" in msg:
                            print("✅ Server accepted setup. Session is Live.")
                        
                    except json.JSONDecodeError as e:
                        print(f"⚠️ Warning: Failed to parse JSON: {e}")
                        print("Response (raw string representation):")
                        print(repr(response_raw))
                    
                    print("=" * 80)
                    print()

                except asyncio.TimeoutError:
                    if message_count == 0:
                        print("⏱️  No response for 10s (Session might be idle).")
                    else:
                        print(f"⏱️  Received {message_count} message(s). No more messages for 10s.")
                    break
                    
    except websockets.InvalidStatusCode as e:
        status_code = getattr(e, 'status_code', 'unknown')
        print(f"❌ Connection Failed: HTTP Status {status_code}")
        if status_code in [403, 401]:
            print("   (Check your Token validity or API Key permissions)")
    except websockets.ConnectionClosed as e:
        close_code = getattr(e, 'code', 'unknown')
        close_reason = getattr(e, 'reason', 'unknown')
        print(f"❌ Connection Closed: {close_code} - {close_reason}")
        if close_code == 1007 and "project-scoped" in str(close_reason).lower():
            print()
            print("⚠️  ERROR: Ephemeral tokens cannot use project-scoped models!")
            print("   The token was created with a model that requires project-scoped access.")
            print("   Solution: Recreate the token with a base model (e.g., gemini-1.5-flash)")
    except Exception as e:
        error_type = type(e).__name__
        print(f"❌ Error ({error_type}): {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    # Only accept token as command line argument, no environment variables
    if len(sys.argv) < 2:
        print("Usage: python3 gemini-test.py <auth_tokens/...>")
        print("")
        print("Example:")
        print("  python3 gemini-test.py \"auth_tokens/abc123...\"")
        print("")
        print("Note: Token should be obtained from create_token.sh")
        sys.exit(1)
    
    token_arg = sys.argv[1]
    
    if not token_arg.startswith("auth_tokens/"):
        print("Warning: Token usually starts with 'auth_tokens/'. Proceeding...")

    asyncio.run(connect_and_run(token_arg))