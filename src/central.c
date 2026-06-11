// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * ZMK ESB split central. Source id = ESB pipe.
 */
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/split/transport/central.h>
#include <zmk/split/transport/types.h>

#include "esb_link.h"
#include "esb_wire.h"

LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(sizeof(struct zmk_split_transport_peripheral_event) <= CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD,
             "peripheral event does not fit in one ESB payload; raise ZMK_SPLIT_ESB_MAX_PAYLOAD");
BUILD_ASSERT(sizeof(struct zmk_split_transport_central_command) <= CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD,
             "central command does not fit in one ESB payload; raise ZMK_SPLIT_ESB_MAX_PAYLOAD");

static bool transport_enabled;

static int central_send_command(uint8_t source,
                                struct zmk_split_transport_central_command command) {
    return esb_link_stage_reply(source, (const uint8_t *)&command, sizeof(command));
}

static int central_get_available_source_ids(uint8_t *sources) {
    return esb_link_source_ids(sources);
}

static int central_set_enabled(bool enabled) {
    transport_enabled = enabled;
    return esb_link_set_enabled(enabled);
}

static struct zmk_split_transport_status central_get_status(void) {
    return (struct zmk_split_transport_status){
        .available = true,
        .enabled = transport_enabled,
        /* ESB is connectionless: no connection state to track, so always connected. */
        .connections = ZMK_SPLIT_TRANSPORT_CONNECTIONS_STATUS_ALL_CONNECTED,
    };
}

static int
central_set_status_callback(zmk_split_transport_central_status_changed_cb_t callback) {
    ARG_UNUSED(callback);
    return 0;
}

static const struct zmk_split_transport_central_api central_api = {
    .send_command = central_send_command,
    .get_available_source_ids = central_get_available_source_ids,
    .set_enabled = central_set_enabled,
    .get_status = central_get_status,
    .set_status_callback = central_set_status_callback,
};

ZMK_SPLIT_TRANSPORT_CENTRAL_REGISTER(esb_central, &central_api, CONFIG_ZMK_SPLIT_ESB_PRIORITY);

struct central_inbound_event {
    uint8_t source;
    struct zmk_split_transport_peripheral_event event;
};

/* ZMK behavior state has no locks, its timers fire on system workqueue:
 * behavior-bound events must run there too.
 * Input events bypass, input_report is safe from any context. */
K_MSGQ_DEFINE(central_event_msgq, sizeof(struct central_inbound_event),
              CONFIG_ZMK_SPLIT_ESB_EVENT_QUEUE_SIZE, 4);

static void central_event_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    struct central_inbound_event inbound;
    while (k_msgq_get(&central_event_msgq, &inbound, K_NO_WAIT) == 0) {
        zmk_split_transport_central_peripheral_event_handler(&esb_central, inbound.source,
                                                             inbound.event);
    }
}

static K_WORK_DEFINE(central_event_work, central_event_work_fn);

static void central_on_rx(uint8_t pipe, const uint8_t *data, size_t length) {
    /* One packet may carry several coalesced events.
     * Decode and replay each in order. */
    size_t offset = 0;
    while (offset < length) {
        struct zmk_split_transport_peripheral_event event;
        size_t consumed = esb_wire_decode_event(&data[offset], length - offset, &event);
        if (consumed == 0) {
            LOG_WRN("Dropping packet, undecodable event at offset %u", (unsigned int)offset);
            return;
        }
        offset += consumed;
        if (event.type == ZMK_SPLIT_TRANSPORT_PERIPHERAL_EVENT_TYPE_INPUT_EVENT) {
            zmk_split_transport_central_peripheral_event_handler(&esb_central, pipe, event);
            continue;
        }
        struct central_inbound_event inbound = {
            .source = pipe,
            .event = event,
        };
        if (k_msgq_put(&central_event_msgq, &inbound, K_NO_WAIT) < 0) {
            LOG_WRN("Dropping event, central event queue full");
            continue;
        }
        k_work_submit(&central_event_work);
    }
}

static int central_init(void) {
    return esb_link_init(central_on_rx);
}

SYS_INIT(central_init, APPLICATION, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
