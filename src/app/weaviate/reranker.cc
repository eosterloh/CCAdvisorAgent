#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/geminiclient/gemini_generation.hpp"
#include "app/weaviate/reranker.hpp"

#include <absl/status/statusor.h>
#include <nlohmann/json.hpp>
#include <sstream>

absl::StatusOr<std::string> Reranker::rerank(std::string_view retrieved,
                                             std::string_view query) {
  GeminiGenerator g;
  const std::string prompt = absl::StrCat(
      "You are a reranker for academic advising retrieval.\n"
      "Given a user query and retrieved candidate text, rank the candidate "
      "chunks by relevance to the query.\n"
      "Prioritize exact course codes, prerequisites, major/minor "
      "requirements, and policy constraints.\n"
      "Do not invent facts. Use only provided candidate text.\n\n"
      "Return JSON only in this exact schema:\n"
      "{\n"
      "  \"ranked\": [\n"
      "    {\"index\": 0, \"score\": 0.0, \"reason\": \"...\"}\n"
      "  ],\n"
      "  \"top_summary\": \"1-2 sentence grounded summary\"\n"
      "}\n\n"
      "Scoring guidance:\n"
      "- score in [0.0, 1.0]\n"
      "- higher is more relevant\n"
      "- penalize generic/off-topic chunks\n\n"
      "User query:\n",
      query, "\n\nRetrieved candidates:\n", retrieved);
  absl::Status err = g.geminiGen(prompt);
  if (!err.ok()) {
    return err;
  }

  using json = nlohmann::json;
  const json outer = json::parse(g.getContent(), nullptr, false);
  if (outer.is_discarded() || !outer.contains("candidates") ||
      !outer["candidates"].is_array() || outer["candidates"].empty()) {
    return absl::InternalError("Reranker returned invalid outer JSON.");
  }

  const json &candidate = outer["candidates"].at(0);
  if (!candidate.contains("content") || !candidate["content"].is_object() ||
      !candidate["content"].contains("parts") ||
      !candidate["content"]["parts"].is_array() ||
      candidate["content"]["parts"].empty() ||
      !candidate["content"]["parts"].at(0).contains("text") ||
      !candidate["content"]["parts"].at(0)["text"].is_string()) {
    return absl::InternalError("Reranker response missing content text.");
  }

  const std::string reranker_json_text =
      candidate["content"]["parts"].at(0)["text"].get<std::string>();
  const json reranked = json::parse(reranker_json_text, nullptr, false);
  if (reranked.is_discarded()) {
    return absl::InternalError("Reranker content was not valid JSON.");
  }

  std::ostringstream out;
  if (reranked.contains("top_summary") && reranked["top_summary"].is_string()) {
    out << "Summary: " << reranked["top_summary"].get<std::string>() << '\n';
  }

  if (!reranked.contains("ranked") || !reranked["ranked"].is_array()) {
    return out.str();
  }

  out << "Ranked Results:\n";
  for (const json::value_type &item : reranked["ranked"]) {
    if (!item.is_object()) {
      continue;
    }
    const int idx = item.contains("index") && item["index"].is_number_integer()
                        ? item["index"].get<int>()
                        : -1;
    const double score = item.contains("score") && item["score"].is_number()
                             ? item["score"].get<double>()
                             : 0.0;
    const std::string reason =
        item.contains("reason") && item["reason"].is_string()
            ? item["reason"].get<std::string>()
            : "";
    out << "- index=" << idx << ", score=" << score;
    if (!reason.empty()) {
      out << ", reason=" << reason;
    }
    out << '\n';
  }
  return out.str();
}