# Copyright 2026 Roman Kuzmitskii (@damex)
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_SPLIT_ESB)
  zephyr_include_directories(include)
  zephyr_library_named(zmk_feature_split_esb)
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_compile_options(
      -Werror
      -Wextra
      -Wno-unused
      -Wshadow
      -Wformat=2
      -Wlogical-op
      -Wduplicated-cond
      -Wmissing-prototypes
      -Wjump-misses-init
      -Wwrite-strings
      -Winit-self
      -Wold-style-definition
      -Wswitch-enum
      -Wswitch-default
      -Wimplicit-fallthrough=5
      -Wmissing-declarations
      -Wnull-dereference
      -Wstack-usage=256
      -fanalyzer
  )
  zephyr_library_sources(src/esb_keepalive.c)
  zephyr_library_sources(src/esb_log.c)
  zephyr_library_sources(src/esb_sensor_sync.c)
  zephyr_library_sources(src/esb_wire.c)
  zephyr_library_sources(src/esb_link.c)
  zephyr_library_sources(src/esb_link_hfclk.c)
  zephyr_library_sources(src/hop.c)
  zephyr_library_sources(src/hop_policy.c)
  zephyr_library_sources(src/spsc_latch.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_SLEEP src/esb_sleep.c)
  # 4 is LOG_LEVEL_DBG.
  if(CONFIG_ZMK_SPLIT_ESB_LOG_LEVEL GREATER_EQUAL 4)
    zephyr_library_sources(src/esb_stats.c)
  endif()
  zephyr_library_sources(src/central.c)
  zephyr_library_sources(src/esb_survey.c)
  zephyr_library_sources(src/esb_link_central.c)
  zephyr_library_sources(src/hop_central.c)
  zephyr_library_sources(src/esb_peripheral_changed.c)
  zephyr_library_sources(src/hid_state_central.c)
  if(CONFIG_ZMK_INPUT_SPLIT)
    zephyr_library_sources(src/central_input.c)
  endif()
  if(CONFIG_ZMK_SPLIT_ESB_HID_RELAY)
    zephyr_library_sources(src/esb_hid_relay_central.c)
  endif()
  if(CONFIG_DT_HAS_ZMK_INPUT_PROCESSOR_ESB_RELAY_POINTER_ENABLED)
    zephyr_library_sources(src/esb_hid_relay_pointer.c)
  endif()
  zephyr_library_sources_ifdef(CONFIG_ZMK_SPLIT_ESB_WIRE_PEER src/wire_central.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_SPLIT_ESB_WIRE src/cobs.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_SPLIT_ESB_WIRE src/wire_frame.c)
  zephyr_library_sources_ifdef(CONFIG_ZMK_SPLIT_ESB_WIRE src/wire_link.c)
endif()
