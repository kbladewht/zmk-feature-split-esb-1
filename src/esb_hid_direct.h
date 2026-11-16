// Copyright 2026 Roman Kuzmitskii (@damex)
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

bool esb_hid_direct_handle(uint8_t pipe, const uint8_t *data, size_t length);
