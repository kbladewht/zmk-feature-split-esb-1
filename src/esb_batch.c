// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Coalesce one report's per-axis events into a single packet: ZMK forwards each
 * axis as its own event (REL_X sync=0, REL_Y sync=1), doubling on-air packets at high rate.
 * Buffer input events and flush on the sync event (the report boundary, us later, so no
 * real latency added).
 * Non-input events flush the batch and go alone.
 * Single context (the input thread), so no lock.
 */
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

#include "esb_batch.h"
#include "esb_link.h"
#include "esb_wire.h"

BUILD_ASSERT(sizeof(struct zmk_split_transport_peripheral_event) <= CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD,
             "peripheral event does not fit in one ESB payload; raise ZMK_SPLIT_ESB_MAX_PAYLOAD");
BUILD_ASSERT(ESB_BATCH_MAX >= 2,
             "ZMK_SPLIT_ESB_MAX_PAYLOAD too small to coalesce a 2-axis sample; raise it");

int esb_batch_flush(struct esb_batch *batch) {
    __ASSERT_NO_MSG(batch != NULL);
    if (batch->count == 0) {
        return 0;
    }
    uint8_t wire[CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD];
    size_t length = 0;
    for (size_t index = 0; index < batch->count; index++) {
        size_t written =
            esb_wire_encode_event(&wire[length], sizeof(wire) - length, &batch->events[index]);
        if (written == 0) {
            break; /* next event would overflow the packet, send what fits */
        }
        length += written;
    }
    int error = esb_link_send(wire, length, batch->wants_ack);
    batch->count = 0;
    batch->wants_ack = false;
    return error;
}

int esb_batch_report_event(struct esb_batch *batch,
                           const struct zmk_split_transport_peripheral_event *event,
                           bool wants_ack) {
    __ASSERT_NO_MSG(batch != NULL);
    __ASSERT_NO_MSG(event != NULL);
    if (event->type != ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT) {
        int flush_error = esb_batch_flush(batch);
        if (flush_error < 0) {
            return flush_error;
        }
        uint8_t wire[ESB_WIRE_MAX_EVENT_SIZE];
        size_t length = esb_wire_encode_event(wire, sizeof(wire), event);
        return esb_link_send(wire, length, wants_ack);
    }
    batch->events[batch->count] = *event;
    batch->count++;
    if (wants_ack) {
        batch->wants_ack = true;
    }
    if (event->data.input_event.sync || batch->count >= ESB_BATCH_MAX) {
        return esb_batch_flush(batch);
    }
    return 0;
}
