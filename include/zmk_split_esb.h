// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Read-only ESB link status, for display widgets and diagnostics.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

struct zmk_split_esb_status {
    uint8_t channel;
    uint8_t epoch;
    bool searching;
    int8_t rssi_dbm;
};

/* Lock-free snapshot of local link state.
 * rssi_dbm is received signal, never own transmit.
 * Central reports worst sampled peripheral link. */
void zmk_split_esb_get_status(struct zmk_split_esb_status *status);

/* Links tracked: served peripherals on central, 1 on peripheral. */
uint8_t zmk_split_esb_pipe_count(void);

/* Received signal for one link, 0 before first sample or out of range.
 * Central: indexed by ESB pipe.
 * Peripheral: index 0 only. */
int8_t zmk_split_esb_pipe_rssi_dbm(uint8_t pipe);
