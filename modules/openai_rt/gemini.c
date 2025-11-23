/**
 * @file gemini.c  Google Gemini-specific implementation of AI model interface
 *
 * This file contains Gemini-specific code for:
 * - WebSocket connection setup (headers, URLs)
 * - JSON/Protocol Buffer message building (session setup, audio, responses, tools)
 * - Message parsing (event handling)
 *
 * Note: Gemini Live API may use a different protocol than WebSocket.
 * This implementation provides a structure that can be adapted based on
 * the actual Gemini Live API protocol specification.
 *
 * Copyright (C) 2025 Sipfront
 */

#include "openai_rt.h"
#include "ai_model.h"
#include <json-c/json.h>
#include <libwebsockets.h>

/* Gemini-specific configuration */
#define GEMINI_API_HOST "generativelanguage.googleapis.com"
#define GEMINI_API_PORT 443
/* WebSocket endpoint pattern:
 * wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.{version}.GenerativeService.{method}
 * 
 * For regular API key:
 * /ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContent
 * 
 * For ephemeral auth token (starts with "auth_tokens/"):
 * /ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContentConstrained
 */
#define GEMINI_API_PATH "/ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContent"
/* Default model - can be overridden for ephemeral tokens */
#define GEMINI_MODEL "gemini-2.5-flash-native-audio-preview-09-2025"
/* For ephemeral tokens, use gemini-2.5-flash-native-audio-preview-09-2025 */
#define GEMINI_MODEL_EPHEMERAL "gemini-2.5-flash-native-audio-preview-09-2025"

/* Forward declarations */
static int gemini_init(struct openai_rt *ort);
static void gemini_close(void);
static int gemini_get_connection_info(char *address, size_t address_len,
                                      int *port, char *path, size_t path_len);
static int gemini_add_auth_headers(void *in, size_t len);
static int gemini_build_session_update(const char *prompt, char **json_msg);
static int gemini_build_audio_append(const char *base64_audio, char **json_msg);
static int gemini_build_response_create(const char *instructions, char **json_msg);
static int gemini_build_function_call_output(const char *call_id, const char *output,
                                             char **json_msg);
static int gemini_parse_message(const char *json_str,
                               void (*audio_delta_cb)(const char *base64_audio, void *arg),
                               void (*session_updated_cb)(void *arg),
                               void (*speech_started_cb)(void *arg),
                               void (*function_call_cb)(const char *call_id,
                                                       const char *name,
                                                       const char *arguments,
                                                       void *arg),
                               void (*response_done_cb)(const char *response_json, void *arg),
                               void *cb_arg);

/* JSON parsing helpers */
static struct json_object *parse_json_safe(const char *json_str, const char *context);
static const char *get_json_string_field(struct json_object *obj, const char *field_name,
                                         const char *context);
static struct json_object *get_json_object_field(struct json_object *obj,
                                                 const char *field_name,
                                                 const char *context);

/* Gemini AI model implementation */
struct ai_model gemini_model = {
	.name = "gemini",
	.init = gemini_init,
	.close = gemini_close,
	.get_connection_info = gemini_get_connection_info,
	.add_auth_headers = gemini_add_auth_headers,
	.build_session_update = gemini_build_session_update,
	.build_audio_append = gemini_build_audio_append,
	.build_response_create = gemini_build_response_create,
	.build_function_call_output = gemini_build_function_call_output,
	.parse_message = gemini_parse_message,
};

/* Gemini-specific implementations */

static int gemini_init(struct openai_rt *ort)
{
	(void)ort;
	/* Gemini doesn't need special initialization beyond config */
	return 0;
}

static void gemini_close(void)
{
	/* Gemini doesn't need special cleanup */
}

static int gemini_get_connection_info(char *address, size_t address_len,
                                     int *port, char *path, size_t path_len)
{
	if (!address || !port || !path) {
		return EINVAL;
	}
	
	/* Check if API key is an ephemeral token (starts with "auth_tokens/") */
	const char *api_path = GEMINI_API_PATH;
	if (str_isset(g_oairt.api_key) && 
	    strncmp(g_oairt.api_key, "auth_tokens/", 12) == 0) {
		/* Use constrained endpoint for ephemeral tokens */
		api_path = "/ws/google.ai.generativelanguage.v1alpha.GenerativeService.BidiGenerateContentConstrained";
	}
	
	if (address_len < strlen(GEMINI_API_HOST) + 1) {
		return EOVERFLOW;
	}
	str_ncpy(address, GEMINI_API_HOST, address_len);
	
	*port = GEMINI_API_PORT;
	
	if (path_len < strlen(api_path) + 1) {
		return EOVERFLOW;
	}
	str_ncpy(path, api_path, path_len);
	
	return 0;
}

