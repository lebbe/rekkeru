#pragma once

#include <stddef.h>

#include "data.h"

// Connects to Wi-Fi, fetches screen data, and turns Wi-Fi off again.
// On success, data is overwritten. On failure, error contains a short message.
bool fetchScreen(NetData &data, char *error, size_t errorSize);
