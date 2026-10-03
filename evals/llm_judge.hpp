#ifndef EVAL_LLM_JUDGE
#define EVAL_LLM_JUDGE

#include "reporting.hpp"
#include <string>
#include <string_view>

EvalMetricResult RunLlmJudge(std::string_view query,
                             std::string_view advisor_text,
                             std::string_view evidence);

#endif
