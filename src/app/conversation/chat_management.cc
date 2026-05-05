#include "../../../include/app/conversation/chat_management.hpp"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/geminiclient/gemini_generation.hpp"
#include "app/toolcalling/retrieve_from_weaviate.hpp"
#include "app/toolcalling/scraper_tool.hpp"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>
using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr int kMaxToolcalls = 10;
constexpr std::chrono::milliseconds kMaxLatencyDelay =
    std::chrono::milliseconds(5000);
constexpr double kToolCallThreshold = .6;

std::string normalizeMajorKey(std::string major) {
  for (char &c : major) {
    const bool is_alnum =
        (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    if (is_alnum) {
      if (c >= 'A' && c <= 'Z') {
        c = static_cast<char>(c - 'A' + 'a');
      }
      continue;
    }
    c = '_';
  }
  std::string compact;
  compact.reserve(major.size());
  bool previous_underscore = false;
  for (char c : major) {
    if (c == '_') {
      if (!previous_underscore) {
        compact.push_back(c);
      }
      previous_underscore = true;
    } else {
      compact.push_back(c);
      previous_underscore = false;
    }
  }
  while (!compact.empty() && compact.front() == '_') {
    compact.erase(compact.begin());
  }
  while (!compact.empty() && compact.back() == '_') {
    compact.pop_back();
  }
  if (compact.empty()) {
    return "unspecified_major";
  }
  return compact;
}

std::string shellEscapeSingleQuoted(const std::string &raw) {
  std::string escaped;
  escaped.reserve(raw.size() + 8);
  for (char c : raw) {
    if (c == '\'') {
      escaped.append("'\\''");
      continue;
    }
    escaped.push_back(c);
  }
  return escaped;
}

fs::path resolveProjectRoot() {
  const fs::path cwd = fs::current_path();
  if (fs::exists(cwd / "scripts" / "populate_major_in_background.sh")) {
    return cwd;
  }
  if (fs::exists(cwd / ".." / "scripts" / "populate_major_in_background.sh")) {
    return fs::weakly_canonical(cwd / "..");
  }
  return cwd;
}
std::string extractJsonText(std::string preparsed) {
  const json root = json::parse(preparsed, nullptr, false);
  if (root.is_discarded()) {
    return "";
  }

  if (!root.contains("candidates") || !root.at("candidates").is_array() ||
      root.at("candidates").empty()) {
    return "";
  }

  const json::value_type &candidate = root.at("candidates").at(0);
  if (!candidate.contains("content")) {
    return "";
  }

  // Most common Gemini shape:
  // candidates[0].content.parts[0].text
  if (candidate.at("content").is_object()) {
    const json::value_type &content_obj = candidate.at("content");
    if (!content_obj.contains("parts") || !content_obj.at("parts").is_array() ||
        content_obj.at("parts").empty()) {
      return "";
    }
    const json::value_type &first_part = content_obj.at("parts").at(0);
    if (!first_part.contains("text") || !first_part.at("text").is_string()) {
      return "";
    }
    return first_part.at("text").get<std::string>();
  }

  // Fallback for older/alternate shape your code previously assumed:
  // candidates[0].content[0].parts[0].text
  if (candidate.at("content").is_array() && !candidate.at("content").empty()) {
    const json::value_type &first_content = candidate.at("content").at(0);
    if (!first_content.contains("parts") ||
        !first_content.at("parts").is_array() ||
        first_content.at("parts").empty()) {
      return "";
    }
    const json::value_type &first_part = first_content.at("parts").at(0);
    if (!first_part.contains("text") || !first_part.at("text").is_string()) {
      return "";
    }
    return first_part.at("text").get<std::string>();
  }

  // Unexpected content shape.
  return "RAHHHH";
}

std::vector<Tool> parseOrchResultForTools(std::string result) {
  std::string parsed = extractJsonText(result);
  if (parsed.empty()) {
    return {};
  }

  const json orchestration_json = json::parse(parsed, nullptr, false);
  if (orchestration_json.is_discarded() ||
      !orchestration_json.contains("tool_calls") ||
      !orchestration_json.at("tool_calls").is_array()) {
    return {};
  }

  std::vector<Tool> tool_calls;
  for (const json::value_type &call : orchestration_json.at("tool_calls")) {
    if (!call.is_object() || !call.contains("tool_name") ||
        !call.at("tool_name").is_string() || !call.contains("args") ||
        !call.at("args").is_object()) {
      continue;
    }

    const std::string tool_name = call.at("tool_name").get<std::string>();
    std::string description = tool_name;
    if (call.contains("tool_description") &&
        call.at("tool_description").is_string()) {
      description = call.at("tool_description").get<std::string>();
    }

    std::string reason = "";
    if (call.contains("reason") && call.at("reason").is_string()) {
      reason = call.at("reason").get<std::string>();
    }

    Tool tool(tool_name, description);
    tool.setInvocation(call.at("args"), reason);
    tool_calls.push_back(tool);
  }
  return tool_calls;
}

std::pair<bool, bool> parseObserverDecision(std::string returned) {
  std::string parsed = extractJsonText(returned);
  // now this will be in json format:
  json bools = json::parse(parsed);
  bool p1 = bools.at("observer_decision");
  bool p2 = bools.at("planner_retry");

  return std::make_pair(p1, p2);
}

std::string parsePlan(std::string msg) { return extractJsonText(msg); }

struct GroundedResponderOutput {
  std::string final_answer;
  std::string evidence_blob;
  bool parsed;
};

std::string normalizeJsonPayload(std::string text) {
  const std::string fenced_prefix = "```json";
  const std::string fence = "```";

  auto trim = [](std::string *value) {
    while (!value->empty() &&
           (value->front() == ' ' || value->front() == '\n' ||
            value->front() == '\t' || value->front() == '\r')) {
      value->erase(value->begin());
    }
    while (!value->empty() &&
           (value->back() == ' ' || value->back() == '\n' ||
            value->back() == '\t' || value->back() == '\r')) {
      value->pop_back();
    }
  };

  trim(&text);
  if (text.rfind(fenced_prefix, 0) == 0) {
    text.erase(0, fenced_prefix.size());
    trim(&text);
    if (text.size() >= fence.size() &&
        text.compare(text.size() - fence.size(), fence.size(), fence) == 0) {
      text.erase(text.size() - fence.size());
    }
    trim(&text);
  } else if (text.rfind(fence, 0) == 0) {
    text.erase(0, fence.size());
    trim(&text);
    if (text.size() >= fence.size() &&
        text.compare(text.size() - fence.size(), fence.size(), fence) == 0) {
      text.erase(text.size() - fence.size());
    }
    trim(&text);
  }

  const std::size_t first_brace = text.find('{');
  const std::size_t last_brace = text.rfind('}');
  if (first_brace != std::string::npos && last_brace != std::string::npos &&
      last_brace >= first_brace) {
    return text.substr(first_brace, last_brace - first_brace + 1);
  }
  return text;
}

GroundedResponderOutput parseGroundedResponderOutput(const std::string &text) {
  const std::string normalized = normalizeJsonPayload(text);
  const json parsed = json::parse(normalized, nullptr, false);
  if (parsed.is_discarded() || !parsed.is_object() ||
      !parsed.contains("final_answer") ||
      !parsed.at("final_answer").is_string()) {
    return {"", "", false};
  }

  std::string evidence_blob;
  if (parsed.contains("grounded_claims") &&
      parsed.at("grounded_claims").is_array()) {
    evidence_blob = parsed.at("grounded_claims").dump();
  }
  return {parsed.at("final_answer").get<std::string>(), evidence_blob, true};
}

double parseToolCallingThreshold(const std::string &plan_text) {
  if (plan_text.empty()) {
    return 0.0;
  }

  const json plan_json = json::parse(plan_text, nullptr, false);
  if (!plan_json.is_discarded() &&
      plan_json.contains("tool_calling_necessity")) {
    const json &necessity = plan_json.at("tool_calling_necessity");
    if (necessity.is_number()) {
      const double value = necessity.get<double>();
      if (value >= 0.0 && value <= 1.0) {
        return value;
      }
    }
  }

  std::istringstream parser(plan_text);
  double first_token_value = 0.0;
  if ((parser >> first_token_value) && first_token_value >= 0.0 &&
      first_token_value <= 1.0) {
    return first_token_value;
  }

  return 0.0;
}

void updateToolsFromPlannerPlan(const std::string &plan_text,
                                const std::vector<Tool> &available_tools,
                                std::vector<Tool> *tools_to_use,
                                std::vector<std::string> *justifications) {
  tools_to_use->clear();
  justifications->clear();
  if (plan_text.empty()) {
    return;
  }

  auto add_tool_if_known = [&](const std::string &name,
                               const std::string &reason) {
    for (const Tool &available : available_tools) {
      if (available.getToolName() == name) {
        tools_to_use->push_back(
            Tool(available.getToolName(), available.getToolDescription()));
        if (!reason.empty()) {
          justifications->push_back(reason);
        }
        return;
      }
    }
  };

  const json plan_json = json::parse(plan_text, nullptr, false);
  if (!plan_json.is_discarded() && plan_json.is_object() &&
      plan_json.contains("tools_to_use") &&
      plan_json.at("tools_to_use").is_array()) {
    for (const json::value_type &tool_entry : plan_json.at("tools_to_use")) {
      if (tool_entry.is_string()) {
        add_tool_if_known(tool_entry.get<std::string>(), "");
        continue;
      }
      if (!tool_entry.is_object()) {
        continue;
      }

      std::string tool_name = "";
      if (tool_entry.contains("tool_name") &&
          tool_entry.at("tool_name").is_string()) {
        tool_name = tool_entry.at("tool_name").get<std::string>();
      } else if (tool_entry.contains("name") &&
                 tool_entry.at("name").is_string()) {
        tool_name = tool_entry.at("name").get<std::string>();
      }
      if (tool_name.empty()) {
        continue;
      }

      std::string reason = "";
      if (tool_entry.contains("justification") &&
          tool_entry.at("justification").is_string()) {
        reason = tool_entry.at("justification").get<std::string>();
      } else if (tool_entry.contains("reason") &&
                 tool_entry.at("reason").is_string()) {
        reason = tool_entry.at("reason").get<std::string>();
      }
      add_tool_if_known(tool_name, reason);
    }
  }

  // Fallback: if planner format is loose text, infer tools by name mention.
  if (tools_to_use->empty()) {
    for (const Tool &available : available_tools) {
      if (plan_text.find(available.getToolName()) != std::string::npos) {
        tools_to_use->push_back(
            Tool(available.getToolName(), available.getToolDescription()));
      }
    }
  }
}

std::string summarizeShort(const std::string &convosum,
                           const std::string &curconversation,
                           const std::string &user_info, GeminiGenerator &g) {
  if (curconversation.empty()) {
    return convosum;
  }
  const std::string prompt = absl::StrCat(
      "Summarize this latest query cycle into a short rolling memory. Keep it "
      "compact and focused on immediate context, unresolved questions, and "
      "next "
      "actions.\n\nExisting short memory:\n",
      convosum, "\n\nBase user profile:\n", user_info, "\n\nLatest cycle:\n",
      curconversation, "\n\nReturn plain text only.");
  const absl::Status status = g.geminiGen(prompt, "lightweight");
  if (!status.ok()) {
    return absl::StrCat(convosum, "\n", curconversation);
  }
  const std::string extracted = extractJsonText(g.getContent());
  if (extracted.empty()) {
    return absl::StrCat(convosum, "\n", curconversation);
  }
  return extracted;
}

std::string summarizeLong(const std::string &convosum,
                          const std::string &curconversation,
                          const std::string &user_info, GeminiGenerator &g) {
  if (curconversation.empty()) {
    return convosum;
  }
  const std::string prompt = absl::StrCat(
      "Update the long-term advising memory. Preserve stable facts about the "
      "student (major/minor, constraints, interests, completed/planned "
      "courses) "
      "and remove transient details.\n\nExisting long memory:\n",
      convosum, "\n\nBase user profile:\n", user_info, "\n\nLatest cycle:\n",
      curconversation, "\n\nReturn plain text only.");
  const absl::Status status = g.geminiGen(prompt, "lightweight");
  if (!status.ok()) {
    return absl::StrCat(convosum, "\n", curconversation);
  }
  const std::string extracted = extractJsonText(g.getContent());
  if (extracted.empty()) {
    return absl::StrCat(convosum, "\n", curconversation);
  }
  return extracted;
}

bool parseDeciderDecision(std::string conent) {
  std::string parsed = extractJsonText(conent);
  if (parsed.empty()) {
    return false;
  }

  const json decision_json = json::parse(parsed, nullptr, false);
  if (decision_json.is_discarded() || !decision_json.contains("done") ||
      !decision_json.at("done").is_boolean()) {
    return false;
  }
  return decision_json.at("done").get<bool>();
}

absl::Status makePlanAsPdf(GeminiGenerator &g, std::string convo_s,
                           std::string convo_l) {
  std::ofstream plan("plan.md");
  if (!plan.is_open()) {
    return absl::InternalError("Failed to open plan.md for writing.");
  }

  const std::string prompt = absl::StrCat(
      "Write a finalized academic advising plan as valid markdown.\n"
      "Preserve markdown structure and include these exact sections:\n"
      "# Academic Plan\n"
      "## Student Profile\n"
      "## Recommended Course Path\n"
      "## Risks and Open Questions\n"
      "## Next Actions\n"
      "Use concise bullets where appropriate.\n"
      "Do not wrap output in code fences.\n\n"
      "Short-term conversation memory:\n",
      convo_s, "\n\nLong-term conversation memory:\n", convo_l);

  const absl::Status status = g.geminiGen(prompt, "lightweight");
  if (!status.ok()) {
    return status;
  }

  std::string markdown = extractJsonText(g.getContent());
  if (markdown.empty()) {
    markdown = "# Academic Plan\n\n"
               "## Student Profile\n"
               "- Unable to auto-generate profile summary.\n\n"
               "## Recommended Course Path\n"
               "- Unable to auto-generate recommendations.\n\n"
               "## Risks and Open Questions\n"
               "- Generation output was empty.\n\n"
               "## Next Actions\n"
               "- Re-run planning with more context.\n";
  }

  // Write line-by-line so Markdown layout is preserved exactly.
  std::istringstream in(markdown);
  std::string line;
  while (std::getline(in, line)) {
    plan << line << '\n';
  }
  plan.close();
  return absl::OkStatus();
}

} // namespace