static int gemini_add_auth_headers(void *in, size_t len)
{
	unsigned char **p = (unsigned char **)in;
	unsigned char *end = (*p) + len;
	int written;

	if (!str_isset(g_oairt.api_key)) {
		warning("openai_rt: No API key available for Gemini headers\n");
		return -1;
	}

	/* Check if API key is an ephemeral token */
	if (strncmp(g_oairt.api_key, "auth_tokens/", 12) == 0) {
		/* Ephemeral token uses Authorization header */
		written = lws_snprintf((char *)*p, end - *p,
				"Authorization: Token %s\r\n", g_oairt.api_key);
	} else {
		/* Regular API key uses x-goog-api-key header */
		written = lws_snprintf((char *)*p, end - *p,
				"x-goog-api-key: %s\r\n", g_oairt.api_key);
	}
	
	if (written < 0 || written >= (int)(end - *p)) {
		warning("openai_rt: failed to add Gemini API key header\n");
		return -1;
	}
	*p += written;

	/* Add Content-Type header */
	written = lws_snprintf((char *)*p, end - *p,
			"Content-Type: application/json\r\n");
	if (written < 0 || written >= (int)(end - *p)) {
		warning("openai_rt: failed to add Content-Type header\n");
		return -1;
	}
	*p += written;

	return 0;
}

/**
 * Build session setup message for Gemini
 * 
 * Note: Gemini Live API uses a different message format than OpenAI.
 * This implementation adapts the structure based on Gemini's protocol.
 * Gemini expects:
 * - System instructions in the initial connection config
 * - Audio input at 16kHz PCM16 (different from OpenAI's 24kHz)
 * - Audio output at 24kHz PCM16
 */
static int gemini_build_session_update(const char *prompt, char **json_msg)
{
	char *escaped_prompt = NULL;
	int err;

	if (!prompt || !json_msg) {
		return EINVAL;
	}

	/* Escape JSON special characters in the prompt */
	size_t prompt_len = strlen(prompt);
	escaped_prompt = mem_zalloc(prompt_len * 2 + 1, NULL);
	if (!escaped_prompt) {
		return ENOMEM;
	}

	/* Simple JSON escaping - escape quotes, backslashes, and control characters */
	const char *src = prompt;
	char *dst = escaped_prompt;
	while (*src) {
		switch (*src) {
		case '"':
			*dst++ = '\\';
			*dst++ = '"';
			break;
		case '\\':
			*dst++ = '\\';
			*dst++ = '\\';
			break;
		case '\b':
			*dst++ = '\\';
			*dst++ = 'b';
			break;
		case '\f':
			*dst++ = '\\';
			*dst++ = 'f';
			break;
		case '\n':
			*dst++ = '\\';
			*dst++ = 'n';
			break;
		case '\r':
			*dst++ = '\\';
			*dst++ = 'r';
			break;
		case '\t':
			*dst++ = '\\';
			*dst++ = 't';
			break;
		default:
			if (*src < 0x20) {
				/* Escape other control characters as \uXXXX */
				re_snprintf(dst, 7, "\\u%04x", (unsigned char)*src);
				dst += 6;
			} else {
				*dst++ = *src;
			}
			break;
		}
		src++;
	}
	*dst = '\0';

	/* Gemini Live API uses "setup" message format (not session.update like OpenAI) */
	/* According to SF_DOC.md, the setup message format */
	/* NOTE: Temporarily removed tools support for troubleshooting */
	
	/* Determine model based on API key type */
	/* All models now use gemini-2.5-flash-native-audio-preview-09-2025 */
	const char *model_name = GEMINI_MODEL;
	if (str_isset(g_oairt.api_key) && 
	    strncmp(g_oairt.api_key, "auth_tokens/", 12) == 0) {
		/* Use same model for ephemeral tokens (as per working SDK example) */
		model_name = GEMINI_MODEL_EPHEMERAL;
	}
	
	/* Setup message format matching working SDK example:
	 * - Model name includes "models/" prefix
	 * - systemInstruction includes "role": "user"
	 * - No sessionResumption field (not in SDK example)
	 */
	static const char *setup_template =
		"{"
			"\"setup\": {"
				"\"model\": \"models/%s\","
				"\"generationConfig\": {"
					"\"responseModalities\": [\"AUDIO\"],"
					"\"temperature\": 0.7"
				"},"
				"\"systemInstruction\": {"
					"\"parts\": ["
						"{"
							"\"text\": \"%s\""
						"}"
					"],"
					"\"role\": \"user\""
				"}"
			"}"
		"}";
	err = re_sdprintf(json_msg, setup_template, 
	                  model_name, escaped_prompt);
	
	mem_deref(escaped_prompt);

	if (err) {
		return err;
	}

	info("openai_rt: Gemini setup message built: %s\n", *json_msg);
	return 0;
}

