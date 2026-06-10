// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Central half of the ESB radio layer: source ids and the ACK reverse channel.
 */
#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>

#include <esb.h>

#include "esb_link.h"
#include "esb_link_internal.h"

/* Reverse-channel replies staged by a thread, drained into the ACK FIFO by the
 * ISR on the next received packet. */
K_MSGQ_DEFINE(reply_queue, sizeof(struct esb_link_packet), CONFIG_ZMK_SPLIT_ESB_REPLY_QUEUE_SIZE, 4);

/* Assumes peripheral pipes are contiguous from 0. */
uint8_t esb_link_source_ids(uint8_t *out_ids) {
    __ASSERT_NO_MSG(out_ids != NULL);
    for (uint8_t pipe = 0; pipe < esb_link_pipe_count; pipe++) {
        out_ids[pipe] = pipe;
    }
    return esb_link_pipe_count;
}

int esb_link_stage_reply(uint8_t pipe, const uint8_t *data, size_t length) {
    if (length > CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD) {
        return -EMSGSIZE;
    }
    struct esb_link_packet packet = {0};
    packet.pipe = pipe;
    packet.length = (uint8_t)length;
    memcpy(packet.data, data, length);
    if (k_msgq_put(&reply_queue, &packet, K_NO_WAIT) != 0) {
        return -ENOBUFS;
    }
    return 0;
}

int esb_link_role_start(void) {
    return esb_start_rx();
}

/* Peek-then-remove keeps a reply queued if the ACK FIFO is full.
 * ISR-only, so esb_write_payload has a single caller context (no lock needed). */
void esb_link_role_rx_done(void) {
    struct esb_link_packet packet;
    while (k_msgq_peek(&reply_queue, &packet) == 0) {
        struct esb_payload payload = {0};
        payload.pipe = packet.pipe;
        payload.length = packet.length;
        memcpy(payload.data, packet.data, packet.length);
        if (esb_write_payload(&payload) != 0) {
            break; /* ACK FIFO full, retry on the next received packet */
        }
        (void)k_msgq_get(&reply_queue, &packet, K_NO_WAIT);
    }
}
