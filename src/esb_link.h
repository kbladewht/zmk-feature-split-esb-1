// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link for the central role. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Initialize the central radio in PRX mode; received payloads are logged for diagnostics. */
int esb_link_init(void);

int esb_link_set_enabled(bool enabled);

/* True when the pipe's DT role is "relay" (downlink sink, no upstream events). */
bool esb_link_pipe_is_relay(uint8_t pipe);

bool esb_link_pipe_is_self(uint8_t pipe);

/* Fill out_ids with the peripheral source ids (= pipe numbers), self and relay pipes left out.
 * Returns the count. */
uint8_t esb_link_source_ids(uint8_t *out_ids);

/* Queue one packet to ride peripheral `pipe`'s next ACK back to it.
 * Returns -EMSGSIZE on empty or oversize payload, -ENOBUFS if the reply queue is full. */
int esb_link_stage_reply(uint8_t pipe, const uint8_t *data, size_t length);

/* Latest value for a poll with nothing queued, newest call wins.
 * Dropped once a reply is staged after it, so it never undoes a queued change. */
int esb_link_latch_idle_reply(uint8_t pipe, const uint8_t *data, size_t length);

uint8_t esb_central_battery_level(uint8_t pipe);

#define ESB_LINK_CONTROL_MAX_LENGTH 16

enum esb_link_control {
    ESB_LINK_CONTROL_BEACON,
    ESB_LINK_CONTROL_MASK,
    ESB_LINK_CONTROL_COUNT,
};

int esb_link_latch_control(uint8_t pipe, enum esb_link_control kind, const uint8_t *data,
                           size_t length);
