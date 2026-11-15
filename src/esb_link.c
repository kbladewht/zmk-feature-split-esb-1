// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link setup with QMK-style RX diagnostics. */
#define DT_DRV_COMPAT zmk_split_esb

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <esb.h>

#include "esb_link.h"
#include "esb_link_internal.h"
#include "esb_stats.h"

LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(zmk_split_esb),
             "a zmk,split-esb node (base-address/peripherals/hop-channels) is required");

#define ESB_PERIPHERALS DT_INST_CHILD(0, peripherals)

static const uint8_t peripheral_prefixes[] = {0xE7, 0xC2, 0xA7, 0xA2};
#define PERIPHERAL_PIPE(node) [DT_PROP(node, pipe)] = 1,
static const uint8_t peripheral_pipes[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS, PERIPHERAL_PIPE)};
#define PERIPHERAL_COUNT ARRAY_SIZE(peripheral_pipes)

static const uint8_t address_length = DT_INST_PROP(0, address_length);
static const int8_t tx_power_dbm = DT_INST_PROP(0, tx_power_dbm);

static const uint8_t base_addr_0[4] = {0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};

#define PIPE_BIT(node) +(1u << DT_PROP(node, pipe))
#define PIPE_BIT_SUM (0u DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS, PIPE_BIT))
BUILD_ASSERT(PIPE_BIT_SUM == (1u << DT_CHILD_NUM_STATUS_OKAY(ESB_PERIPHERALS)) - 1u,
             "peripheral pipes must be exactly 0..N-1, one per peripheral");
BUILD_ASSERT(PERIPHERAL_COUNT <= ARRAY_SIZE(peripheral_prefixes),
             "the receiver address table supports up to four pipes");
BUILD_ASSERT(CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD <= CONFIG_ESB_MAX_PAYLOAD_LENGTH,
             "ESB_MAX_PAYLOAD_LENGTH must hold the configured split payload");

const uint8_t esb_link_pipe_count = (uint8_t)PERIPHERAL_COUNT;

static atomic_t link_ready;

static void esb_link_event_handler(const struct esb_evt *event)
{
    switch (event->evt_id) {
    case ESB_EVENT_TX_SUCCESS:
    case ESB_EVENT_TX_FAILED:
        break;
    case ESB_EVENT_RX_RECEIVED: {
        struct esb_payload payload = {0};
        LOG_INF("RX event received");
        int error = esb_read_rx_payload(&payload);
        if (error != 0) {
            LOG_ERR("ESB RX payload read failed (%d)", error);
            break;
        }

        LOG_INF("RX received on pipe %u, length %u", payload.pipe, payload.length);
        esb_link_role_rx_done(BIT(payload.pipe));
        break;
    }
    default:
        break;
    }
}

static int configure_radio(void)
{
    int error = esb_set_address_length(5);
    if (error == 0) {
        error = esb_set_rf_channel(26);
    }
    if (error == 0) {
        error = esb_set_base_address_0(base_addr_0);
    }
    if (error == 0) {
        error = esb_set_base_address_1(base_addr_1);
    }
    if (error == 0) {
        error = esb_set_prefixes(peripheral_prefixes, 4);
    }
    return error;
}

int esb_link_init(void)
{
    int error = esb_link_hfclk_acquire();
    if (error != 0) {
        LOG_ERR("HFCLK start failed (%d)", error);
        return error;
    }

    const struct esb_config config = {
        .protocol = ESB_PROTOCOL_ESB_DPL,
        .mode = ESB_MODE_PRX,
        .event_handler = esb_link_event_handler,
        .bitrate = ESB_BITRATE_1MBPS,
        .crc = ESB_CRC_16BIT,
        .tx_output_power = tx_power_dbm,
        .retransmit_delay = 250,
        .retransmit_count = 3,
        .tx_mode = ESB_TXMODE_AUTO,
        .payload_length = 0,
        /* Reverse-channel replies are carried in ACK payloads. */
        .selective_auto_ack = false,
        .use_fast_ramp_up = false,
    };

    error = esb_init(&config);
    if (error != 0) {
        LOG_ERR("ESB initialization failed (%d)", error);
        esb_link_hfclk_release();
        return error;
    }

    error = configure_radio();
    if (error != 0) {
        LOG_ERR("ESB radio configuration failed (%d)", error);
        esb_disable();
        esb_link_hfclk_release();
        return error;
    }

    error = esb_start_rx();
    if (error != 0) {
        LOG_ERR("ESB PRX start failed (%d)", error);
        esb_disable();
        esb_link_hfclk_release();
        return error;
    }

    atomic_set(&link_ready, 1);
    LOG_INF("ESB PRX started on channel 26 (address length %u)", address_length);
    return 0;
}

int esb_link_set_enabled(bool enabled)
{
    if (atomic_get(&link_ready) == 0) {
        return 0;
    }

    if (!enabled) {
        int error = esb_stop_rx();
        if (error != 0) {
            LOG_ERR("esb_stop_rx failed (%d)", error);
            return error;
        }
        error = esb_flush_tx();
        if (error != 0) {
            LOG_ERR("esb_flush_tx failed (%d)", error);
            return error;
        }
        esb_stat_tx_cleared();
        esb_link_hfclk_release();
        return 0;
    }

    int error = esb_link_hfclk_acquire();
    if (error != 0) {
        LOG_ERR("HFCLK start failed (%d)", error);
        return error;
    }
    error = esb_link_role_start();
    if (error != 0) {
        esb_link_hfclk_release();
        return error;
    }

    return 0;
}