/**
 * Build audio append message for Gemini
 * 
 * Note: Gemini uses "realtime_input" format for audio, not a separate append message.
 * Audio format: PCM16 at 16kHz, mono
 * Base64 URL-safe encoding is used (no padding)
 */
static int gemini_build_audio_append(const char *base64_audio, char **json_msg)
{
	if (!base64_audio || !json_msg) {
		return EINVAL;
	}

	/* Gemini Live API uses "realtime_input" format for audio */
	/* According to SF_DOC.md: */
	int err = re_sdprintf(json_msg,
		"{"
			"\"realtime_input\": {"
				"\"audio\": {"
					"\"data\": \"%s\","
					"\"mimeType\": \"audio/pcm;rate=16000\""
				"}"
			"}"
		"}",
		base64_audio);

	return err;
}

/**
 * Build response create message for Gemini
 * 
 * Note: Gemini uses "client_content" format for turn-based text messages.
 * For starting a conversation, we can send a simple "Hello" message.
 */
static int gemini_build_response_create(const char *instructions, char **json_msg)
{
	char *escaped_text = NULL;
	int err;

	if (!json_msg) {
		return EINVAL;
	}

	const char *text_content = (instructions && *instructions) ? instructions : "Hello";

	/* Escape JSON special characters */
	size_t text_len = strlen(text_content);
	escaped_text = mem_zalloc(text_len * 2 + 1, NULL);
	if (!escaped_text) {
		return ENOMEM;
	}

	/* Simple JSON escaping */
	const char *src = text_content;
	char *dst = escaped_text;
	while (*src) {
		switch (*src) {
		case '"':
			*dst++ = '\\';
			*dst++ = '"';
			break;
		case '\\':
			*dst++ = '\\';
			*dst++ = '\\';
			break;
		case '\n':
			*dst++ = '\\';
			*dst++ = 'n';
			break;
		case '\r':
			*dst++ = '\\';
			*dst++ = 'r';
			break;
		case '\t':
			*dst++ = '\\';
			*dst++ = 't';
			break;
		default:
			*dst++ = *src;
			break;
		}
		src++;
	}
	*dst = '\0';

	/* Gemini Live API uses "client_content" format for turn-based text */
	err = re_sdprintf(json_msg,
		"{"
			"\"client_content\": {"
				"\"turns\": ["
					"{"
						"\"role\": \"user\","
						"\"parts\": ["
							"{"
								"\"text\": \"%s\""
							"}"
						"]"
					"}"
				"],"
				"\"turnComplete\": true"
			"}"
		"}",
		escaped_text);
	mem_deref(escaped_text);

	return err;
}

/**
 * Build function call output message for Gemini
 * 
 * Note: Gemini uses "tool_response" format with functionResponses array.
 */
