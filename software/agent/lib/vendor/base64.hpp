#pragma once

#include <string>
#include <vector>

namespace base64 {

static const char kEncodeTable[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

inline std::string encode(const unsigned char* data, size_t len) {
    std::string result;
    result.reserve(((len + 2) / 3) * 4);

    for (size_t i = 0; i < len; i += 3) {
        unsigned int octet_a = data[i];
        unsigned int octet_b = (i + 1 < len) ? data[i + 1] : 0;
        unsigned int octet_c = (i + 2 < len) ? data[i + 2] : 0;

        unsigned int triple = (octet_a << 16) | (octet_b << 8) | octet_c;

        result += kEncodeTable[(triple >> 18) & 0x3F];
        result += kEncodeTable[(triple >> 12) & 0x3F];
        result += (i + 1 < len) ? kEncodeTable[(triple >> 6) & 0x3F] : '=';
        result += (i + 2 < len) ? kEncodeTable[triple & 0x3F] : '=';
    }

    return result;
}

inline std::string encode(const std::string& binary_data) {
    return encode(reinterpret_cast<const unsigned char*>(binary_data.data()),
                  binary_data.size());
}

} // namespace base64
