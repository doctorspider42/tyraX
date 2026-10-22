#include "renderer/core/paths/path1/vif1_chain_check.hpp"
#include <cstdint>
#include <fstream>
#include <iostream>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 2) return 1;
  std::ifstream file(argv[1], std::ios::binary | std::ios::ate);
  if (!file) return 1;
  const auto bytes = file.tellg();
  if (bytes <= 0 || bytes % 4 != 0) return 1;
  std::vector<std::uint32_t> words(static_cast<std::size_t>(bytes) / 4);
  file.seekg(0);
  if (!file.read(reinterpret_cast<char*>(words.data()), bytes)) return 1;
  Tyra::Vif1ChainCheck::Stream stream;
  for (std::size_t i = 0; i < words.size(); ++i) {
    const bool command = stream.result.pending == 0;
    stream.word(words[i]);
    if (!stream.result) return 2;
    if (command) std::cout << i << ' ' << words[i] << ' ' << stream.result.pending << '\n';
  }
  return stream.result.pending ? 3 : 0;
}