chat_manager::chat_manager() {}
void chat_manager::clearTraceEvents() { trace_events.clear(); }
void chat_manager::launchMajorIngestionInBackground(std::ostream &o) {
  if (u_major_key.empty() || u_major_key == "none" || u_major_key == "unspecified") {
    return;
  }
  const fs::path project_root = resolveProjectRoot();
  major_ingest_status_file =
      (project_root / "data" / "major_ingest" /
       absl::StrCat(u_major_key, ".status.json"))
          .string();
  const fs::path script_path =
      project_root / "scripts" / "populate_major_in_background.sh";
  if (!fs::exists(script_path)) {
    o << "Warning: major ingestion script not found. Continuing without "
         "background population.\n";
    return;
  }

  const std::string command =
      absl::StrCat("bash '", shellEscapeSingleQuoted(script_path.string()),
                   "' '", shellEscapeSingleQuoted(u_major), "' >/dev/null 2>&1 &");
  const int launch_code = std::system(command.c_str());
  if (launch_code == 0) {
    o << "Started background major ingestion for '" << u_major
      << "'. Advising can continue while data is still populating.\n";
  } else {
    o << "Warning: failed to start background major ingestion. Continuing "
         "without it.\n";
  }
}

void chat_manager::addTraceEvent(const std::string &phase,
                                 const std::string &event_summary, bool success,
                                 int query_id,
                                 std::chrono::milliseconds latency,
                                 std::optional<std::string> tool_name,
                                 std::optional<std::string> evidence) {
  TraceEvent event;
  event.phase = phase;
  event.event_summary = event_summary;
  event.tool_name = tool_name;
  event.evidence = evidence;
  event.latency = latency;
  event.query_id = query_id;
  const std::chrono::system_clock::time_point now =
      std::chrono::system_clock::now();
  event.timestamp = static_cast<int>(
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch())
          .count());
  event.success = success;
  trace_events.push_back(event);
}
void chat_manager::loadTestProfilePreset() {
  u_advisor = "JaneDoe";
  u_major = "Undeclared";
  u_major_key = normalizeMajorKey(u_major);
  u_minor = "none";
  major_ingest_status_file.clear();

  user_info = absl::StrCat(
      "Student profile:\n",
      "- Name: Erick O\n",
      "- Class year: 2028\n",
      "- Advisor: ", u_advisor, "\n",
      "- Major: ", u_major, "\n",
      "- Major description: Temporary test preset profile for chat iteration.\n",
      "- Minor: ", u_minor, "\n",
      "- Completed courses:\n",
      "- Applied Python\n",
      "- Introduction to College Writing\n",
      "- Introduction to Economics\n",
      "- Calculus I\n",
      "- Current course:\n",
      "- Introductory Psychology\n",
      "- Desired courses:\n",
      "- Public Policy Analysis\n",
      "- Interests: interdisciplinary planning\n",
      "- Constraints: none\n",
      "- Goal: get further along in major\n");

  convo_longterm_history = user_info;
  convo_shortterm_history = user_info;
  curquery.clear();

  if (availible_tools.empty()) {
    availible_tools.push_back(
        Tool("scraper", "Fetches up-to-date page content from a URL."));
    availible_tools.push_back(
        Tool("retrieve_from_weaviate",
             "Retrieves relevant catalog/advising chunks from Weaviate."));
  }
}
std::string chat_manager::getUserInfo(std::istream &i, std::ostream &o) {
  if (!user_info.empty()) {
    o << "Loaded existing student profile.\n";
    return user_info;
  }

  auto ask = [&](const std::string &prompt) -> std::string {
    o << prompt;
    std::string answer;
    std::getline(i, answer);
    if (answer.empty()) {
      return "unspecified";
    }
    return answer;
  };

  const std::string student_name = ask("Student name/preferred name: ");
  const std::string class_year = ask("Class year (e.g. 2027): ");
  u_advisor = ask("Assigned advisor name/email: ");
  u_major = ask("Declared/intended major: ");
  u_major_key = normalizeMajorKey(u_major);
  launchMajorIngestionInBackground(o);
  u_minor = ask("Declared/intended minor (or 'none'): ");
  const std::string interests =
      ask("Academic interests (AI, systems, theory, etc.): ");
  const std::string constraints =
      ask("Constraints (time load, athletics, work, abroad, etc.): ");
  const std::string goals =
      ask("Primary advising goal for this semester/year: ");

  GeminiGenerator profile_llm;
  auto describe_course = [&](const std::string &course_name) -> std::string {
    absl::StatusOr<std::string> retrieved_or =
        retrieveFromWeaviate(course_name, u_major_key, major_ingest_status_file);
    std::string retrieved_context =
        retrieved_or.ok() ? *retrieved_or : "No retrieval context available.";
    const std::string prompt = absl::StrCat(
        "You are producing a short academic profile note.\n"
        "Given a course name and retrieval context, write a concise summary "
        "with:\n"
        "1) what the course is about (1 sentence)\n"
        "2) likely prerequisites (short phrase or list)\n"
        "If prerequisites are unclear, say \"prerequisites unclear\".\n"
        "Assume the student has completed all prerequisites.\n"
        "Return plain text only.\n\n"
        "Course name: ",
        course_name, "\n\nRetrieved context:\n", retrieved_context);
    const absl::Status llm_status =
        profile_llm.geminiGen(prompt, "lightweight");
    if (!llm_status.ok()) {
      return "Description unavailable; prerequisites unclear.";
    }
    const std::string extracted = extractJsonText(profile_llm.getContent());
    if (extracted.empty()) {
      return "Description unavailable; prerequisites unclear.";
    }
    return extracted;
  };
  auto describe_major = [&](const std::string &major_name) -> std::string {
    if (major_name == "unspecified" || major_name == "none") {
      return "No major provided.";
    }
    absl::StatusOr<std::string> retrieved_or =
        retrieveFromWeaviate(major_name, u_major_key, major_ingest_status_file);
    std::string retrieved_context =
        retrieved_or.ok() ? *retrieved_or : "No retrieval context available.";
    const std::string prompt = absl::StrCat(
        "You are producing a short major profile note.\n"
        "Given a major name and retrieval context, write 2-3 concise sentences "
        "covering:\n"
        "1) what this major focuses on\n"
        "2) typical core themes/courses a student should expect\n"
        "Return plain text only.\n\n"
        "Major name: ",
        major_name, "\n\nRetrieved context:\n", retrieved_context);
    const absl::Status llm_status =
        profile_llm.geminiGen(prompt, "lightweight");
    if (!llm_status.ok()) {
      return "Major description unavailable.";
    }
    const std::string extracted = extractJsonText(profile_llm.getContent());
    if (extracted.empty()) {
      return "Major description unavailable.";
    }
    return extracted;
  };

  auto collect_courses = [&](const std::string &bucket_name,
                             bool single_course_only) -> std::string {
    std::string bucket_details;
    int accepted_count = 0;
    while (true) {
      o << "Enter a " << bucket_name << " course name (or type 'done'): ";
      std::string course_name;
      std::getline(i, course_name);
      if (course_name == "done") {
        break;
      }
      if (course_name.empty()) {
        o << "Please enter a course name or 'done'.\n";
        continue;
      }
      if (single_course_only && accepted_count >= 1) {
        o << "Colorado College students take one current course at a time. "
             "Type 'done' to continue.\n";
        continue;
      }

      const std::string description = describe_course(course_name);
      bucket_details = absl::StrCat(bucket_details, "- ", course_name, ": ",
                                    description, "\n");
      ++accepted_count;
    }

    if (bucket_details.empty()) {
      bucket_details = "- none provided\n";
    }
    return bucket_details;
  };

  const std::string completed_courses =
      collect_courses("completed", /*single_course_only=*/false);
  const std::string current_course =
      collect_courses("current", /*single_course_only=*/true);
  const std::string target_courses =
      collect_courses("desired", /*single_course_only=*/false);
  const std::string major_description = describe_major(u_major);

  user_info = absl::StrCat(
      "Student profile:\n", "- Name: ", student_name, "\n",
      "- Class year: ", class_year, "\n", "- Advisor: ", u_advisor, "\n",
      "- Major: ", u_major, "\n", "- Major description: ", major_description,
      "\n", "- Minor: ", u_minor, "\n", "- Completed courses:\n",
      completed_courses, "- Current course:\n", current_course,
      "- Desired courses:\n", target_courses, "- Interests: ", interests, "\n",
      "- Constraints: ", constraints, "\n", "- Goal: ", goals, "\n");

  // Seed both memories with the initial profile so planner/responder calls
  // always have baseline student context.
  convo_longterm_history = user_info;
  convo_shortterm_history = user_info;
  curquery.clear();

  if (availible_tools.empty()) {
    availible_tools.push_back(
        Tool("scraper", "Fetches up-to-date page content from a URL."));
    availible_tools.push_back(
        Tool("retrieve_from_weaviate",
             "Retrieves relevant catalog/advising chunks from Weaviate."));
  }

  o << "\nProfile captured. Starting advising chat.\n";
  return user_info;
}

