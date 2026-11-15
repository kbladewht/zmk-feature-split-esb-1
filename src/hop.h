// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Central channel-hopping engine.
 * Hop logic is a no-op when hop-channels lists a single channel.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Schedule the hop-decision window. Safe before esb_link_init runs. */
void hop_start(void);

void hop_stop(void);

/* Called per received payload in the radio ISR. Returns true for control packets. */
bool hop_consume_rx(uint8_t pipe, const uint8_t *data, uint8_t length, int8_t rssi);

/* Channel the radio should currently tune to. */
uint8_t hop_current_channel(void);

/* Tunes the radio to the current hop channel, a no-op when already there.
 * Busy radio refuses the retune.
 * Next call retries it. */
void apply_hop_channel(void);

int hop_stage_beacon(uint8_t pipe, uint8_t hid_modifiers, uint8_t hid_indicators);

bool hop_pipe_needs_rendezvous(uint8_t pipe);

uint32_t hop_pipe_quiet_ms(uint8_t pipe);
bool hop_pipe_heard(uint8_t pipe);

/* Non-ESB path (wire peer) stamps liveness without touching channel-quality state. */
void hop_pipe_note_seen(uint8_t pipe);

/* Boot mask from persisted spectrum and an energy sweep.
 * Radio must be idle: run before esb_link_init, HFCLK running. */
void hop_boot_mask(void);
