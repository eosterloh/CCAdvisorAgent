#include "../../../include/app/conversation/chat_management.hpp"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/geminiclient/gemini_generation.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <utility>
#include <vector>
using json = nlohmann::json;

namespace {
constexpr int kMaxToolcalls = 5;
constexpr std::chrono::milliseconds kMaxLatencyDelay =
    std::chrono::milliseconds(5000);

} // namespace

chat_manager::chat_manager() {}
std::string chat_manager::getUserInfo(std::istream &i, std::ostream &o) {
  o << user_info;
}

std::string chat_manager::getPlannerPrompt() {
  std::string planner_prompt =
      "You are a planning agent for academic advising.\n"
      "For every user prompt, do three things:\n"
      "1) Output tool-calling necessity from 0.0 to 1.0 as the first token.\n"
      "2) If tools are needed, provide a detailed step-by-step tool plan.\n"
      "3) Explain why each selected tool is necessary.\n"
      "Available tools:\n";

  if (availible_tools.empty()) {
    planner_prompt =
        absl::StrCat(planner_prompt, "- No tools are currently registered.\n");
  } else {

    for (Tool &tool : availible_tools) {
      planner_prompt =
          absl::StrCat(planner_prompt, tool.getToolDescription(), "\n");
    }
  }
  planner_prompt = absl::StrCat(
      planner_prompt, "Also, make sure that you put it in json format\n",
      "Here is the humans prompt:");

  return planner_prompt;
}

/*
Ideas for the chat:
Phase like system
Phase 1: deterministic quick algorithm like the one above to get the
user's basic information like major, classes taken ,current
schedule, and what not Phase 2: Ask questions until there is zero
ambiguity about what they want to do while in school, do you plan on
going abroad? What sorts of classes are you interested in taking?
Phase 3: Allow user to ask questions after getting approiate
information Give the tools neccessary to respond correctly. The
whole point of this Is getting the user a good plan so that we
minimize the Advisors Schedules. Tools we can use: Not enough info
about a major: scrape through some data try different links as
supplied by gemini, remeber, have it run a tool and then check the
output to see if its happy with it. Need a COI/ doesn't meet the
prerequistites, if the degree of seperation is only 1, we can draft
an email for the user to send for a COI. If the question is out of
scope or too complex for the agent sign up to one of the calenders
of an advisor Finally, hand the user a personalized md or PDF plan
of what they should be taking. Another thing to note: every call we
will call a lighter weight model as well to summarize the
conversation, and pass that thru as input. Final thing to note: For
every prompt, its a good idea to pull from weaviate everything thats
relevant for the major. Implement: Plan based on retrieved Take an
action based on that plan Observe what happens
*/

std::string extractJsonText(std::string preparsed) {
  json content = json::parse(preparsed);
  // Add basic safety checks to avoid exceptions or invalid accesses.
  if (!content.contains("candidates") || !content.at("candidates").is_array() ||
      content.at("candidates").empty()) {
    return "";
  }
  const json::value_type &candidates = content.at("candidates");
  if (!candidates.at(0).contains("content") ||
      !candidates.at(0).at("content").is_array() ||
      candidates.at(0).at("content").empty()) {
    return "";
  }
  const json::value_type &candidate_content = candidates.at(0).at("content");
  if (!candidate_content.at(0).contains("parts") ||
      !candidate_content.at(0).at("parts").is_array() ||
      candidate_content.at(0).at("parts").empty()) {
    return "";
  }
  const json::value_type &parts = candidate_content.at(0).at("parts");
  if (!parts.at(0).contains("text") || !parts.at(0).at("text").is_string()) {
    return "";
  }

  std::string textcontent = content.at("candidates")
                                .at(0)
                                .at("content")
                                .at(0)
                                .at("parts")
                                .at(0)
                                .at("text");
  return textcontent;
}

std::vector<Tool> parseOrchResultForTools(std::string result) {
  // json parse it and turn that into a list of tools
  // maybe have to overload some cast overload.
}

