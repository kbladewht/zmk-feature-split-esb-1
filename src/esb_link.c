// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link setup with QMK-style RX diagnostics. */
#define DT_DRV_COMPAT zmk_split_esb

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <nrf_esb.h>

#include "esb_link.h"
#include "esb_link_internal.h"
#include "esb_stats.h"
#include "hop.h"

LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(zmk_split_esb),
             "a zmk,split-esb node (base-address/peripherals/hop-channels) is required");

#define ESB_PERIPHERALS DT_INST_CHILD(0, peripherals)

static const uint8_t peripheral_prefixes[] = {0xE7, 0xC2, 0xA7, 0xA2};
#define PERIPHERAL_PIPE(node) [DT_PROP(node, pipe)] = 1,
static const uint8_t peripheral_pipes[] = {DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS,
                                                                        PERIPHERAL_PIPE)};
#define PERIPHERAL_COUNT ARRAY_SIZE(peripheral_pipes)

static const uint8_t address_length = DT_INST_PROP(0, address_length);
static const int8_t tx_power_dbm = DT_INST_PROP(0, tx_power_dbm);

static nrf_esb_payload_t rx_payload;

static void nrf_esb_event_handler_n(nrf_esb_evt_t const *p_event)
{
    switch (p_event->evt_id) {
        case NRF_ESB_EVENT_TX_SUCCESS:
            break;
        case NRF_ESB_EVENT_TX_FAILED:
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
            if (nrf_esb_read_rx_payload(&rx_payload) == NRF_SUCCESS) {
                LOG_INF("Received payload on pipe %d, length %d", rx_payload.pipe, rx_payload.length);
            }
            break;
        default:
            break;
    }
}

#define PIPE_BIT(node) +(1u << DT_PROP(node, pipe))
#define PIPE_BIT_SUM (0u DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS, PIPE_BIT))
BUILD_ASSERT(PIPE_BIT_SUM == (1u << DT_CHILD_NUM_STATUS_OKAY(ESB_PERIPHERALS)) - 1u,
             "peripheral pipes must be exactly 0..N-1, one per peripheral");
const uint8_t esb_link_pipe_count = (uint8_t)PERIPHERAL_COUNT;

BUILD_ASSERT(PERIPHERAL_COUNT <= ARRAY_SIZE(peripheral_prefixes),
             "the nRF5 SDK 17.1 receiver address table supports up to four pipes");

static atomic_t link_ready;

static int radio_rx_start(void) {
    return esb_link_nrf_status_to_errno(nrf_esb_start_rx());
}

    uint8_t base_addr_0[4] = { 0xE7, 0xE7, 0xE7, 0xE7 };
    uint8_t base_addr_1[4] = {0xC2, 0xC2, 0xC2, 0xC2};
    uint8_t addr_prefix[4] = { 0xE7, 0xC2,0xA7, 0xA2 }; 
int esb_link_init(void) {
    int error = esb_link_hfclk_acquire();
    if (error) {
        LOG_ERR("HFCLK start failed (%d)", error);
        return error;
    }

    nrf_esb_config_t nrf_esb_config = NRF_ESB_DEFAULT_CONFIG;
    nrf_esb_config.protocol = NRF_ESB_PROTOCOL_ESB_DPL;
    nrf_esb_config.mode = NRF_ESB_MODE_PRX;
    nrf_esb_config.bitrate = NRF_ESB_BITRATE_1MBPS;
    nrf_esb_config.crc = NRF_ESB_CRC_16BIT;
    nrf_esb_config.event_handler = nrf_esb_event_handler_n;
    nrf_esb_config.tx_output_power = (nrf_esb_tx_power_t)tx_power_dbm;
    nrf_esb_config.payload_length = 0;
    nrf_esb_config.retransmit_delay = 250;
    nrf_esb_config.radio_irq_priority = CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY;
    nrf_esb_config.event_irq_priority = CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY;
    /* Reverse channel replies ride on ACKs requested by peripherals. */
    nrf_esb_config.selective_auto_ack = false;

    error = esb_link_nrf_status_to_errno(nrf_esb_init(&nrf_esb_config));
    if (error) {
        return error;
    }

    error = esb_link_nrf_status_to_errno(nrf_esb_set_address_length(address_length));
    if (error) {
        return error;
    }
    error = esb_link_nrf_status_to_errno(nrf_esb_set_rf_channel(26));
    if (error) {
        return error;
    }
    error = esb_link_nrf_status_to_errno(nrf_esb_set_base_address_0(base_addr_0));
    if (error) {
        return error;
    }
    error = esb_link_nrf_status_to_errno(nrf_esb_set_base_address_1(base_addr_1));
    if (error) {
        return error;
    }
    error = esb_link_nrf_status_to_errno(nrf_esb_set_prefixes(addr_prefix, 4));
    if (error) {
        return error;
    }

    int start_error = radio_rx_start();
    if (start_error) {
        return start_error;
    }
    hop_start();
    atomic_set(&link_ready, 1);
    return 0;
}

int esb_link_set_enabled(bool enabled) {
    if (atomic_get(&link_ready) == 0) {
        return 0;
    }

    if (!enabled) {
        hop_stop();
        int stop_error = esb_link_nrf_status_to_errno(nrf_esb_stop_rx());
        if (stop_error) {
            LOG_DBG("esb_stop_rx returned %d", stop_error);
        }
        int flush_error = esb_link_nrf_status_to_errno(nrf_esb_flush_tx());
        if (flush_error) {
            LOG_DBG("esb_flush_tx returned %d", flush_error);
        }
        esb_stat_tx_cleared();
        esb_link_hfclk_release();
        return 0;
    }

    int acquire_error = esb_link_hfclk_acquire();
    if (acquire_error) {
        LOG_ERR("HFCLK start failed (%d)", acquire_error);
        return acquire_error;
    }
    int start_error = esb_link_role_start();
    if (start_error) {
        esb_link_hfclk_release();
        return start_error;
    }
    hop_start();
    return 0;
}
