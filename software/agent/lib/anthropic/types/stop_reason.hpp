#pragma once

#include <string>

namespace anthropic {
namespace StopReason {

static const std::string END_TURN = "end_turn";
static const std::string MAX_TOKENS = "max_tokens";
static const std::string STOP_SEQUENCE = "stop_sequence";
static const std::string TOOL_USE = "tool_use";

} // namespace StopReason
} // namespace anthropic
