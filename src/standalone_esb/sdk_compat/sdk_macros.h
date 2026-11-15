#ifndef ZMK_SPLIT_ESB_NRF5_SDK_MACROS_H
#define ZMK_SPLIT_ESB_NRF5_SDK_MACROS_H

#include <zephyr/sys/util.h>

#ifndef NUM_VA_ARGS
#define NUM_VA_ARGS(...) NUM_VA_ARGS_LESS_1(0, __VA_ARGS__)
#endif

#endif
