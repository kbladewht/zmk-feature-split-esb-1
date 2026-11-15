// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

/* Hop and link policy, pure functions. */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/sys/__assert.h>
#include <zephyr/sys/util.h>

#include "esb_keepalive.h"
#include "hop_policy.h"

uint8_t hop_policy_saturating_add(uint8_t value, uint8_t add) {
    if (value > UINT8_MAX - add) {
        return UINT8_MAX;
    }
    return (uint8_t)(value + add);
}

bool hop_policy_keepalive_is_active(uint8_t byte) {
    return byte == ESB_KEEPALIVE_ACTIVE;
}

int8_t hop_policy_rssi_to_dbm(int8_t rssi_magnitude) {
    return (int8_t)(-rssi_magnitude);
}

uint8_t hop_policy_loss_penalty(int8_t rssi_dbm, int8_t floor_dbm) {
    if (rssi_dbm >= floor_dbm) {
        return 0;
    }
    int below = (int)floor_dbm - (int)rssi_dbm;
    int penalty = 1 + below / HOP_POLICY_RSSI_GRADE_STEP_DB;
    if (penalty > HOP_POLICY_MAX_LOSS_PENALTY) {
        penalty = HOP_POLICY_MAX_LOSS_PENALTY;
    }
    return (uint8_t)penalty;
}

uint8_t hop_policy_channel_for_epoch(uint16_t epoch, size_t hop_count) {
    __ASSERT_NO_MSG(hop_count > 0);
    return (uint8_t)(epoch % hop_count);
}

uint8_t hop_policy_anchor_default_index(size_t slot, size_t pool_count, size_t anchor_count) {
    __ASSERT_NO_MSG(anchor_count > 0);
    __ASSERT_NO_MSG(anchor_count <= pool_count);
    __ASSERT_NO_MSG(slot < anchor_count);
    return (uint8_t)((pool_count * (2 * slot + 1)) / (2 * anchor_count));
}

bool hop_policy_mask_get(const uint8_t *mask, size_t index) {
    __ASSERT_NO_MSG(mask != NULL);
    return (mask[index / 8] & (uint8_t)(1u << (index % 8))) != 0;
}

void hop_policy_mask_set(uint8_t *mask, size_t index, bool active) {
    __ASSERT_NO_MSG(mask != NULL);
    uint8_t bit = (uint8_t)(1u << (index % 8));
    if (active) {
        mask[index / 8] = (uint8_t)(mask[index / 8] | bit);
    } else {
        mask[index / 8] = (uint8_t)(mask[index / 8] & (uint8_t)~bit);
    }
}

uint8_t hop_policy_window_penalty(uint32_t motion_mask, uint32_t active_mask,
                                  const int8_t *rssi_dbm, int8_t floor_dbm, size_t count) {
    __ASSERT_NO_MSG(rssi_dbm != NULL);
    uint8_t worst = 0;
    for (size_t index = 0; index < count; index++) {
        if (!(active_mask & (1u << index))) {
            continue;
        }
        uint8_t penalty;
        if (motion_mask & (1u << index)) {
            penalty = hop_policy_loss_penalty(rssi_dbm[index], floor_dbm);
        } else {
            penalty = HOP_POLICY_MAX_LOSS_PENALTY;
        }
        if (penalty > worst) {
            worst = penalty;
        }
    }
    return worst;
}

size_t hop_policy_mask_active_count(const uint8_t *mask, size_t pool_count) {
    __ASSERT_NO_MSG(mask != NULL);
    size_t count = 0;
    for (size_t index = 0; index < pool_count; index++) {
        if (hop_policy_mask_get(mask, index)) {
            count++;
        }
    }
    return count;
}

#define MASK_STORE_POOL_COUNT_OFFSET 0
#define MASK_STORE_CHANNELS_OFFSET 1

size_t hop_policy_mask_store_encode(uint8_t *stored, size_t stored_size, const uint8_t *channels,
                                    const uint8_t *mask, size_t pool_count) {
    __ASSERT_NO_MSG(stored != NULL);
    __ASSERT_NO_MSG(channels != NULL);
    __ASSERT_NO_MSG(mask != NULL);
    size_t length = HOP_POLICY_MASK_STORE_LENGTH(pool_count);
    if (pool_count > UINT8_MAX || stored_size < length) {
        return 0;
    }
    stored[MASK_STORE_POOL_COUNT_OFFSET] = (uint8_t)pool_count;
    memcpy(&stored[MASK_STORE_CHANNELS_OFFSET], channels, pool_count);
    memcpy(&stored[MASK_STORE_CHANNELS_OFFSET + pool_count], mask, (pool_count + 7) / 8);
    return length;
}

bool hop_policy_mask_store_matches(const uint8_t *stored, size_t stored_length,
                                   const uint8_t *channels, size_t pool_count) {
    __ASSERT_NO_MSG(stored != NULL);
    __ASSERT_NO_MSG(channels != NULL);
    if (pool_count > UINT8_MAX || stored_length != HOP_POLICY_MASK_STORE_LENGTH(pool_count)) {
        return false;
    }
    if (stored[MASK_STORE_POOL_COUNT_OFFSET] != pool_count) {
        return false;
    }
    return memcmp(&stored[MASK_STORE_CHANNELS_OFFSET], channels, pool_count) == 0;
}

const uint8_t *hop_policy_mask_store_mask(const uint8_t *stored, size_t pool_count) {
    __ASSERT_NO_MSG(stored != NULL);
    return &stored[MASK_STORE_CHANNELS_OFFSET + pool_count];
}

