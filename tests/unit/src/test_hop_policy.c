// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

#include <stddef.h>
#include <stdint.h>

#include <zephyr/ztest.h>

#include "hop_policy.h"

ZTEST_SUITE(hop_policy, NULL, NULL, NULL, NULL, NULL);

ZTEST(hop_policy, test_should_hop_threshold) {
    uint8_t bad_windows = 0;
    const uint16_t threshold = 3;
    zassert_false(hop_policy_should_hop(&bad_windows, true, threshold), "1 of 3");
    zassert_false(hop_policy_should_hop(&bad_windows, true, threshold), "2 of 3");
    zassert_true(hop_policy_should_hop(&bad_windows, true, threshold), "3rd consecutive hops");
    zassert_equal(bad_windows, 0, "streak clears after hop");
}

ZTEST(hop_policy, test_should_hop_good_window_resets) {
    uint8_t bad_windows = 0;
    const uint16_t threshold = 3;
    hop_policy_should_hop(&bad_windows, true, threshold);
    hop_policy_should_hop(&bad_windows, true, threshold);
    zassert_false(hop_policy_should_hop(&bad_windows, false, threshold), "good window, no hop");
    zassert_equal(bad_windows, 0, "good window clears the streak");
    zassert_false(hop_policy_should_hop(&bad_windows, true, threshold), "streak restarts from 0");
}

ZTEST(hop_policy, test_should_hop_threshold_one) {
    uint8_t bad_windows = 0;
    zassert_true(hop_policy_should_hop(&bad_windows, true, 1), "threshold 1 hops on first fail");
}

ZTEST(hop_policy, test_marks_active) {
    zassert_true(hop_policy_marks_active(8, KEEPALIVE_RATE_IDLE), "data marks active, byte ignored");
    zassert_true(hop_policy_marks_active(2, KEEPALIVE_RATE_IDLE), "2-byte data marks active");
    zassert_true(hop_policy_marks_active(ESB_KEEPALIVE_LENGTH, KEEPALIVE_RATE_ACTIVE),
                 "active keepalive marks active");
    zassert_false(hop_policy_marks_active(ESB_KEEPALIVE_LENGTH, KEEPALIVE_RATE_IDLE),
                  "idle keepalive does not mark active");
}

ZTEST(hop_policy, test_is_keepalive) {
    zassert_true(hop_policy_is_keepalive(ESB_KEEPALIVE_LENGTH), "length 1 is keepalive");
    zassert_false(hop_policy_is_keepalive(2), "length 2 is data");
    zassert_false(hop_policy_is_keepalive(8), "length 8 is data");
}

ZTEST(hop_policy, test_index_next_wraps) {
    zassert_equal(hop_policy_index_next(0, 3), 1, NULL);
    zassert_equal(hop_policy_index_next(1, 3), 2, NULL);
    zassert_equal(hop_policy_index_next(2, 3), 0, "wraps at count");
    zassert_equal(hop_policy_index_next(0, 1), 0, "single channel stays put");
}

ZTEST(hop_policy, test_channel_for_epoch) {
    zassert_equal(hop_policy_channel_for_epoch(0, 3), 0, NULL);
    zassert_equal(hop_policy_channel_for_epoch(1, 3), 1, NULL);
    zassert_equal(hop_policy_channel_for_epoch(3, 3), 0, "wraps");
    zassert_equal(hop_policy_channel_for_epoch(7, 3), 1, NULL);
    zassert_equal(hop_policy_channel_for_epoch(5, 1), 0, "single channel always 0");
}

ZTEST(hop_policy, test_hop_vote) {
    const uint8_t weights[3] = {3, 1, 1};
    const uint16_t threshold = 6;
    uint8_t link_loss[3];

    link_loss[0] = 0; link_loss[1] = 0; link_loss[2] = 0;
    zassert_false(hop_policy_hop_vote(link_loss, weights, 3, threshold), "all good, no hop");

    link_loss[0] = 2; link_loss[1] = 0; link_loss[2] = 0;
    zassert_true(hop_policy_hop_vote(link_loss, weights, 3, threshold), "high-weight bad trips it");

    link_loss[0] = 0; link_loss[1] = 5; link_loss[2] = 0;
    zassert_false(hop_policy_hop_vote(link_loss, weights, 3, threshold),
                  "one low-weight under threshold");

    link_loss[0] = 0; link_loss[1] = 3; link_loss[2] = 3;
    zassert_true(hop_policy_hop_vote(link_loss, weights, 3, threshold),
                 "low-weight sum reaches threshold");
}
