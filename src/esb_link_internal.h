// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* ESB radio link internals. */
#pragma once

#include <stdint.h>

#include <errno.h>

#include <nrf_error.h>
#include <nrf_esb.h>

static inline int esb_link_nrf_status_to_errno(uint32_t status) {
    if ((int32_t)status < 0) {
        return (int32_t)status;
    }

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
    default:
        return -EIO;
    }
}


#define ESB_LINK_PIPE_MAX 8 /* ESB hardware pipe count */

struct esb_link_packet {
    uint8_t pipe;
    uint8_t length;
    uint8_t data[CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD];
};

extern const uint8_t esb_link_pipe_count;

/* Starts receiving packets. */
int esb_link_role_start(void);

/* Radio ISR after RX FIFO drains, pipes_acked a bit per pipe whose packet asked for an ACK. */
void esb_link_role_rx_done(uint8_t pipes_acked);

/* Held at most once. */
int esb_link_hfclk_acquire(void);
void esb_link_hfclk_release(void);
