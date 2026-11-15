#ifndef ZMK_SPLIT_ESB_NRF5_SDK_COMMON_H
#define ZMK_SPLIT_ESB_NRF5_SDK_COMMON_H

#include "app_util.h"
#include "nrf_error.h"
#include "sdk_errors.h"

#define VERIFY_PARAM_NOT_NULL(parameter) \
    do {                                  \
        if ((parameter) == NULL) {        \
            return NRF_ERROR_NULL;        \
        }                                 \
    } while (0)

#define VERIFY_TRUE(condition, error_code) \
    do {                                    \
        if (!(condition)) {                 \
            return (error_code);            \
        }                                   \
    } while (0)

#define VERIFY_FALSE(condition, error_code) \
    do {                                     \
        if (condition) {                     \
            return (error_code);             \
        }                                    \
    } while (0)

#endif
