// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/*
 * Single-device ESB radio layer.
 * One packet in, one out.
 * Reliability is ESB hardware ACK + retransmit, so no framing, reassembly,
 * software CRC or retry table here.
 * The reverse channel rides ACK payloads: the central stages a reply, it goes
 * out on the next received packet's ACK.
 * Channel hopping lives in hop.c.
 */
#define DT_DRV_COMPAT zmk_split_esb

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/spsc_lockfree.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>

#include <esb.h>

#include "esb_link.h"
#include "esb_link_internal.h"
#include "hop.h"

LOG_MODULE_REGISTER(zmk_split_esb, CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL);

BUILD_ASSERT(DT_HAS_COMPAT_STATUS_OKAY(zmk_split_esb),
             "a zmk,split-esb node is required");

static const uint8_t base_address_0[] = {0xE7, 0xE7, 0xE7, 0xE7};
static const uint8_t base_address_1[] = {0xC2, 0xC2, 0xC2, 0xC2};
static const uint8_t address_length = 5;
BUILD_ASSERT(sizeof(base_address_0) == 4, "base-address-0 must be exactly 4 bytes");
BUILD_ASSERT(sizeof(base_address_1) == 4, "base-address-1 must be exactly 4 bytes");

#define ESB_PERIPHERALS DT_INST_CHILD(0, peripherals)
#define ADDR_PREFIX_ESB_QF {0xE7, 0xC2}
static const uint8_t peripheral_prefixes[] = ADDR_PREFIX_ESB_QF;
#define PERIPHERAL_COUNT 2
BUILD_ASSERT(PERIPHERAL_COUNT == ARRAY_SIZE(peripheral_prefixes),
             "PERIPHERAL_COUNT must match the hard-coded prefixes");
BUILD_ASSERT(DT_CHILD_NUM_STATUS_OKAY(ESB_PERIPHERALS) == PERIPHERAL_COUNT,
             "hard-coded ESB configuration requires exactly two peripherals");
const uint8_t esb_link_pipe_count = (uint8_t)PERIPHERAL_COUNT;

/* NCS's CONFIG_ESB_MAX_PAYLOAD_LENGTH default (32) wins over ours on Kconfig parse order.
 * Raise it in your .conf or the radio rejects oversized payloads. */
BUILD_ASSERT(CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD <= CONFIG_ESB_MAX_PAYLOAD_LENGTH,
             "set CONFIG_ESB_MAX_PAYLOAD_LENGTH >= CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD in your .conf");

/* RX path: the radio ISR writes each payload straight into a lock-free SPSC ring
 * (no irq_lock, zero-copy slot) and signals the dispatch thread, which hands packets
 * to the role layer in thread context.
 * Single producer (the ESB ISR), single consumer (rx_thread), as the SPSC contract
 * requires. */
SPSC_DEFINE(rx_spsc, struct esb_link_packet, CONFIG_ZMK_SPLIT_ESB_RX_QUEUE_SIZE);
static K_SEM_DEFINE(rx_sem, 0, 1);
static K_THREAD_STACK_DEFINE(rx_thread_stack, CONFIG_ZMK_SPLIT_ESB_RX_THREAD_STACK_SIZE);
static struct k_thread rx_thread;

static esb_link_rx_callback_t rx_callback;

static uint32_t tx_last_event_ms;

void esb_link_mark_tx_event(void) {
    tx_last_event_ms = k_uptime_get_32();
}

uint32_t esb_link_tx_last_event_ms(void) {
    return tx_last_event_ms;
}

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
            LOG_INF("88888 RX pipe=%u len=%u", packet->pipe, packet->length);
            if (rx_callback != NULL) {
                rx_callback(packet->pipe, packet->data, packet->length);
            }
            spsc_release(&rx_spsc);
        }
    }
}
uint32_t cnt_rx_packets = 0;
/* Runs in the ESB IRQ: only move bytes into queues, defer the ZMK handoff. */
static void on_esb_event(const struct esb_evt *event) {
    switch (event->evt_id) {
    case ESB_EVENT_RX_RECEIVED: {
        bool received = false;
        struct esb_payload payload;
        LOG_INF("333333333");
        //目前这里是rx专用的,因为开启了start rx主机这里,这个事件才触发
        while (esb_read_rx_payload(&payload) == 0) {
            LOG_DBG("xxxxxxxxx");
            if (hop_consume_rx(payload.pipe, payload.data, payload.length, payload.rssi)) {
                continue; /* control packet (keepalive or beacon), not queued */
            }
            //Zephyr 提供的 SPSC (Single Producer Single Consumer) Lock-Free Queue API
            struct esb_link_packet *slot = spsc_acquire(&rx_spsc);
            if (slot == NULL) {
                LOG_DBG("RX ring full, dropping packet");
                continue; /* keep draining the radio FIFO */
            }
            slot->pipe = payload.pipe;
            slot->length = (uint8_t)MIN(payload.length, (int)sizeof(slot->data));
            memcpy(slot->data, payload.data, slot->length);
            spsc_produce(&rx_spsc);
            received = true;
            if(cnt_rx_packets++ % 100 == 0){
             LOG_INF("RX pipe=%u len=%u rssi=%d", 
                payload.pipe, payload.length, payload.rssi);
             LOG_HEXDUMP_INF(payload.data, payload.length, "RX data:");
            }

        }
        if (received) {
            k_sem_give(&rx_sem);
        }
        esb_link_role_rx_done();
        break;
    }
    case ESB_EVENT_TX_SUCCESS:
        // 此时可以读取对端在ACK中返回的数据
        #ifndef CONFIG_ZMK_SPLIT_ROLE_CENTRAL  // ✅ 正确，不加括号
            struct esb_payload ack_rx;
            LOG_INF("TX111 ACK received: "  );
            while (esb_read_rx_payload(&ack_rx) == 0) {
                // ack_rx.data 包含了对端在ACK中发回的数据
                // 这就是"反向通道"收到的数据
                // LOG_INF("TX222 ACK received: pipe=%u length=%u", 
                //     ack_rx.pipe, ack_rx.length);
            
                // 打印原始数据（十六进制）
                // LOG_HEXDUMP_INF(ack_rx.data, 8, "ACK data:");
                process_ack_payload(ack_rx.data, ack_rx.length);
            }
          #endif
        esb_link_mark_tx_event();
        hop_note_tx_success((uint8_t)event->tx_attempts);
        break;
    case ESB_EVENT_TX_FAILED: {
        esb_link_mark_tx_event();
        /* Retransmits exhausted: drop the packet, flush so the TX FIFO can advance. */
        LOG_WRN("TX retransmits exhausted, flushing TX FIFO");
        int flush_error = esb_flush_tx();
        if (flush_error) {
            LOG_DBG("esb_flush_tx after TX_FAILED returned %d", flush_error);
        }
        hop_note_tx_failed();
        break;
    }
    default:
        /* other ESB events carry nothing this layer acts on */
        break;
    }
}

