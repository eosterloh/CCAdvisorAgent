#ifndef TOOLS
#define TOOLS
#include <string>

class Tool {
private:
  std::string toolDescription;

public:
  std::string getToolDescription();
};

#endif