/**
 * @file openai.c  OpenAI-specific implementation of AI model interface
 *
 * This file contains OpenAI-specific code for:
 * - WebSocket connection setup (headers, URLs)
 * - JSON message building (session setup, audio, responses, tools)
 * - JSON message parsing (event handling)
 *
 * Copyright (C) 2024 SIPFront
 */

#include "openai_rt.h"
#include "ai_model.h"
#include <json-c/json.h>
#include <libwebsockets.h>

/* OpenAI-specific configuration */
#define OPENAI_API_HOST "api.openai.com"
#define OPENAI_API_PORT 443
#define OPENAI_API_PATH "/v1/realtime?model=gpt-realtime"

/* Tool call definitions - centralized for consistency across implementations */
const struct ai_tool_call AI_TOOL_HANGUP_CALL = {
	.name = "hangup_call",
	.description = "Hang up the call",
	.parameters_json = NULL
};

const struct ai_tool_call AI_TOOL_SEND_DTMF = {
	.name = "send_dtmf",
	.description = "Send DTMF tones to the call",
	.parameters_json = 
		"{"
			"\"type\": \"object\","
			"\"properties\": {"
				"\"digits\": {"
					"\"type\": \"string\","
					"\"description\": \"String of DTMF digits to send (e.g., '123', '*9#', '1234*'). Valid characters: 0-9, *, #, A-D\","
					"\"pattern\": \"^[0-9*#A-D]+$\""
				"}"
			"},"
			"\"required\": [\"digits\"]"
		"}"
};

/* Array of all available tool calls */
const struct ai_tool_call *AI_AVAILABLE_TOOLS[] = {
	&AI_TOOL_HANGUP_CALL,
	&AI_TOOL_SEND_DTMF,
	NULL  /* Sentinel */
};

const size_t AI_AVAILABLE_TOOLS_COUNT = 2;

/* Forward declarations */
static int openai_init(struct openai_rt *ort);
static void openai_close(void);
static int openai_get_connection_info(char *address, size_t address_len,
                                      int *port, char *path, size_t path_len);
static int openai_add_auth_headers(void *in, size_t len);
static int openai_build_session_update(const char *prompt, char **json_msg);
static int openai_build_audio_append(const char *base64_audio, char **json_msg);
static int openai_build_response_create(const char *instructions, char **json_msg);
static int openai_build_function_call_output(const char *call_id, const char *output,
                                             char **json_msg);
