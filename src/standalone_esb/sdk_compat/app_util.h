#ifndef ZMK_SPLIT_ESB_NRF5_APP_UTIL_H
#define ZMK_SPLIT_ESB_NRF5_APP_UTIL_H

#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>

#ifndef STATIC_ASSERT
#define STATIC_ASSERT(condition) BUILD_ASSERT(condition)
#endif

#ifndef __ALIGN
#define __ALIGN(size) __aligned(size)
#endif

#endif