void hop_policy_score_update(uint8_t *score, uint8_t penalty, uint8_t decay) {
    __ASSERT_NO_MSG(score != NULL);
    if (penalty == 0) {
        *score = (uint8_t)(*score - MIN(*score, decay));
        return;
    }
    *score = hop_policy_saturating_add(*score, penalty);
}

size_t hop_policy_worst_channel(const uint8_t *channel_bad, const uint8_t *mask,
                                const uint8_t *anchor_mask, size_t pool_count,
                                uint16_t mask_threshold) {
    __ASSERT_NO_MSG(channel_bad != NULL);
    __ASSERT_NO_MSG(mask != NULL);
    __ASSERT_NO_MSG(anchor_mask != NULL);
    size_t worst = pool_count;
    for (size_t channel = 0; channel < pool_count; channel++) {
        if (hop_policy_mask_get(anchor_mask, channel) || !hop_policy_mask_get(mask, channel)) {
            continue;
        }
        if (channel_bad[channel] < mask_threshold) {
            continue;
        }
        if (worst == pool_count || channel_bad[channel] > channel_bad[worst]) {
            worst = channel;
        }
    }
    return worst;
}

uint16_t hop_policy_retest_threshold(uint16_t base_windows, uint8_t level) {
    if (level > HOP_POLICY_RETEST_LEVEL_MAX) {
        level = HOP_POLICY_RETEST_LEVEL_MAX;
    }
    uint32_t threshold = (uint32_t)base_windows << level;
    if (threshold > UINT16_MAX) {
        return UINT16_MAX;
    }
    return (uint16_t)threshold;
}

uint8_t hop_policy_index_next_active(uint8_t index, const uint8_t *mask, size_t count) {
    __ASSERT_NO_MSG(mask != NULL);
    __ASSERT_NO_MSG(count > 0);
    for (size_t step = 1; step <= count; step++) {
        size_t candidate = ((size_t)index + step) % count;
        if (hop_policy_mask_get(mask, candidate)) {
            return (uint8_t)candidate;
        }
    }
    return index;
}

uint8_t hop_policy_channel_for_epoch_masked(uint16_t epoch, const uint8_t *mask, size_t pool_count) {
    __ASSERT_NO_MSG(mask != NULL);
    __ASSERT_NO_MSG(pool_count > 0);
    size_t base = (size_t)(epoch % pool_count);
    for (size_t step = 0; step < pool_count; step++) {
        size_t index = (base + step) % pool_count;
        if (hop_policy_mask_get(mask, index)) {
            return (uint8_t)index;
        }
    }
    return (uint8_t)base;
}

bool hop_policy_hop_vote(const uint8_t *link_loss, const uint8_t *weights, size_t count,
                         uint16_t threshold) {
    __ASSERT_NO_MSG(link_loss != NULL);
    __ASSERT_NO_MSG(weights != NULL);
    uint32_t weighted = 0;
    for (size_t index = 0; index < count; index++) {
        weighted += (uint32_t)link_loss[index] * weights[index];
    }
    return weighted >= threshold;
}

void hop_policy_accrue_loss(uint8_t *link_loss, size_t count, uint32_t motion_mask,
                            uint32_t active_mask, const int8_t *rssi_dbm, int8_t floor_dbm) {
    __ASSERT_NO_MSG(link_loss != NULL);
    __ASSERT_NO_MSG(rssi_dbm != NULL);
    for (size_t index = 0; index < count; index++) {
        if (!(active_mask & (1u << index))) {
            link_loss[index] = 0;
            continue;
        }
        uint8_t penalty;
        if (motion_mask & (1u << index)) {
            penalty = hop_policy_loss_penalty(rssi_dbm[index], floor_dbm);
        } else {
            penalty = HOP_POLICY_MAX_LOSS_PENALTY;
        }
        if (penalty == 0) {
            link_loss[index] = 0;
        } else {
            link_loss[index] = hop_policy_saturating_add(link_loss[index], penalty);
        }
    }
}

size_t hop_policy_survey_mask(const int8_t *energy_dbm, size_t pool_count,
                              const uint8_t *anchor_mask, size_t min_active,
                              int8_t threshold_dbm, uint8_t *mask) {
    __ASSERT_NO_MSG(energy_dbm != NULL);
    __ASSERT_NO_MSG(anchor_mask != NULL);
    __ASSERT_NO_MSG(mask != NULL);
    size_t masked = 0;
    while (hop_policy_mask_active_count(mask, pool_count) > min_active) {
        size_t worst = pool_count;
        for (size_t channel = 0; channel < pool_count; channel++) {
            if (hop_policy_mask_get(anchor_mask, channel) ||
                !hop_policy_mask_get(mask, channel)) {
                continue;
            }
            if (energy_dbm[channel] < threshold_dbm) {
                continue;
            }
            if (worst == pool_count || energy_dbm[channel] > energy_dbm[worst]) {
                worst = channel;
            }
        }
        if (worst == pool_count) {
            break;
        }
        hop_policy_mask_set(mask, worst, false);
        masked++;
    }
    return masked;
}

bool hop_policy_window_period_fires(uint8_t *counter, uint8_t period) {
    __ASSERT_NO_MSG(counter != NULL);
    if (++(*counter) >= period) {
        *counter = 0;
        return true;
    }
    return false;
}

bool hop_policy_should_beacon(uint8_t epoch, uint8_t *beaconed_epoch, uint8_t *repeats_left,
                              uint8_t repeat_windows) {
    __ASSERT_NO_MSG(beaconed_epoch != NULL);
    __ASSERT_NO_MSG(repeats_left != NULL);
    if (epoch != *beaconed_epoch) {
        *beaconed_epoch = epoch;
        *repeats_left = repeat_windows;
    }
    if (*repeats_left > 0) {
        (*repeats_left)--;
        return true;
    }
    return false;
}
