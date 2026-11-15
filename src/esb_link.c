// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link, shared radio setup and RX dispatch. */
#define DT_DRV_COMPAT zmk_split_esb

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/spsc_lockfree.h>
#include <zephyr/sys/util.h>

#include <nrf_esb.h>

#include "esb_link.h"
#include "esb_link_internal.h"
#include "esb_stats.h"
#include "hop.h"

#define RX_THREAD_STACK_SIZE 1536
#define RX_THREAD_PRIORITY   2

LOG_MODULE_DECLARE(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(zmk_split_esb),
             "a zmk,split-esb node (base-address/peripherals/hop-channels) is required");

#define ESB_PERIPHERALS DT_INST_CHILD(0, peripherals)

static const uint8_t base_address_0[] = {0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t base_address_1[] = {0xC2, 0xC2, 0xC2, 0xC2};
static const uint8_t peripheral_prefixes[] = {0xE7, 0xC2, 0xC3};
#define PERIPHERAL_PIPE(node) [DT_PROP(node, pipe)] = 1,
static const uint8_t peripheral_pipes[] = {
    DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS, PERIPHERAL_PIPE)
};
#define PERIPHERAL_COUNT ARRAY_SIZE(peripheral_pipes)


static const uint8_t address_length = DT_INST_PROP(0, address_length);
static const int8_t tx_power_dbm = DT_INST_PROP(0, tx_power_dbm);



#define PIPE_BIT(node) +(1u << DT_PROP(node, pipe))
#define PIPE_BIT_SUM (0u DT_FOREACH_CHILD_STATUS_OKAY(ESB_PERIPHERALS, PIPE_BIT))
BUILD_ASSERT(PIPE_BIT_SUM == (1u << DT_CHILD_NUM_STATUS_OKAY(ESB_PERIPHERALS)) - 1u,
             "peripheral pipes must be exactly 0..N-1, one per peripheral");
const uint8_t esb_link_pipe_count = (uint8_t)PERIPHERAL_COUNT;

/* RX path: the radio ISR writes each payload straight into a lock-free SPSC ring
 * (no irq_lock, zero-copy slot) and signals the dispatch thread, which hands packets
 * to the role layer in thread context.
 * Single producer (the ESB ISR), single consumer (rx_thread), as the SPSC contract
 * requires. */