static int gemini_build_function_call_output(const char *call_id, const char *output,
                                            char **json_msg)
{
	if (!call_id || !output || !json_msg) {
		return EINVAL;
	}

	/* Escape output text for JSON */
	char *escaped_output = NULL;
	size_t output_len = strlen(output);
	escaped_output = mem_zalloc(output_len * 2 + 1, NULL);
	if (!escaped_output) {
		return ENOMEM;
	}

	const char *src = output;
	char *dst = escaped_output;
	while (*src) {
		switch (*src) {
		case '"':
			*dst++ = '\\';
			*dst++ = '"';
			break;
		case '\\':
			*dst++ = '\\';
			*dst++ = '\\';
			break;
		case '\n':
			*dst++ = '\\';
			*dst++ = 'n';
			break;
		case '\r':
			*dst++ = '\\';
			*dst++ = 'r';
			break;
		default:
			*dst++ = *src;
			break;
		}
		src++;
	}
	*dst = '\0';

	/* Gemini Live API uses "tool_response" format */
	int err = re_sdprintf(json_msg,
		"{"
			"\"tool_response\": {"
				"\"functionResponses\": ["
					"{"
						"\"id\": \"%s\","
						"\"response\": {"
							"\"result\": \"%s\""
						"}"
					"}"
				"]"
			"}"
		"}",
		call_id, escaped_output);
	mem_deref(escaped_output);

	return err;
}

/* JSON parsing helpers */

static struct json_object *parse_json_safe(const char *json_str, const char *context)
{
	struct json_object *root = json_tokener_parse(json_str);
	if (!root) {
		warning("openai_rt: Failed to parse Gemini JSON in %s\n", context);
	}
	return root;
}

/* Silent versions for optional fields (no warnings) */
static struct json_object *get_json_object_field_optional(struct json_object *obj,
                                                          const char *field_name)
{
	struct json_object *field_obj = NULL;
	json_object_object_get_ex(obj, field_name, &field_obj);
	return field_obj;
}

static const char *get_json_string_field_optional(struct json_object *obj,
                                                   const char *field_name)
{
	struct json_object *field_obj = NULL;
	if (!json_object_object_get_ex(obj, field_name, &field_obj)) {
		return NULL;
	}
	if (!json_object_is_type(field_obj, json_type_string)) {
		return NULL;
	}
	return json_object_get_string(field_obj);
}

/* Warning versions for required fields */
static const char *get_json_string_field(struct json_object *obj, const char *field_name,
                                         const char *context)
{
	struct json_object *field_obj = NULL;
	if (!json_object_object_get_ex(obj, field_name, &field_obj)) {
		warning("openai_rt: Gemini JSON message missing required '%s' field in %s\n", field_name, context);
		return NULL;
	}
	if (!json_object_is_type(field_obj, json_type_string)) {
		warning("openai_rt: Gemini JSON '%s' field is not a string in %s\n", field_name, context);
		return NULL;
	}
	return json_object_get_string(field_obj);
}

static struct json_object *get_json_object_field(struct json_object *obj,
                                                 const char *field_name,
                                                 const char *context)
{
	struct json_object *field_obj = NULL;
	if (!json_object_object_get_ex(obj, field_name, &field_obj)) {
		warning("openai_rt: Gemini JSON message missing required '%s' field in %s\n", field_name, context);
		return NULL;
	}
	return field_obj;
}

/**
 * Parse Gemini message and invoke appropriate callbacks
 * 
 * According to SF_DOC.md, Gemini Live API messages can contain:
 * - setupComplete: Session initialized
 * - serverContent: Model-generated content (audio, text)
 * - toolCall: Function call requests
 * - usageMetadata: Token usage information
 * - goAway: Server will disconnect
 */
