#include "ols_core.hpp"
#include <cassert>
#include <iostream>
int main() {
  using namespace ols;
  assert(normalizeVehicleBrand(L"VW") == L"Volkswagen");
  assert(normalizeVehicleBrand(L"Alfa") == L"Alfa Romeo");
  assert(identifyEcuManufacturer(L"Robert Bosch GmbH") == L"Bosch");
  assert(identifyEcuManufacturer(L"Continental Automotive") == L"Continental");
  assert(inferEcuManufacturerFromFamily(L"EDC17C46") == L"Bosch");
  assert(inferEcuManufacturerFromFamily(L"SIMOS 18.1") == L"Continental");
  assert(softwareStemFromSourceFilename(L"C:\\\\ROMs\\\\ori_Alfa_1.4T_510422.bin") == L"ori_Alfa_1.4T_510422");
  assert(softwareStemFromSourceFilename(L"project.ols").empty());
  assert(isDefinedMap(L"LDRXN", L"Bosch 16 bit. 16 x 16"));
  assert(!isDefinedMap(L"Bosch 16 bit. 8 x 10", L""));
  ws longName=proposedFilename(ws(100,L'B'),ws(100,L'M'),ws(100,L'E'),ws(100,L'S'),42);
  assert(longName.size()<=240 && longName.find(L"(42).ols")!=ws::npos);
  assert(proposedFilename(L"BMW",L"320d",L"EDC17C46",L"",0).find(L"(Nil).ols")!=ws::npos);
  std::cout<<"core regression checks passed\\n";
}
