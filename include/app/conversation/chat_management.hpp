#ifndef CHAT
#define CHAT

#include "../toolcalling/tools.hpp"
#include "absl/status/status.h"
#include <ostream>
#include <vector>

class chat_manager {
private:
  std::string convo_history; // a summarized conversation window passed to the
                             // LLM every time.
  std::vector<Tool> availible_tools;
  std::string sysprompt;
  std::string user_info;
  std::string u_advisor; // if writing an email is neccessary
  std::string u_major;
  std::string u_minor;

public:
  chat_manager();
  std::string getUserInfo(std::istream &i, std::ostream &o);
  absl::Status chat(std::istream &i, std::ostream &o); // chat loop
};

#endif