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
static const uint8_t peripheral_prefixes[] = {0xE7, 0xC2, 0xA7, 0xA2};
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

BUILD_ASSERT(PERIPHERAL_COUNT <= ARRAY_SIZE(peripheral_prefixes),
             "the nRF5 SDK 17.1 receiver address table supports up to four pipes");

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
        // uint8_t pipes_acked = 0;
        uint32_t drained = 0;
        nrf_esb_payload_t payload;
         LOG_INF("xxxxx111 received on pipe %d", 1);
        while (esb_link_nrf_status_to_errno(nrf_esb_read_rx_payload(&payload)) == 0) {
            drained++;
            LOG_INF("xxxxx received on pipe %d", payload.pipe);
        
            received = true;
        }
     
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
typedef void (*esb_handler_t)(nrf_esb_payload_t payload);
static void radio_rx_start(esb_handler_t handler)
{
    ARG_UNUSED(handler);
    // m_handler_n = handler;
    uint32_t err_code = nrf_esb_start_rx();
    APP_ERROR_CHECK(err_code);
}
static void esb_rx_data_cb(nrf_esb_payload_t payload)
{
    LOG_DBG("esb_rx_data_cb pipe=%u length=%u", payload.pipe, payload.length);

}

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

    nrf_esb_config_t nrf_esb_config = NRF_ESB_DEFAULT_CONFIG;
    nrf_esb_config.protocol = NRF_ESB_PROTOCOL_ESB_DPL;
    nrf_esb_config.mode = NRF_ESB_MODE_PRX;
    nrf_esb_config.event_handler = on_esb_event;
    nrf_esb_config.bitrate = NRF_ESB_BITRATE_1MBPS;
    nrf_esb_config.crc = NRF_ESB_CRC_16BIT;
    nrf_esb_config.tx_output_power = NRF_ESB_TX_POWER_0DBM;
    nrf_esb_config.payload_length = 0;
    nrf_esb_config.retransmit_delay = 250;
    nrf_esb_config.radio_irq_priority = CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY;
    nrf_esb_config.event_irq_priority = CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY;
    /* Reverse channel replies ride on ACKs requested by peripherals. */
    nrf_esb_config.selective_auto_ack = false;

uint8_t base_addr_0[4] = { 0xE7, 0xE7, 0xE7, 0xE7 };
    uint8_t base_addr_1[4] = { 0xC2, 0xC2, 0xC2, 0xC2 };
    uint8_t addr_prefix[3] = { 0xE7, 0xC2, 0xC3 };

    uint32_t err_code = nrf_esb_init(&nrf_esb_config);


      nrf_esb_set_rf_channel(26);
     nrf_esb_set_base_address_0(base_addr_0);
    nrf_esb_set_base_address_1(base_addr_1);

    
        nrf_esb_set_prefixes(addr_prefix, 4);
  
    LOG_INF("esb_link_radio_setup  22222 %d", err_code);

   radio_rx_start(esb_rx_data_cb);
    return 0;
}
