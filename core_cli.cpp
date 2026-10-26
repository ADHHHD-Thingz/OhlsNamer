#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "ols_core.hpp"
int main(int argc, char** argv) {
  for (int i = 1; i < argc; i++) {
    std::ifstream f(argv[i], std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    auto a = ols::analyzeOls(data.data(), data.size());
    printf("%s\n  ok=%d brand=%s model=%s ecu=%s sw=%s\n  maps=%zu defined=%d skipped=%d src=%s ver=%u\n  proposed=%s\n",
           argv[i], a.ok ? 1 : 0, ols::utf8(a.brand).c_str(), ols::utf8(a.model).c_str(),
           ols::utf8(a.ecu).c_str(), ols::utf8(a.software).c_str(), a.mapCount, a.definedCount,
           a.skippedCount, a.mapSource.c_str(), a.formatVersion, ols::utf8(a.proposed).c_str());
    if (getenv("WARN"))
      for (auto& w : a.warnings) printf("  warn=%s\n", ols::utf8(w).c_str());
  }
}
