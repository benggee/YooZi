#pragma once

#include <cstdint>

namespace anthropic {

struct Usage {
    int64_t input_tokens = 0;
    int64_t output_tokens = 0;
    int64_t cache_creation_input_tokens = 0;
    int64_t cache_read_input_tokens = 0;
};

} // namespace anthropic
