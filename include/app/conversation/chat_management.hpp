#ifndef CHAT
#define CHAT

#include "../toolcalling/tools.hpp"
#include "absl/status/status.h"
#include "app/common/types.hpp"
#include <ostream>
#include <vector>

class chat_manager {
private:
  std::string convo_shortterm_history;
  std::string convo_longterm_history;
  std::vector<Tool> availible_tools;
  std::string user_info;
  std::string u_advisor; // if writing an email is neccessary
  std::string u_major;
  std::string u_minor;
  std::string curquery;
  std::string getUserInfo(std::istream &i, std::ostream &o);
  std::string plannerPrompt();
  std::string orchPrompt(std::vector<Tool> tools,
                         std::vector<std::string> justifications);
  std::string observerPrompt(std::vector<ToolCallingLogs> logs);
  std::string responderPrompt(std::string curquery);
  std::string
  deciderPrompt(); // will really just use the convo history to decide this.
  std::string getSummarization();

public:
  chat_manager();
  absl::Status chat(std::istream &i, std::ostream &o); // chat loop
};

#endif