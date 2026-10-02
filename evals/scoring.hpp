#ifndef EVAL_SCORING
#define EVAL_SCORING

#include "app/conversation/chat_management.hpp"
#include "absl/status/statusor.h"
#include <chrono>
#include <string_view>

absl::StatusOr<bool> HasPhase(const chat_manager &c, std::string_view phase);
absl::StatusOr<bool> PhaseLatencyUnder(const chat_manager &c,
                                       std::string_view phase,
                                       std::chrono::milliseconds maxlatency);
absl::StatusOr<bool> HasSuccessfulTool(const chat_manager &c);
absl::StatusOr<bool> HasSuccessfulToolNamed(const chat_manager &c,
                                            std::string_view tool_name);
absl::StatusOr<bool> NoFailedCriticalPhase(const chat_manager &c);
absl::StatusOr<bool> ResponderProducedOutput(const chat_manager &c);
absl::StatusOr<bool> DeciderMarkedDone(const chat_manager &c);

#endif
