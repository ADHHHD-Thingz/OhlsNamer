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
    // Fast map finding: the map/ID area ends where the embedded ROM images start.
    // needLoad is what the GUI loader reads (1 MiB first, grown on request).
    uint64_t need = std::min<size_t>(data.size(), ols::kInitialLoadBytes);
    for (int round = 0; round < 6; ++round) {
      const uint64_t want = ols::requiredLoadLength(data.data(), (size_t)need, data.size());
      if (want <= need) break;
      need = want;
    }
    printf("  mapAreaEnd=0x%zx romImage=0x%zx-0x%zx mapDirectory=0x%zx needLoad=%llu of %zu bytes\n",
           a.mapAreaEnd, a.romImageStart, a.romImageEnd, a.mapDirectoryOffset,
           (unsigned long long)need, data.size());
    if (getenv("WARN"))
      for (auto& w : a.warnings) printf("  warn=%s\n", ols::utf8(w).c_str());
    // Opt-in corpus audit: expose labels excluded from the filename map count.
    if (getenv("MAP_AUDIT")) {
      printf("  map-audit total=%zu defined=%d skipped=%d\n",
             a.mapCount, a.definedCount, a.skippedCount);
      size_t index = 0;
      for (const auto& m : a.mapDetails) {
        std::string name = ols::utf8(m.name);
        std::string description = ols::utf8(m.description);
        if (name.size() > 4096) name.resize(4096);
        if (description.size() > 4096) description.resize(4096);
        printf("  map-record index=%zu defined=%d name=%s description=%s\n",
               index++, m.defined ? 1 : 0, name.c_str(), description.c_str());
      }
    }
  }
}
