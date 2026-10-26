// Host-side batch dumper: runs the firmware's real parser and envelope
// builder over a field log, one JSON envelope per line.
// Build: g++ -std=c++17 -I../src batch_dump.cpp ../src/WardriftFormat.cpp
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include "WardriftFormat.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <wardrive.log> [batch_rows]\n", argv[0]);
    return 2;
  }
  int batch_rows = (argc > 2) ? std::atoi(argv[2]) : 250;
  std::ifstream in(argv[1]);
  if (!in) {
    std::perror("open");
    return 1;
  }

  std::string line, items;
  items.reserve(64 * 1024);
  int rows = 0, skipped = 0, seq = 0;
  uint64_t sequence = 1750000000000ULL;
  char item[512];
  char prefix[256];

  auto flush = [&]() {
    if (rows == 0) return;
    size_t n = wd_envelope_prefix(prefix, sizeof(prefix),
                                  "12345678-1234-4123-8123-123456789abc",
                                  sequence, "2026-10-24T12:00:00Z");
    if (!n) return;
    std::string env(prefix, n);
    env += items;
    env += wd_envelope_suffix();
    std::printf("%s\n", env.c_str());
    sequence++;
    seq++;
    rows = 0;
    items.clear();
  };

  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    if (line.rfind("WigleWifi", 0) == 0 || line.rfind("MAC,", 0) == 0) continue;
    WdRow r{};
    if (!wd_parse_row(line.c_str(), line.size(), &r)) { skipped++; continue; }
    size_t n = wd_row_json(r, item, sizeof(item));
    if (!n) { skipped++; continue; }
    if (rows > 0) items += ',';
    items += item;
    rows++;
    if (rows >= batch_rows) flush();
  }
  flush();
  std::fprintf(stderr, "batches=%d skipped=%d\n", seq, skipped);
  return 0;
}
