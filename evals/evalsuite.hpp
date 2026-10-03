#ifndef EVALSUITE
#define EVALSUITE

#include <string>

struct EvalSuiteOutcome {
  std::string summary;
  int passed = 0;
  int total = 0;
};

EvalSuiteOutcome RunEvalSuite();

#endif
