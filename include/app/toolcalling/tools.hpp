#ifndef TOOLS
#define TOOLS
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

class Tool {
private:
  std::string toolName;
  std::string toolDescription;
  bool hasInvocationPayload = false;
  nlohmann::json invocationArgs = nlohmann::json::object();
  std::string invocationReason;

public:
  Tool() = default;
  Tool(std::string name, std::string description)
      : toolName(std::move(name)), toolDescription(std::move(description)) {}

  const std::string &getToolName() const { return toolName; }
  const std::string &getToolDescription() const { return toolDescription; }
  void setToolDescription(const std::string &description) {
    toolDescription = description;
  }

  void setInvocation(const nlohmann::json &args, const std::string &reason) {
    hasInvocationPayload = true;
    invocationArgs = args;
    invocationReason = reason;
  }
  void clearInvocation() {
    hasInvocationPayload = false;
    invocationArgs = nlohmann::json::object();
    invocationReason.clear();
  }
  bool hasInvocation() const { return hasInvocationPayload; }
  const nlohmann::json &getInvocationArgs() const { return invocationArgs; }
  const std::string &getInvocationReason() const { return invocationReason; }
};

#endif