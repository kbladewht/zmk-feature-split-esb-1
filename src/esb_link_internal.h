// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link internals. */
#pragma once

#include <stdint.h>

#define ESB_LINK_PIPE_MAX 8 /* ESB hardware pipe count */

struct esb_link_packet {
    uint8_t pipe;
    uint8_t length;
    uint8_t data[CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD];
};

extern const uint8_t esb_link_pipe_count;

/* Starts receiving packets. */
int esb_link_role_start(void);

/* Radio event after RX is read; each set pipe bit gets a staged ACK reply. */
void esb_link_role_rx_done(uint8_t pipes_acked);

/* Held at most once. */
int esb_link_hfclk_acquire(void);
void esb_link_hfclk_release(void);
