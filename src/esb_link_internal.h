// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Role seam between the shared radio layer (esb_link.c) and the per-role halves
 * (esb_link_central.c, esb_link_peripheral.c).
 */
#pragma once

#include <stdint.h>

#include <esb.h>

#if defined(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
#define ESB_LINK_ROLE_MODE ESB_MODE_PRX
#else
#define ESB_LINK_ROLE_MODE ESB_MODE_PTX
#endif

struct esb_link_packet {
    uint8_t pipe;
    uint8_t length;
    uint8_t data[CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD];
};

extern const uint8_t esb_link_pipe_count;

/* Role tail of radio setup and enable.
 * Central: starts RX.
 * Peripheral: no-op. */
int esb_link_role_start(void);

/* Called in the radio ISR after the RX FIFO drains.
 * Central: drains staged replies into the ACK FIFO, each rides the next ACK out.
 * Peripheral: no-op. */
void esb_link_role_rx_done(void);

void esb_link_mark_tx_event(void);
uint32_t esb_link_tx_last_event_ms(void);
