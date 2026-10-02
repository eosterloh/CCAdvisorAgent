#include "absl/status/status.h"
#include "absl/strings/str_cat.h"
#include "app/common/types.hpp"
#include "app/geminiclient/gemini_embedding.hpp"
#include "app/weaviate/weaviate_port.hpp"

#include <iostream>
#include <string>
#include <vector>

int main() {
  struct CatalogSeed {
    std::string code;
    std::string title;
    std::string url;
    std::string text;
  };

  const std::vector<CatalogSeed> seeds = {
      {"CP341", "Topics in Computer Science: Applied AI",
       "https://coursecatalog.coloradocollege.edu/courses/CP341",
       "Topics in Computer Science: Applied AI. Upper-division CS elective on "
       "applied machine learning and AI systems. Prerequisites typically include "
       "Data Structures and Algorithms plus programming maturity. Official course "
       "descriptions emphasize projects, models, and evaluation."},
      {"CP222", "Data Structures and Algorithms",
       "https://coursecatalog.coloradocollege.edu/courses/CP222",
       "Data Structures and Algorithms. Core CS course covering lists, trees, "
       "graphs, and algorithmic analysis. Prerequisite for many upper-level CS "
       "and AI courses."},
      {"CP275", "Design and Analysis of Algorithms",
       "https://coursecatalog.coloradocollege.edu/courses/CP275",
       "Design and Analysis of Algorithms. Foundational course building on Data "
       "Structures. Essential for advanced CS topics including many AI areas."},
      {"CP274", "Theory of Computation",
       "https://coursecatalog.coloradocollege.edu/courses/CP274",
       "Theory of Computation. Automata, computability, and complexity. Helps "
       "with advanced AI theory classes that discuss computational limits."},
      {"MA117", "Introduction to Probability and Statistics",
       "https://coursecatalog.coloradocollege.edu/courses/MA117",
       "Introduction to Probability and Statistics. Mathematical foundation for "
       "modern AI and machine learning. Balances a heavy CS workload."},
      {"CP215", "Computer Organization",
       "https://coursecatalog.coloradocollege.edu/courses/CP215",
       "Computer Organization. Systems-oriented core covering architecture and "
       "low-level execution."},
      {"CP255", "Software Design",
       "https://coursecatalog.coloradocollege.edu/courses/CP255",
       "Software Design. Team-based software engineering practices."},
      {"CP341B", "Database Systems",
       "https://coursecatalog.coloradocollege.edu/courses/CP341B",
       "Database Systems. Practical applied CS course that complements systems "
       "interest without being as intense as pairing Applied AI with Algorithms."}};

  GeminiEmbedding embedder;
  weaviateClient client;
  int stored = 0;
  for (const CatalogSeed &seed : seeds) {
    const std::string chunk = absl::StrCat(
        "[source] ", seed.url, "\n[content] ", seed.text, "\n");
    const absl::Status embed_status = embedder.embed(chunk);
    if (!embed_status.ok()) {
      std::cerr << "embed failed for " << seed.code << ": "
                << embed_status.ToString() << '\n';
      return 1;
    }
    absl::StatusOr<EmbeddedRecord> record_or = embedder.getContent();
    if (!record_or.ok()) {
      std::cerr << record_or.status().ToString() << '\n';
      return 1;
    }
    record_or->source_url = seed.url;
    record_or->source_path = "seed://computer_science";
    record_or->major_key = "computer_science";
    record_or->major_name = "Computer Science";
    record_or->course_code = seed.code;
    record_or->course_title = seed.title;
    record_or->chunk_text = seed.text;
    const absl::Status put = client.embed(*record_or);
    if (!put.ok()) {
      std::cerr << "weaviate embed failed for " << seed.code << ": "
                << put.ToString() << '\n';
      return 1;
    }
    ++stored;
    std::cout << "Seeded " << seed.code << '\n';
  }
  std::cout << "Seeded " << stored << " Computer Science catalog chunks.\n";
  return 0;
}
