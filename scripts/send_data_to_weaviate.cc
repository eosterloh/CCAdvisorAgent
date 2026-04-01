#include "app/geminiclient/gemini_embedding.hpp"
#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/weaviate/weaviate_port.hpp"
#include <algorithm>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

std::vector<std::string> get_file_names(const fs::path &path) {
  std::vector<std::string> file_names;
  if (!fs::exists(path) || !fs::is_directory(path)) {
    return file_names;
  }

  for (const fs::directory_entry &entry : fs::directory_iterator(path)) {
    if (entry.is_regular_file() && entry.path().extension() == ".jsonl") {
      file_names.push_back(entry.path().filename().string());
    }
  }

  std::sort(file_names.begin(), file_names.end());
  return file_names;
}

int main() {
  std::string dir = "data/rawscrape";
  fs::path dir_path = dir;
  const std::vector<std::string> file_names = get_file_names(dir_path);
  if (file_names.empty()) {
    std::cerr << "No .jsonl files found in " << dir << '\n';
    return 1;
  }
  GeminiEmbedding e{};
  weaviateClient w{};

  for (const std::string &name : file_names) {
    const fs::path full_path = dir_path / name;
    absl::Status err = e.embedFile(full_path.string());
    if (!err.ok()) {
      std::cerr << absl::StrCat("Error embedding file ", name, ": ",
                                err.ToString(), "\n");
      return 1;
    }

    while (true) {
      absl::StatusOr<EmbeddedRecord> record = e.getContent();
      if (!record.ok()) {
        break;
      }

      err = w.embed(*record);
      if (!err.ok()) {
        std::cerr << absl::StrCat("Error sending embedded record from ", name,
                                  " to Weaviate: ", err.ToString(), "\n");
        return 1;
      }
    }
  }

  return 0;
}
