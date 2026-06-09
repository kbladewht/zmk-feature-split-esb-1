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

/* Snapshot of the local link state, lock-free.
 * rssi_dbm is what this device receives from its peer, never its own transmit. */
void zmk_split_esb_get_status(struct zmk_split_esb_status *status);
