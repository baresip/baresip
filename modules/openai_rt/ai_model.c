/**
 * @file ai_model.c  AI Model selection and management
 *
 * This file provides the shared model selection logic that chooses
 * between different AI model implementations (OpenAI, Gemini, etc.)
 * based on configuration.
 *
 * Copyright (C) 2025 Sipfront
 */

#include "openai_rt.h"
#include "ai_model.h"

/* Get the current AI model implementation based on configuration */
struct ai_model *get_ai_model(void)
{
	/* Select model based on configured backend type */
	switch (g_oairt.backend_type) {
	case AI_BACKEND_OPENAI_REALTIME:
		return &openai_model;
	case AI_BACKEND_GEMINI_LIVE:
		return &gemini_model;
	default:
		/* Default to OpenAI if unknown */
		warning("openai_rt: Unknown backend type %d, defaulting to OpenAI\n", 
		        g_oairt.backend_type);
		return &openai_model;
	}
}

/* Initialize AI model system */
int ai_model_init(struct openai_rt *ort)
{
	struct ai_model *model = get_ai_model();
	
	if (!model || !model->init) {
		warning("openai_rt: AI model '%s' not available\n", 
		        model ? model->name : "unknown");
		return EINVAL;
	}
	
	info("openai_rt: Initializing AI model: %s\n", model->name);
	return model->init(ort);
}

/* Close AI model system */
void ai_model_close(void)
{
	struct ai_model *model = get_ai_model();
	
	if (model && model->close) {
		info("openai_rt: Closing AI model: %s\n", model->name);
		model->close();
	}
}

