
#include <absl/status/statusor.h>

#include "../include/app/geminiclient/gemini_generation.hpp"
#include "app/conversation/chat_management.hpp"
#include <../include/app/common/types.hpp>

absl::StatusOr<bool> HasPhase(const chat_manager &c, std::string_view phase) {
  std::vector<TraceEvent> res = c.getTraceEvents();
  for (TraceEvent t : res) {
    if (t.phase == phase) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<bool> PhaseLatencyUnder(const chat_manager &c,
                                       std::chrono::milliseconds maxlatency){

    return false} // have ai do this

absl::StatusOr<bool> HasSuccessfulTool(const chat_manager &c) {}

absl::StatusOr<bool> NoFailedCriticalPhase(const chat_manager &c) {}

absl::StatusOr<bool> ResponderProducedOutput(const chat_manager &c) {}