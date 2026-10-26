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
  assert(identifyEcuManufacturer(L"Hella Automotive") == L"Hella");
  assert(identifyEcuManufacturer(L"Hyundai Kefico") == L"Hyundai Kefico");
  assert(softwareStemFromSourceFilename(L"C:\\\\ROMs\\\\ori_Alfa_1.4T_510422.bin") == L"ori_Alfa_1.4T_510422");
  const char embeddedRaw[] = "Source file: ori_Alfa_Romeo_1.4T_510422.bin";
  Bytes embeddedBytes(embeddedRaw, embeddedRaw + sizeof(embeddedRaw) - 1);
  assert(scanEmbeddedHexFilename(embeddedBytes.data(), embeddedBytes.size()) == L"ori_Alfa_Romeo_1.4T_510422.bin");
  assert(softwareStemFromSourceFilename(L"project.ols").empty());
  const ws sourceFallback=L"ori_VW_Golf_Bosch_EDC17C46_123456.bin";
  assert(selectSourceFilename(L"project.ols", sourceFallback)==sourceFallback);
  assert(inferVehicleBrandFromText(selectSourceFilename(L"project.ols", sourceFallback))==L"Volkswagen");
  assert(inferEcuManufacturerFromText(selectSourceFilename(L"project.ols", sourceFallback))==L"Bosch");
  assert(selectSourceFilename(L"C:\\\\ROMs\\\\stock.bin", L"other.bin")==L"C:\\\\ROMs\\\\stock.bin");
  const std::string utf16Name="ori_VW_Golf_Bosch_EDC17C46_123456.bin";
  Bytes utf16Bytes;
  for(unsigned char c:utf16Name){utf16Bytes.push_back(c);utf16Bytes.push_back(0);}
  assert(scanEmbeddedHexFilename(utf16Bytes.data(),utf16Bytes.size())==L"ori_VW_Golf_Bosch_EDC17C46_123456.bin");
  assert(isDefinedMap(L"LDRXN", L"Bosch 16 bit. 16 x 16"));
  assert(!isDefinedMap(L"Bosch 16 bit. 8 x 10", L""));
  ws longName=proposedFilename(ws(100,L'B'),ws(100,L'M'),ws(100,L'E'),ws(100,L'S'),42);
  assert(longName.size()<=240 && longName.find(L"(42).ols")!=ws::npos);
  assert(proposedFilename(L"BMW",L"320d",L"EDC17C46",L"",0).find(L"(Nil).ols")!=ws::npos);
  std::cout<<"core regression checks passed\\n";
}
