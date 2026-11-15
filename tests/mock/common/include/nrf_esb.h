/* Test-only nRF5 SDK ESB API shim backed by the radio mock. */
#pragma once

#include <esb.h>

typedef enum esb_protocol nrf_esb_protocol_t;
typedef enum esb_mode nrf_esb_mode_t;
typedef enum esb_bitrate nrf_esb_bitrate_t;
typedef enum esb_crc nrf_esb_crc_t;
typedef enum esb_tx_mode nrf_esb_tx_mode_t;
typedef enum esb_evt_id nrf_esb_evt_id_t;
typedef struct esb_payload nrf_esb_payload_t;
typedef struct esb_evt nrf_esb_evt_t;
typedef struct esb_config nrf_esb_config_t;
typedef esb_event_handler nrf_esb_event_handler_t;

#define NRF_ESB_MAX_PAYLOAD_LENGTH CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD
#define NRF_ESB_DEFAULT_CONFIG ESB_DEFAULT_CONFIG
#define NRF_ESB_PROTOCOL_ESB ESB_PROTOCOL_ESB
#define NRF_ESB_PROTOCOL_ESB_DPL ESB_PROTOCOL_ESB_DPL
#define NRF_ESB_MODE_PTX ESB_MODE_PTX
#define NRF_ESB_MODE_PRX ESB_MODE_PRX
#define NRF_ESB_BITRATE_1MBPS ESB_BITRATE_1MBPS
#define NRF_ESB_BITRATE_2MBPS ESB_BITRATE_2MBPS
#define NRF_ESB_CRC_16BIT ESB_CRC_16BIT
#define NRF_ESB_EVENT_TX_SUCCESS ESB_EVENT_TX_SUCCESS
#define NRF_ESB_EVENT_TX_FAILED ESB_EVENT_TX_FAILED
#define NRF_ESB_EVENT_RX_RECEIVED ESB_EVENT_RX_RECEIVED
#define NRF_ESB_TX_POWER_0DBM 0

#define nrf_esb_init esb_init
#define nrf_esb_read_rx_payload esb_read_rx_payload
#define nrf_esb_write_payload esb_write_payload
#define nrf_esb_start_rx esb_start_rx
#define nrf_esb_stop_rx esb_stop_rx
#define nrf_esb_flush_tx esb_flush_tx
#define nrf_esb_set_address_length esb_set_address_length
#define nrf_esb_set_base_address_0 esb_set_base_address_0
#define nrf_esb_set_base_address_1 esb_set_base_address_1
#define nrf_esb_set_prefixes esb_set_prefixes
#define nrf_esb_set_rf_channel esb_set_rf_channel
#define nrf_esb_set_tx_power esb_set_tx_power