std::string chat_manager::plannerPrompt() {
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
      "User profile:\n", user_info, "\n\nHere is the human's prompt:");

  return planner_prompt;
}

std::string chat_manager::orchPrompt(std::vector<Tool> tools,
                                     std::vector<std::string> justifications) {
  std::string prompt =
      "You are an orchestrator agent. Convert the planner output into concrete "
      "tool calls.\n"
      "Return JSON only with this exact schema:\n"
      "{\"tool_calls\":[{\"tool_name\":\"...\",\"tool_description\":\"...\","
      "\"args\":{...},\"reason\":\"...\"}]}\n"
      "Rules:\n"
      "- Use only tools listed below.\n"
      "- Keep args minimal and valid for each tool.\n"
      "- If no tool is needed, return {\"tool_calls\":[]}.\n\n"
      "User profile:\n";

  prompt = absl::StrCat(prompt, user_info, "\n\nAvailable tools:\n");
  for (const Tool &tool : tools) {
    prompt = absl::StrCat(prompt, "- ", tool.getToolName(), ": ",
                          tool.getToolDescription(), "\n");
  }

  if (!justifications.empty()) {
    prompt = absl::StrCat(prompt, "\nPlanner justifications:\n");
    for (const std::string &reason : justifications) {
      prompt = absl::StrCat(prompt, "- ", reason, "\n");
    }
  }

  prompt = absl::StrCat(prompt, "\nCurrent user query:\n", curquery);
  return prompt;
}