static int gemini_parse_message(const char *json_str,
                               void (*audio_delta_cb)(const char *base64_audio, void *arg),
                               void (*session_updated_cb)(void *arg),
                               void (*speech_started_cb)(void *arg),
                               void (*function_call_cb)(const char *call_id,
                                                       const char *name,
                                                       const char *arguments,
                                                       void *arg),
                               void (*response_done_cb)(const char *response_json, void *arg),
                               void *cb_arg)
{
	(void)speech_started_cb; /* Not currently used in Gemini implementation */
	
	struct json_object *root = parse_json_safe(json_str, "Gemini message parser");
	if (!root) {
		return EINVAL;
	}

	int result = 0;

	/* Check for setupComplete (session initialization) - optional field */
	struct json_object *setup_complete = get_json_object_field_optional(root, "setupComplete");
	if (setup_complete) {
		/* sessionId is optional in setupComplete */
		const char *session_id = get_json_string_field_optional(setup_complete, "sessionId");
		if (session_id) {
			info("openai_rt: Gemini setupComplete received, sessionId: %s\n", session_id);
		} else {
			info("openai_rt: Gemini setupComplete received\n");
		}
		if (session_updated_cb) {
			session_updated_cb(cb_arg);
		}
		goto cleanup; /* Don't process other fields if we got setupComplete */
	}
	
	/* Check for errors in the response - optional field */
	struct json_object *error_obj = get_json_object_field_optional(root, "error");
	if (error_obj) {
		const char *error_msg = get_json_string_field_optional(error_obj, "message");
		const char *error_code = get_json_string_field_optional(error_obj, "code");
		warning("openai_rt: Gemini API error: code=%s, message=%s\n", 
		        error_code ? error_code : "unknown",
		        error_msg ? error_msg : "unknown");
		result = EPROTO;
		goto cleanup;
	}

	/* Check for serverContent (model output) - optional field */
	struct json_object *server_content = get_json_object_field_optional(root, "serverContent");
	if (server_content) {
		/* Check for modelTurn with audio data */
		struct json_object *model_turn = get_json_object_field_optional(server_content, "modelTurn");
		if (model_turn) {
			struct json_object *parts = get_json_object_field_optional(model_turn, "parts");
			if (parts && json_object_is_type(parts, json_type_array)) {
				size_t parts_len = json_object_array_length(parts);
				for (size_t i = 0; i < parts_len; i++) {
					struct json_object *part = json_object_array_get_idx(parts, i);
					if (part) {
						/* Check for inlineData with audio - optional field (text parts don't have it) */
						struct json_object *inline_data = get_json_object_field_optional(part, "inlineData");
						if (inline_data) {
							const char *mime_type = get_json_string_field_optional(inline_data, "mimeType");
							const char *data = get_json_string_field_optional(inline_data, "data");
							
							if (data && mime_type && strstr(mime_type, "audio") != NULL && audio_delta_cb) {
								/* Gemini audio output is at 24kHz PCM16, base64 encoded */
								audio_delta_cb(data, cb_arg);
							}
						}
					}
				}
			}
		}

		/* Check for turn completion */
		struct json_object *turn_complete_obj = NULL;
		if (json_object_object_get_ex(server_content, "turnComplete", &turn_complete_obj)) {
			if (json_object_is_type(turn_complete_obj, json_type_boolean) &&
			    json_object_get_boolean(turn_complete_obj) &&
			    response_done_cb) {
				/* Send the full response as JSON when turn is complete */
				const char *response_json = json_object_to_json_string(root);
				if (response_json) {
					response_done_cb(response_json, cb_arg);
				}
			}
		}
		goto cleanup; /* Don't process other fields if we got serverContent */
	}

	/* Check for toolCall (function calls) - optional field */
	struct json_object *tool_call = get_json_object_field_optional(root, "toolCall");
	if (tool_call) {
		struct json_object *function_calls = get_json_object_field_optional(tool_call, "functionCalls");
		if (function_calls && json_object_is_type(function_calls, json_type_array)) {
			size_t calls_len = json_object_array_length(function_calls);
			for (size_t i = 0; i < calls_len; i++) {
				struct json_object *fc = json_object_array_get_idx(function_calls, i);
				if (fc && function_call_cb) {
					const char *call_id = get_json_string_field(fc, "id", "functionCall");
					const char *name = get_json_string_field(fc, "name", "functionCall");
					
					/* Get arguments as JSON string */
					struct json_object *args_obj = get_json_object_field(fc, "args", "functionCall");
					const char *arguments = NULL;
					if (args_obj) {
						arguments = json_object_to_json_string(args_obj);
					}

					if (name && call_id && arguments) {
						function_call_cb(call_id, name, arguments, cb_arg);
					}
				}
			}
		}
		goto cleanup; /* Don't process other fields if we got toolCall */
	}

	/* Check for goAway (server will disconnect) - optional field */
	struct json_object *go_away = get_json_object_field_optional(root, "goAway");
	if (go_away) {
		const char *reason = get_json_string_field_optional(go_away, "reason");
		warning("openai_rt: Gemini server sending goAway: %s\n", reason ? reason : "unknown");
		goto cleanup;
	}

cleanup:

	json_object_put(root);
	return result;
}

