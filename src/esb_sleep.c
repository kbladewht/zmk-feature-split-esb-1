// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zephyr/logging/log.h>

#include "esb_link.h"
LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

/* Stop radio before poweroff so SYSTEM_OFF can't cut an active transmit. */
static int esb_sleep_listener(const zmk_event_t *eh) {
    const struct zmk_activity_state_changed *ev = as_zmk_activity_state_changed(eh);
    if (ev != NULL && ev->state == ZMK_ACTIVITY_SLEEP) {
        LOG_INF("ESB: going to disable radio");
        (void)esb_link_set_enabled(false);
        LOG_INF("ESB: radio disabled called");
    }
    return 0;
}

ZMK_LISTENER(esb_sleep, esb_sleep_listener);
ZMK_SUBSCRIPTION(esb_sleep, zmk_activity_state_changed);
