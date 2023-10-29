/**
 * @file fvad.h VAD detector -- auxilliary functions
 *
 * Copyright (C) 2023 Lars Immisch
 */

bool find_vad_rx(const struct call *call, void *arg);
bool find_vad_tx(const struct call *call, void *arg);
