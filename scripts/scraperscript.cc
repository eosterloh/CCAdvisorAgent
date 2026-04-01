#include "../include/app/toolcalling/scraper_tool.hpp"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include <absl/status/statusor.h>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

std::vector<std::string> getUrlsFromCoursePage(const std::string &r) {
  std::vector<std::string> urls;
  std::size_t pos = 0;

  while (true) {
    const std::size_t link_open = r.find("](", pos);
    if (link_open == std::string::npos) {
      break;
    }

    const std::size_t url_start = link_open + 2;
    const std::size_t url_end = r.find(')', url_start);
    if (url_end == std::string::npos) {
      break;
    }

    const std::string candidate = r.substr(url_start, url_end - url_start);
    if (candidate.compare(0, 8, "https://") == 0 ||
        candidate.compare(0, 7, "http://") == 0) {
      urls.push_back(candidate);
    }

    pos = url_end + 1;
  }

  return urls;
}

int main(int argc, char *argv[]) {
  std::string url;
  std::string dest;
  if (argc == 3) {
    url = argv[1];
    dest = argv[2];
  } else {
    return 1;
  }
  Scraper s{true};
  absl::StatusOr<std::string> res = s.scrapeFromUrl(url);
  if (!res.ok()) {
    std::cerr << res.status() << '\n';
    return 1;
  }

  std::ofstream outfile(dest, std::ios::out);
  // note: be aware that course scraping takes about 10-15 mins
  // per major
  if (dest.find("courses") != std::string::npos) {
    std::vector<std::string> urlsToSearch = getUrlsFromCoursePage(*res);
    if (outfile.is_open()) {
      for (size_t i = 0; i < urlsToSearch.size(); ++i) {
        absl::StatusOr<std::string> course_scraped =
            s.scrapeFromUrl(urlsToSearch.at(i));
        if (!course_scraped.ok()) {
          std::cout << "Error scraping file: " << urlsToSearch.at(i);
          return 1;
        }
        outfile << *course_scraped << '\n';
      }
      return 0;
    } else {
      std::cerr << "failed to open output file: " << dest << '\n';
    }
  } else {
    // if its not a course list, we just want to return the original data.
    if (outfile.is_open()) {
      outfile << *res << '\n';
    } else {
      std::cerr << "failed to open output file: " << dest << '\n';
      return 1;
    }
  }
  return 0;
}