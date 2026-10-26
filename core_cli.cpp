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
    printf("%s\n  ok=%d brand=%s model=%s ecu=%s ecuMaker=%s ecuSource=%s sw=%s swSource=%s\n  original=%s embeddedHex=%s hw=%s production=%s engine=%s year=%s\n  header=%s size=%s renameSafe=%s maps=%zu defined=%d skipped=%d src=%s ver=%u versions=%u versionArray=%s romStart=%zu romSize=%u\n  proposed=%s\n",
           argv[i], a.ok ? 1 : 0, ols::utf8(a.brand).c_str(), ols::utf8(a.model).c_str(),
           ols::utf8(a.ecu).c_str(), ols::utf8(a.ecuManufacturer).c_str(), ols::utf8(a.ecuSource).c_str(),
           ols::utf8(a.software).c_str(), ols::utf8(a.softwareSource).c_str(),
           ols::utf8(a.originalFileName).c_str(), ols::utf8(a.embeddedHexFilename).c_str(),
           ols::utf8(a.hardwareNumber).c_str(), ols::utf8(a.productionNumber).c_str(),
           ols::utf8(a.engineCode).c_str(), ols::utf8(a.vehicleYear).c_str(),
           a.headerComplete ? "complete" : "incomplete", a.formatSizeConsistent ? "consistent" : "mismatch",
           a.renameSafe ? "yes" : "no", a.mapCount, a.definedCount, a.skippedCount, a.mapSource.c_str(),
           a.formatVersion, a.versionCount, a.versionDirectoryValid ? "valid" : "invalid",
           a.versionDataStart, a.versionRecordSize, ols::utf8(a.proposed).c_str());
    if (getenv("WARN"))
      for (auto& w : a.warnings) printf("  warn=%s\n", ols::utf8(w).c_str());
  }
}
