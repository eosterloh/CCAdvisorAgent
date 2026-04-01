#include "../../../include/app/conversation/chat_management.hpp"
#include "absl/status/status.h"

chat_manager::chat_manager() {}
std::string chat_manager::getUserInfo(std::istream &i, std::ostream &o) {
  o << user_info;
}
/*
Ideas for the chat:
Phase like system
Phase 1: deterministic quick algorithm like the one above to get the user's
basic information like major, classes taken ,current schedule, and what not
Phase 2: Ask questions until there is zero ambiguity about what they
want to do while in school, do you plan on going abroad? What sorts of
classes are you interested in taking?
Phase 3: Allow user to ask questions after getting approiate information
Give the tools neccessary to respond correctly. The whole point of this
Is getting the user a good plan so that we minimize the Advisors
Schedules.
Tools we can use: Not enough info about a major: scrape through some data
try different links as supplied by gemini, remeber, have it run a tool
and then check the output to see if its happy with it.
Need a COI/ doesn't meet the prerequistites, if the degree of
seperation is only 1, we can draft an email for the user to send
for a COI.
If the question is out of scope or too complex for the agent sign up
to one of the calenders of an advisor
Finally, hand the user a personalized md or PDF plan of what they should
be taking.
Another thing to note: every call we will call a lighter weight model as well
to summarize the conversation, and pass that thru as input.
Final thing to note: For every prompt, its a good idea to pull from
weaviate everything thats relevant for the major.
Implement:
Plan based on retrieved
Take an action based on that plan
Observe what happens
*/
absl::Status chat_manager::chat(std::istream &i, std::ostream &o) {}
