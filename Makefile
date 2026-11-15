# Copyright 2026 Roman Kuzmitskii (@damex)
# SPDX-License-Identifier: MIT

PYTHON ?= python3
ZEPHYR_BASE ?= $(error ZEPHYR_BASE is not set)
ZEPHYR_TOOLCHAIN_VARIANT ?= host
ZMK_APP ?= $(error ZMK_APP is not set)

MOCK_ROOT := $(CURDIR)/tests/mock
MOCK_CASES += \
	central/ack_fifo_room_lossy_mouse \
	central/battery_from_keepalive \
	central/held_input_two_buttons \
	central/hid_state_sync \
	central/idle_ack \
	central/input_key_keepalive_presses_lost_press \
	central/input_key_keepalive_releases_lost_release \
	central/input_key_late_press_dropped \
	central/input_key_tracking_limit \
	central/keepalive_bitmap_replays_key_positions \
	central/key_orphan_release_dropped \
	central/key_repeated_press_heals_lost_release \
	central/lossy_totals \
	central/pointer_lossless \
	central/rendezvous \
	central/return_path \
	central/self_pipe_not_a_source \
	central/sensor_keepalive_replays_lost_rotation \
	central/sensor_resync_past_cap \
	central/sleep \
	central/stale_peripheral_keepalive_represses \
	central/stale_peripheral_releases_held \
	central/startup_order \
	central_hid_relay/ack_fifo_room_idle \
	central_hid_relay/ack_fifo_room_lossless_mouse \
	central_hid_relay/ack_fifo_room_lossless_mouse_two_halves \
	central_hid_relay/ack_fifo_room_lossy_mouse \
	central_hid_relay/ack_fifo_room_lossy_mouse_two_halves \
	central_hid_relay/burst \
	central_hid_relay/burst_room \
	central_hid_relay/button_backlog \
	central_hid_relay/button_heal \
	central_hid_relay/commands \
	central_hid_relay/host_pause_drops_key_press \
	central_hid_relay/host_pause_drops_motion \
	central_hid_relay/host_pause_releases_held_button \
	central_hid_relay/host_pause_releases_held_key \
	central_hid_relay/host_pause_releases_queued_button_press \
	central_hid_relay/host_pause_saves_dongle_leds_for_no_host \
	central_hid_relay/host_resume_relays_keys \
	central_hid_relay/host_resume_represses_held_button \
	central_hid_relay/idle_ack \
	central_hid_relay/indicators \
	central_hid_relay/mouse_keys \
	central_hid_relay/pointer \
	central_hid_relay/pointer_retry \
	central_hid_relay/poll \
	central_hid_relay/relay_poll_carries_new_motion_lossless \
	central_hid_relay/relay_poll_carries_new_motion_lossy \
	central_hid_relay/tap \
	event_codec \
	wire/central_hid_state \
	wire/central_simplex
MOCK_TARGETS := $(addprefix mock-test-,$(MOCK_CASES))

.PHONY: unit-test native-sim-test mock-test $(MOCK_TARGETS)
unit-test:
	ZEPHYR_TOOLCHAIN_VARIANT=$(ZEPHYR_TOOLCHAIN_VARIANT) $(PYTHON) $(ZEPHYR_BASE)/scripts/twister --testsuite-root tests/unit --platform unit_testing --inline-logs --clobber-output

native-sim-test:
	ZEPHYR_TOOLCHAIN_VARIANT=$(ZEPHYR_TOOLCHAIN_VARIANT) $(PYTHON) $(ZEPHYR_BASE)/scripts/twister --testsuite-root tests/native_sim --platform native_sim/native/64 --inline-logs --clobber-output --outdir twister-out-native-sim

mock-test: $(MOCK_TARGETS)

define mock_case_rule
mock-test-$(1):
	cd $$(ZMK_APP) && ZEPHYR_TOOLCHAIN_VARIANT=$$(ZEPHYR_TOOLCHAIN_VARIANT) west build -p -d $$(CURDIR)/mock-test-out/$(1) -b native_sim//zmk_test_mock -- -DZMK_CONFIG=$$(MOCK_ROOT)/$(1) -DZMK_EXTRA_MODULES="$$(MOCK_ROOT)/$(1)/module;$$(MOCK_ROOT)/common"
	mock-test-out/$(1)/zephyr/zmk.exe
endef

$(foreach case,$(MOCK_CASES),$(eval $(call mock_case_rule,$(case))))
