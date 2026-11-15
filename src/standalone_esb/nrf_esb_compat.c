#include <errno.h>
#include <string.h>

#include <zephyr/irq.h>
#include <zephyr/kernel.h>

#include <esb.h>
#include "nrf5sdk/nrf_esb.h"
#include "nrf_error.h"

extern void RADIO_IRQHandler(void);
extern void ESB_EVT_IRQHandler(void);

static esb_event_handler esb_handler;
static bool irq_handlers_connected;

static void radio_irq_wrapper(const void *arg)
{
    ARG_UNUSED(arg);
    RADIO_IRQHandler();
}

static void esb_event_irq_wrapper(const void *arg)
{
    ARG_UNUSED(arg);
    ESB_EVT_IRQHandler();
}

static int nrf_status_to_errno(uint32_t status)
{
    switch (status) {
    case NRF_SUCCESS:
        return 0;
    case NRF_ERROR_BUSY:
        return -EBUSY;
    case NRF_ERROR_NO_MEM:
        return -ENOMEM;
    case NRF_ERROR_INVALID_LENGTH:
        return -EMSGSIZE;
    case NRF_ERROR_NULL:
    case NRF_ERROR_INVALID_PARAM:
        return -EINVAL;
    case NRF_ERROR_INVALID_STATE:
        return -EIO;
    default:
        return -EIO;
    }
}

static int connect_irq_handlers(void)
{
    if (irq_handlers_connected) {
        return 0;
    }

    int error = irq_connect_dynamic(RADIO_IRQn, CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY,
                                    radio_irq_wrapper, NULL, 0);
    if (error < 0) {
        return error;
    }

    error = irq_connect_dynamic(ESB_EVT_IRQ, CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY,
                                esb_event_irq_wrapper, NULL, 0);
    if (error < 0) {
        return error;
    }

    irq_handlers_connected = true;
    return 0;
}

static void nrf_event_handler(nrf_esb_evt_t const *event)
{
    if (esb_handler == NULL) {
        return;
    }

    struct esb_evt translated = {
        .evt_id = (enum esb_evt_id)event->evt_id,
        .tx_attempts = event->tx_attempts,
    };
    esb_handler(&translated);
}

int esb_init(const struct esb_config *config)
{
    if (config == NULL) {
        return -EINVAL;
    }

    int error = connect_irq_handlers();
    if (error != 0) {
        return error;
    }

    nrf_esb_config_t nrf_config = NRF_ESB_DEFAULT_CONFIG;
    nrf_config.protocol = (nrf_esb_protocol_t)config->protocol;
    nrf_config.mode = (nrf_esb_mode_t)config->mode;
    nrf_config.event_handler = nrf_event_handler;
    nrf_config.bitrate = (nrf_esb_bitrate_t)config->bitrate;
    nrf_config.crc = (nrf_esb_crc_t)config->crc;
    nrf_config.tx_output_power = (nrf_esb_tx_power_t)config->tx_output_power;
    nrf_config.retransmit_delay = config->retransmit_delay;
    nrf_config.retransmit_count = config->retransmit_count;
    nrf_config.tx_mode = (nrf_esb_tx_mode_t)config->tx_mode;
    nrf_config.radio_irq_priority = CONFIG_ZMK_SPLIT_ESB_RADIO_IRQ_PRIORITY;
    nrf_config.event_irq_priority = CONFIG_ZMK_SPLIT_ESB_EVENT_IRQ_PRIORITY;
    nrf_config.payload_length = config->payload_length;
    nrf_config.selective_auto_ack = config->selective_auto_ack;

    esb_handler = config->event_handler;
    error = nrf_status_to_errno(nrf_esb_init(&nrf_config));
    if (error != 0) {
        esb_handler = NULL;
    }
    return error;
}

int esb_suspend(void)
{
    return nrf_status_to_errno(nrf_esb_suspend());
}

void esb_disable(void)
{
    (void)nrf_esb_disable();
    esb_handler = NULL;
}

