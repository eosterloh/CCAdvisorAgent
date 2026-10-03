#include "evalsuite.hpp"

#include <iostream>

int main() {
  const EvalSuiteOutcome outcome = RunEvalSuite();
  std::cout << outcome.summary << '\n';
  if (outcome.total == 0) {
    return 1;
  }
  return outcome.passed == outcome.total ? 0 : 1;
}
