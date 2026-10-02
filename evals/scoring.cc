#include "scoring.hpp"

absl::StatusOr<bool> HasPhase(const chat_manager &c, std::string_view phase) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == phase) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<bool> PhaseLatencyUnder(const chat_manager &c,
                                       std::string_view phase,
                                       std::chrono::milliseconds maxlatency) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == phase && event.latency > maxlatency) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<bool> HasSuccessfulTool(const chat_manager &c) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == "tool" && event.success) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<bool> HasSuccessfulToolNamed(const chat_manager &c,
                                            std::string_view tool_name) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == "tool" && event.success && event.tool_name.has_value() &&
        *event.tool_name == tool_name) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<bool> NoFailedCriticalPhase(const chat_manager &c) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if ((event.phase == "planner" || event.phase == "orchestrator" ||
         event.phase == "observer" || event.phase == "decider") &&
        !event.success) {
      return false;
    }
  }
  return true;
}

absl::StatusOr<bool> ResponderProducedOutput(const chat_manager &c) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == "responder" && event.success) {
      return true;
    }
  }
  return false;
}

absl::StatusOr<bool> DeciderMarkedDone(const chat_manager &c) {
  for (const TraceEvent &event : c.getTraceEvents()) {
    if (event.phase == "decider" &&
        event.event_summary == "Decider marked workflow complete.") {
      return true;
    }
  }
  return false;
}