std::pair<bool, bool> parseDecision(std::string returned) {
  std::string parsed = extractJsonText(returned);
  // now this will be in json format:
  json bools = json::parse(parsed);
  bool p1 = bools.at("observer_decision");
  bool p2 = bools.at("planner_retry");

  return std::make_pair(p1, p2);
}

std::string parsePlan(std::string msg) { return extractJsonText(msg); }

std::string chat_manager::getSummarization() { return convo_shortterm_history; }

absl::Status chat_manager::chat(std::istream &i, std::ostream &o) {
  // Never really return an error if something goes wrong
  // just have it ask the user to escalate
  // e.g. of a possible error: hits a tool call or
  // latency or retry threshold.
  GeminiGenerator g;
  getUserInfo(i, o);
  absl::Status curstatus = absl::OkStatus();
  // Add logs for each of these steps
  while (curstatus.ok()) {
    std::getline(i, curquery);
    // planner part
    //  decides where a tool call is neccessary or
    // just needs to respond or something.
    // we will use gemini 3.0 pro for this
    std::string planner_prompt = getPlannerPrompt();
    curstatus =
        g.geminiGen(absl::StrCat(planner_prompt, curquery, getUserInfo()));
    if (!curstatus.ok()) {
      return curstatus;
    }
    std::string plan = parsePlan(g.getContent());
    float tool_calling_threshold;
    std::vector<Tool> tools_to_use;
    std::vector<std::string> explaination_for_use;

    // sends plan to orchestrator which handles the tool calls
    // while loop for orchestrator//observer
    // this only happens if the planner deemed the tool call
    // neccessary
    if (tool_calling_threshold >= .5) {
      bool observerDecision = true;
      bool plannerRetry = false;
      int curToolCalls = 0;
      while (observerDecision || curToolCalls <= kMaxToolcalls) {
        if (plannerRetry) {
          // set up the planning agent again.
          std::string planner_prompt = getPlannerPrompt();
          curstatus = g.geminiGen(
              absl::StrCat(planner_prompt, curquery, getUserInfo()));
          std::string plan = parsePlan(g.getContent());
        }
        std::string orchestrator_prompt =
            orchPrompt(tools_to_use, explaination_for_use);
        absl::Status err = g.geminiGen(orchestrator_prompt);
        std::vector<Tool> order = parseOrchResultForTools(g.getContent());
        std::vector<ToolCallingLogs> logs;
        for (Tool &tool : order) {

          if (curToolCalls >= kMaxToolcalls) {
            break;
          }
        }
        // After this happens, give the logs to an observer judge

        std::string observer_prompt = getObserverPrompt(logs);
        err = g.geminiGen(observer_prompt);
        std::pair<bool, bool> observerResults = parseDecision(g.getContent());

        observerDecision = observerResults.first;
        plannerRetry = observerResults.second;
        // should probably do two things, see if this reached the desired
        //  result, if not, lets ot back to the planning stages
      }
      // summarize the conversation be like: heres what I did.
      convo_longterm_history = summarizeLong(convo_longterm_history);
      convo_shortterm_history = summarizeShort(convo_shortterm_history);

      // another agent call: heres what I did.
      o << getSummarization() << "\n";
    } else {
      // Responder agent.
      //  if we do nothing we should respond
      std::string responder_prompt = getResponderPrompt();
      curstatus = g.geminiGen(responder_prompt);

      convo_longterm_history = summarizeLong(convo_longterm_history);
      convo_shortterm_history = summarizeShort(convo_shortterm_history);
      // Summarizer summarizes this conversation twice: in the conte
      // xt of the whole thing and in the short term conversation
    }
    // finally, the decider judge, decides from the content if a proper plan
    //  cna be made and there is no ambiguity in a list of things.

    if (done) {
      makePlanAsPdf();
      break;
    }
  }
  return absl::OkStatus();
  // end condition: When we finalize the plan for the user
}
