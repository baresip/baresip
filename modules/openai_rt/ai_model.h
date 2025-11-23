/**
 * @file ai_model.h  AI Model Interface
 *
 * This header defines an abstract interface for AI model integration.
 * Implementations (like OpenAI) provide model-specific code while
 * the rest of the module works through this interface.
 *
 * Copyright (C) 2025 Sipfront
 */

#ifndef AI_MODEL_H
#define AI_MODEL_H

#include <re.h>
#include <baresip.h>
#include <libwebsockets.h>

/* Forward declarations */
struct openai_rt;

/**
 * Tool Call Definition Structure
 *
 * Defines a tool call with its name, description, and parameters schema.
 * This ensures tool calls are named consistently across different AI model implementations.
 */
struct ai_tool_call {
	const char *name;           /* Tool call name (e.g., "hangup_call") */
	const char *description;    /* Tool call description */
	const char *parameters_json; /* JSON string defining parameters schema (NULL if no parameters) */
};

/**
 * Available Tool Calls
 *
 * Centralized definitions of all available tool calls.
 * New tool calls should be added here to ensure consistent naming.
 */
extern const struct ai_tool_call AI_TOOL_HANGUP_CALL;
extern const struct ai_tool_call AI_TOOL_SEND_DTMF;

/* Array of all available tool calls */
extern const struct ai_tool_call *AI_AVAILABLE_TOOLS[];
extern const size_t AI_AVAILABLE_TOOLS_COUNT;

/**
 * Build tools JSON array for session update
 *
 * @param enabled_tools  Comma-separated list of enabled tool names (e.g., "hangup_call,send_dtmf")
 * @param tools_json     Output: JSON array string for tools (allocated, must be freed)
 * @return 0 if success, error code otherwise
 */
int ai_model_build_tools_json(const char *enabled_tools, char **tools_json);

/**
 * Check if a tool name is enabled in the comma-separated list
 *
 * @param tool_name      Tool name to check (e.g., "hangup_call")
 * @param enabled_tools  Comma-separated list of enabled tool names (e.g., "hangup_call,send_dtmf")
 * @return true if tool is enabled, false otherwise
 */
bool ai_model_is_tool_enabled(const char *tool_name, const char *enabled_tools);

/**
 * AI Model Interface Structure
 *
 * This structure defines the functions that must be implemented
 * by any AI model provider (OpenAI, etc.)
 */
struct ai_model {
	/* Configuration */
	const char *name;
	
	/**
	 * Initialize the AI model
	 * @param ort  Module state
	 * @return 0 on success, error code otherwise
	 */
	int (*init)(struct openai_rt *ort);
	
	/**
	 * Clean up the AI model
	 */
	void (*close)(void);
	
	/**
	 * Get connection information for WebSocket
	 * @param address  Output: server address
	 * @param address_len  Buffer size for address
	 * @param port  Output: server port
	 * @param path  Output: connection path
	 * @param path_len  Buffer size for path
	 * @return 0 on success, error code otherwise
	 */
	int (*get_connection_info)(char *address, size_t address_len,
	                           int *port, char *path, size_t path_len);
	
	/**
	 * Add authentication headers to WebSocket handshake
	 * @param in  Buffer pointer (will be advanced)
	 * @param len  Remaining buffer length
	 * @return 0 on success, -1 on error
	 */
	int (*add_auth_headers)(void *in, size_t len);
	
	/**
	 * Build session setup message
	 * @param prompt  System prompt/instructions
	 * @param json_msg  Output: JSON message string (allocated, must be freed)
	 * @return 0 on success, error code otherwise
	 */
	int (*build_session_update)(const char *prompt, char **json_msg);
	
	/**
	 * Build audio append message
	 * @param base64_audio  Base64-encoded audio data
	 * @param json_msg  Output: JSON message string (allocated, must be freed)
	 * @return 0 on success, error code otherwise
	 */
	int (*build_audio_append)(const char *base64_audio, char **json_msg);
	
	/**
	 * Build response create message
	 * @param instructions  Optional instructions for this response
	 * @param json_msg  Output: JSON message string (allocated, must be freed)
	 * @return 0 on success, error code otherwise
	 */
	int (*build_response_create)(const char *instructions, char **json_msg);
	
	/**
	 * Build function call output message
	 * @param call_id  Function call ID
	 * @param output  Function call output text
	 * @param json_msg  Output: JSON message string (allocated, must be freed)
	 * @return 0 on success, error code otherwise
	 */
	int (*build_function_call_output)(const char *call_id, const char *output,
	                                  char **json_msg);
	
	/**
	 * Parse incoming message from AI model
	 * @param json_str  JSON message string
	 * @param audio_delta_cb  Callback for audio delta events
	 * @param session_updated_cb  Callback for session updated events
	 * @param speech_started_cb  Callback for speech started events
	 * @param function_call_cb  Callback for function call events
	 * @param response_done_cb  Callback for response done events
	 * @param cb_arg  Callback argument
	 * @return 0 on success, error code otherwise
	 */
	int (*parse_message)(const char *json_str,
	                     void (*audio_delta_cb)(const char *base64_audio, void *arg),
	                     void (*session_updated_cb)(void *arg),
	                     void (*speech_started_cb)(void *arg),
	                     void (*function_call_cb)(const char *call_id,
	                                             const char *name,
	                                             const char *arguments,
	                                             void *arg),
	                     void (*response_done_cb)(const char *response_json, void *arg),
	                     void *cb_arg);
};

/* AI model implementations - exported from their respective files */
extern struct ai_model openai_model;
extern struct ai_model gemini_model;

/* Get the current AI model implementation based on configuration */
struct ai_model *get_ai_model(void);

/* Initialize AI model system */
int ai_model_init(struct openai_rt *ort);

/* Close AI model system */
void ai_model_close(void);

#endif /* AI_MODEL_H */

