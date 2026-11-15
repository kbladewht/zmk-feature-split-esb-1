// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link setup with QMK-style RX diagnostics. */
#define DT_DRV_COMPAT zmk_split_esb

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
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
static atomic_t radio_irq_selftest_pending;
static atomic_t radio_hw_event_logged;
static atomic_t esb_irq_selftest_pending;
static atomic_t esb_irq_logged;

static void nrf_esb_event_handler_n(nrf_esb_evt_t const *p_event)
{
    switch (p_event->evt_id) {
        case NRF_ESB_EVENT_TX_SUCCESS:
            break;
        case NRF_ESB_EVENT_TX_FAILED:
            break;
        case NRF_ESB_EVENT_RX_RECEIVED:
            LOG_INF("RX received, reading payload");
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

extern void RADIO_IRQHandler(void);
extern void ESB_EVT_IRQHandler(void);

static void radio_irq_wrapper(const void *arg) {
    ARG_UNUSED(arg);
    bool radio_event = NRF_RADIO->EVENTS_READY || NRF_RADIO->EVENTS_END ||
                       NRF_RADIO->EVENTS_DISABLED;
    if (atomic_cas(&radio_irq_selftest_pending, 1, 0)) {
        LOG_INF("ESB RADIO IRQ self-test dispatched");
    } else if (radio_event && atomic_cas(&radio_hw_event_logged, 0, 1)) {
        LOG_INF("ESB RADIO hardware event reached");
    }
    RADIO_IRQHandler();
    ISR_DIRECT_PM();
}

static void esb_event_irq_wrapper(const void *arg) {
    ARG_UNUSED(arg);
    if (atomic_cas(&esb_irq_selftest_pending, 1, 0)) {
        LOG_INF("ESB event IRQ self-test dispatched");
    } else if (atomic_cas(&esb_irq_logged, 0, 1)) {
        LOG_INF("ESB event IRQ reached");
    }
    ESB_EVT_IRQHandler();
    ISR_DIRECT_PM();
}
extern void esb_radio_irq_handler(void);
extern void esb_evt_irq_handler(void);
static void radio_irq_handler(const void *arg)
{
    /* 关键：调 NCS ESB 内部 handler，它会清 EVENTS_* 并推进状态机 */
    esb_radio_irq_handler();
}

static void esb_event_irq_handler(const void *arg)
{
    esb_evt_irq_handler();
}

static int connect_irq_handlers(void) {
    irq_disable(RADIO_IRQn);
    irq_disable(ESB_EVT_IRQ);

    // ARM_IRQ_DIRECT_DYNAMIC_CONNECT(RADIO_IRQn, CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY, 0,
    //                                reschedule);
    // ARM_IRQ_DIRECT_DYNAMIC_CONNECT(ESB_EVT_IRQ, CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY, 0,
    //                                reschedule);

 /* 只保留 dynamic，删掉 ARM_IRQ_DIRECT_* */
    int error = irq_connect_dynamic(RADIO_IRQn,
                                     CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY,
                                     radio_irq_handler, NULL, 0);
    if (error < 0) {
        return error;
    }

    error = irq_connect_dynamic(ESB_EVT_IRQ, CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY,
                                esb_event_irq_handler, NULL, 0);
    if (error < 0) {
        return error;
    }

    irq_enable(RADIO_IRQn);
    irq_enable(ESB_EVT_IRQ);
    return 0;
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

    error = connect_irq_handlers();
    if (error) {
        LOG_ERR("ESB IRQ connection failed (%d)", error);
        return error;
    }

    error = esb_link_nrf_status_to_errno(nrf_esb_init(&nrf_esb_config));
    if (error) {
        return error;
    }

    // error = esb_link_nrf_status_to_errno(nrf_esb_set_address_length(address_length));
    // if (error) {
    //     return error;
    // }
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
    // atomic_set(&radio_irq_selftest_pending, 1);
    // atomic_set(&esb_irq_selftest_pending, 1);
    // NVIC_SetPendingIRQ(RADIO_IRQn);
    // NVIC_SetPendingIRQ(ESB_EVT_IRQ);
    LOG_INF("ESB PRX started on channel 26 (address length %u)", address_length);
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
