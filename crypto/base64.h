#pragma once
#include "../lib/types.h"

// Base64 encode: returns length of encoded string
int base64_encode(const void* data, int len, char* out, int out_max);

// Base64 decode: returns length of decoded data
int base64_decode(const char* in, int len, uint8_t* out, int out_max);