std::string chat_manager::observerPrompt(std::vector<ToolCallingLogs> logs) {
  std::string prompt =
      "You are an observer judge. Decide whether another tool call is needed.\n"
      "Respond in JSON: {\"observer_decision\": bool, \"planner_retry\": "
      "bool}\n"
      "Tool logs:\n";
  for (const ToolCallingLogs &log : logs) {
    prompt = absl::StrCat(prompt, "- ", log.toolname,
                          " | success=", log.success ? "true" : "false",
                          " | latency_ms=", log.latency_ms, " | ", log.summary,
                          "\n");
  }
  return prompt;
}

std::string chat_manager::responderPrompt(std::string curquery) {
  return absl::StrCat(
      "You are a concise academic advising assistant that must stay grounded.\n"
      "Respond directly without tool calls when confidence is high.\n"
      "If uncertain, ask one clarifying question.\n\n"
      "Return JSON only in this exact schema:\n"
      "{\n"
      "  \"final_answer\": \"...\",\n"
      "  \"grounded_claims\": [\n"
      "    {\n"
      "      \"claim\": \"...\",\n"
      "      \"evidence\": [\"source_url or source_path snippet\", \"...\"]\n"
      "    }\n"
      "  ],\n"
      "  \"uncertainties\": [\"...\"]\n"
      "}\n"
      "Rules:\n"
      "- Use only grounded evidence from provided context and memory.\n"
      "- If evidence is missing, state uncertainty instead of guessing.\n\n"
      "User profile:\n",
      user_info, "\n\nShort-term memory:\n", convo_shortterm_history,
      "\n\nLong-term memory:\n", convo_longterm_history,
      "\n\nCurrent user query:\n", curquery);
}

