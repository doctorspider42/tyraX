#pragma once

#include <string>
#include <vector>

struct Project;

namespace templates {

struct File {
    std::string relativePath;  // path inside project dir, '\\' separated
    std::string content;
};

// All files generated for a new Tyra game project (sources, Makefile,
// docker-compose, run scripts...) with project values substituted.
std::vector<File> generate(const Project& p);

// True when `content` is byte-identical to what an older editor version
// generated for this file - i.e. the user never edited it and it is safe
// to regenerate even though it predates the ownership marker.
bool matchesLegacy(const Project& p, const std::string& relativePath,
                   const std::string& content);

}  // namespace templates
