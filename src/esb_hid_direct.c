// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

#include <string.h>

#include <zephyr/logging/log.h>

#include <zmk/endpoints.h>
#include <zmk/hid.h>

#include "esb_hid_direct.h"

LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(sizeof(struct zmk_hid_keyboard_report_body) == 8,
             "legacy HID pass-through requires an 8-byte keyboard report");

bool esb_hid_direct_handle(uint8_t pipe, const uint8_t *data, size_t length) {
    if (pipe != 0 || length != sizeof(struct zmk_hid_keyboard_report_body)) {
        return false;
    }

    switch (data[1]) {
    case 0x00: {
        struct zmk_hid_keyboard_report *report = zmk_hid_get_keyboard_report();
        memcpy(&report->body, data, sizeof(report->body));
        int err = zmk_endpoint_send_report(HID_USAGE_KEY);
        if (err) {
            LOG_ERR("Failed to forward legacy keyboard report: %d", err);
        }
        break;
    }
    case 0x02:
#if IS_ENABLED(CONFIG_ZMK_POINTING)
    {
        struct zmk_hid_mouse_report *report = zmk_hid_get_mouse_report();
        report->body.buttons = data[3] & ((1U << ZMK_HID_MOUSE_NUM_BUTTONS) - 1U);
        report->body.d_x = (int8_t)data[4];
        report->body.d_y = (int8_t)data[5];
        report->body.d_scroll_y = (int8_t)data[6];
        report->body.d_scroll_x = (int8_t)data[7];
        int err = zmk_endpoint_send_mouse_report();
        report->body.d_x = 0;
        report->body.d_y = 0;
        report->body.d_scroll_y = 0;
        report->body.d_scroll_x = 0;
        if (err) {
            LOG_ERR("Failed to forward legacy mouse report: %d", err);
        }
        break;
    }
#else
        LOG_WRN("Legacy mouse report received but ZMK pointing is disabled");
        break;
#endif
    case 0x03: {
        uint16_t usage = (uint16_t)data[6] | ((uint16_t)data[7] << 8);
        struct zmk_hid_consumer_report *report = zmk_hid_get_consumer_report();
        memset(&report->body, 0, sizeof(report->body));
#if IS_ENABLED(CONFIG_ZMK_HID_CONSUMER_REPORT_USAGES_FULL)
        report->body.keys[0] = usage;
#else
        if (usage > UINT8_MAX) {
            LOG_WRN("Legacy consumer usage 0x%04x is unsupported by the configured report", usage);
            break;
        }
        report->body.keys[0] = (uint8_t)usage;
#endif
        int err = zmk_endpoint_send_report(HID_USAGE_CONSUMER);
        if (err) {
            LOG_ERR("Failed to forward legacy consumer report: %d", err);
        }
        break;
    }
    default:
        LOG_WRN("Dropping unsupported legacy HID report type 0x%02x", data[1]);
        break;
    }

    return true;
}
