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
 * Build tools JSON for Gemini in Gemini's format
 * Gemini uses: tools: [{ function_declarations: [{ name, description, parameters }] }]
 * 
 * @param enabled_tools  Comma-separated list of enabled tool names (e.g., "hangup_call,send_dtmf")
 * @param tools_json     Output: JSON array string for tools (allocated, must be freed)
 * @return 0 if success, error code otherwise
 * 
 * Example: enabled_tools="hangup_call,send_dtmf" produces:
 *   [{"function_declarations":[
 *     {"name":"hangup_call","description":"Hang up the call","parameters":{...}},
 *     {"name":"send_dtmf","description":"Send DTMF tones to the call","parameters":{...}}
 *   ]}]
 */
static int gemini_build_tools_json(const char *enabled_tools, char **tools_json)
{
	char *result = NULL;
	char *temp = NULL;
	size_t i;
	int err;
	bool first = true;
	bool has_enabled = false;
	
	if (!tools_json) {
		return EINVAL;
	}
	
	/* If no enabled tools specified, return empty array */
	if (!enabled_tools || !*enabled_tools) {
		err = str_dup(tools_json, "[]");
		return err ? err : 0;
	}
	
	/* Start tools array with function_declarations wrapper */
	err = re_sdprintf(&result, "[{\"function_declarations\":[");
	if (err) {
		return err;
	}
	
	/* Iterate through available tools and include enabled ones */
	for (i = 0; i < AI_AVAILABLE_TOOLS_COUNT; i++) {
		const struct ai_tool_call *tool = AI_AVAILABLE_TOOLS[i];
		
		if (!tool || !tool->name) {
			continue;
		}
		
		if (ai_model_is_tool_enabled(tool->name, enabled_tools)) {
			has_enabled = true;
			
			/* Build function declaration JSON object */
			if (tool->parameters_json) {
				/* Tool with parameters */
				if (!first) {
					err = re_sdprintf(&temp, "%s,{"
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\","
					                       "\"parameters\":%s"
					                       "}",
					                       result,
					                       tool->name,
					                       tool->description ? tool->description : "",
					                       tool->parameters_json);
				} else {
					err = re_sdprintf(&temp, "%s{"
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\","
					                       "\"parameters\":%s"
					                       "}",
					                       result,
					                       tool->name,
					                       tool->description ? tool->description : "",
					                       tool->parameters_json);
				}
			} else {
				/* Tool without parameters */
				if (!first) {
					err = re_sdprintf(&temp, "%s,{"
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\","
					                       "\"parameters\":{\"type\":\"object\",\"properties\":{}}"
					                       "}",
					                       result,
					                       tool->name,
					                       tool->description ? tool->description : "");
				} else {
					err = re_sdprintf(&temp, "%s{"
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\","
					                       "\"parameters\":{\"type\":\"object\",\"properties\":{}}"
					                       "}",
					                       result,
					                       tool->name,
					                       tool->description ? tool->description : "");
				}
			}
			
			if (err) {
				mem_deref(result);
				return err;
			}
			
			mem_deref(result);
			result = temp;
			temp = NULL;
			
			first = false;
		}
	}
	
	/* If no tools were enabled, return empty array */
	if (!has_enabled) {
		mem_deref(result);
		err = str_dup(tools_json, "[]");
		return err ? err : 0;
	}
	
	/* Close function_declarations array and tools array */
	err = re_sdprintf(&temp, "%s]}]", result);
	if (err) {
		mem_deref(result);
		return err;
	}
	
	mem_deref(result);
	*tools_json = temp;
	
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
	err = json_escape(&escaped_prompt, prompt);
	if (err) {
		return err;
	}

	/* Build tools JSON array based on enabled tools */
	char *tools_json = NULL;
	err = gemini_build_tools_json(g_oairt.enabled_tools, &tools_json);
	if (err) {
		warning("openai_rt: Failed to build Gemini tools JSON: %m\n", err);
		mem_deref(escaped_prompt);
		return err;
	}
	
	/* Determine model based on API key type */
	/* All models now use gemini-2.5-flash-native-audio-preview-09-2025 */
	const char *model_name = GEMINI_MODEL;
	if (str_isset(g_oairt.api_key) && 
	    strncmp(g_oairt.api_key, "auth_tokens/", 12) == 0) {
		/* Use same model for ephemeral tokens (as per working SDK example) */
		model_name = GEMINI_MODEL_EPHEMERAL;
	}
	
	/* Get temperature (default to 0.7 if not set) */
	float temperature = g_oairt.temperature > 0.0f ? g_oairt.temperature : 0.7f;
	
	/* Get voice name (default to "Aoede" if not set) */
	const char *voice_name = str_isset(g_oairt.voice) ? g_oairt.voice : "Aoede";
	
	/* Build VAD config JSON if enabled */
	char *vad_json = NULL;
	if (g_oairt.gemini_vad_enabled) {
		const char *vad_sensitivity =
			str_isset(g_oairt.gemini_vad_start_sensitivity) ?
			g_oairt.gemini_vad_start_sensitivity :
			"START_SENSITIVITY_HIGH";

		re_sdprintf(&vad_json,
			"\"realtimeInputConfig\":{"
				"\"automaticActivityDetection\":{"
					"\"startOfSpeechSensitivity\":\"%s\","
					"\"prefixPaddingMs\":%d,"
					"\"silenceDurationMs\":%d"
				"}"
			"},",
			vad_sensitivity,
			g_oairt.gemini_vad_prefix_padding_ms,
			g_oairt.gemini_vad_silence_duration_ms);
	}

	/* Check if tools are enabled */
	/* Check if tools are enabled */
	bool has_tools = (tools_json && strcmp(tools_json, "[]") != 0);
	char *tools_block = NULL;
	if (has_tools) {
		re_sdprintf(&tools_block, "\"tools\":%s,", tools_json);
	}
	
	/* Build setup message - always include voice config and realtime input config for interruption detection */
	err = re_sdprintf(json_msg,
		"{"
			"\"setup\":{"
				"\"model\":\"models/%s\","
				"\"generationConfig\":{"
					"\"responseModalities\":[\"AUDIO\"],"
					"\"speechConfig\":{"
						"\"voiceConfig\":{"
							"\"prebuiltVoiceConfig\":{"
								"\"voiceName\":\"%s\""
							"}"
						"}"
					"},"
					"\"temperature\":%.2f"
				"},"
				"\"systemInstruction\":{"
					"\"parts\":["
						"{"
							"\"text\":\"%s\""
						"}"
					"],"
					"\"role\":\"user\""
				"},"
				"%s"
				"%s"
				"\"sessionResumption\":{}"
			"}"
		"}",
		model_name, voice_name, temperature, escaped_prompt, 
		vad_json ? vad_json : "",
		tools_block ? tools_block : "");

	mem_deref(vad_json);
	mem_deref(tools_block);
	mem_deref(escaped_prompt);
	mem_deref(tools_json);

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
	err = json_escape(&escaped_text, text_content);
	if (err) {
		return err;
	}

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
	char *escaped_output = NULL;
	int err;

	if (!call_id || !output || !json_msg) {
		return EINVAL;
	}

	/* Escape output text for JSON */
	err = json_escape(&escaped_output, output);
	if (err) {
		return err;
	}

	/* Gemini Live API uses "tool_response" format */
	err = re_sdprintf(json_msg,
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
		warning("openai_rt: Gemini API error: %s\n", json_object_to_json_string(error_obj));
		result = EPROTO;
		goto cleanup;
	}

	/* Check for serverContent (model output) - optional field */
	struct json_object *server_content = get_json_object_field_optional(root, "serverContent");
	if (server_content) {
		/* Check for interrupted flag - when True, clear audio buffer (similar to OpenAI speech_started) */
		struct json_object *interrupted_obj = get_json_object_field_optional(server_content, "interrupted");
		if (interrupted_obj && json_object_is_type(interrupted_obj, json_type_boolean)) {
			if (json_object_get_boolean(interrupted_obj)) {
				DEBUG_INFO("openai_rt: Gemini serverContent.interrupted=True, clearing audio buffer\n");
				audio_clear_injection_buffer();
			}
		}

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