bool esb_is_idle(void)
{
    return nrf_esb_is_idle();
}

int esb_write_payload(const struct esb_payload *payload)
{
    if (payload == NULL) {
        return -EINVAL;
    }
    if (payload->length > CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD) {
        return -EMSGSIZE;
    }

    nrf_esb_payload_t nrf_payload = {
        .length = payload->length,
        .pipe = payload->pipe,
        .rssi = payload->rssi,
        .noack = payload->noack,
        .pid = payload->pid,
    };
    memcpy(nrf_payload.data, payload->data, payload->length);
    return nrf_status_to_errno(nrf_esb_write_payload(&nrf_payload));
}

int esb_read_rx_payload(struct esb_payload *payload)
{
    if (payload == NULL) {
        return -EINVAL;
    }

    nrf_esb_payload_t nrf_payload;
    int error = nrf_status_to_errno(nrf_esb_read_rx_payload(&nrf_payload));
    if (error != 0) {
        return error;
    }

    payload->length = nrf_payload.length;
    payload->pipe = nrf_payload.pipe;
    payload->rssi = nrf_payload.rssi;
    payload->noack = nrf_payload.noack;
    payload->pid = nrf_payload.pid;
    memcpy(payload->data, nrf_payload.data, nrf_payload.length);
    return 0;
}

int esb_start_tx(void)
{
    return nrf_status_to_errno(nrf_esb_start_tx());
}

int esb_start_rx(void)
{
    return nrf_status_to_errno(nrf_esb_start_rx());
}

int esb_stop_rx(void)
{
    return nrf_status_to_errno(nrf_esb_stop_rx());
}

int esb_flush_tx(void)
{
    return nrf_status_to_errno(nrf_esb_flush_tx());
}

int esb_pop_tx(void)
{
    return nrf_status_to_errno(nrf_esb_pop_tx());
}

int esb_flush_rx(void)
{
    return nrf_status_to_errno(nrf_esb_flush_rx());
}

int esb_set_address_length(uint8_t length)
{
    return nrf_status_to_errno(nrf_esb_set_address_length(length));
}

int esb_set_base_address_0(const uint8_t *addr)
{
    return nrf_status_to_errno(nrf_esb_set_base_address_0(addr));
}

int esb_set_base_address_1(const uint8_t *addr)
{
    return nrf_status_to_errno(nrf_esb_set_base_address_1(addr));
}

int esb_set_prefixes(const uint8_t *prefixes, uint8_t num_pipes)
{
    return nrf_status_to_errno(nrf_esb_set_prefixes(prefixes, num_pipes));
}

int esb_enable_pipes(uint8_t enable_mask)
{
    return nrf_status_to_errno(nrf_esb_enable_pipes(enable_mask));
}

int esb_update_prefix(uint8_t pipe, uint8_t prefix)
{
    return nrf_status_to_errno(nrf_esb_update_prefix(pipe, prefix));
}

int esb_set_rf_channel(uint32_t channel)
{
    return nrf_status_to_errno(nrf_esb_set_rf_channel(channel));
}

int esb_get_rf_channel(uint32_t *channel)
{
    return nrf_status_to_errno(nrf_esb_get_rf_channel(channel));
}

int esb_set_tx_power(int8_t tx_output_power)
{
    return nrf_status_to_errno(nrf_esb_set_tx_power((nrf_esb_tx_power_t)tx_output_power));
}

int esb_set_retransmit_delay(uint16_t delay)
{
    return nrf_status_to_errno(nrf_esb_set_retransmit_delay(delay));
}

int esb_set_retransmit_count(uint16_t count)
{
    return nrf_status_to_errno(nrf_esb_set_retransmit_count(count));
}

int esb_set_bitrate(enum esb_bitrate bitrate)
{
    return nrf_status_to_errno(nrf_esb_set_bitrate((nrf_esb_bitrate_t)bitrate));
}

int esb_reuse_pid(uint8_t pipe)
{
    return nrf_status_to_errno(nrf_esb_reuse_pid(pipe));
}
