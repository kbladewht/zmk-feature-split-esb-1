set(esb_patch_yml "${CMAKE_CURRENT_LIST_DIR}/../zephyr/patches.yml")
set(esb_patch_base "${CMAKE_CURRENT_LIST_DIR}/../zephyr/patches")

file(GLOB esb_patch_files CONFIGURE_DEPENDS "${esb_patch_base}/nrf/*.patch")
set(esb_patches_apply_cleanly TRUE)
set(esb_patches_already_applied TRUE)

foreach(esb_patch_file IN LISTS esb_patch_files)
  execute_process(
    COMMAND git -C "${WEST_TOPDIR}/nrf" apply --check "${esb_patch_file}"
    RESULT_VARIABLE can_apply
    OUTPUT_QUIET ERROR_QUIET
  )
  if(NOT can_apply EQUAL 0)
    set(esb_patches_apply_cleanly FALSE)
  endif()

  execute_process(
    COMMAND git -C "${WEST_TOPDIR}/nrf" apply --reverse --ignore-whitespace --check "${esb_patch_file}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET ERROR_QUIET
  )

  get_filename_component(esb_patch_name "${esb_patch_file}" NAME)
  if(NOT already_applied EQUAL 0 AND esb_patch_name STREQUAL "sys-clock-static-zephyr41.patch")
    file(READ "${WEST_TOPDIR}/nrf/soc/nordic/nrf71/Kconfig.defconfig" esb_nrf71_config)
    string(FIND "${esb_nrf71_config}" "config SYS_CLOCK_HW_CYCLES_PER_SEC" clock_config_start)
    string(FIND "${esb_nrf71_config}" "endif # SOC_SERIES_NRF71X" clock_config_end)
    if(clock_config_start GREATER_EQUAL 0 AND clock_config_end GREATER clock_config_start)
      math(EXPR clock_config_length "${clock_config_end} - ${clock_config_start}")
      string(SUBSTRING "${esb_nrf71_config}" ${clock_config_start} ${clock_config_length} clock_config)
      string(FIND "${clock_config}" "# default $(dt_nodelabel_int_prop,grtc,clock-frequency) if NRF_GRTC_TIMER" clock_expr_commented)
      string(FIND "${clock_config}" "default 32768" clock_default_static)
      if(clock_expr_commented GREATER_EQUAL 0 AND clock_default_static GREATER_EQUAL 0)
        set(already_applied 0)
      endif()
    endif()
  endif()

  if(NOT already_applied EQUAL 0)
    set(esb_patches_already_applied FALSE)
  endif()
endforeach()

if(esb_patches_already_applied)
  message(STATUS "sdk-nrf already contains the ESB Kconfig fixes")
elseif(esb_patches_apply_cleanly)
  execute_process(
    COMMAND west patch
      -l "${esb_patch_yml}"
      -b "${esb_patch_base}"
      clean
    WORKING_DIRECTORY ${WEST_TOPDIR}
    OUTPUT_QUIET ERROR_QUIET
  )
  execute_process(
    COMMAND west patch
      -l "${esb_patch_yml}"
      -b "${esb_patch_base}"
      apply
    WORKING_DIRECTORY ${WEST_TOPDIR}
    RESULT_VARIABLE patch_result
  )
  if(NOT patch_result EQUAL 0)
    message(FATAL_ERROR "west patch apply for zmk-feature-split-esb failed (${patch_result})")
  endif()
else()
  message(FATAL_ERROR "sdk-nrf patches are partially applied or do not match the ESB patch set")
endif()