SPSC_DEFINE(rx_spsc, struct esb_link_packet, CONFIG_ZMK_SPLIT_ESB_RX_QUEUE_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);
static K_THREAD_STACK_DEFINE(rx_thread_stack, RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static esb_link_rx_callback_t rx_callback;
static atomic_t link_ready;

/* Dedicated dispatch thread: drain the SPSC and hand each packet to the role layer.
 * Off the shared system workqueue so a high RX rate isn't gated by unrelated work. */
static FUNC_NORETURN void rx_thread_fn(void *unused_a, void *unused_b, void *unused_c) {
    ARG_UNUSED(unused_a);
    ARG_UNUSED(unused_b);
    ARG_UNUSED(unused_c);
    while (1) {
        k_sem_take(&rx_sem, K_FOREVER);
        struct esb_link_packet *packet;
        while ((packet = spsc_consume(&rx_spsc)) != NULL) {
            if (rx_callback != NULL) {
                rx_callback(packet->pipe, packet->data, packet->length);
            }
            spsc_release(&rx_spsc);
        }
    }
}

/* Runs in the ESB IRQ: only move bytes into queues, defer the ZMK handoff. */
static void on_esb_event(nrf_esb_evt_t const *event) {
    switch (event->evt_id) {
    case NRF_ESB_EVENT_RX_RECEIVED: {
        bool received = false;
        uint8_t pipes_acked = 0;
        uint32_t drained = 0;
        nrf_esb_payload_t payload;
        while (esb_link_nrf_status_to_errno(nrf_esb_read_rx_payload(&payload)) == 0) {
            drained++;
            LOG_INF("xxxxx received on pipe %d", payload.pipe);
            if (payload.noack == 0) {
                pipes_acked |= (uint8_t)BIT(payload.pipe);
                esb_stat_reply_asked(payload.pipe);
            }
            if (hop_consume_rx(payload.pipe, payload.data, payload.length, payload.rssi)) {
                continue;
            }
            struct esb_link_packet *slot = spsc_acquire(&rx_spsc);
            if (slot == NULL) {
                LOG_DBG("RX ring full, dropping packet");
                esb_stat_add(ESB_STAT_RX_RING_DROPPED, 1);
                continue; /* keep draining the radio FIFO */
            }
            slot->pipe = payload.pipe;
            slot->length = payload.length;
            memcpy(slot->data, payload.data, slot->length);
            spsc_produce(&rx_spsc);
            received = true;
        }
        if (received) {
            k_sem_give(&rx_sem);
        }
        esb_stat_peak(ESB_STAT_RX_PEAK, drained);
        if (drained >= CONFIG_ZMK_SPLIT_ESB_RX_FIFO_SIZE) {
            esb_stat_add(ESB_STAT_RX_FULL, 1);
        }
        esb_link_role_rx_done(pipes_acked);
        break;
    }
    case NRF_ESB_EVENT_TX_SUCCESS:
        /* PRX ACK payload transmission needs no TX completion handling here. */
        break;
    case NRF_ESB_EVENT_TX_FAILED: {
        /* Only a flush moves the TX FIFO past a failed head.
         * esb_pop_tx moves the write end instead. */
        LOG_WRN("TX retransmits exhausted, flushing TX FIFO");
        int flush_error = esb_link_nrf_status_to_errno(nrf_esb_flush_tx());
        if (flush_error) {
            LOG_DBG("esb_flush_tx after TX_FAILED returned %d", flush_error);
        }
        esb_stat_tx_cleared();
        break;
    }
    default:
        /* other ESB events carry nothing this layer acts on */
        break;
    }
}


static __noinline int esb_link_radio_setup(void) {
    LOG_INF("esb_link_radio_setup  33333");
    int set_error =
        esb_link_nrf_status_to_errno(nrf_esb_set_address_length(address_length));
    if (set_error) {
        LOG_INF("esb_set_address_length returned %d", set_error);
    }
    LOG_HEXDUMP_INF(base_address_0, sizeof(base_address_0), "addr0");
    set_error = esb_link_nrf_status_to_errno(nrf_esb_set_base_address_0(base_address_0));
    LOG_INF("esb_set_base_address_0 returned %d", set_error);
    LOG_HEXDUMP_INF(base_address_1, sizeof(base_address_1), "addr1");
    set_error = esb_link_nrf_status_to_errno(nrf_esb_set_base_address_1(base_address_1));
    LOG_INF("esb_set_base_address_1 returned %d", set_error);
    set_error = esb_link_nrf_status_to_errno(
        nrf_esb_set_prefixes(peripheral_prefixes, (uint8_t)PERIPHERAL_COUNT));
    if (set_error) {
        LOG_INF("esb_set_prefixes returned %d", set_error);
    }
    set_error = esb_link_nrf_status_to_errno(nrf_esb_set_rf_channel(hop_current_channel()));
    if (set_error) {
        LOG_DBG("esb_set_rf_channel returned %d", set_error);
    }
    set_error =
        esb_link_nrf_status_to_errno(nrf_esb_set_tx_power((nrf_esb_tx_power_t)tx_power_dbm));
    if (set_error) {
        LOG_DBG("esb_set_tx_power returned %d", set_error);
    }
    return esb_link_role_start();
}

#if defined(ZMK_SPLIT_ESB_TEST_MOCK)
static int connect_irq_handlers(void) {
    return 0;
}
#else
extern void RADIO_IRQHandler(void);
extern void ESB_EVT_IRQHandler(void);

static void radio_irq_wrapper(const void *arg) {
    ARG_UNUSED(arg);
    RADIO_IRQHandler();
}

static void esb_event_irq_wrapper(const void *arg) {
    ARG_UNUSED(arg);
    ESB_EVT_IRQHandler();
}

static int connect_irq_handlers(void) {
    int error = irq_connect_dynamic(RADIO_IRQn, CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY,
                                    radio_irq_wrapper, NULL, 0);
    if (error < 0) {
        return error;
    }

    error = irq_connect_dynamic(ESB_EVT_IRQ, CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY,
                                esb_event_irq_wrapper, NULL, 0);
    return error < 0 ? error : 0;
}
#endif

int esb_link_init(esb_link_rx_callback_t callback) {
    rx_callback = callback;
    k_thread_create(&rx_thread, rx_thread_stack, K_THREAD_STACK_SIZEOF(rx_thread_stack),
                    rx_thread_fn, NULL, NULL, NULL, RX_THREAD_PRIORITY, 0,
                    K_NO_WAIT);

    int error = esb_link_hfclk_acquire();
    if (error) {
        LOG_ERR("HFCLK start failed (%d)", error);
        return error;
    }

    nrf_esb_config_t config = NRF_ESB_DEFAULT_CONFIG;
    config.protocol = NRF_ESB_PROTOCOL_ESB_DPL;
    config.mode = NRF_ESB_MODE_PRX;
    config.event_handler = on_esb_event;
    config.bitrate = NRF_ESB_BITRATE_1MBPS;
    config.crc = NRF_ESB_CRC_16BIT;
    config.retransmit_count = 2;
    config.payload_length = 0;
    config.retransmit_delay = 250;
    config.radio_irq_priority = CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY;
    config.event_irq_priority = CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY;
    /* Reverse channel replies ride on ACKs requested by peripherals. */
    config.selective_auto_ack = false;

    error = connect_irq_handlers();
    if (error) {
        LOG_ERR("ESB IRQ connection failed (%d)", error);
        return error;
    }

    error = esb_link_nrf_status_to_errno(nrf_esb_init(&config));
    if (error) {
        LOG_ERR("nrf_esb_init failed (%d)", error);
        return error;
    }
    (void)nrf_esb_set_rf_channel(26);
    LOG_INF("ESB esb_link_radio_setup  11111");
    error = esb_link_radio_setup();
    LOG_INF("esb_link_radio_setup  22222");
    if (error) {
        return error;
    }
    hop_start();
    atomic_set(&link_ready, 1);
    LOG_INF("ESB link initialized, %d peripherals, %d pipes",
             PERIPHERAL_COUNT, esb_link_pipe_count);
    return 0;
}

int esb_link_set_enabled(bool enabled) {
    /* ZMK enables at our init priority and links first, esb_link_init starts the radio itself. */
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
        return start_error;
    }
    hop_start();
    return 0;
}