std::string chat_manager::deciderPrompt() {
  return absl::StrCat(
      "You are a completion judge for an advising workflow.\n"
      "Decide if the conversation has enough information to produce a final "
      "plan.\n"
      "Return JSON only in this exact shape:\n"
      "{\"done\": true|false, \"reason\": \"short reason\"}\n\n"
      "User profile:\n",
      user_info, "\n\nShort-term memory:\n", convo_shortterm_history,
      "\n\nLong-term memory:\n", convo_longterm_history, "\n\nCurrent query:\n",
      curquery);
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

std::string chat_manager::getSummarization() { return convo_shortterm_history; }

absl::Status chat_manager::chat(std::istream &i, std::ostream &o) {
  // Never really return an error if something goes wrong
  // just have it ask the user to escalate
  // e.g. of a possible error: hits a tool call or
  // latency or retry threshold.
  GeminiGenerator g;
  getUserInfo(i, o);
  clearTraceEvents();
  absl::Status curstatus = absl::OkStatus();
  int query_number = 0;
  // Add logs for each of these steps
  while (curstatus.ok()) {
    o << "\nYou: " << std::flush;
    if (!std::getline(i, curquery)) {
      o << "\nInput stream closed. Ending chat.\n";
      addTraceEvent("input", "Input stream closed.", true, query_number,
                    std::chrono::milliseconds(0),
                    std::optional<std::string>());
      break;
    }
    if (curquery.empty()) {
      o << "Please enter a question for the advisor.\n";
      addTraceEvent("input", "Skipped empty user query.", false, query_number,
                    std::chrono::milliseconds(0),
                    std::optional<std::string>());
      continue;
    }
    std::string curconversation = "";
    ++query_number;
    curconversation =
        absl::StrCat("Query #", query_number, "\n", curquery, "\n");
    // planner part
    //  decides where a tool call is neccessary or
    // just needs to respond or something.
    // we will use gemini 3.0 pro for this
    o << "Thinking (planner)... " << std::flush;
    std::chrono::steady_clock::time_point starttime =
        std::chrono::steady_clock::now();
    std::string planner_prompt = plannerPrompt();
    curstatus = g.geminiGen(absl::StrCat(planner_prompt, "\n", curquery));
    std::chrono::steady_clock::time_point endtime =
        std::chrono::steady_clock::now();
    const std::chrono::milliseconds thinkingtime =
        std::chrono::duration_cast<std::chrono::milliseconds>(endtime -
                                                               starttime);
    if (!curstatus.ok()) {
      addTraceEvent("planner", absl::StrCat("Planner failed: ", curstatus),
                    false, query_number, thinkingtime,
                    std::optional<std::string>());
      return curstatus;
    }
    std::string plan = parsePlan(g.getContent());
    curconversation =
        absl::StrCat(curconversation, "Planned to do ", plan, "\n");
    const double tool_calling_threshold = parseToolCallingThreshold(plan);
    std::vector<Tool> tools_to_use;
    std::vector<std::string> explaination_for_use;
    updateToolsFromPlannerPlan(plan, availible_tools, &tools_to_use,
                               &explaination_for_use);
    const long long thinkingtime_ms = thinkingtime.count();
    addTraceEvent("planner", "Planner generated plan.", true, query_number,
                  thinkingtime, std::optional<std::string>());
    o << "done (" << thinkingtime_ms << " ms)\n" << std::flush;
    // sends plan to orchestrator which handles the tool calls
    // while loop for orchestrator//observer
    // this only happens if the planner deemed the tool call
    // neccessary
    if (tool_calling_threshold >= kToolCallThreshold) {
      bool observerDecision = true;
      bool plannerRetry = false;
      int curToolCalls = 0;
      while (observerDecision && curToolCalls <= kMaxToolcalls) {
        bool latencyExceededThisCycle = false;
        if (plannerRetry) {
          // set up the planning agent again.
          std::string planner_prompt = plannerPrompt();
          curstatus = g.geminiGen(absl::StrCat(planner_prompt, "\n", curquery));
          if (!curstatus.ok()) {
            addTraceEvent("planner_retry",
                          absl::StrCat("Planner retry failed: ", curstatus),
                          false, query_number, std::chrono::milliseconds(0),
                          std::optional<std::string>());
            return curstatus;
          }
          plan = parsePlan(g.getContent());
          updateToolsFromPlannerPlan(plan, availible_tools, &tools_to_use,
                                     &explaination_for_use);
          curconversation =
              absl::StrCat(curconversation, "Planned to do ", plan, "\n");
          addTraceEvent("planner_retry", "Planner retry regenerated plan.", true,
                        query_number, std::chrono::milliseconds(0),
                        std::optional<std::string>());
        }
        std::string orchestrator_prompt =
            orchPrompt(tools_to_use, explaination_for_use);
        o << "Orchestrating tool calls... " << std::flush;
        const std::chrono::steady_clock::time_point orchestrator_started =
            std::chrono::steady_clock::now();
        absl::Status err = g.geminiGen(orchestrator_prompt);
        const std::chrono::steady_clock::time_point orchestrator_ended =
            std::chrono::steady_clock::now();
        const std::chrono::milliseconds orchestrator_latency =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                orchestrator_ended - orchestrator_started);
        if (!err.ok()) {
          addTraceEvent("orchestrator",
                        absl::StrCat("Orchestrator failed: ", err), false,
                        query_number, orchestrator_latency,
                        std::optional<std::string>());
          return err;
        }
        addTraceEvent("orchestrator", "Orchestrator produced tool calls.", true,
                      query_number, orchestrator_latency,
                      std::optional<std::string>());
        o << "done (" << orchestrator_latency.count() << " ms)\n"
          << std::flush;
        if (orchestrator_latency > kMaxLatencyDelay) {
          latencyExceededThisCycle = true;
          curconversation =
              absl::StrCat(curconversation, "called tool: orchestrator -> ",
                           "Latency exceeded ", orchestrator_latency.count(),
                           "ms (limit ", kMaxLatencyDelay.count(), "ms)\n");
        }
        std::vector<Tool> order = parseOrchResultForTools(g.getContent());
        std::vector<ToolCallingLogs> logs;
        for (Tool &tool : order) {
          if (curToolCalls >= kMaxToolcalls) {
            break;
          }
          o << "Running tool: " << tool.getToolName() << "...\n" << std::flush;

          ToolCallingLogs log_entry;
          std::optional<std::string> tool_evidence;
          log_entry.toolname = tool.getToolName();
          log_entry.success = false;
          log_entry.summary = "Tool not executed.";
          log_entry.latency_ms = 0;
          const std::chrono::steady_clock::time_point started =
              std::chrono::steady_clock::now();

          if (!tool.hasInvocation()) {
            log_entry.summary = "Skipped: missing invocation payload.";
            const std::chrono::steady_clock::time_point ended =
                std::chrono::steady_clock::now();
            log_entry.latency_ms = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                      started)
                    .count());
            logs.push_back(log_entry);
            addTraceEvent("tool", log_entry.summary, false, query_number,
                          std::chrono::milliseconds(log_entry.latency_ms),
                          std::optional<std::string>(tool.getToolName()));
            ++curToolCalls;
            continue;
          }

          const json &args = tool.getInvocationArgs();
          if (tool.getToolName() == "scraper") {
            if (!args.contains("url") || !args.at("url").is_string()) {
              log_entry.summary = "Skipped: scraper call missing string url.";
            } else {
              Scraper scraper(true);
              absl::StatusOr<std::string> scrape_or =
                  scraper.scrapeFromUrl(args.at("url").get<std::string>());
              if (scrape_or.ok()) {
                log_entry.success = true;
                log_entry.summary = absl::StrCat(
                    "Scraper succeeded. Response size=", scrape_or->size(),
                    " chars.");
              } else {
                log_entry.summary =
                    absl::StrCat("Scraper failed: ", scrape_or.status());
              }
            }
          } else if (tool.getToolName() == "retrieve_from_weaviate") {
            if (!args.contains("query") || !args.at("query").is_string()) {
              log_entry.summary =
                  "Skipped: retrieve_from_weaviate missing string query.";
            } else {
              absl::StatusOr<std::string> retrieval_or =
                  retrieveFromWeaviate(args.at("query").get<std::string>(),
                                       u_major_key, major_ingest_status_file);
              if (retrieval_or.ok()) {
                log_entry.success = true;
                log_entry.summary = absl::StrCat(
                    "Weaviate retrieval + rerank succeeded. Output size=",
                    retrieval_or->size(), " chars.");
                tool_evidence = *retrieval_or;
              } else {
                log_entry.summary = absl::StrCat("Weaviate retrieval failed: ",
                                                 retrieval_or.status());
              }
            }
          } else {
            log_entry.summary = absl::StrCat("Skipped: unknown tool '",
                                             tool.getToolName(), "'.");
          }

          const std::chrono::steady_clock::time_point ended =
              std::chrono::steady_clock::now();
          log_entry.latency_ms = static_cast<int>(
              std::chrono::duration_cast<std::chrono::milliseconds>(ended -
                                                                    started)
                  .count());
          if (log_entry.latency_ms > kMaxLatencyDelay.count()) {
            latencyExceededThisCycle = true;
            log_entry.success = false;
            log_entry.summary = absl::StrCat(
                log_entry.summary, " | latency exceeded ", log_entry.latency_ms,
                "ms (limit ", kMaxLatencyDelay.count(), "ms)");
          }
          logs.push_back(log_entry);
          addTraceEvent("tool", log_entry.summary, log_entry.success,
                        query_number,
                        std::chrono::milliseconds(log_entry.latency_ms),
                        std::optional<std::string>(tool.getToolName()),
                        tool_evidence);
          curconversation =
              absl::StrCat(curconversation, "called tool: ", tool.getToolName(),
                           " -> ", log_entry.summary, "\n");
          ++curToolCalls;
        }
        // After this happens, give the logs to an observer judge

        std::string observer_prompt = observerPrompt(logs);
        o << "Evaluating results (observer)... " << std::flush;
        const std::chrono::steady_clock::time_point observer_started =
            std::chrono::steady_clock::now();
        err = g.geminiGen(observer_prompt);
        const std::chrono::steady_clock::time_point observer_ended =
            std::chrono::steady_clock::now();
        const std::chrono::milliseconds observer_latency =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                observer_ended - observer_started);
        if (!err.ok()) {
          addTraceEvent("observer", absl::StrCat("Observer failed: ", err),
                        false, query_number, observer_latency,
                        std::optional<std::string>());
          return err;
        }
        addTraceEvent("observer", "Observer evaluated tool outcomes.", true,
                      query_number, observer_latency,
                      std::optional<std::string>());
        o << "done (" << observer_latency.count() << " ms)\n" << std::flush;
        if (observer_latency > kMaxLatencyDelay) {
          latencyExceededThisCycle = true;
          curconversation =
              absl::StrCat(curconversation, "called tool: observer -> ",
                           "Latency exceeded ", observer_latency.count(),
                           "ms (limit ", kMaxLatencyDelay.count(), "ms)\n");
        }
        std::pair<bool, bool> observerResults =
            parseObserverDecision(g.getContent());

        observerDecision = observerResults.first || latencyExceededThisCycle;
        plannerRetry = observerResults.second || latencyExceededThisCycle;
        // should probably do two things, see if this reached the desired
        //  result, if not, lets ot back to the planning stages
      }
      // summarize the conversation be like: heres what I did.
      // format:
      // Query #X
      // query
      // Planned to do blah
      // called tool: blah
      // end of current query
      curconversation = absl::StrCat(curconversation, "end of current query\n");
      o << "Updating memory... " << std::flush;
      const std::chrono::steady_clock::time_point memory_started =
          std::chrono::steady_clock::now();
      convo_longterm_history =
          summarizeLong(convo_longterm_history, curconversation, user_info, g);
      convo_shortterm_history = summarizeShort(convo_shortterm_history,
                                               curconversation, user_info, g);
      const std::chrono::steady_clock::time_point memory_ended =
          std::chrono::steady_clock::now();
      const std::chrono::milliseconds memory_latency =
          std::chrono::duration_cast<std::chrono::milliseconds>(memory_ended -
                                                                 memory_started);
      addTraceEvent("memory", "Updated long and short summaries.", true,
                    query_number, memory_latency,
                    std::optional<std::string>());
      o << "done (" << memory_latency.count() << " ms)\n" << std::flush;

      // another agent call: heres what I did.
      o << getSummarization() << "\n";
    } else {
      // Responder agent.
      //  if we do nothing we should respond
      std::string responder_prompt = responderPrompt(curquery);
      o << "Drafting response... " << std::flush;
      const std::chrono::steady_clock::time_point responder_started =
          std::chrono::steady_clock::now();
      curstatus = g.geminiGen(responder_prompt);
      const std::chrono::steady_clock::time_point responder_ended =
          std::chrono::steady_clock::now();
      const std::chrono::milliseconds responder_latency =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              responder_ended - responder_started);
      if (!curstatus.ok()) {
        addTraceEvent("responder",
                      absl::StrCat("Responder failed: ", curstatus), false,
                      query_number, responder_latency,
                      std::optional<std::string>());
        return curstatus;
      }
      o << "done (" << responder_latency.count() << " ms)\n" << std::flush;
      const std::string responder_text = extractJsonText(g.getContent());
      const GroundedResponderOutput grounded =
          parseGroundedResponderOutput(responder_text);
      if (grounded.parsed) {
        o << "Advisor: " << grounded.final_answer << "\n";
        addTraceEvent("responder", "Responder drafted grounded answer.", true,
                      query_number, responder_latency,
                      std::optional<std::string>(),
                      grounded.evidence_blob.empty()
                          ? std::optional<std::string>()
                          : std::optional<std::string>(grounded.evidence_blob));
      } else if (!responder_text.empty()) {
        o << "Advisor: " << responder_text << "\n";
        addTraceEvent("responder",
                      "Responder output was not grounded JSON schema.", false,
                      query_number, responder_latency,
                      std::optional<std::string>(),
                      std::optional<std::string>(responder_text));
      } else {
        const std::string raw = g.getContent();
        o << "Advisor: " << raw << "\n";
        addTraceEvent("responder",
                      "Responder output parse failed; raw content returned.",
                      false, query_number, responder_latency,
                      std::optional<std::string>(),
                      std::optional<std::string>(raw));
      }
      curconversation = absl::StrCat(
          curconversation, "Planned to do respond directly without tools.\n",
          "end of current query\n");

      o << "Updating memory... " << std::flush;
      const std::chrono::steady_clock::time_point memory_started =
          std::chrono::steady_clock::now();
      convo_longterm_history =
          summarizeLong(convo_longterm_history, curconversation, user_info, g);
      convo_shortterm_history = summarizeShort(convo_shortterm_history,
                                               curconversation, user_info, g);
      const std::chrono::steady_clock::time_point memory_ended =
          std::chrono::steady_clock::now();
      const std::chrono::milliseconds memory_latency =
          std::chrono::duration_cast<std::chrono::milliseconds>(memory_ended -
                                                                 memory_started);
      addTraceEvent("memory", "Updated long and short summaries.", true,
                    query_number, memory_latency,
                    std::optional<std::string>());
      o << "done (" << memory_latency.count() << " ms)\n" << std::flush;
      // Summarizer summarizes this conversation twice: in the conte
      // xt of the whole thing and in the short term conversation
    }
    // finally, the decider judge, decides from the content if a proper plan
    //  cna be made and there is no ambiguity in a list of things.

    std::string decider_prompt = deciderPrompt();
    const std::chrono::steady_clock::time_point decider_started =
        std::chrono::steady_clock::now();
    curstatus = g.geminiGen(decider_prompt);
    const std::chrono::steady_clock::time_point decider_ended =
        std::chrono::steady_clock::now();
    const std::chrono::milliseconds decider_latency =
        std::chrono::duration_cast<std::chrono::milliseconds>(decider_ended -
                                                               decider_started);
    if (!curstatus.ok()) {
      addTraceEvent("decider", absl::StrCat("Decider failed: ", curstatus),
                    false, query_number, decider_latency,
                    std::optional<std::string>());
      return curstatus;
    }
    std::string decider_result = g.getContent();
    bool done = parseDeciderDecision(decider_result);
    addTraceEvent("decider",
                  done ? "Decider marked workflow complete."
                       : "Decider requested another round.",
                  true, query_number, decider_latency,
                  std::optional<std::string>());

    if (done) {
      curstatus =
          makePlanAsPdf(g, convo_shortterm_history, convo_longterm_history);
      addTraceEvent("plan_generation",
                    curstatus.ok() ? "Generated final plan markdown."
                                   : "Failed to generate final plan markdown.",
                    curstatus.ok(), query_number, std::chrono::milliseconds(0),
                    std::optional<std::string>());
      if (!curstatus.ok()) {
        return curstatus;
      }
      break;
    }
  }
  return absl::OkStatus();
  // end condition: When we finalize the plan for the user
}

// What I am missing:
// Need to pull data from weaviate