static int hfclk_request(void) {
    struct onoff_manager *manager = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    struct onoff_client client;
    sys_notify_init_spinwait(&client.notify);
    int error = onoff_request(manager, &client);
    if (error < 0) {
        return error;
    }
    int result;
    while (sys_notify_fetch_result(&client.notify, &result) == -EAGAIN) {
    }
    return result;
}

/* Push base addresses, prefix, channel, TX power into the radio.
 * set_* failures are logged and ignored.
 * The radio starts on whatever values it already held. */
static int esb_link_radio_setup(void) {
    LOG_INF("base_address_0: %02X %02X %02X %02X",
            base_address_0[0], base_address_0[1], base_address_0[2], base_address_0[3]);
    LOG_INF("base_address_1: %02X %02X %02X %02X",
            base_address_1[0], base_address_1[1], base_address_1[2], base_address_1[3]);
    int set_error = esb_set_address_length(5);
    if (set_error) {
        LOG_DBG("esb_set_address_length returned %d", set_error);
    }
    set_error = esb_set_base_address_0(base_address_0);
    if (set_error) {
        LOG_INF("esb_set_base_address_0 returned %d", set_error);
    }
    set_error = esb_set_base_address_1(base_address_1);
    if (set_error) {
        LOG_DBG("esb_set_base_address_1 returned %d", set_error);
    }
    set_error = esb_set_prefixes(peripheral_prefixes, 2);
    if (set_error) {
        LOG_DBG("esb_set_prefixes returned %d", set_error);
    }
    set_error = esb_set_rf_channel(26);
    if (set_error) {
        LOG_DBG("esb_set_rf_channel returned %d", set_error);
    }
    set_error = esb_set_tx_power(0);
    if (set_error) {
        LOG_DBG("esb_set_tx_power returned %d", set_error);
    }
    return esb_link_role_start();
}


int esb_link_init(esb_link_rx_callback_t callback) {
    rx_callback = callback;
    k_thread_create(&rx_thread, rx_thread_stack, K_THREAD_STACK_SIZEOF(rx_thread_stack),
                    rx_thread_fn, NULL, NULL, NULL, CONFIG_ZMK_SPLIT_ESB_RX_THREAD_PRIORITY, 0,
                    K_NO_WAIT);

    int error = hfclk_request();
    if (error) {
        LOG_ERR("HFCLK start failed (%d)", error);
        return error;
    }

    struct esb_config config = ESB_DEFAULT_CONFIG;
    config.protocol = ESB_PROTOCOL_ESB_DPL;
    config.mode = ESB_LINK_ROLE_MODE;
    config.event_handler = on_esb_event;
    config.bitrate = ESB_BITRATE_2MBPS;
    config.crc = ESB_CRC_16BIT;
    config.retransmit_count = 3;
    config.retransmit_delay = 600;
    config.payload_length = 8;
    /* Per-packet ACK is controlled by event_wants_ack() in peripheral.c.
     * The reverse channel rides the ACKs that the peripheral does request. */
    config.selective_auto_ack = false;
    config.tx_mode = ESB_TXMODE_MANUAL;
    config.use_fast_ramp_up = false;

    error = esb_init(&config);
    if (error) {
        LOG_ERR("esb_init failed (%d)", error);
        return error;
    }

    error = esb_link_radio_setup();
    if (error) {
        return error;
    }
    // hop_start();
    //  int start_error = esb_link_role_start();
    // // hop_start();
    // return start_error;
    return 0;
}

int esb_link_set_enabled(bool enabled) {
    if (!enabled) {
        hop_stop();
        int stop_error = esb_stop_rx();
        if (stop_error) {
            LOG_DBG("esb_stop_rx returned %d", stop_error);
        }
        int flush_error = esb_flush_tx();
        if (flush_error) {
            LOG_DBG("esb_flush_tx returned %d", flush_error);
        }
        return 0;
    }
    int start_error = esb_link_role_start();
    // hop_start();
    return start_error;
}
