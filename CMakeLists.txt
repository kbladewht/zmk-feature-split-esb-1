# Copyright 2026 Roman Kuzmitskii (@damex)
# SPDX-License-Identifier: MIT

if(CONFIG_ZMK_SPLIT_ESB)
  zephyr_include_directories(include)
  zephyr_library_named(zmk_feature_split_esb)
  zephyr_library_include_directories(${APPLICATION_SOURCE_DIR}/include)
  zephyr_library_compile_options(
      -Werror
      -Wextra
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
  if(CONFIG_ZMK_SPLIT_ESB_STANDALONE)
    if(NOT EXISTS "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/components/libraries/util/app_util.h" OR
       NOT EXISTS "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/components/drivers_nrf/nrf_soc_nosd/nrf_error.h")
      message(FATAL_ERROR
        "ZMK_SPLIT_ESB_STANDALONE requires a valid nRF5 SDK 17.1 path; "
        "set CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH")
    endif()
    zephyr_library_include_directories(
        src/standalone_esb/sdk_compat
        src/standalone_esb/nrf5sdk
        "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/components/libraries/util"
        "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/components/libraries/delay"
        "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/components/drivers_nrf/nrf_soc_nosd"
        "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/modules/nrfx/hal"
        "${CONFIG_ZMK_SPLIT_ESB_NRF5_SDK_PATH}/modules/nrfx/mdk"
    )
    zephyr_library_compile_definitions(
        NRF_ESB_MAX_PAYLOAD_LENGTH=${CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD}
        CONFIG_ESB_MAX_PAYLOAD_LENGTH=${CONFIG_ZMK_SPLIT_ESB_MAX_PAYLOAD}
    )
    zephyr_library_sources(
        src/standalone_esb/nrf5sdk/nrf_esb.c
        src/standalone_esb/nrf_esb_compat.c
    )
    set_source_files_properties(
        "${CMAKE_CURRENT_LIST_DIR}/src/standalone_esb/nrf5sdk/nrf_esb.c"
        PROPERTIES COMPILE_OPTIONS
        "-Wno-old-style-definition;-Wno-missing-prototypes;-Wno-missing-declarations;-Wno-switch-default;-Wno-unused-parameter"
    )
  endif()
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
