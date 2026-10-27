#include "ols_core.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace ols;
  assert(normalizeVehicleBrand(L"VW") == L"Volkswagen");
  assert(normalizeVehicleBrand(L"Alfa") == L"Alfa Romeo");
  assert(inferVehicleBrandFromText(L"ori_Alfa_Romeo_1.4T_510422.bin") == L"Alfa Romeo");
  assert(inferVehicleBrandFromText(L"VW_Golf_2.0TDI_03G906016.bin") == L"Volkswagen");
  assert(inferVehicleBrandFromText(L"Mercedes-Benz_W204_EDC17.bin") == L"Mercedes Benz");
  assert(inferEcuManufacturerFromText(L"Bosch_EDC17C46.bin") == L"Bosch");
  assert(inferVehicleBrandFromText(L"unknown_read.bin") == L"Unknown");
  assert(identifyEcuManufacturer(L"Robert Bosch GmbH") == L"Bosch");
  assert(identifyEcuManufacturer(L"Continental Automotive") == L"Continental");
  assert(inferEcuManufacturerFromFamily(L"EDC17C46") == L"Bosch");
  assert(inferEcuManufacturerFromFamily(L"SIMOS 18.1") == L"Continental");
  const std::string familyNoise="Meine Kennfelder ME7910 ME7.9.10";
  Bytes familyBytes(familyNoise.begin(),familyNoise.end());
  assert(inferEcuFamily(familyBytes.data(),familyBytes.size())==L"ME7.9.10");
  assert(inferEcuManufacturerFromFamily(inferEcuFamily(familyBytes.data(),familyBytes.size()))==L"Bosch");
  assert(identifyEcuManufacturer(L"Hella Automotive") == L"Hella");
  assert(identifyEcuManufacturer(L"Hyundai Kefico") == L"Hyundai Kefico");
  assert(softwareStemFromSourceFilename(L"C:\\\\ROMs\\\\ori_Alfa_1.4T_510422.bin") == L"ori_Alfa_1.4T_510422");
  const char embeddedRaw[] = "Source file: ori_Alfa_Romeo_1.4T_510422.bin";
  Bytes embeddedBytes(embeddedRaw, embeddedRaw + sizeof(embeddedRaw) - 1);
  assert(scanEmbeddedHexFilename(embeddedBytes.data(), embeddedBytes.size()) == L"ori_Alfa_Romeo_1.4T_510422.bin");
  const std::string sampleSourcePath = "\\\\BUEROTUSSI\\ftp\\Tuningfiles\\ALFA ROMEO\\Giulietta\\Benzin\\1.4 TB 16V 120PS ME7.9.10\\0261S05118\\ori_Alfa_Romeo_Giulietta_1.4T_120Ps_510422.bin";
  Bytes sampleSourceBytes(sampleSourcePath.begin(), sampleSourcePath.end());
  const ws sampleSource = scanEmbeddedHexFilename(sampleSourceBytes.data(), sampleSourceBytes.size());
  assert(sampleSource == L"ori_Alfa_Romeo_Giulietta_1.4T_120Ps_510422.bin");
  assert(inferVehicleBrandFromText(sampleSource) == L"Alfa Romeo");
  assert(softwareStemFromSourceFilename(sampleSource) == L"ori_Alfa_Romeo_Giulietta_1.4T_120Ps_510422");
  assert(softwareStemFromSourceFilename(L"project.ols").empty());
  assert(softwareVersionFromSourceFilename(
      L"\\\\BUEROTUSSI\\ftp\\Tuningfiles\\ALFA ROMEO\\Giulietta\\1.4 TB 16V 120PS ME7.9.10\\0261S05118\\ori_Alfa_Romeo_Giulietta_1.4T_120Ps_510422.bin",
      L"Alfa Romeo", L"Giulietta", L"ME7.9.10") == L"510422");
  assert(softwareVersionFromSourceFilename(L"tunecenter.bin").empty());
  assert(softwareVersionFromSourceFilename(
      L"ori_VW_Golf_Bosch_EDC17C46_123456.bin", L"Volkswagen", L"Golf", L"Bosch EDC17C46") == L"123456");
  assert(softwareVersionFromSourceFilename(L"ori_Alfa_Romeo_Giulietta.bin",
      L"Alfa Romeo", L"Giulietta", L"ME7.9.10").empty());
  assert(proposedFilename(L"VW", L"VW", L"EDC17C46", L"EDC17C46", 1) ==
      L"VW EDC17C46 (1).ols");
  assert(proposedFilename(L"Alfa Romeo", L"Alfa Romeo Giulietta", L"ME7.9.10", L"Giulietta", 1) ==
      L"Alfa Romeo Giulietta ME7.9.10 (1).ols");
  assert(proposedFilename(L"BMW", L"320d", L"EDC17C46", L"03G906016", 42) ==
      L"BMW 320d EDC17C46 03G906016 (42).ols");
  const ws sourceFallback=L"ori_VW_Golf_Bosch_EDC17C46_123456.bin";
  assert(selectSourceFilename(L"project.ols", sourceFallback)==sourceFallback);
  assert(inferVehicleBrandFromText(selectSourceFilename(L"project.ols", sourceFallback))==L"Volkswagen");
  assert(inferEcuManufacturerFromText(selectSourceFilename(L"project.ols", sourceFallback))==L"Bosch");
  assert(selectSourceFilename(L"C:\\\\ROMs\\\\ori_Alfa_1.4T_510422.bin", L"other.bin")==L"C:\\\\ROMs\\\\ori_Alfa_1.4T_510422.bin");
  const std::string utf16Name="ori_VW_Golf_Bosch_EDC17C46_123456.bin";
  Bytes utf16Bytes;
  for(unsigned char c:utf16Name){utf16Bytes.push_back(c);utf16Bytes.push_back(0);}
  assert(scanEmbeddedHexFilename(utf16Bytes.data(),utf16Bytes.size())==L"ori_VW_Golf_Bosch_EDC17C46_123456.bin");
  assert(isDefinedMap(L"LDRXN", L"Bosch 16 bit. 16 x 16"));
  assert(!isDefinedMap(L"Bosch 16 bit. 8 x 10", L""));
  assert(!isDefinedMap(L"Bosch 16 bit. 16 x 16", L""));
  assert(!isDefinedMap(L"Bosch 16 bit. 16 X 16", L""));
  assert(!isDefinedMap(L"Bosch 16 bit. 16*16", L""));
  assert(!isDefinedMap(L"16 bit. 16 x 16", L""));
  assert(isDefinedMap(L"LDRXN 16 bit. 16 x 16", L""));
  assert(isDefinedMap(L"Bosch torque limiter 16 bit. 16 x 16", L""));
  assert(isDefinedMap(L"Torque limiter 16 bit 16x16", L""));
  assert(!isDefinedMap(L"Bosch map", L""));
  assert(!isDefinedMap(L"0000002b", L""));
  assert(!isDefinedMap(L"0000371f", L""));
  assert(!isDefinedMap(L"0x10", L""));
  assert(!isDefinedMap(L"010021a8 / 000021a8", L""));
  assert(!isDefinedMap(L"A0", L""));
  assert(!isDefinedMap(L"A1", L""));
  assert(!isDefinedMap(L"A2", L""));
  assert(isDefinedMap(L"ABICLOSV", L""));
  assert(isDefinedMap(L"AADPD_ON", L""));
  assert(!isDefinedMap(L"Denso Map", L""));
  assert(!isDefinedMap(L"Siemens map 2", L""));
  assert(!isDefinedMap(L"Bosch map", L"Fuel / 16 x 16 / 16 bit"));
  assert(!isDefinedMap(L"Denso Map", L"Torque limiter"));
  // Generated supplier labels: case/number/dimension variants must not count.
  for (const ws& generated : {
           ws(L"Bosch map (1)"), ws(L"Bosch Map #2"), ws(L"Bosch map 3 (16x16)"),
           ws(L"Bosch map (16 X 16)"),
           ws(L"Bosch map [4]"), ws(L"Denso Map (8 x 8)"), ws(L"Siemens map 2"),
           ws(L"Continental map 12"), ws(L"Delphi map (1)"), ws(L"Mitsubishi map 7"),
           ws(L"Valeo map (16×16)"),
           ws(L"Bosch map 2: 16 bit / 16x16"),
           ws(L"Denso Map #3 - 16-bit 8x8"),
           ws(L"Siemens map (4) 32 bit little endian"),
           ws(L"Bosch map 0x10"), ws(L"Bosch Kennfeld 2 (16x16)"), ws(L"Siemens Kennlinie 3 16 bit")}) {
    assert(!isDefinedMap(generated, L""));
  }
  // Genuine map names remain countable even when they mention a supplier.
  for (const ws& humanName : {
  assert(isDefinedMap(L"", L"Kennfeld \"Denso injection quantity limiter\""));
           ws(L"Bosch map torque limiter"),
           ws(L"Siemens rail pressure target"), ws(L"Continental boost pressure limit"),
           ws(L"Torque limiter"), ws(L"Driver wish"), ws(L"KFZWOP"), ws(L"LDRXN"),
           ws(L"Boost pressure limit 1")}) {
    assert(isDefinedMap(humanName, L""));
  }
  // Bare supplier/family labels are autogenerated placeholders, not real map titles.
  for (const ws& supplierFamily : {
           ws(L"Bosch II"), ws(L"Bosch III"), ws(L"Bosch II 16"),
           ws(L"Siemens II"), ws(L"Denso IV"), ws(L"Continental III")}) {
    assert(!isDefinedMap(supplierFamily, L""));
    assert(!isDefinedMap(L"", supplierFamily));
  }
  assert(isDefinedMap(L"Bosch II torque limiter", L""));
  // Some OLS families keep human map titles in descriptions when name is empty.
  assert(isDefinedMap(L"", L"Human-looking description must replace a missing map name"));
  assert(isDefinedMap(L"", L"EGR"));
  assert(isDefinedMap(L"", L"P0420 DTC OFF ( Replace the dtc line with 0)"));
  assert(!isDefinedMap(L"", L"Hexdump"));
  assert(!isDefinedMap(L"", L"Map"));
  for (const ws& statisticalLabel : {
           ws(L"Statistich"), ws(L"Statistich 16"), ws(L"Statistical 16"),
           ws(L"Map \"Statistich 16\""), ws(L"Kennfeld \"Statistische 16\""),
           ws(L"Kennlinie statistich 8")}) {
    assert(!isDefinedMap(L"", statisticalLabel));
    assert(!isDefinedMap(statisticalLabel, L""));
  }
  for (const ws& generatedDescription : {
           ws(L"Map \"Bosch II 16\""), ws(L"Map \"Bosch 16\""),
           ws(L"Map \"Statistical 16\" (!)"),
           ws(L"Kennfeld \"Denso Vertikal 16\""),
           ws(L"Kennfeld \"Bosch II 16\""),
           ws(L"Kennfeld \"Bosch III 16/8\""),
           ws(L"Kennfeld \"Denso Float\"")}) {
    assert(!isDefinedMap(L"", generatedDescription));
  }
  assert(isDefinedMap(L"Bosch torque limiter", L""));
  assert(isDefinedMap(L"", L"Kennfeld \\"Denso injection quantity limiter\\""));
  assert(isDefinedMap(L"LDRXN", L"Bosch 16 bit. 16 x 16"));
  ws longName=proposedFilename(ws(100,L'B'),ws(100,L'M'),ws(100,L'E'),ws(100,L'S'),42);
  assert(longName.size()<=240 && longName.find(L"(42).ols")!=ws::npos);
  assert(proposedFilename(L"BMW",L"320d",L"EDC17C46",L"",0).find(L"(Nil).ols")!=ws::npos);
  const ws partialName = proposedFilename(L"BMW", L"Unknown", L"EDC17C46", L"", 12);
  assert(partialName.find(L"Unknown") == ws::npos);
  assert(partialName.find(L"BMW EDC17C46") == 0);
  const ws softwareOnlyName = proposedFilename(L"Unknown", L"Unknown", L"Unknown", L"03G906016", 4);
  assert(softwareOnlyName.find(L"Unknown") == ws::npos);
  assert(softwareOnlyName.find(L"03G906016") == 0);
  // Pre-330 map-prefix fast path must preserve the version-gated wire layout.
  auto putCString = [](Bytes& bytes, size_t offset, const std::string& value) -> size_t {
    const uint32_t length = static_cast<uint32_t>(value.size());
    bytes[offset] = static_cast<uint8_t>(length);
    bytes[offset + 1] = static_cast<uint8_t>(length >> 8);
    bytes[offset + 2] = static_cast<uint8_t>(length >> 16);
    bytes[offset + 3] = static_cast<uint8_t>(length >> 24);
    if (!value.empty()) {
      std::memcpy(bytes.data() + offset + 4, value.data(), value.size());
      bytes[offset + 4 + value.size()] = 0;
      return offset + 5 + value.size();
    }
    return offset + 4;
  };
  for (uint32_t schema : {80u, 92u, 93u, 199u, 200u, 267u, 268u, 281u, 282u, 286u, 287u, 329u}) {
    Bytes prefixSample(256, 0);
    const size_t start = 64;
    size_t p = start;
    if (schema >= 268) p += 1;
    if (schema >= 282) p += 4;
    if (schema >= 287) p = putCString(prefixSample, p, "prefix");
    if (schema >= 93) p += 1;
    const size_t descriptionEnd = putCString(prefixSample, p, "");
    const size_t nameStart = descriptionEnd + 24; // five map words + folder ID
    putCString(prefixSample, nameStart, "LDRXN");
    assert(plausibleMapPrefix(prefixSample.data(), prefixSample.size(), start, schema));
  }
  Bytes emptyPrefixSample(256, 0);
  assert(!plausibleMapPrefix(emptyPrefixSample.data(), emptyPrefixSample.size(), 64, 200));

  // M1 version-array dispatch: validate count/start/record-size before using
  // the ROM-array boundary, and ignore M2-like markers inside the ROM bytes.
  Bytes versioned(256, 0);
  auto put32 = [&](size_t off, uint32_t value) {
    versioned[off] = static_cast<uint8_t>(value);
    versioned[off + 1] = static_cast<uint8_t>(value >> 8);
    versioned[off + 2] = static_cast<uint8_t>(value >> 16);
    versioned[off + 3] = static_cast<uint8_t>(value >> 24);
  };
  put32(52, 1);       // version_count_minus_one => two versions
  put32(56, 96);      // version_data_start (M3 fence)
  put32(60, 64);      // bytes per version
  put32(64, 0x42007899u);
  put32(80, 0x11883377u);
  versioned[84] = 0;  // uncompressed
  put32(85, 1);       // one map-record candidate
  put32(150, 0x11883377u); // false marker inside ROM/version payload
  versioned[154] = 0;
  put32(155, 1);
  VersionDirectory vd;
  assert(locateVersionDirectory(versioned.data(), versioned.size(), vd));
  assert(vd.markerOffset == 64 && vd.count == 2);
  assert(vd.dataStart == 96 && vd.recordSize == 64 && vd.endOffset == 228);
  const auto boundedMarkers = findMarkers(versioned.data(), versioned.size(), vd.dataStart);
  assert(boundedMarkers.size() == 1);
  assert(boundedMarkers[0].at == 89);

  // A valid map marker after more than eight plausible decoys must still be
  // considered; otherwise map counts can silently fall back to an earlier decoy.
  Bytes manyMarkers(512, 0);
  auto putMarker = [&](size_t off) {
    manyMarkers[off] = 0x77; manyMarkers[off + 1] = 0x33;
    manyMarkers[off + 2] = 0x88; manyMarkers[off + 3] = 0x11;
    manyMarkers[off + 4] = 0;
    manyMarkers[off + 5] = 1;
  };
  for (size_t i = 0; i < 10; ++i) putMarker(64 + i * 16);
  putMarker(300);
  const auto allMarkers = findMarkers(manyMarkers.data(), manyMarkers.size(), manyMarkers.size());
  assert(allMarkers.size() == 11);
  assert(allMarkers.back().at == 309);

  Candidate completeMapDirectory;
  completeMapDirectory.source = "map directory";
  completeMapDirectory.maps = {{L"Bosch map", L"", false},
                               {L"Denso map 2", L"", false},
                               {L"MAP_123", L"", true}};
  Candidate partialMapDirectory;
  partialMapDirectory.source = "map directory";
  partialMapDirectory.maps = {{L"Generated supplier label", L"", false},
                              {L"Valid_Name", L"", true}};
  partialMapDirectory.warning = L"Read 2 of 10 map records before the directory ended.";
  assert(!hasPartialMapDirectory(completeMapDirectory));
  assert(hasPartialMapDirectory(partialMapDirectory));
  assert(betterMapCandidate(completeMapDirectory, partialMapDirectory));
  assert(definedMapCount(completeMapDirectory) == 1);
  assert(definedMapCount(partialMapDirectory) == 1);
  Candidate longerGeneratedDirectory;
  longerGeneratedDirectory.source = "map directory";
  longerGeneratedDirectory.maps = {{L"Bosch map", L"", false},
                                   {L"Denso map", L"", false},
                                   {L"Siemens map", L"", false},
                                   {L"Continental map", L"", false}};
  Candidate shorterHumanDirectory;
  shorterHumanDirectory.source = "map directory";
  shorterHumanDirectory.maps = {{L"Boost pressure target", L"", true}};
  assert(betterMapCandidate(longerGeneratedDirectory, shorterHumanDirectory));
  Candidate oneRecord;
  oneRecord.source = "scanned map directory";
  oneRecord.warning = L"Recovered by sequential scan; directory count is heuristic.";
  oneRecord.maps = {{L"TorqueLimit", L"", true}};
  Candidate empty;
  assert(betterMapCandidate(oneRecord, empty));

  // Invalid version-array dimensions must not be dispatched as a valid array.
  Bytes invalidVersioned = versioned;
  invalidVersioned[52] = 0xff;
  invalidVersioned[53] = 0xff;
  invalidVersioned[54] = 0xff;
  invalidVersioned[55] = 0xff;
  VersionDirectory invalidVd;
  assert(!locateVersionDirectory(invalidVersioned.data(), invalidVersioned.size(), invalidVd));

  // A false M1-like sequence inside a payload can have valid arithmetic and end
  // exactly at EOF. Prefer the structurally corroborated directory whose data
  // starts with M3 instead of dispatching to the false candidate.
  Bytes m1Collision(304, 0);
  auto put32Collision = [&](size_t off, uint32_t value) {
    m1Collision[off] = static_cast<uint8_t>(value);
    m1Collision[off + 1] = static_cast<uint8_t>(value >> 8);
    m1Collision[off + 2] = static_cast<uint8_t>(value >> 16);
    m1Collision[off + 3] = static_cast<uint8_t>(value >> 24);
  };
  put32Collision(52, 1); put32Collision(56, 100); put32Collision(60, 80);
  put32Collision(64, 0x42007899u);
  put32Collision(100, 0x98728833u);
  put32Collision(188, 0); put32Collision(192, 232); put32Collision(196, 68);
  put32Collision(200, 0x42007899u);
  VersionDirectory collisionVd;
  assert(locateVersionDirectory(m1Collision.data(), m1Collision.size(), collisionVd));
  assert(collisionVd.markerOffset == 64);
  assert(collisionVd.dataStart == 100 && collisionVd.count == 2);

  // A marker-shaped sequence deep in a large ROM must not trigger version-array
  // dispatch. The version directory is expected in the bounded serialized header.
  Bytes deepMarker(8u * 1024u * 1024u + 256u, 0);
  auto putDeep = [&](size_t off, uint32_t value) {
    deepMarker[off] = static_cast<uint8_t>(value);
    deepMarker[off + 1] = static_cast<uint8_t>(value >> 8);
    deepMarker[off + 2] = static_cast<uint8_t>(value >> 16);
    deepMarker[off + 3] = static_cast<uint8_t>(value >> 24);
  };
  size_t deep = 8u * 1024u * 1024u + 64u;
  putDeep(deep - 12, 1); putDeep(deep - 8, static_cast<uint32_t>(deep + 4));
  putDeep(deep - 4, 64); putDeep(deep, 0x42007899u);
  putDeep(deep + 4, 0x98728833u);
  VersionDirectory deepVd;
  assert(!locateVersionDirectory(deepMarker.data(), deepMarker.size(), deepVd));

  // Malformed-input safety: all short prefixes must be rejected before field reads.
  for (size_t n = 0; n < 24; ++n) {
    Bytes truncated(n, 0);
    const auto a = analyzeOls(truncated.empty() ? nullptr : truncated.data(), truncated.size());
    assert(!a.ok);
  }

  // A valid signature with an impossible metadata length must remain review-only.
  Bytes malformed(32, 0);
  malformed[0] = 0x0b;
  std::memcpy(malformed.data() + 4, "WinOLS File", 11);
  malformed[15] = 0;
  malformed[16] = 0xdf; malformed[17] = 0x01; // format version 479, LE
  malformed[20] = 32; // declared file size
  malformed[24] = 0xff; malformed[25] = 0xff;
  malformed[26] = 0xff; malformed[27] = 0x7f; // impossible string length
  const auto badHeader = analyzeOls(malformed.data(), malformed.size());
  assert(badHeader.ok);
  assert(!badHeader.headerComplete);
  assert(!badHeader.renameSafe);

  // Deterministic fuzz smoke test over schema boundaries and arbitrary payload bytes.
  const uint32_t versions[] = {97, 199, 200, 247, 396, 438, 439, 479, 503, 597, 804, 0xffffffffu};
  uint32_t state = 0x6d2b79f5u;
  for (size_t sample = 0; sample < 128; ++sample) {
    const size_t n = 24 + ((sample * 37) % 2048);
    Bytes fuzz(n, 0);
    fuzz[0] = 0x0b;
    std::memcpy(fuzz.data() + 4, "WinOLS File", 11);
    fuzz[15] = 0;
    const uint32_t version = versions[sample % (sizeof(versions) / sizeof(versions[0]))];
    fuzz[16] = static_cast<uint8_t>(version);
    fuzz[17] = static_cast<uint8_t>(version >> 8);
    fuzz[18] = static_cast<uint8_t>(version >> 16);
    fuzz[19] = static_cast<uint8_t>(version >> 24);
    for (size_t j = 20; j < n; ++j) {
      state = state * 1664525u + 1013904223u;
      fuzz[j] = static_cast<uint8_t>(state >> 24);
    }
    const auto result = analyzeOls(fuzz.data(), fuzz.size());
    assert(result.proposed.size() <= 240);
    if (!result.headerComplete) assert(!result.renameSafe);
  }

  std::cout<<"core regression checks passed\\n";
}