static int openai_parse_message(const char *json_str,
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

/* OpenAI AI model implementation */
static struct ai_model openai_model = {
	.name = "openai",
	.init = openai_init,
	.close = openai_close,
	.get_connection_info = openai_get_connection_info,
	.add_auth_headers = openai_add_auth_headers,
	.build_session_update = openai_build_session_update,
	.build_audio_append = openai_build_audio_append,
	.build_response_create = openai_build_response_create,
	.build_function_call_output = openai_build_function_call_output,
	.parse_message = openai_parse_message,
};

/* Static instance pointer */
static struct ai_model *current_model = &openai_model;

/* Get current AI model implementation */
struct ai_model *get_ai_model(void)
{
	return current_model;
}

/* Initialize AI model system */
int ai_model_init(struct openai_rt *ort)
{
	if (!current_model || !current_model->init) {
		return EINVAL;
	}
	return current_model->init(ort);
}

/* Close AI model system */
void ai_model_close(void)
{
	if (current_model && current_model->close) {
		current_model->close();
	}
}

/* OpenAI-specific implementations */

static int openai_init(struct openai_rt *ort)
{
	(void)ort;
	/* OpenAI doesn't need special initialization beyond config */
	return 0;
}

static void openai_close(void)
{
	/* OpenAI doesn't need special cleanup */
}

static int openai_get_connection_info(char *address, size_t address_len,
                                     int *port, char *path, size_t path_len)
{
	if (!address || !port || !path) {
		return EINVAL;
	}
	
	if (address_len < strlen(OPENAI_API_HOST) + 1) {
		return EOVERFLOW;
	}
	str_ncpy(address, OPENAI_API_HOST, address_len);
	
	*port = OPENAI_API_PORT;
	
	if (path_len < strlen(OPENAI_API_PATH) + 1) {
		return EOVERFLOW;
	}
	str_ncpy(path, OPENAI_API_PATH, path_len);
	
	return 0;
}

static int openai_add_auth_headers(void *in, size_t len)
{
	unsigned char **p = (unsigned char **)in;
	unsigned char *end = (*p) + len;
	int written;

	if (!str_isset(g_oairt.api_key)) {
		warning("openai_rt: No API key available for headers\n");
		return -1;
	}

	written = lws_snprintf((char *)*p, end - *p,
			"Authorization: Bearer %s\r\n", g_oairt.api_key);
	if (written < 0 || written >= (int)(end - *p)) {
		warning("openai_rt: failed to add Authorization header\n");
		return -1;
	}
	*p += written;

	return 0;
}

/**
 * Check if a tool name is enabled in the comma-separated list
 */
bool ai_model_is_tool_enabled(const char *tool_name, const char *enabled_tools)
{
	const char *p;
	size_t tool_name_len;
	
	if (!tool_name || !enabled_tools || !*enabled_tools) {
		return false;
	}
	
	tool_name_len = strlen(tool_name);
	p = enabled_tools;
	
	/* Check if tool name appears in the list (as a whole word/entry) */
	while (*p) {
		/* Skip whitespace */
		while (*p == ' ' || *p == '\t') {
			p++;
		}
		
		if (*p == '\0') {
			break;
		}
		
		/* Check if this entry matches the tool name */
		if (strncmp(p, tool_name, tool_name_len) == 0) {
			/* Check if this is a complete match (followed by comma, space, or end) */
			char next_char = p[tool_name_len];
			if (next_char == '\0' || next_char == ',' || 
			    next_char == ' ' || next_char == '\t') {
				return true;
			}
		}
		
		/* Skip to next comma or end */
		while (*p && *p != ',') {
			p++;
		}
		if (*p == ',') {
			p++;
		}
	}
	
	return false;
}

/**
 * Build tools JSON array for session update
 */
int ai_model_build_tools_json(const char *enabled_tools, char **tools_json)
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
	
	/* Start tools array */
	err = re_sdprintf(&result, "[");
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
			
			/* Build tool JSON object */
			if (tool->parameters_json) {
				/* Tool with parameters */
				if (!first) {
					err = re_sdprintf(&temp, "%s,{"
					                       "\"type\":\"function\","
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
					                       "\"type\":\"function\","
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
					                       "\"type\":\"function\","
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\""
					                       "}",
					                       result,
					                       tool->name,
					                       tool->description ? tool->description : "");
				} else {
					err = re_sdprintf(&temp, "%s{"
					                       "\"type\":\"function\","
					                       "\"name\":\"%s\","
					                       "\"description\":\"%s\""
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
	
	/* Close tools array */
	err = re_sdprintf(&temp, "%s]", result);
	if (err) {
		mem_deref(result);
		return err;
	}
	
	mem_deref(result);
	*tools_json = temp;
	
	return 0;
}

static int openai_build_session_update(const char *prompt, char **json_msg)
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

	/* Build tools JSON array based on enabled tools */
	char *tools_json = NULL;
	err = ai_model_build_tools_json(g_oairt.enabled_tools, &tools_json);
	if (err) {
		warning("openai_rt: Failed to build tools JSON: %m\n", err);
		mem_deref(escaped_prompt);
		return err;
	}

	/* Build session update JSON with tools */
	static const char *session_update_template =
		"{"
			"\"type\": \"session.update\","
			"\"session\": {"
				"\"type\": \"realtime\","
				"\"instructions\": \"%s\","
				"\"tool_choice\": \"auto\","
				"\"tools\": %s"
			"}"
		"}";

	err = re_sdprintf(json_msg, session_update_template, escaped_prompt, tools_json);
	mem_deref(escaped_prompt);
	mem_deref(tools_json);

	if (err) {
		return err;
	}

	info("openai_rt: Session update built: %s\n", *json_msg);
	return 0;
}

static int openai_build_audio_append(const char *base64_audio, char **json_msg)
{
	if (!base64_audio || !json_msg) {
		return EINVAL;
	}

	int err = re_sdprintf(json_msg,
		"{"
			"\"type\":\"input_audio_buffer.append\","
			"\"audio\":\"%s\""
		"}",
		base64_audio);

	return err;
}

static int openai_build_response_create(const char *instructions, char **json_msg)
{
	char *escaped_instructions = NULL;
	int err;

	if (!json_msg) {
		return EINVAL;
	}

	if (instructions && *instructions) {
		/* Escape JSON special characters in the instructions */
		size_t instructions_len = strlen(instructions);
		escaped_instructions = mem_zalloc(instructions_len * 2 + 1, NULL);
		if (!escaped_instructions) {
			return ENOMEM;
		}

		/* Simple JSON escaping - escape quotes, backslashes, and control characters */
		const char *src = instructions;
		char *dst = escaped_instructions;
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

		err = re_sdprintf(json_msg,
			"{"
				"\"type\":\"response.create\","
				"\"response\":{"
					"\"instructions\":\"%s\""
				"}"
			"}",
			escaped_instructions);
		mem_deref(escaped_instructions);
	} else {
		err = re_sdprintf(json_msg,
			"{"
				"\"type\":\"response.create\""
			"}");
	}

	return err;
}

static int openai_build_function_call_output(const char *call_id, const char *output,
                                            char **json_msg)
{
	if (!call_id || !output || !json_msg) {
		return EINVAL;
	}

	int err = re_sdprintf(json_msg,
		"{"
			"\"type\": \"conversation.item.create\","
			"\"item\": {"
				"\"type\":\"function_call_output\","
				"\"call_id\": \"%s\","
				"\"output\": \"%s\""
			"}"
		"}",
		call_id, output);

	return err;
}

/* JSON parsing helpers */

static struct json_object *parse_json_safe(const char *json_str, const char *context)
{
	struct json_object *root = json_tokener_parse(json_str);
	if (!root) {
		warning("openai_rt: Failed to parse JSON in %s\n", context);
	}
	return root;
}

static const char *get_json_string_field(struct json_object *obj, const char *field_name,
                                         const char *context)
{
	struct json_object *field_obj = NULL;
	if (!json_object_object_get_ex(obj, field_name, &field_obj)) {
		warning("openai_rt: JSON message missing '%s' field in %s\n", field_name, context);
		return NULL;
	}
	if (!json_object_is_type(field_obj, json_type_string)) {
		warning("openai_rt: JSON '%s' field is not a string in %s\n", field_name, context);
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
		warning("openai_rt: JSON message missing '%s' field in %s\n", field_name, context);
		return NULL;
	}
	return field_obj;
}

/* Parse OpenAI message and invoke appropriate callbacks */
static int openai_parse_message(const char *json_str,
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
	struct json_object *root = parse_json_safe(json_str, "message parser");
	if (!root) {
		return EINVAL;
	}

	const char *type = get_json_string_field(root, "type", "message parser");
	if (!type) {
		json_object_put(root);
		return EINVAL;
	}

	int result = 0;

	/* Handle different OpenAI event types */
	if (strcmp(type, "response.output_audio.delta") == 0) {
		const char *delta = get_json_string_field(root, "delta", "audio_delta");
		if (delta && *delta && audio_delta_cb) {
			audio_delta_cb(delta, cb_arg);
		}
	} else if (strcmp(type, "session.updated") == 0) {
		if (session_updated_cb) {
			session_updated_cb(cb_arg);
		}
	} else if (strcmp(type, "input_audio_buffer.speech_started") == 0) {
		if (speech_started_cb) {
			speech_started_cb(cb_arg);
		}
	} else if (strcmp(type, "response.output_item.done") == 0) {
		struct json_object *item_obj = get_json_object_field(root, "item", "output_item.done");
		if (item_obj) {
			const char *item_type = get_json_string_field(item_obj, "type", "output_item.done");
			if (item_type && strcmp(item_type, "function_call") == 0) {
				const char *name = get_json_string_field(item_obj, "name", "function_call");
				const char *call_id = get_json_string_field(item_obj, "call_id", "function_call");
				const char *arguments = get_json_string_field(item_obj, "arguments", "function_call");

				if (name && call_id && arguments && function_call_cb) {
					function_call_cb(call_id, name, arguments, cb_arg);
				}
			}
		}
	} else if (strcmp(type, "response.done") == 0) {
		struct json_object *response_obj = get_json_object_field(root, "response", "response.done");
		if (response_obj && response_done_cb) {
			const char *response_json = json_object_to_json_string(response_obj);
			if (response_json) {
				response_done_cb(response_json, cb_arg);
			}
		}
	} else {
		DEBUG_INFO("openai_rt: Unhandled message type: %s\n", type);
	}

	json_object_put(root);
	return result;
}

