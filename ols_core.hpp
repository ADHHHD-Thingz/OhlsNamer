// Port of src/lib/ols/{labels,filename,analyze}.ts. Platform independent.
#pragma once
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <map>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace ols {

using ws = std::wstring;
using Bytes = std::vector<uint8_t>;

struct Err : std::runtime_error {
  explicit Err(const char* m) : std::runtime_error(m) {}
};

// ---------------------------------------------------------------- text utils
inline bool isJsSpace(wchar_t c) {
  return c == 0x20 || (c >= 0x09 && c <= 0x0d) || c == 0xa0 || c == 0x1680 ||
         (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f ||
         c == 0x205f || c == 0x3000 || c == 0xfeff;
}

inline ws jsTrim(const ws& s) {
  size_t a = 0, b = s.size();
  while (a < b && isJsSpace(s[a])) a++;
  while (b > a && isJsSpace(s[b - 1])) b--;
  return s.substr(a, b - a);
}

inline ws removeNul(const ws& s) {
  ws o;
  for (wchar_t c : s)
    if (c != 0) o += c;
  return o;
}

inline wchar_t lowerCh(wchar_t c) {
  if (c < 128) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
  if (c >= 0xc0 && c <= 0xde && c != 0xd7) return c + 32;
  if (c == 0x160 || c == 0x17d || c == 0x152) return c + 1;
  if (c == 0x178) return 0xff;
  return c;
}

inline ws lower(const ws& s) {
  ws o = s;
  for (auto& c : o) c = lowerCh(c);
  return o;
}

// Normalize aliases for comparisons while preserving unknown raw metadata.
inline ws catalogNorm(const ws& raw) {
  ws out; bool space=false;
  for (wchar_t c : lower(jsTrim(removeNul(raw)))) {
    bool word=(c>=L'a'&&c<=L'z')||(c>=L'0'&&c<=L'9')||c>=0x80;
    if(word){if(space&&!out.empty())out+=L' ';out+=c;space=false;}else space=true;
  }
  return out;
}
inline const std::vector<ws>& vehicleBrandCatalogue() {
  static const std::vector<ws> v={
    L"Abarth",L"Acura",L"Aixam",L"Alfa Romeo",L"Alpina",L"Alpine",L"Aston Martin",L"Audi",L"BAIC",
    L"Bentley",L"BMW",L"Brilliance",L"Bugatti",L"Buick",L"BYD",L"Cadillac",L"Changan",L"Chery",
    L"Chevrolet",L"Chrysler",L"Citroen",L"Cupra",L"Dacia",L"Daewoo",L"Daihatsu",L"Dodge",L"DS",
    L"Ferrari",L"Fiat",L"Ford",L"Foton",L"GAC",L"Geely",L"Genesis",L"GMC",L"Great Wall",L"Haval",
    L"Holden",L"Honda",L"Hongqi",L"Hummer",L"Hyundai",L"Infiniti",L"Isuzu",L"Iveco",L"JAC",L"Jaguar",
    L"Jeep",L"Jetour",L"Kia",L"Koenigsegg",L"Lada",L"Lamborghini",L"Lancia",L"Land Rover",L"Lexus",
    L"Ligier",L"Lincoln",L"Lotus",L"Lucid",L"Lynk & Co",L"Mahindra",L"Maserati",L"Mazda",L"McLaren",
    L"Mercedes Benz",L"Mercury",L"MG",L"Mini",L"Mitsubishi",L"Morgan",L"NIO",L"Nissan",L"Opel",L"ORA",
    L"Pagani",L"Peugeot",L"Perodua",L"Peterbilt",L"Polestar",L"Porsche",L"Proton",L"Ram",L"Renault",
    L"Rimac",L"Roewe",L"Rolls Royce",L"Rover",L"Saab",L"Scania",L"Seat",L"Seres",L"Skoda",L"Smart",
    L"SsangYong",L"Subaru",L"Suzuki",L"Tata",L"Tesla",L"Toyota",L"Vauxhall",L"VinFast",L"Volkswagen",
    L"Volvo",L"Voyah",L"Wey",L"Wuling",L"XPeng",L"Yugo",L"Zeekr",L"Zotye",L"FAW",L"Dongfeng",L"JMC",
    L"Maxus",L"LDV",L"TVR",L"Caterham",L"Plymouth",L"Pontiac",L"Oldsmobile",L"Saturn",L"Geo",L"MAN",
    L"DAF",L"Setra",L"Temsa",L"Yutong",L"King Long",L"Hino",L"UD Trucks",L"Kenworth",L"Freightliner",
    L"Mack",L"Western Star",L"General Motors",L"SAIC",L"Prodrive",L"TVR",
    L"Ashok Leyland",L"Baojun",L"Brabus",L"Datsun",L"De Tomaso",L"Eagle",L"Exeed",L"Fisker",
    L"GAZ",L"Gonow",L"Hindustan",L"Hudson",L"Lifan",L"Maruti Suzuki",L"Maybach",L"Moskvich",
    L"Polaris",L"Rivian",L"Scion",L"Shacman",L"Studebaker",L"Talbot",L"Tatra",L"UAZ",L"Zastava",
    L"ZIL",L"Indian Motorcycle",L"KTM",L"Kawasaki",L"Triumph",L"Royal Enfield",L"Harley Davidson",
    L"Aprilia",L"Piaggio",L"MV Agusta",L"Benelli",L"CFMoto",L"Can Am",L"BRP",L"Yamaha Motor",
    L"Sany",L"XCMG",L"JCB",L"Case IH",L"New Holland",L"Fendt",L"Claas",L"International Harvester",
    L"Belarus",L"Zetor",L"Valtra",L"Same Deutz-Fahr",L"Doosan",L"Bobcat",L"Terex",L"JLG",
    L"Takeuchi",L"Hitachi Construction",L"Hyundai Construction",L"Volvo Construction",L"McCormick",
    L"Mahindra Tractors",L"Deutz-Fahr",L"Steyr",L"Kioti",L"Yanmar Tractors",L"Ferris",L"Toro",
    L"Club Car",L"John Deere Gator",L"Arctic Cat",L"Sea-Doo",L"Ski-Doo"
  }; return v;
}
inline const std::vector<ws>& ecuManufacturerCatalogue() {
  static const std::vector<ws> v={
    L"Bosch",L"Denso",L"Denso Ten",L"Delphi",L"Delphi Technologies",L"Continental",L"Siemens VDO",
    L"Siemens",L"Magneti Marelli",L"Marelli",L"Valeo",L"Hitachi",L"Hitachi Astemo",L"Keihin",
    L"Mitsubishi Electric",L"Panasonic Automotive",L"Temic",L"TRW",L"ZF",L"Aisin",L"Hyundai Kefico",
    L"Kefico",L"Hyundai Mobis",L"Lear",L"Visteon",L"Aptiv",L"Yazaki",L"Sumitomo Electric",L"Fujitsu Ten",
    L"Renesas",L"NXP",L"Infineon",L"STMicroelectronics",L"Texas Instruments",L"Motorola",L"Freescale",
    L"Nidec",L"BorgWarner",L"Eaton",L"Cummins",L"Caterpillar",L"John Deere",L"AGCO",L"Yamaha",L"Mikuni",
    L"Sagem",L"EControls",L"Ecotrons",L"EFI Technology",L"MoTeC",L"Haltech",L"Link ECU",L"Syvecs",
    L"Life Racing",L"Pectel",L"Cosworth",L"Transtron",L"ACDelco",L"Motorcraft",L"Jatco",L"Getrag",
    L"Tremec",L"Allison Transmission",L"Knorr Bremse",L"Wabco",L"Bendix",L"Paccar",L"Detroit Diesel",
    L"Volvo Penta",L"Yanmar",L"Kubota",L"Komatsu",L"Perkins",L"Deutz",L"Liebherr",L"MTU",L"Navistar",
    L"Eberspacher",L"Webasto",L"ECUMaster",L"FuelTech",L"AEM Electronics",L"Hondata",L"Nistune",L"Vi-PEC",
    L"KMS",L"DTAFast",L"Emerald",L"Omex",L"PerfectTuning",L"Alientech",L"EVC Electronic",
    L"Magic Motorsport",L"CMD Flash",L"Continental Powertrain",L"Continental SIMOS",L"Bosch Motorsport",
    L"Hella",L"Mando",L"Autoliv",L"Kostal",L"Mahle",L"Harman",L"Sirius",L"UAES",
    L"Shanghai United Automotive Electronics",L"Weifu",L"Rheinmetall",L"Porsche Engineering",
    L"Jabil",L"BHTC",L"Ficosa",L"Kongsberg Automotive",L"Johnson Controls",L"Nippon Seiki",
    L"Shindengen",L"JTEKT",L"Astemo",L"Hitachi Automotive",L"Bosch Engineering",L"Delco Electronics",
    L"GM Delphi",L"Valeo Siemens eAutomotive",L"Continental Automotive",L"ZF Friedrichshafen",
    L"Mitsubishi Motors",L"Panasonic",L"Fujitsu",L"Magna",L"Magna Powertrain",L"TRW Automotive",
    L"Knorr-Bremse",L"Eberspächer",L"Eberspacher",L"Landirenzo",L"Landi Renzo",L"BRC Gas Equipment",
    L"Prins",L"Tomasetto Achille",L"AC Stag",L"Vialle",L"Emer",L"Lovato",L"Bigas",L"OMVL",
    L"Delphi Packard",L"Siemens Automotive",L"Motorola Automotive",L"Freescale Semiconductor",
    L"Renesas Electronics",L"Infineon Technologies",L"NXP Semiconductors",L"STMicroelectronics"
  }; return v;
}
inline ws normalizeVehicleBrand(const ws& raw) {
  ws n=catalogNorm(raw); if(n.empty())return L"Unknown";
  static const std::map<ws,ws> aliases={{L"alfa",L"Alfa Romeo"},{L"vw",L"Volkswagen"},
    {L"volkswagen ag",L"Volkswagen"},{L"mercedes",L"Mercedes Benz"},{L"landrover",L"Land Rover"},
    {L"rolls royce",L"Rolls Royce"},{L"gm",L"General Motors"},{L"psa",L"Peugeot"},
    {L"ssang yong",L"SsangYong"},{L"bmw ag",L"BMW"},{L"tata motors",L"Tata"},{L"citroën",L"Citroen"}};
  auto a=aliases.find(n);if(a!=aliases.end())return a->second;
  for(const auto& b:vehicleBrandCatalogue())if(catalogNorm(b)==n)return b;
  return jsTrim(raw);
}
inline ws inferVehicleBrandFromText(const ws& raw) {
  ws n=L" "+catalogNorm(raw)+L" ";ws best;size_t bestLen=0;bool ambiguous=false;
  auto consider=[&](const ws& alias,const ws& canonical) {
    ws needle=L" "+catalogNorm(alias)+L" ";
    if(n.find(needle)==ws::npos)return;
    if(needle.size()>bestLen){best=canonical;bestLen=needle.size();ambiguous=false;}
    else if(needle.size()==bestLen&&canonical!=best)ambiguous=true;
  };
  // Dump filenames commonly abbreviate makes; only complete tokens are considered.
  static const std::vector<std::pair<ws,ws>> aliases={
    {L"vw",L"Volkswagen"},{L"volkswagen ag",L"Volkswagen"},{L"alfa",L"Alfa Romeo"},
    {L"mercedes",L"Mercedes Benz"},{L"mercedes benz",L"Mercedes Benz"},
    {L"mercedes-benz",L"Mercedes Benz"},{L"landrover",L"Land Rover"},
    {L"land rover",L"Land Rover"},{L"bmw ag",L"BMW"},{L"gm",L"General Motors"},
    {L"citroën",L"Citroen"},{L"ssang yong",L"SsangYong"},{L"tata motors",L"Tata"}
  };
  for(const auto& a:aliases)consider(a.first,a.second);
  for(const auto& b:vehicleBrandCatalogue())consider(b,b);
  return best.empty()||ambiguous?ws(L"Unknown"):best;
}
inline ws identifyEcuManufacturer(const ws& raw) {
  ws n=catalogNorm(raw);if(n.empty()||n==L"-"||n==L"?"||n==L"%")return L"Unknown";
  static const std::map<ws,ws> aliases={{L"robert bosch gmbh",L"Bosch"},{L"bosch motorsport",L"Bosch"},
    {L"nippon denso",L"Denso"},{L"denso corporation",L"Denso"},{L"continental automotive",L"Continental"},
    {L"magneti marelli",L"Magneti Marelli"},{L"zf friedrichshafen",L"ZF"},{L"zf trw",L"ZF"},
    {L"hyundai kefico",L"Hyundai Kefico"},{L"hitachi automotive systems",L"Hitachi"},{L"mobis",L"Hyundai Mobis"}};
  auto a=aliases.find(n);if(a!=aliases.end())return a->second;
  for(const auto& m:ecuManufacturerCatalogue()){ws mn=catalogNorm(m);if(n==mn||(L" "+n+L" ").find(L" "+mn+L" ")!=ws::npos)return m;}
  return jsTrim(raw);
}
inline ws inferEcuManufacturerFromText(const ws& raw) {
  ws n=L" "+catalogNorm(raw)+L" ";ws best;size_t bestLen=0;bool ambiguous=false;
  for(const auto& m:ecuManufacturerCatalogue()) {
    ws needle=L" "+catalogNorm(m)+L" ";
    if(n.find(needle)==ws::npos)continue;
    if(needle.size()>bestLen){best=m;bestLen=needle.size();ambiguous=false;}
    else if(needle.size()==bestLen&&m!=best)ambiguous=true;
  }
  if(!best.empty()&&!ambiguous)return best;
  ws normalized=identifyEcuManufacturer(raw);
  for(const auto& m:ecuManufacturerCatalogue())if(catalogNorm(m)==catalogNorm(normalized))return m;
  return L"Unknown";
}
inline ws inferEcuManufacturerFromFamily(const ws& ecu) {
  ws n=lower(jsTrim(ecu));
  if(n.rfind(L"edc",0)==0||n.rfind(L"me",0)==0||n.rfind(L"med",0)==0||n.rfind(L"md1",0)==0||n.rfind(L"mg1",0)==0||n.rfind(L"dde",0)==0||n.rfind(L"motronic",0)==0)return L"Bosch";
  if(n.rfind(L"simos",0)==0)return L"Continental";
  if(n.rfind(L"dcm",0)==0||n.rfind(L"sid",0)==0)return L"Delphi";
  if(n.rfind(L"mjd",0)==0||n.rfind(L"iaw",0)==0)return L"Marelli";
  if(n.rfind(L"keihin",0)==0)return L"Keihin";
  if(n.rfind(L"kefico",0)==0)return L"Hyundai Kefico";
  if(n.rfind(L"transtron",0)==0)return L"Transtron";
  if(n.rfind(L"denso",0)==0)return L"Denso";
  if(n.rfind(L"temic",0)==0)return L"Temic";
  return L"Unknown";
}

// JS \s -> plain space so std::wregex behaves like the JS regexes.
inline ws spaceNorm(const ws& s) {
  ws o = s;
  for (auto& c : o)
    if (isJsSpace(c)) c = L' ';
  return o;
}

// windows-1252 as decoded by the WHATWG TextDecoder.
inline wchar_t cp1252(uint8_t b) {
  static const wchar_t hi[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
                                 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
                                 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
                                 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
  if (b >= 0x80 && b < 0xa0) return hi[b - 0x80];
  return b;
}

inline ws decode(const uint8_t* p, size_t n) {
  ws o;
  o.reserve(n);
  for (size_t i = 0; i < n; i++)
    if (p[i] != 0) o += cp1252(p[i]);
  return o;
}

inline std::string utf8(const ws& s) {
  std::string o;
  for (size_t i = 0; i < s.size(); i++) {
    uint32_t u = s[i];
    if (sizeof(wchar_t) == 2 && u >= 0xd800 && u < 0xdc00 && i + 1 < s.size() &&
        (uint32_t)s[i + 1] >= 0xdc00 && (uint32_t)s[i + 1] < 0xe000) {
      u = 0x10000 + ((u - 0xd800) << 10) + ((uint32_t)s[++i] - 0xdc00);
    }
    if (u < 0x80) o += (char)u;
    else if (u < 0x800) {
      o += (char)(0xc0 | (u >> 6));
      o += (char)(0x80 | (u & 0x3f));
    } else if (u >= 0x10000) {
      o += (char)(0xf0 | (u >> 18));
      o += (char)(0x80 | ((u >> 12) & 0x3f));
      o += (char)(0x80 | ((u >> 6) & 0x3f));
      o += (char)(0x80 | (u & 0x3f));
    } else {
      o += (char)(0xe0 | (u >> 12));
      o += (char)(0x80 | ((u >> 6) & 0x3f));
      o += (char)(0x80 | (u & 0x3f));
    }
  }
  return o;
}

inline ws fromAscii(const char* s) {
  ws o;
  while (*s) o += (wchar_t)(unsigned char)*s++;
  return o;
}

// ---------------------------------------------------------------- labels.ts
// Supplier words used by WinOLS when it generates placeholder labels.
inline bool isSupplierWord(const ws& w) {
  static const std::set<ws> s = {
      L"bosch", L"siemens", L"continental", L"delphi", L"denso", L"marelli", L"magneti", L"delco",
      L"visteon", L"temic", L"keihin", L"hitachi", L"sagem", L"lucas", L"motorola", L"trionic",
      L"transtron", L"vdo", L"kefico", L"uaes", L"weifu", L"mitsubishi", L"panasonic", L"valeo",
      L"vale\x00f3", L"aptiv", L"renesas", L"infineon", L"hella", L"aisin", L"zf"};
  return s.count(w) != 0;
}

// First run of ASCII letters, lower-cased ("Map \"Bosch\"" -> "map").
inline ws leadingWord(const ws& lowered) {
  ws w;
  for (wchar_t c : lowered) {
    if (c >= L'a' && c <= L'z') w += c;
    else if (c == 0x00f3 && !w.empty()) w += c;  // "valeó"
    else break;
  }
  return w;
}

// WinOLS "potential map" statistics captions: axis/size figures and the cell
// value range, e.g. "1700 =8192", "7500x120c =8192/0", "G 2000x1317 =<666",
// "95% =0" and a bare "10%". They contain no map identity.
inline bool isStatisticsCaption(const ws& s) {
  bool sawEquals = false, sawPercent = false, sawDigit = false;
  for (wchar_t c : s) {
    if (c == L'=') sawEquals = true;
    else if (c == L'%') sawPercent = true;
    else if (c >= L'0' && c <= L'9') sawDigit = true;
  }
  if (!sawDigit || (!sawEquals && !sawPercent)) return false;
  static const std::wregex R(
      L"^(?:[a-z]{1,2}\\s+)?-?\\d+(?:[.,]\\d+)?%?(?:\\s*[x\x00d7*]\\s*-?\\d+(?:[.,]\\d+)?)?[a-z]?"
      L"(?:\\s*=\\s*[<>]?\\s*-?\\d+(?:[.,]\\d+)?%?(?:\\s*/\\s*-?\\d+(?:[.,]\\d+)?)?[a-z]?)?\\s*$",
      std::regex::ECMAScript | std::regex::icase);
  if (!std::regex_match(s, R)) return false;
  // Without "=" only a bare percentage ("10%") qualifies.
  return sawEquals || s.back() == L'%';
}

// True when every word in lowered[from, to) is WinOLS layout vocabulary
// (supplier, family numeral, orientation, data type) and the rest is numbers or
// punctuation, e.g. `Bosch II 16/8`, `Denso Float`, `"Bosch 8"`.
inline bool isTypeVocabularyOnly(const ws& lowered, size_t from, size_t to) {
  static const std::set<ws> vocabulary = {
      L"i", L"ii", L"iii", L"iv", L"v", L"vi", L"vii", L"viii", L"ix", L"x", L"vertikal",
      L"vertical", L"horizontal", L"float", L"double", L"bit", L"bits", L"signed", L"unsigned",
      L"lohi", L"hilo", L"big", L"little", L"endian", L"statistical", L"statistisch",
      L"statistische", L"statistich", L"statistic", L"statistics",
      // Layout labels WinOLS gives DAMOS/A2L-imported maps: Kennfeld "Damos 8".
      L"damos", L"a2l", L"asap", L"asap2"};
  ws token;
  bool meaningful = false;
  auto flush = [&]() {
    if (!token.empty() && !vocabulary.count(token) && !isSupplierWord(token)) meaningful = true;
    token.clear();
  };
  for (size_t i = from; i < to && i < lowered.size(); ++i) {
    const wchar_t c = lowered[i];
    if ((c >= L'a' && c <= L'z') || c == 0x00f3) token += c;
    else if ((c >= L'0' && c <= L'9') || c == L' ' || c == L'/' || c == L'"' || c == L'\'' ||
             c == L'(' || c == L')' || c == L'!' || c == L'?' || c == L'-' || c == L'.')
      flush();
    else return false;  // anything else (letters outside ASCII, '_', ...) is not generated text
  }
  flush();
  return !meaningful;
}

// `Map "<type>"` / `Kennfeld '<type>'` where the quoted part is only WinOLS
// layout vocabulary (supplier, family numeral, orientation, data type, size).
inline bool isQuotedTypeLabel(const ws& lowered) {
  const ws first = leadingWord(lowered);
  if (first != L"map" && first != L"kennfeld" && first != L"kennlinie") return false;
  size_t q = first.size();
  while (q < lowered.size() && lowered[q] == L' ') ++q;
  if (q >= lowered.size() || (lowered[q] != L'"' && lowered[q] != L'\'')) return false;
  const wchar_t quote = lowered[q];
  const size_t close = lowered.find(quote, q + 1);
  if (close == ws::npos) return false;
  // Allowed after the quotes: WinOLS markers only, i.e. "(!)" and a part index
  // such as "[2/3]" or "[4]" (Map "Denso 8" [2/3], Kennfeld "Denso 16" [3/3] (!)).
  const ws tail = lowered.substr(close + 1);
  for (size_t i = 0; i < tail.size(); ++i) {
    const wchar_t c = tail[i];
    if (c == L' ' || c == L'(' || c == L')' || c == L'!') continue;
    if (c != L'[') return false;
    size_t j = i + 1;
    bool digits = false;
    while (j < tail.size() && ((tail[j] >= L'0' && tail[j] <= L'9') || tail[j] == L'/' || tail[j] == L' ')) {
      digits = digits || (tail[j] >= L'0' && tail[j] <= L'9');
      ++j;
    }
    if (!digits || j >= tail.size() || tail[j] != L']') return false;
    i = j;
  }
  return isTypeVocabularyOnly(lowered, q + 1, close);
}

inline bool isPlaceholderLabelUncached(const ws& raw) {
  static const std::set<ws> ENV = [] {
    std::set<ws> e;
    const wchar_t* list[] = {L"(damos)", L"ols 5.0 (windows)", L"hexdump", L"hex dump", L"hex-dump",
                             L"my maps", L"my map", L"meine kennfelder", L"eigene kennfelder",
                             L"potential maps", L"potential map", L"potentielle kennfelder",
                             L"potentielles kennfeld", L"m\x00f6gliche kennfelder",
                             L"moegliche kennfelder", L"original", L"passenger car", L"engine",
                             L"binary data", L"0 (original)", L"version 1", L"differences", L"text",
                             L"untitled", L"unnamed", L"new map", L"newmap", L"map", L"kennfeld",
                             L"kennlinie", L"einzelwert", L"value", L"curve", L"imported",
                             L"default"};
    for (auto* x : list) e.insert(x);
    return e;
  }();
  using R = std::wregex;
  const auto fl = std::regex::ECMAScript | std::regex::icase;
  static const R PRODUCER(
      L"^(bosch|siemens|continental|delphi|denso|marelli|magneti(?:\\s+marelli)?|delco|visteon|temic|"
      L"keihin|hitachi|sagem|lucas|motorola|trionic|transtron|vdo|kefico|uaes|weifu)\\b",
      fl);
  static const R GENERIC(
      L"^(map|kennfeld|kennlinie|einzelwert|value|curve|potential(?:\\s+map)?|pot\\.?\\s*map|"
      L"my\\s+map|new\\s+map|unnamed|untitled)(?:\\s*[#:_-]?\\s*\\d+)?$",
      fl);
  // Exclude generated family/supplier/index labels such as Map "Bosch II 16".
  static const R FAMILY_MAP(
      L"^(?:map|kennfeld|kennlinie)\\s*\\\"?\\s*(?:bosch|siemens|continental|delphi|denso|marelli|"
      L"magneti(?:\\s+marelli)?|delco|visteon|temic|keihin|hitachi|sagem|lucas|motorola|trionic|transtron|"
      L"vdo|kefico|uaes|weifu|mitsubishi|panasonic|vale[o\x00f3]|aptiv|renesas|hella|aisin|zf|statistical)"
      L"(?:\\s+(?:ii|iii|iv|vertikal|vertical|horizontal))?\\s+(?:\\d+(?:\\s*/\\s*\\d+)?|float)\\s*\\\"?\\s*(?:\\(!\\))?$",
      fl);
  // A supplier followed only by a Roman-numeral family/index label (e.g. "Bosch II")
  // is an autogenerated family label, not a human-authored map title. Keep descriptive
  // names such as "Bosch II torque limiter" eligible.
  static const R PRODUCER_FAMILY(
      L"^(bosch|siemens|continental|delphi|denso|marelli|magneti(?:\\s+marelli)?|delco|visteon|temic|"
      L"keihin|hitachi|sagem|lucas|motorola|trionic|transtron|vdo|kefico|uaes|weifu|mitsubishi|"
      L"panasonic|vale[o\x00f3]|aptiv|renesas|infineon|hella|aisin|zf)\\s+(?:i|ii|iii|iv|v|vi|vii|viii|ix|x)(?:\\s+\\d+)?$",
      fl);
  // WinOLS-generated supplier labels are placeholders, not human-authored map names.
  static const R PRODUCER_MAP(
      L"^(bosch|siemens|continental|delphi|denso|marelli|magneti(?:\\s+marelli)?|delco|visteon|temic|"
      L"keihin|hitachi|sagem|lucas|motorola|trionic|transtron|vdo|kefico|uaes|weifu|mitsubishi|"
      L"panasonic|vale[o\x00f3]|aptiv|renesas|infineon|hella|aisin|zf)\\s+(?:map|kennfeld|kennlinie)\\b",
      fl);
  static const R ONLYNUM(L"^\\d+$");
  static const R HEXID(L"^(?:0x[0-9a-f]{1,8}|(?=[0-9a-f]{8,16}$)(?=[0-9a-f]*[0-9])[0-9a-f]+)$", fl);
  static const R HEXADDRPAIR(L"^(?:0x)?[0-9a-f]{4,16}(?:\\s*/\\s*(?:0x)?[0-9a-f]{4,16})+$", fl);
  static const R SHORTINDEX(L"^[a-z][0-9]$", fl);
  static const R DIMONLY(L"^\\d+\\s*[xX\x00d7*]\\s*\\d+$", fl);
  static const R BIT(L"\\d+\\s*-?\\s*bits?\\b", fl);
  static const R DIM(L"\\d+\\s*[xX\x00d7*]\\s*\\d+", fl);
  static const R ORDER(L"\\b(lohi|hilo|big[\\s-]?endian|little[\\s-]?endian)\\b", fl);
  static const R BITORDER(L"^\\d+\\s*-?\\s*bits?\\b[\\s,.-]*(lohi|hilo)\\b", fl);
  ws s = spaceNorm(jsTrim(removeNul(raw)));  // trim happens before normalising in JS; same result
  if (s.empty()) return true;
  if (s == L"-" || s == L"?" || s == L"%") return true;
  // One character ("V", "?") never names a map.
  if (s.size() == 1) return true;
  // Unknown-marker question marks ("Map ???", "???") add nothing: judge the rest.
  if (s.find(L'?') != ws::npos) {
    ws stripped;
    for (wchar_t ch : s)
      if (ch != L'?') stripped += ch;
    stripped = jsTrim(stripped);
    if (stripped.empty()) return true;
    if (isPlaceholderLabelUncached(stripped)) return true;
  }

  // n = s.toLowerCase().replace(/\s+/g, " ")
  ws n;
  bool prevSpace = false;
  for (wchar_t c : lower(s)) {
    if (c == L' ') {
      if (!prevSpace) n += L' ';
      prevSpace = true;
    } else {
      n += c;
      prevSpace = false;
    }
  }
  if (ENV.count(n)) return true;
  if (n.rfind(L"imported from", 0) == 0) return true;
  // WinOLS uses several spellings of its autogenerated statistical-map label,
  // including the truncated/misspelled "Statistich". Strip generic record
  // prefixes first so these labels never become real map names.
  ws statistical = n;
  for (const ws& prefix : {ws(L"map"), ws(L"kennfeld"), ws(L"kennlinie")}) {
    if (statistical.rfind(prefix, 0) == 0) {
      statistical.erase(0, prefix.size());
      while (!statistical.empty() &&
             (statistical.front() == L' ' || statistical.front() == 34 ||
              statistical.front() == L':' || statistical.front() == L'_' ||
              statistical.front() == L'-'))
        statistical.erase(statistical.begin());
      break;
    }
  }
  if (statistical.rfind(L"statist", 0) == 0) return true;

  // Cheap necessary conditions gate every regex below. Most real labels are
  // DAMOS identifiers (e.g. "Mo_trqIdcMax_C") that pass none of the gates, so a
  // multi-thousand-map project no longer runs ~15 regexes per record.
  // Prefix (not whole-word) tests: the regexes allow zero spaces after the
  // leading word (e.g. "mapBosch 16", "potmap"), so the gates must too.
  auto startsWithAny = [&](std::initializer_list<const wchar_t*> prefixes) {
    for (const wchar_t* p : prefixes)
      if (n.rfind(p, 0) == 0) return true;
    return false;
  };
  const bool genericLead = startsWithAny({L"map", L"kennfeld", L"kennlinie", L"einzelwert", L"value",
                                          L"curve", L"pot", L"my", L"new", L"unnamed", L"untitled"});
  const bool typeLead = startsWithAny({L"map", L"kennfeld", L"kennlinie"});
  const bool supplierLead = startsWithAny({
      L"bosch", L"siemens", L"continental", L"delphi", L"denso", L"marelli", L"magneti", L"delco",
      L"visteon", L"temic", L"keihin", L"hitachi", L"sagem", L"lucas", L"motorola", L"trionic",
      L"transtron", L"vdo", L"kefico", L"uaes", L"weifu", L"mitsubishi", L"panasonic", L"vale",
      L"aptiv", L"renesas", L"infineon", L"hella", L"aisin", L"zf"});
  bool hasDigit = false, onlyDigits = true, hexish = true, hexPairish = true;
  bool hasDimMark = false;
  for (wchar_t c : n) {
    const bool digit = c >= L'0' && c <= L'9';
    const bool hex = digit || (c >= L'a' && c <= L'f');
    if (digit) hasDigit = true;
    else onlyDigits = false;
    if (!hex && c != L'x') hexish = false;
    if (!hex && c != L'x' && c != L'/' && c != L' ') hexPairish = false;
    if (c == L'x' || c == 0x00d7 || c == L'*') hasDimMark = true;
  }
  const bool mentionsBit = n.find(L"bit") != ws::npos;
  const bool mentionsOrder = n.find(L"lohi") != ws::npos || n.find(L"hilo") != ws::npos ||
                             n.find(L"endian") != ws::npos;

  if (genericLead && std::regex_search(s, GENERIC)) return true;
  if (typeLead) {
    // WinOLS may serialize the same generated family label with either quote style.
    // Canonicalize apostrophes only for this narrowly-scoped placeholder test.
    ws familyLabel = s;
    std::replace(familyLabel.begin(), familyLabel.end(), static_cast<wchar_t>(39), static_cast<wchar_t>(34));
    if (std::regex_match(familyLabel, FAMILY_MAP)) return true;
    if (isQuotedTypeLabel(n)) return true;
  }
  if (supplierLead || n[0] == L'"' || n[0] == L'\'') {
    // Supplier/type captions on their own: `Bosch 16`, `Denso Float`, `"Bosch 8"`.
    size_t from = 0, to = n.size();
    if (n[0] == L'"' || n[0] == L'\'') ++from;
    ws lead = leadingWord(n.substr(from));
    if (isSupplierWord(lead) && isTypeVocabularyOnly(n, from, to)) return true;
  }
  if (supplierLead) {
    if (std::regex_match(s, PRODUCER_FAMILY)) return true;
    // WinOLS-generated supplier map labels can carry machine-generated
    // formatting (e.g. "Bosch map 2: 16 bit / 16x16"). Exclude these only
    // when the tail contains numeric/dimension/encoding tokens and no human words.
    std::wsmatch producerMapMatch;
    if (std::regex_search(s, producerMapMatch, PRODUCER_MAP) &&
        producerMapMatch.position() == 0) {
      ws tail = lower(s.substr((size_t)producerMapMatch.length()));
      bool hasHumanTailWord = false;
      ws token;
      auto checkTailToken = [&]() {
        if (token.empty()) return;
        ws cleaned;
        for (wchar_t c : token)
          if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == 0x00d7)
            cleaned += c;
        if (cleaned.empty()) { token.clear(); return; }
        static const std::set<ws> technical = {
            L"bit", L"bits", L"lohi", L"hilo", L"big", L"little", L"endian", L"bigendian", L"littleendian",
            L"signed", L"unsigned", L"be", L"le", L"msb", L"lsb", L"x"};
        bool numeric = std::all_of(cleaned.begin(), cleaned.end(), [](wchar_t c) {
          return (c >= L'0' && c <= L'9') || c == 0x00d7;
        });
        bool hexIndex = cleaned.size() > 2 && cleaned[0] == L'0' && cleaned[1] == L'x' &&
            std::all_of(cleaned.begin() + 2, cleaned.end(), [](wchar_t c) {
              return (c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f');
            });
        if (!numeric && !hexIndex && !technical.count(cleaned) &&
            !std::regex_match(cleaned, DIMONLY))
          hasHumanTailWord = true;
        token.clear();
      };
      for (wchar_t c : tail) {
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == 0x00d7)
          token += c;
        else
          checkTailToken();
      }
      checkTailToken();
      if (!hasHumanTailWord) return true;
    }
  }
  // "beef / cafe" is a valid all-letter hex address pair, so no digit gate here.
  if (hexPairish && n.find(L'/') != ws::npos && std::regex_search(s, HEXADDRPAIR)) return true;
  if (hasDigit) {
    if (onlyDigits && std::regex_search(s, ONLYNUM)) return true;
    if (hexish && std::regex_search(s, HEXID)) return true;
    if (s.size() == 2 && std::regex_search(s, SHORTINDEX)) return true;
    if (hasDimMark && n[0] >= L'0' && n[0] <= L'9' && std::regex_search(s, DIMONLY)) return true;
    if (isStatisticsCaption(n)) return true;
  }

  bool hasBit = hasDigit && mentionsBit && std::regex_search(s, BIT);
  bool hasDim = hasDigit && hasDimMark && std::regex_search(s, DIM);
  bool hasOrder = mentionsOrder && std::regex_search(s, ORDER);
  // A bit-width/dimension string is autogenerated only when it contains no
  // actual map identifier. Strip known technical descriptors and (if present) the
  // leading supplier; any meaningful remainder makes it a human map name.
  if (hasBit || hasDim || hasOrder) {
    ws residue = lower(s);
    std::wsmatch supplierMatch;
    if (supplierLead && std::regex_search(residue, supplierMatch, PRODUCER) &&
        supplierMatch.position() == 0) {
      residue = residue.substr((size_t)supplierMatch.length());
    }
    // Tokenize cheaply instead of running several regex replacements per map.
    static const std::set<ws> TECHNICAL_TOKENS = {
        L"bit", L"bits", L"map", L"maps", L"x", L"lohi", L"hilo",
        L"big", L"little", L"endian", L"be", L"le", L"msb", L"lsb",
        L"signed", L"unsigned"
    };
    bool meaningful = false;
    ws token;
    auto consumeToken = [&]() {
      if (!token.empty() && !TECHNICAL_TOKENS.count(token)) meaningful = true;
      token.clear();
    };
    for (wchar_t c : residue) {
      const bool asciiLetter = c >= L'a' && c <= L'z';
      const bool latinLetter = c >= 0x00c0 && c <= 0x00ff && c != 0x00d7 && c != 0x00f7;
      if (asciiLetter || latinLetter) token += c;
      else consumeToken();
    }
    consumeToken();
    if (!meaningful) return true;
  }
  if (hasDigit && mentionsBit && n[0] >= L'0' && n[0] <= L'9' &&
      std::regex_search(s, BITORDER) && s.size() < 48)
    return true;
  return false;
}

// Labels repeat heavily across a project (and across projects of one family),
// so memoize the verdict per worker thread. Bounded to keep memory flat on
// 4,000+ project scans.
inline bool isPlaceholderLabel(const ws& raw) {
  thread_local std::unordered_map<ws, bool> cache;
  auto it = cache.find(raw);
  if (it != cache.end()) return it->second;
  const bool verdict = isPlaceholderLabelUncached(raw);
  if (cache.size() >= 200000) cache.clear();
  cache.emplace(raw, verdict);
  return verdict;
}

inline bool isHexdump(const ws& description) {
  static const std::wregex R(L"^hex\\s*-?\\s*dump$", std::regex::ECMAScript | std::regex::icase);
  return std::regex_search(spaceNorm(jsTrim(removeNul(description))), R);
}

// A map *name* that is only a record identifier: a decimal/hex ID, address
// (pair), one-letter index or symbol such as "00004b0e", "0x2",
// "0x1b2f / 0x1b2f", "A0", "N0" or "n", or a bare record word such as "Map". Importers (Cummins
// calibrations, GM mappacks, DAMOS conversions) put the real map title in the
// description in that case.
inline bool isIdOnlyMapName(const ws& raw) {
  ws s = jsTrim(removeNul(raw));
  if (s.empty()) return false;
  const ws n = lower(spaceNorm(s));
  if (n == L"map" || n == L"kennfeld" || n == L"kennlinie" || n == L"einzelwert" ||
      n == L"value" || n == L"curve")
    return true;
  // One-letter symbols ("n" = engine speed) carry their meaning in the description.
  if (n.size() == 1) return true;
  bool hasDigit = false;
  for (wchar_t c : n) {
    const bool digit = c >= L'0' && c <= L'9';
    const bool hex = digit || (c >= L'a' && c <= L'f');
    if (digit) hasDigit = true;
    if (!hex && c != L'x' && c != L'/' && c != L' ' && !(c >= L'a' && c <= L'z')) return false;
  }
  if (!hasDigit) return false;
  static const std::wregex ID(
      L"^(?:\\d+|0x[0-9a-f]{1,8}|[0-9a-f]{8,16}|(?:0x)?[0-9a-f]{4,16}(?:\\s*/\\s*(?:0x)?[0-9a-f]{4,16})+|[a-z][0-9])$",
      std::regex::ECMAScript | std::regex::icase);
  return std::regex_match(n, ID);
}

inline bool isDefinedMap(const ws& name, const ws& description) {
  // The explicit map name is authoritative when it is a real label. Two
  // fallbacks keep valuable names from being dropped:
  //  - empty name: some schema families store the human label in the description;
  //  - ID-only name (hex/number/address/index or bare "Map"): importers put the
  //    record ID in the name and the calibration name in the description.
  // A generated supplier name ("Bosch map 2") still never borrows a description.
  ws t = jsTrim(removeNul(name));
  ws fallback = jsTrim(removeNul(description));
  if (!t.empty()) {
    if (!isPlaceholderLabel(t)) return true;
    if (!isIdOnlyMapName(t)) return false;
    if (fallback.empty() || lower(fallback) == lower(t)) return false;
    return !isPlaceholderLabel(fallback);
  }
  return !fallback.empty() && !isPlaceholderLabel(fallback);
}

inline bool isIdent(const ws& s) {
  ws t = jsTrim(s);
  if (t.size() < 2 || t.size() > 64) return false;
  auto first = [](wchar_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; };
  if (!first(t[0])) return false;
  for (wchar_t c : t)
    if (!(first(c) || (c >= '0' && c <= '9'))) return false;
  return true;
}

inline bool isHumanTitle(const ws& raw) {
  ws t = jsTrim(removeNul(raw));
  if (t.size() < 3 || t.size() > 180) return false;
  if (isPlaceholderLabel(t)) return false;
  size_t n = 0;
  for (wchar_t c : t)
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0x24f)) n++;
  return n >= 3 && (double)n / (double)t.size() >= 0.35;
}

inline bool looksLikeRealMap(const ws& name, const ws& description) {
  ws nameT = jsTrim(name), descT = jsTrim(description);
  if (nameT.empty() && descT.empty()) return false;
  if (isPlaceholderLabel(nameT) || isPlaceholderLabel(descT)) return true;
  if (isIdent(nameT)) return true;
  if (isHumanTitle(nameT) || isHumanTitle(descT)) return true;
  return false;
}

// -------------------------------------------------------------- filename.ts
// NFKD + strip combining marks for the characters windows-1252 can produce.
inline ws foldAccents(const ws& in) {
  ws o;
  for (wchar_t c : in) {
    const wchar_t* rep = nullptr;
    wchar_t one[2] = {0, 0};
    if (c >= 0xc0 && c <= 0xff) {
      static const char* map =
          "AAAAAA?CEEEEIIII?NOOOOO??UUUUY??aaaaaa?ceeeeiiii?nooooo??uuuuy?y";  // index c-0xC0
      char m = map[c - 0xc0];
      if (m != '?') {
        one[0] = (wchar_t)m;
        rep = one;
      }
    } else if (c == 0x160) rep = L"S";
    else if (c == 0x161) rep = L"s";
    else if (c == 0x17d) rep = L"Z";
    else if (c == 0x17e) rep = L"z";
    else if (c == 0x178) rep = L"Y";
    else if (c == 0x2122) rep = L"TM";
    else if (c == 0xb2) rep = L"2";
    else if (c == 0xb3) rep = L"3";
    else if (c == 0xb9) rep = L"1";
    else if (c == 0xaa) rep = L"a";
    else if (c == 0xba) rep = L"o";
    else if (c == 0xa0 || c == 0xa8 || c == 0xaf || c == 0xb4 || c == 0xb8 || c == 0x2dc) rep = L" ";
    else if (c == 0xb5) rep = L"\x03bc";
    else if (c == 0x2026) rep = L"...";
    else if (c >= 0x300 && c <= 0x36f) rep = L"";
    if (rep) o += rep;
    else o += c;
  }
  return o;
}

inline ws filenamePart(const ws& raw) {
  ws s = foldAccents(raw);
  ws t;
  for (wchar_t c : s) {  // ILLEGAL = [<>:"/\\|?*\u0000-\u001f]
    if (c < 0x20 || c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
        c == '|' || c == '?' || c == '*')
      continue;
    t += c;
  }
  ws u;  // [-\u2010-\u2015\u2212_]+ -> " "
  bool inRun = false;
  for (wchar_t c : t) {
    bool sep = c == '-' || (c >= 0x2010 && c <= 0x2015) || c == 0x2212 || c == '_';
    if (sep) {
      if (!inRun) u += L' ';
      inRun = true;
    } else {
      u += c;
      inRun = false;
    }
  }
  ws v;  // \s+ -> " "
  bool inSp = false;
  for (wchar_t c : u) {
    if (isJsSpace(c)) {
      if (!inSp) v += L' ';
      inSp = true;
    } else {
      v += c;
      inSp = false;
    }
  }
  ws w;  // \.+ -> "."
  for (wchar_t c : v) {
    if (c == '.' && !w.empty() && w.back() == '.') continue;
    w += c;
  }
  auto trimDotSpace = [](ws x) {
    size_t a = 0, b = x.size();
    while (a < b && (x[a] == '.' || isJsSpace(x[a]))) a++;
    while (b > a && (x[b - 1] == '.' || isJsSpace(x[b - 1]))) b--;
    return x.substr(a, b - a);
  };
  ws cleaned = trimDotSpace(w);
  if (cleaned.size() > 80) cleaned.resize(80);
  {
    size_t b = cleaned.size();
    while (b > 0 && (cleaned[b - 1] == '.' || isJsSpace(cleaned[b - 1]))) b--;
    cleaned.resize(b);
  }
  ws lc = lower(cleaned);
  bool reserved = lc == L"con" || lc == L"prn" || lc == L"aux" || lc == L"nul";
  if (lc.size() == 4 && (lc.rfind(L"com", 0) == 0 || lc.rfind(L"lpt", 0) == 0) && lc[3] >= '1' &&
      lc[3] <= '9')
    reserved = true;
  if (reserved) cleaned += L" file";
  return cleaned.empty() ? ws(L"Unknown") : cleaned;
}

inline bool isUnknownNamePart(const ws& raw);
inline ws firstFilled(const std::vector<ws>& values) {
  for (const ws& v : values) {
    ws t = jsTrim(removeNul(v));
    if (!isUnknownNamePart(t)) return t;  // skips "", "-", "?", "unknow", "n/a", ...
  }
  return L"";
}

inline bool isUnknownNamePart(const ws& raw) {
  ws value = lower(jsTrim(removeNul(raw)));
  static const std::set<ws> placeholders = {
      L"", L"unknown", L"unknow", L"unkown", L"unknwon", L"unknonw", L"unbekannt", L"inconnu",
      L"desconocido", L"sconosciuto", L"n/a", L"n.a.", L"none", L"not identified",
      L"not available", L"-", L"--", L"?", L"??", L"???", L"%"};
  return placeholders.count(value) != 0;
}

// Letters and digits only, lower-cased: "MSD 85" == "MSD85" == "msd-85".
inline ws alnumKey(const ws& s) {
  ws k;
  for (wchar_t c : s) {
    if (c >= L'A' && c <= L'Z') k += (wchar_t)(c + 32);
    else if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9')) k += c;
  }
  return k;
}

// "DAMOS" (Bosch/Siemens calibration description files) says how the project
// was built, not what it is, so it never appears in a proposed file name.
// Removes the word wherever it occurs ("Damos", "(DAMOS)", "2.0D - DAMOS",
// "damos35d", "BMWedc15damos", "Z-DAMOS") and tidies what is left; a part
// that held nothing else becomes empty.
inline ws stripDamos(const ws& raw) {
  ws s = raw;
  for (;;) {
    const ws low = lower(s);
    const size_t at = low.find(L"damos");
    if (at == ws::npos) break;
    s.erase(at, 5);
  }
  if (s.size() == raw.size()) return raw;
  // Brackets that are now empty, then separators left dangling at either end.
  for (bool changed = true; changed;) {
    changed = false;
    for (const wchar_t* pair : {L"()", L"[]", L"{}"}) {
      for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != pair[0]) continue;
        size_t j = i + 1;
        while (j < s.size() && (s[j] == L' ' || s[j] == L'-' || s[j] == L'_' || s[j] == L',')) ++j;
        if (j < s.size() && s[j] == pair[1]) { s.erase(i, j - i + 1); changed = true; break; }
      }
    }
  }
  ws out;
  bool space = false;
  for (wchar_t c : s) {
    if (isJsSpace(c) || c == L'_') { space = true; continue; }
    if (space && !out.empty()) out += L' ';
    space = false;
    out += c;
  }
  auto sep = [](wchar_t c) {
    return c == L' ' || c == L'-' || c == L',' || c == L';' || c == L':' || c == L'/' || c == L'?' ||
           c == L'.' || c == L'+' || c == L'&';
  };
  size_t a = 0, b = out.size();
  while (a < b && sep(out[a])) ++a;
  while (b > a && sep(out[b - 1])) --b;
  out = out.substr(a, b - a);
  // " - " or " , " left in the middle by "2.0D - DAMOS Stage" style text.
  for (const wchar_t* pat : {L" - - ", L" , ", L" -  "}) {
    size_t at;
    while ((at = out.find(pat)) != ws::npos) out.replace(at, wcslen(pat), L" ");
  }
  return out;
}

// Standard OBD-II diagnostic trouble codes (P0420, C0035, B1234, U0100, and
// optional manufacturer suffix). These are never software/calibration IDs.
inline bool isDtcCode(const ws& raw) {
  static const std::wregex re(L"^[PCBU]-?[0-9]{4}[0-9A-F]{0,2}$",
                              std::regex::ECMAScript | std::regex::icase);
  return std::regex_match(jsTrim(raw), re);
}

inline ws proposedFilename(const ws& brand, const ws& model, const ws& ecu, const ws& software,
                           int definedMaps) {
  ws prefix;
  std::set<ws> seenParts;
  std::vector<ws> priorParts;
  auto appendPart = [&](const ws& rawPart) {
    const ws raw = stripDamos(rawPart);
    if (isUnknownNamePart(raw)) return;
    // Diagnostic trouble codes are never useful filename identity.
    if (isDtcCode(raw)) return;
    ws part = jsTrim(raw);
    // Remove an already-emitted slot when it is embedded as a whole phrase in a
    // longer later slot (e.g. make="Alfa Romeo", model="Alfa Romeo Giulietta").
    for (const ws& prior : priorParts) {
      ws needle = lower(jsTrim(prior));
      if (needle.empty()) continue;
      ws lowPart = lower(part);
      size_t pos = lowPart.find(needle);
      while (pos != ws::npos) {
        const size_t end = pos + needle.size();
        auto word = [](wchar_t c) {
          return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                 (c >= L'0' && c <= L'9');
        };
        if ((pos == 0 || !word(lowPart[pos - 1])) &&
            (end == lowPart.size() || !word(lowPart[end]))) {
          part.replace(pos, needle.size(), L" ");
          lowPart = lower(part);
          pos = lowPart.find(needle, pos);
        } else {
          pos = lowPart.find(needle, end);
        }
      }
    }
    part = jsTrim(part);
    ws compact;
    bool wasSpace = false;
    for (wchar_t c : part) {
      if (isJsSpace(c)) {
        if (!wasSpace) compact += L' ';
        wasSpace = true;
      } else {
        compact += c;
        wasSpace = false;
      }
    }
    part = jsTrim(compact);
    if (part.empty()) return;
    part = filenamePart(part);
    if (part.empty() || lower(part) == L"unknown") return;
    // Compare normalized words, so separators/case changes cannot duplicate a slot.
    ws key = catalogNorm(part);
    if (key.empty() || seenParts.count(key)) return;
    // Same token written differently ("MSD85" then "MSD 85").
    for (const ws& prior : priorParts)
      if (alnumKey(prior) == alnumKey(part)) return;
    // Avoid adding a shorter slot already present as a complete phrase in the name.
    ws prefixKey = L" " + catalogNorm(prefix) + L" ";
    ws phrase = L" " + key + L" ";
    if (!prefix.empty() && prefixKey.find(phrase) != ws::npos) {
      seenParts.insert(key);
      return;
    }
    if (!prefix.empty()) prefix += L" ";
    prefix += part;
    seenParts.insert(key);
    priorParts.push_back(part);
  };
  appendPart(brand);
  appendPart(model);
  appendPart(ecu);
  ws sw = firstFilled({software});
  if (!sw.empty()) appendPart(sw);
  if (prefix.empty()) prefix = L"OLS Project";
  ws countLabel = definedMaps == 0 ? ws(L"Nil") : std::to_wstring(definedMaps);
  ws suffix = L" (" + countLabel + L").ols";
  size_t cap = 240 > suffix.size() ? 240 - suffix.size() : 1;
  if (prefix.size() > cap) prefix.resize(cap);
  while (!prefix.empty() && (prefix.back() == L' ' || prefix.back() == L'.')) prefix.pop_back();
  return prefix + suffix;
}

inline ws disambiguate(const ws& desired, std::set<ws>& taken) {
  ws key = lower(desired);
  if (!taken.count(key)) {
    taken.insert(key);
    return desired;
  }
  ws stem = desired;
  if (stem.size() >= 4 && lower(stem.substr(stem.size() - 4)) == L".ols") stem.resize(stem.size() - 4);
  int n = 2;
  ws cand = stem + L" " + std::to_wstring(n) + L".ols";
  while (taken.count(lower(cand))) {
    n++;
    cand = stem + L" " + std::to_wstring(n) + L".ols";
  }
  taken.insert(lower(cand));
  return cand;
}

// --------------------------------------------------------------- analyze.ts
struct MapRow {
  ws name, description;
  bool defined = false;
};

struct MapDetail { ws name, description; bool defined = false; };

struct Analysis {
  bool ok = false, headerComplete = false, formatSizeConsistent = true, renameSafe = false;
  ws error;
  std::vector<ws> warnings;
  uint32_t formatVersion = 0;
  bool versionDirectoryValid = false;
  uint32_t versionCount = 0, versionRecordSize = 0;
  size_t versionDataStart = 0;
  ws brand = L"Unknown", model = L"Unknown", ecu = L"Unknown", software;
  ws ecuManufacturer = L"Unknown", hardwareNumber, productionNumber, engineCode, vehicleYear;
  ws brandSource = L"unknown";
  ws originalFileName, embeddedHexFilename, softwareSource = L"unknown", ecuSource = L"unknown";
  std::vector<std::pair<ws, ws>> metadata;
  std::vector<MapDetail> mapDetails;
  size_t mapCount = 0;
  int definedCount = 0, skippedCount = 0;
  ws proposed;
  std::string mapSource = "unread";
  // Fast map finding diagnostics: where the map/ID area ends and the embedded
  // ROM images begin, and how much of the file had to be read.
  size_t mapAreaEnd = 0, romImageStart = 0, romImageEnd = 0;
  size_t mapDirectoryOffset = 0;  // marker/record start of the chosen map directory
  uint64_t fileSize = 0, bytesLoaded = 0;
  bool needsFullFile = false;
};

inline uint32_t rd32(const uint8_t* d, size_t p) {
  return (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) | ((uint32_t)d[p + 2] << 16) |
         ((uint32_t)d[p + 3] << 24);
}
inline uint64_t rd64(const uint8_t* d, size_t p) {
  return (uint64_t)rd32(d, p) | ((uint64_t)rd32(d, p + 4) << 32);
}

struct VersionDirectory {
  size_t markerOffset = 0;
  uint32_t count = 0;
  size_t dataStart = 0;       // start of version records, normally begins with M3
  uint32_t recordSize = 0;
  size_t endOffset = 0;
};

// M1 is a version-directory fence, not a map-directory marker.
// Its preceding words are [version_count - 1, version_data_start, version_record_size].
// `loaded` bytes of a `fileSize`-byte project are in memory. When only a prefix
// is loaded (fast map-area loading), arithmetic is still checked against the
// real file size; M3 corroboration needs the 4 bytes at version_data_start.
// `earlyStop` reports whether the scan ended on the strongest possible match
// (M3-corroborated, array ending exactly at EOF), i.e. no byte beyond it could
// change the result.
inline bool locateVersionDirectory(const uint8_t* d, size_t loaded, uint64_t fileSize,
                                   VersionDirectory& out, bool* earlyStop = nullptr) {
  if (earlyStop) *earlyStop = false;
  const size_t n = loaded;
  if (!d || n < 16) return false;
  bool found = false;
  bool bestHasM3 = false;
  uint64_t bestTrailing = fileSize + 1;
  // The version-directory marker belongs to the serialized project header,
  // before the embedded ROM/version array. Scanning an entire multi-megabyte
  // ROM for marker-shaped bytes is both slow and prone to false dispatch.
  // Real corpus samples place M1 well inside this bounded header region.
  const size_t searchEnd = std::min<size_t>(n, 8u * 1024u * 1024u);
  // Search for the first byte of the little-endian M1 marker with memchr.
  // The former byte-by-byte rd32 loop made four loads at every offset and
  // examined nearly 8 MiB per project; that compounds on 4,000+ file corpora.
  size_t cursor = 12;
  const size_t last = searchEnd >= 4 ? searchEnd - 4 : 0;
  while (cursor <= last) {
    const void* hit = std::memchr(d + cursor, 0x99, last - cursor + 1);
    if (!hit) break;
    const size_t p = static_cast<const uint8_t*>(hit) - d;
    cursor = p + 1;
    if (rd32(d, p) != 0x42007899u) continue;
    const uint32_t countMinusOne = rd32(d, p - 12);
    const uint32_t start = rd32(d, p - 8);
    const uint32_t recordSize = rd32(d, p - 4);
    const uint64_t count64 = (uint64_t)countMinusOne + 1u;
    if (count64 > 64 || recordSize == 0 ||
        recordSize > 64u * 1024u * 1024u) continue;
    if (start < p + 4 || (uint64_t)start + 4u > fileSize) continue;
    const uint64_t end64 = (uint64_t)start + 4u + (uint64_t)recordSize * count64;
    if (end64 > fileSize) continue;

    // M3 (0x98728833) is commonly the first word at version_data_start.
    // Treat it as corroborating structure, not an unconditional requirement:
    // older/unusual schema variants may omit it. This prevents a random M1-like
    // sequence in a ROM payload from winning merely because its arithmetic ends
    // exactly at EOF.
    const bool hasM3 = (size_t)start + 4u <= n && rd32(d, start) == 0x98728833u;
    const uint64_t trailing = fileSize - end64;
    const bool better = !found || (hasM3 && !bestHasM3) ||
                        (hasM3 == bestHasM3 && trailing < bestTrailing);
    if (better) {
      found = true;
      bestHasM3 = hasM3;
      bestTrailing = trailing;
      out.markerOffset = p;
      out.count = (uint32_t)count64;
      out.dataStart = start;
      out.recordSize = recordSize;
      out.endOffset = (size_t)end64;
      // A corroborated M3 directory whose record array ends exactly at EOF is
      // the strongest available structural match. Stop here instead of scanning
      // megabytes of embedded ROM bytes for further marker collisions.
      if (hasM3 && trailing == 0) {
        if (earlyStop) *earlyStop = true;
        return true;
      }
    }
  }
  return found;
}

inline bool locateVersionDirectory(const uint8_t* d, size_t n, VersionDirectory& out) {
  return locateVersionDirectory(d, n, n, out);
}

// Fast map finding: the map/ID area is everything before the embedded ROM
// version array (M1 version_data_start). Only that area, plus the first ROM
// image for identity strings, is needed for naming. Returns how many leading
// bytes of the file must be loaded; callers grow their buffer until it is
// satisfied. Falls back to the whole file whenever the boundary cannot be
// proven from the loaded prefix, so results never depend on the prefix size.
// First read size for the map-area loader. M1 sits in the serialized header
// (0x5FE in the documented sample), so 1 MiB normally proves the boundary in
// one read; the loader grows the buffer when it does not.
static constexpr size_t kInitialLoadBytes = 1u << 20;

inline uint64_t requiredLoadLength(const uint8_t* d, size_t loaded, uint64_t fileSize) {
  if (loaded >= fileSize) return fileSize;
  VersionDirectory vd;
  bool early = false;
  if (!locateVersionDirectory(d, loaded, fileSize, vd, &early)) {
    // M1 may lie beyond the loaded prefix: its search window is the first 8 MiB.
    const uint64_t window = 8ull * 1024u * 1024u;
    return loaded < std::min<uint64_t>(fileSize, window) ? std::min<uint64_t>(fileSize, window)
                                                          : fileSize;
  }
  // Without the early stop the full-file scan could still prefer a later M1
  // inside its 8 MiB window; load that window so the choice is identical.
  uint64_t need = (uint64_t)vd.dataStart + 4u + (uint64_t)vd.recordSize;  // first ROM image
  if (!early) need = std::max<uint64_t>(need, std::min<uint64_t>(fileSize, 8ull * 1024u * 1024u));
  return std::min<uint64_t>(need, fileSize);
}

inline bool isText(const uint8_t* d, size_t off, size_t n) {
  if (n == 0) return false;
  size_t ok = 0;
  for (size_t i = off; i < off + n; i++) {
    uint8_t b = d[i];
    if ((b >= 32 && b < 127) || b == 9 || b == 10 || b == 13 || b >= 0x80) ok++;
  }
  return ok * 100 >= n * 85;
}

struct CStr {
  size_t end;
  bool valid;
  ws text;
};

inline CStr readCString(const uint8_t* d, size_t n, size_t off, uint32_t schema, uint32_t maxLen) {
  if (off + 4 > n) return {off, false, L""};
  int32_t slen = (int32_t)rd32(d, off);
  if (schema >= 439) {
    if (slen == -1) return {off + 4, true, L"-"};
    if (slen == -2) return {off + 4, true, L"?"};
    if (slen == -3) return {off + 4, true, L"%"};
    if (slen <= 0) return {off + 4, true, L""};
    if ((uint32_t)slen > maxLen || off + 4 + (size_t)slen > n) return {off, false, L""};
    if (!isText(d, off + 4, slen)) return {off, false, L""};
    return {off + 4 + (size_t)slen, true, decode(d + off + 4, slen)};
  }
  if (slen < 0 || (uint32_t)slen > maxLen) return {off, false, L""};
  if (slen == 0) return {off + 4, true, L""};
  size_t need = 4 + (size_t)slen + 1;
  if (off + need > n) return {off, false, L""};
  if (!isText(d, off + 4, slen)) return {off, false, L""};
  if (d[off + 4 + slen] != 0) return {off, false, L""};
  return {off + need, true, decode(d + off + 4, slen)};
}

struct Field {
  const char* key;
  const wchar_t* label;
};
static const Field STRING_FIELDS[] = {
    {"make", L"Vehicle brand"},       {"model", L"Vehicle model"},
    {"type", L"Vehicle type"},        {"year", L"Year"},
    {"output", L"Output"},            {"cylinders", L"Cylinders"},
    {"country", L"Country"},          {"drivetrain", L"Drivetrain"},
    {"memory", L"Memory"},            {"manufacturer", L"ECU producer"},
    {"ecu_name", L"ECU model"},       {"hw_number", L"Hardware number"},
    {"sw_number", L"Software number"}, {"production_no", L"Production number"},
    {"engine_code", L"Engine code"},  {"transmission", L"Transmission"}};
static const Field AFTER_TIME[] = {
    {"original_file_name", L"Original file"}, {"winols_version_string", L"WinOLS version"},
    {"reserved2", L"Reserved"},               {"revision_tag", L"Revision"},
    {"reserved3", L"Reserved 3"},             {"reserved4", L"Reserved 4"},
    {"reserved5", L"Reserved 5"}};

using Meta = std::map<std::string, ws>;

inline Meta parseMetadata(const uint8_t* d, size_t n, size_t off, uint32_t schema,
                          std::vector<ws>& warnings) {
  Meta m;
  size_t p = off;
  auto readReq = [&](const char* key, const wchar_t* label) {
    CStr r = readCString(d, n, p, schema, 8192);
    if (!r.valid) {
      warnings.push_back(ws(L"Header stopped at ") + label + L".");
      return false;
    }
    m[key] = jsTrim(r.text);
    p = r.end;
    return true;
  };
  for (const auto& f : STRING_FIELDS)
    if (!readReq(f.key, f.label)) return m;
  if (p + 8 > n) {
    warnings.push_back(L"Header stopped at project timestamp.");
    return m;
  }
  m["project_timestamp_raw"] = std::to_wstring(rd64(d, p));
  p += 8;
  for (const auto& f : AFTER_TIME)
    if (!readReq(f.key, f.label)) return m;
  m["__header_complete"] = L"1";
  return m;  // trailing optional fields are not needed for naming
}

class Seq {
 public:
  const uint8_t* d;
  size_t n;
  uint32_t ver;
  size_t pos;
  Seq(const uint8_t* d_, size_t n_, uint32_t v, size_t p) : d(d_), n(n_), ver(v), pos(p) {}

  void need(uint64_t k) {
    if ((uint64_t)pos + k > n) throw Err("truncated");
  }
  uint8_t u8() {
    need(1);
    return d[pos++];
  }
  void b() { u8(); }
  uint32_t u32() {
    need(4);
    uint32_t v = rd32(d, pos);
    pos += 4;
    return v;
  }
  void i32() { u32(); }
  void f64() {
    need(8);
    pos += 8;
  }
  void bytes(int64_t k) {
    if (k < 0 || k > 0x1000000) throw Err("bad byte count");
    need(k);
    pos += k;
  }
  ws str() {
    uint32_t length = u32();
    if (length == 0 || length >= 0xfffffff0u) return L"";
    if (length > 0x100000) throw Err("string too long");
    need(length);
    ws out = decode(d + pos, length);
    if (ver >= 439) pos += length;
    else {
      need((uint64_t)length + 1);
      pos += (size_t)length + 1;
    }
    return out;
  }
  ws lang() {
    ws s = str();
    if (ver >= 345) u32();
    return s;
  }
  ws ml() {
    if (ver < 330) return str();
    ws primary = lang();
    if (ver >= 345) u32();
    uint32_t sub = u32();
    if (sub > 64) throw Err("language list too long");
    for (uint32_t i = 0; i < sub; i++) lang();
    return primary;
  }
  void array() {
    uint32_t count = u32();
    if (count == 0 || count >= 0xfffffff0u) return;
    if (count > 0x1000000) throw Err("array too long");
    bytes(count);
  }
  void u32array() {
    uint32_t count = u32();
    if (count == 0) return;
    if (count > 0x100000) throw Err("u32 array too long");
    bytes((int64_t)count * 4);
  }
  void strarray() {
    uint32_t count = u32();
    if (count > 100000) throw Err("string array too long");
    for (uint32_t i = 0; i < count; i++) str();
  }
  void axisUnit() {
    str();
    str();
    f64();
    f64();
    u32();
    u32();
    u32();
    if (ver >= 264) f64();
    if (ver >= 61) u32();
    if (ver >= 105) {
      u32();
      u32();
      u32();
      i32();
    }
  }
  void axis() {
    ml();
    str();
    f64();
    f64();
    for (int i = 0; i < 5; i++) u32();
    b();
    b();
    if (ver >= 264) f64();
    if (ver >= 241) u32();
    if (ver >= 8) u32();
    if (ver >= 805) u32();
    if (ver >= 12) b();
    if (ver >= 73) array();
    if (ver >= 77) u32();
    if (ver >= 91) i32();
    if (ver >= 354) str();
    if (ver >= 372) {
      uint32_t count = u32();
      if (count > 10000) throw Err("axis folder list too long");
      for (uint32_t i = 0; i < count; i++) ml();
    }
    if (ver >= 440) i32();
    if (ver >= 805) {
      u32();
      u32();
      u32();
      u32();
    }
  }
  void map(ws& name, ws& description) {
    if (ver >= 268) u8();
    if (ver >= 282) u32();
    if (ver >= 287) ml();
    if (ver >= 93) u8();
    description = ml();
    u32();  // type code
    u32();
    u32();
    u32();
    u32();
    name = L"";
    if (ver >= 80) {
      uint32_t folder = u32();
      if (folder > 100000) throw Err("folder id out of range");
      name = str();
    }
    if (ver >= 298) u32();
    if (ver >= 299) u32();
    if (ver >= 94) u32();
    if (ver >= 74) b();
    if (ver >= 123) u32();
    if (ver >= 300)
      for (int i = 0; i < 6; i++) f64();
    if (ver >= 67)
      for (int i = 0; i < 6; i++) f64();
    else if (ver >= 66) {
      f64();
      f64();
    } else if (ver < 59) {
      u32();
      u32();
    }
    for (int i = 0; i < 4; i++) b();
    u32();
    u32();
    u32();
    u32();
    u32();
    axisUnit();
    axis();
    axis();
    if (ver >= 58) b();
    if (ver >= 68) b();
    if (ver >= 90) u32();
    if (ver >= 9) {
      u32();
      u32();
    }
    if (ver >= 49) {
      u32();
      b();
      b();
      f64();
      f64();
    }
    if (ver >= 51) u32();
    if (ver >= 53) {
      b();
      f64();
      f64();
      f64();
      u32();
    }
    if (ver >= 54) f64();
    if (ver >= 55) {
      b();
      b();
      b();
    }
    if (ver >= 315) u32();
    if (ver >= 383) {
      i32();
      bytes(16);
    }
    if (ver >= 329) u32();
    if (ver >= 346) {
      u32();
      u32();
    }
    if (ver >= 395) u32();
    if (ver >= 476) {
      u32();
      u32array();
      strarray();
    }
    if (ver >= 503) {
      u32();
      f64();
    }
    if (ver >= 596) u32();
    if (ver >= 822) u32();
  }
};

struct Candidate {
  std::vector<MapRow> maps;
  std::string source;
  ws warning;
  size_t offset = 0;  // file offset where the directory's records start
};

struct Marker {
  size_t at;
  uint8_t compressed;
  uint32_t count;
};

inline bool hasPartialMapDirectory(const Candidate& candidate) {
  return !candidate.warning.empty();
}

inline std::vector<Marker> findMarkers(const uint8_t* d, size_t n, size_t scanLimit) {
  std::vector<Marker> out;
  const size_t boundedSize = std::min(n, scanLimit);
  if (!d || boundedSize < 9 || boundedSize <= 0x40) return out;
  const size_t limit = std::min(boundedSize - 9, (size_t)8 * 1024 * 1024);

  // memchr is optimized by common C runtimes. Find the first marker byte
  // quickly, then check the remaining bytes only at candidate positions.
  size_t cursor = 0x40;
  while (cursor <= limit) {
    const void* hit = std::memchr(d + cursor, 0x77, limit - cursor + 1);
    if (!hit) break;
    const size_t i = static_cast<const uint8_t*>(hit) - d;
    cursor = i + 1;
    if (d[i + 1] != 0x33 || d[i + 2] != 0x88 || d[i + 3] != 0x11) continue;
    const uint8_t compressed = d[i + 4];
    const uint32_t count = rd32(d, i + 5);
    if (compressed > 1 || count == 0 || count > 200000) continue;
    out.push_back({i + 9, compressed, count});
    // A few marker-shaped false positives can precede the real directory in
    // large headers. Keep searching beyond the old eight-candidate cutoff.
    if (out.size() >= 64) break;
  }
  return out;
}

// Same acceptance rules as fflate.unzlibSync: header check, no dictionary, adler32 ignored.
inline bool unzlib(const uint8_t* p, size_t n, Bytes& out) {
  if (n < 2) return false;
  if ((p[0] & 15) != 8 || (p[0] >> 4) > 7 || (((p[0] << 8) | p[1]) % 31) != 0) return false;
  if (p[1] & 32) return false;
  z_stream zs;
  std::memset(&zs, 0, sizeof zs);
  if (inflateInit2(&zs, -15) != Z_OK) return false;
  zs.next_in = const_cast<Bytes::value_type*>(p + 2);
  zs.avail_in = (uInt)std::min<size_t>(n - 2, 0x7fffffff);
  uint8_t buf[65536];
  int rc = Z_OK;
  while (rc != Z_STREAM_END) {
    zs.next_out = buf;
    zs.avail_out = sizeof buf;
    rc = inflate(&zs, Z_NO_FLUSH);
    out.insert(out.end(), buf, buf + (sizeof buf - zs.avail_out));
    if (rc != Z_OK && rc != Z_STREAM_END) break;
    if (rc == Z_OK && zs.avail_in == 0 && zs.avail_out != 0) break;  // truncated
    if (out.size() > (size_t)1 << 30) break;
  }
  inflateEnd(&zs);
  return rc == Z_STREAM_END;
}

inline Candidate readMapBlock(const uint8_t* d, size_t n, size_t start, uint32_t count,
                              uint32_t version) {
  Seq seq(d, n, version, start);
  Candidate c;
  c.source = "map directory";
  c.offset = start;
  for (uint32_t i = 0; i < count; i++) {
    MapRow r;
    try {
      // Structural parsing determines record validity; label quality is separate.
      seq.map(r.name, r.description);
    } catch (const std::exception&) {
      if (c.maps.empty()) throw;
      c.warning = L"Read " + std::to_wstring(c.maps.size()) + L" of " +
                  std::to_wstring(count) +
                  L" map records before a record became structurally unreadable.";
      break;
    }
    r.defined = isDefinedMap(r.name, r.description);
    c.maps.push_back(std::move(r));
  }
  return c;
}

inline size_t definedMapCount(const Candidate& candidate) {
  return static_cast<size_t>(std::count_if(candidate.maps.begin(), candidate.maps.end(),
      [](const MapRow& row) { return row.defined; }));
}

inline size_t plausibleMapCount(const Candidate& candidate) {
  return static_cast<size_t>(std::count_if(candidate.maps.begin(), candidate.maps.end(),
      [](const MapRow& row) { return looksLikeRealMap(row.name, row.description); }));
}

// Prefer complete directories, then record coverage, defined labels and label
// evidence. No minimum human-name ratio is allowed to invalidate parsed records.
inline bool betterMapCandidate(const Candidate& candidate, const Candidate& current) {
  if (candidate.maps.empty()) return false;
  if (current.maps.empty()) return true;
  const bool candidateComplete = candidate.warning.empty();
  const bool currentComplete = current.warning.empty();
  if (candidateComplete != currentComplete) return candidateComplete;
  if (candidate.maps.size() != current.maps.size())
    return candidate.maps.size() > current.maps.size();
  const size_t candidateDefined = definedMapCount(candidate);
  const size_t currentDefined = definedMapCount(current);
  if (candidateDefined != currentDefined) return candidateDefined > currentDefined;
  return plausibleMapCount(candidate) > plausibleMapCount(current);
}

inline bool hasMapText(const ws& value) {
  for (wchar_t c : value) {
    if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
        (c >= 0x00c0 && c <= 0x00ff && c != 0x00d7 && c != 0x00f7))
      return true;
  }
  return false;
}

inline bool plausibleMapPrefix(const uint8_t* d, size_t n, size_t start, uint32_t version) {
  if (!d || start >= n || n - start < 16) return false;
  try {
    // Fast path for the common pre-330 layouts: validate the serialized string
    // lengths and printable bytes directly, without allocating Seq/string objects
    // for every byte scanned in the 64 KiB search window.
    if (version < 330) {
      size_t p = start;
      if (version >= 268) p += 1;
      if (version >= 282) p += 4;
      if (version >= 287) {
        CStr prefix = readCString(d, n, p, version, 4096);
        if (!prefix.valid || prefix.text.size() > 4096) return false;
        p = prefix.end;
      }
      if (version >= 93) p += 1;
      CStr description = readCString(d, n, p, version, 4096);
      if (!description.valid || description.text.size() > 4096) return false;
      if (hasMapText(description.text)) return true;
      if (version >= 80) {
        // After description: five u32 fields, then folder id and the map name.
        CStr name = readCString(d, n, description.end + 24, version, 4096);
        return name.valid && hasMapText(name.text);
      }
      return false;
    }

    Seq probe(d, n, version, start);
    if (version >= 268) probe.b();
    if (version >= 282) probe.u32();
    if (version >= 287) probe.ml();
    if (version >= 93) probe.u8();
    ws description = probe.ml();
    if (description.size() > 4096) return false;
    if (hasMapText(description)) return true;
    if (version >= 80) {
      for (int i = 0; i < 5; ++i) probe.u32();
      probe.u32();  // folder id
      ws name = probe.str();
      return name.size() <= 4096 && hasMapText(name);
    }
    return false;
  } catch (const std::exception&) {
    return false;
  }
}
inline bool readMapRecordAt(const uint8_t* d, size_t n, size_t start, uint32_t version,
                            MapRow& row, size_t& end) {
  if (!plausibleMapPrefix(d, n, start, version)) return false;
  try {
    Seq seq(d, n, version, start);
    seq.map(row.name, row.description);
    end = seq.pos;
    if (end <= start + 16 || end > n || row.name.size() > 4096 ||
        row.description.size() > 4096) return false;
    // Do not apply human-name heuristics during structural record discovery.
    row.defined = isDefinedMap(row.name, row.description);
    return true;
  } catch (const std::exception&) {
    return false;
  }
}

// Some OLS schema families lack a reliably parseable map-count marker. If the
// explicit directory fails, scan the bounded serialized region for a valid
// version-gated record prefix and follow sequential records with small padding.
inline Candidate scanMapDirectory(const uint8_t* d, size_t n, uint32_t version) {
  Candidate best;
  best.source = "unread";
  if (!d || n < 0x200) return best;
  // Map records may occur beyond 64 KiB. Scan the bounded serialized header,
  // respecting mapLimit's M1 fence when available.
  const size_t scanEnd = std::min<size_t>(n, 8u * 1024u * 1024u);
  size_t start = 0x180;
  while (start + 16 < scanEnd) {
    if (!plausibleMapPrefix(d, n, start, version)) {
      ++start;
      continue;
    }
    Candidate candidate;
    candidate.source = "scanned map directory";
    candidate.offset = start;
    candidate.warning = L"Recovered by sequential scan; directory count is heuristic.";
    size_t cursor = start;
    while (candidate.maps.size() < 200000 && cursor + 16 < n) {
      MapRow row;
      size_t end = 0;
      if (readMapRecordAt(d, n, cursor, version, row, end)) {
        candidate.maps.push_back(std::move(row));
        cursor = end;
        continue;
      }
      bool nextFound = false;
      const size_t gapEnd = std::min<size_t>(n - 16, cursor + 64);
      for (size_t next = cursor + 1; next <= gapEnd; ++next) {
        if (!plausibleMapPrefix(d, n, next, version)) continue;
        MapRow nextRow;
        size_t nextEnd = 0;
        if (readMapRecordAt(d, n, next, version, nextRow, nextEnd)) {
          candidate.maps.push_back(std::move(nextRow));
          cursor = nextEnd;
          nextFound = true;
          break;
        }
      }
      if (!nextFound) break;
    }
    if (betterMapCandidate(candidate, best)) best = std::move(candidate);
    // Skip over a successfully parsed run instead of reparsing each byte in it.
    start = cursor > start ? cursor : start + 1;
  }
  if (!best.maps.empty()) return best;
  Candidate none;
  none.source = "unread";
  return none;
}

inline Candidate extractMaps(const uint8_t* d, size_t n, uint32_t version,
                             const VersionDirectory& versionDirectory, bool haveVersionDirectory,
                             std::vector<ws>& warnings) {
  // Never dispatch map parsing into the embedded ROM/version array. M1's
  // version_data_start marks the beginning of that array; maps precede it.
  const size_t mapLimit = haveVersionDirectory ? versionDirectory.dataStart : n;
  auto markers = findMarkers(d, mapLimit, mapLimit);
  if (!haveVersionDirectory)
    warnings.push_back(L"Validated M1 version directory not found; map marker search cannot be bounded by the ROM array.");
  bool haveBest = false;
  Candidate best;
  for (const auto& marker : markers) {
    try {
      Bytes block;
      const uint8_t* bp = d;
      size_t bn = mapLimit;
      if (marker.compressed) {
        Bytes raw;
        if (marker.at >= mapLimit || !unzlib(d + marker.at, mapLimit - marker.at, raw)) throw Err("inflate failed");
        block.assign(d, d + marker.at);
        block.insert(block.end(), raw.begin(), raw.end());
        bp = block.data();
        bn = block.size();
      }
      Candidate cand = readMapBlock(bp, bn, marker.at, marker.count, version);
      const bool full = cand.maps.size() == marker.count && cand.warning.empty();
      if (!full && cand.warning.empty())
        cand.warning = L"Read " + std::to_wstring(cand.maps.size()) + L" of " +
                       std::to_wstring(marker.count) +
                       L" map records before the directory ended.";
      if (marker.compressed) cand.source = "compressed map directory";
      // Keep structurally valid records regardless of whether their labels look
      // generated, localized, numeric, or unfamiliar.
      if (betterMapCandidate(cand, best)) {
        best = std::move(cand);
        haveBest = true;
      }
    } catch (const std::exception&) {
      // try the next marker
    }
  }
  if (haveBest) {
    if (!best.warning.empty()) warnings.push_back(best.warning);
    return best;
  }

  // Fallback for schema families/projects where no valid count marker could
  // be parsed, or the only marker candidate is a single unnamed placeholder.
  const size_t fallbackLimit = haveVersionDirectory ? mapLimit : std::min<size_t>(mapLimit, 0x10000);
  // Sequential scanning is a recovery path, not part of normal parsing. Running
  // it for every well-formed project caused an 8 MiB worst-case scan per file.
  if (!haveBest || !best.warning.empty()) {
    Candidate scanned = scanMapDirectory(d, fallbackLimit, version);
    if (!scanned.maps.empty() &&
        (!haveBest || betterMapCandidate(scanned, best))) {
      warnings.push_back(L"Map records recovered by bounded sequential scan; map count is heuristic.");
      return scanned;
    }
  }
  if (haveBest) {
    if (!best.warning.empty()) warnings.push_back(best.warning);
    return best;
  }
  if (markers.empty())
    warnings.push_back(L"No map directory marker was found. Defined-map count is 0.");
  else
    warnings.push_back(
        L"Map directory marker was present but the records could not be read. Defined-map count is 0.");
  Candidate none;
  none.source = "unread";
  return none;
}

inline ws softwareStemFromSourceFilename(const ws& raw) {
  ws name=jsTrim(raw);size_t slash=name.find_last_of(L"\\/");
  if(slash!=ws::npos)name=name.substr(slash+1);
  size_t dot=name.find_last_of(L'.');
  if(dot!=ws::npos) {
    ws ext=lower(name.substr(dot));
    static const std::set<ws> allowed={L".bin",L".hex",L".rom",L".mot",L".s19",L".srec",L".ori",L".eep",L".epr",L".dat",L".frf",L".sgo",L".odx",L".vbf",L".flash"};
    if(!allowed.count(ext))return L"";
    name.resize(dot);
  }
  name=jsTrim(name);if(name.empty()||name.size()>120)return L"";
  ws n=lower(name);
  static const std::set<ws> generic={L"original",L"stock",L"unknown",L"dump",L"read",L"hex",L"hexdump",L"rom",L"flash",L"project",L"software",L"sw"};
  return generic.count(n)?ws():name;
}
// A source dump filename is not automatically a software version. Only promote a
// standalone, version-like identifier from the basename; descriptive folder/vendor
// names (e.g. "TuneCenter") and vehicle/engine marketing text are not SW numbers.
inline ws softwareVersionFromSourceFilename(const ws& raw, const ws& brand = L"",
                                            const ws& model = L"", const ws& ecu = L"",
                                            const ws& production = L"", const ws& hardware = L"") {
  ws stem = softwareStemFromSourceFilename(raw);
  if (stem.empty()) return L"";
  auto identity = [&](const ws& token) {
    ws t = catalogNorm(token);
    if (t.empty()) return false;
    for (const ws& part : {brand, model, ecu}) {
      ws p = catalogNorm(part);
      if (!p.empty() && (t == p || (L" " + p + L" ").find(L" " + t + L" ") != ws::npos)) return true;
    }
    return false;
  };
  static const std::set<ws> junk = {
    L"ori",L"original",L"stock",L"read",L"dump",L"tune",L"tuned",L"tuning",
    L"tunecenter",L"tune center",L"tuningcenter",L"tuning center",L"tuningfiles",
    L"files",L"file",L"customer",L"client",L"modified",L"mod",L"backup",
    L"final",L"new",L"old",L"test",L"unknown",L"software",L"sw",L"rom",L"flash",
    L"bosch",L"denso",L"siemens",L"continental",L"delphi",L"marelli",L"magneti",
    L"catalyst",L"system",L"disable",L"pcm",L"dtc",L"dtcs"
  };
  std::vector<ws> tokens;
  ws token;
  auto flush = [&]() { if (!token.empty()) { tokens.push_back(token); token.clear(); } };
  for (wchar_t c : stem) {
    if ((c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') ||
        (c >= L'0' && c <= L'9')) token += c;
    else flush();
  }
  flush();
  ws prodDigits, hwDigits;
  for (wchar_t c : production) if (c >= L'0' && c <= L'9') prodDigits += c;
  for (wchar_t c : hardware) if (c >= L'0' && c <= L'9') hwDigits += c;
  ws best;
  int bestScore = -1;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const ws& t = tokens[i];
    ws low = lower(t);
    if (t.size() < 5 || t.size() > 16 || junk.count(low) || identity(t) || isDtcCode(t)) continue;
    bool hasDigit = false, hasAlpha = false;
    ws digits;
    for (wchar_t c : t) {
      if (c >= L'0' && c <= L'9') { hasDigit = true; digits += c; }
      if ((c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z')) hasAlpha = true;
    }
    if (!hasDigit) continue;
    // Hexadecimal addresses/sizes such as 0x200000 ("stock file_0x00_0x200000.bin")
    // describe the dump range, not the calibration.
    if (low.size() > 2 && low[0] == L'0' && low[1] == L'x') continue;
    // Ignore engine displacement/power fragments such as 1.4T and 120Ps.
    if (low.size() >= 2 && (low.substr(low.size()-2) == L"ps" ||
                            low.substr(low.size()-2) == L"hp" ||
                            low.substr(low.size()-2) == L"kw" ||
                            low.substr(low.size()-2) == L"cc")) continue;
    int score = (hasAlpha ? 40 : 30) + static_cast<int>(t.size());
    // Prefer later tokens: source filenames usually place the SW/calibration ID at the end.
    score += static_cast<int>(i);
    // Prefer a token whose digits appear in the production or hardware number.
    if (digits.size() >= 4 &&
        ((!prodDigits.empty() && prodDigits.find(digits) != ws::npos) ||
         (!hwDigits.empty() && hwDigits.find(digits) != ws::npos)))
      score += 20;
    if (score > bestScore) { best = t; bestScore = score; }
  }
  return best;
}
// Collects candidate embedded source filenames (".bin", ".hex", ...) from
// ANSI and UTF-16LE strings in d[0, n). `romStrict` is used for the ROM image:
// there only the plain-ASCII tail of a basename is kept, because random ROM
// bytes often form ".hex"-looking junk such as "c\x20ac\x00d4X\x00be.Hex".
using SourceCandidate = std::pair<int, ws>;
inline void collectEmbeddedHexCandidates(const uint8_t* d, size_t n, bool romStrict,
                                         std::vector<SourceCandidate>& candidates) {
  static const std::vector<ws> exts={L".bin",L".hex",L".rom",L".mot",L".s19",L".srec",L".ori",L".eep",L".epr",L".dat",L".frf",L".sgo",L".odx",L".vbf",L".flash"};
  auto collectRun=[&](const ws& run) {
    if(run.empty()||run.size()>8192)return;
    for(size_t i=0;i<run.size();++i) {
      if(run[i]!=L'.')continue;
      for(const auto& ext:exts) {
        if(i+ext.size()>run.size()||lower(run.substr(i,ext.size()))!=ext)continue;
        size_t b=i;
        auto allowed=[](wchar_t c) {
          return (c>=L'a'&&c<=L'z')||(c>=L'A'&&c<=L'Z')||(c>=L'0'&&c<=L'9')||
            c>=0x80||c==L'_'||c==L' '||c==L'('||c==L')'||c==L'.'||c==L'+'||
            c==L'-'||c==L'\\'||c==L'/';
        };
        while(b>0&&allowed(run[b-1]))--b;
        ws base=jsTrim(run.substr(b,i+ext.size()-b));
        size_t sep=base.find_last_of(L"\\/");
        if(sep!=ws::npos)base=base.substr(sep+1);
        if(romStrict) {
          // Keep only the trailing plain-ASCII part: "\x00ff\x00ff..CAFT5F04.DAT" -> "CAFT5F04.DAT".
          size_t keep=base.size();
          while(keep>0&&base[keep-1]<0x80)--keep;
          base=jsTrim(base.substr(keep));
        }
        ws stem=softwareStemFromSourceFilename(base);
        if(stem.empty())continue;
        // Binary data that happens to end in ".DAT"/".BIN": a run of 24+ hex
        // digits is not a filename anyone typed (real ones: 1037362706_1468.org).
        {
          size_t hexRun=0,longest=0;
          for(wchar_t c:stem){
            const bool hx=(c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')||(c>=L'A'&&c<=L'F');
            hexRun=hx?hexRun+1:0;longest=std::max(longest,hexRun);
          }
          // "389851_<md5>.bin" (production number + hash) is a real name; only
          // separator-free hex blobs are rejected.
          const bool separated=stem.find_first_of(L"_- ")!=ws::npos;
          if(longest>=48||(longest>=24&&!separated))continue;
        }
        int score=1+(stem.find_first_of(L"0123456789")!=ws::npos?2:0)+
          (stem.find_first_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=ws::npos?1:0);
        ws prefix=lower(run.substr(0,b));
        if(prefix.find(L"source file")!=ws::npos||prefix.find(L"original file")!=ws::npos)score+=4;
        if(run.substr(b,i+ext.size()-b).find_first_of(L"\\/")!=ws::npos)score+=1;
        candidates.push_back({score,base});
      }
    }
  };
  // A filename needs '.' followed by the first letter of a known extension.
  // Map descriptions are full of plain sentences, so test that before decoding.
  auto extStart=[](uint8_t c) {
    c=(uint8_t)(c|0x20);
    return c=='b'||c=='h'||c=='r'||c=='m'||c=='s'||c=='o'||c=='e'||c=='d'||c=='f'||c=='v';
  };
  auto mayHoldFilename=[&](const uint8_t* s,size_t len) {
    const uint8_t* end=s+len;
    for(const uint8_t* q=s;q<end;) {
      const void* dot=std::memchr(q,'.',(size_t)(end-q));
      if(!dot)return false;
      const uint8_t* at=static_cast<const uint8_t*>(dot);
      if(at+1<end&&extStart(at[1]))return true;
      q=at+1;
    }
    return false;
  };
  // ANSI/Windows-1252 strings (the normal WinOLS metadata encoding).
  size_t p=0;
  while(p<n) {
    while(p<n&&(d[p]<0x20||d[p]==0x7f))++p;
    size_t begin=p;
    while(p<n&&d[p]>=0x20&&d[p]!=0x7f)++p;
    if(p>begin&&p-begin<=8192&&mayHoldFilename(d+begin,p-begin))collectRun(decode(d+begin,p-begin));
  }
  // UTF-16LE ASCII/Latin-1 runs can occur in imported metadata and embedded paths.
  p=0;
  while(p+1<n) {
    while(p+1<n&&!(d[p]>=0x20&&d[p]!=0x7f&&d[p+1]==0))++p;
    size_t begin=p;ws run;bool dot=false;
    while(p+1<n&&d[p]>=0x20&&d[p]!=0x7f&&d[p+1]==0) {
      if(d[p]=='.'&&p+2<n&&extStart(d[p+2]))dot=true;
      run+=cp1252(d[p]);p+=2;
      if(run.size()>8192)break;
    }
    if(!run.empty()&&dot)collectRun(run);
    if(p==begin)++p;
  }
}
inline ws bestEmbeddedHexCandidate(std::vector<SourceCandidate>& candidates) {
  // Stable: on equal score/length the earliest (map/ID area) candidate wins.
  std::stable_sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){
    return a.first!=b.first?a.first>b.first:a.second.size()<b.second.size();
  });
  return candidates.empty()?ws():candidates.front().second;
}
inline ws scanEmbeddedHexFilename(const uint8_t* d,size_t n) {
  std::vector<SourceCandidate> candidates;
  collectEmbeddedHexCandidates(d,n,false,candidates);
  return bestEmbeddedHexCandidate(candidates);
}
// Map/ID area first, then the first embedded ROM image with the strict filter.
inline ws scanEmbeddedHexFilename(const uint8_t* d,size_t headerEnd,size_t romBegin,size_t romEnd) {
  std::vector<SourceCandidate> candidates;
  collectEmbeddedHexCandidates(d,headerEnd,false,candidates);
  if(romEnd>romBegin)collectEmbeddedHexCandidates(d+romBegin,romEnd-romBegin,true,candidates);
  return bestEmbeddedHexCandidate(candidates);
}
inline ws selectSourceFilename(const ws& original,const ws& scanned) {
  return !softwareStemFromSourceFilename(original).empty()?original:scanned;
}
struct FamilyHit { ws value; size_t offset; int score; };

// Finds ECU-family strings such as EDC17CP02, ME7.5 or SIMOS 18.1 in d[0, n).
// Semantics are those of running the family regex over decode(d, n) (NUL bytes
// dropped, CP1252), but the regex only runs where a cheap byte test shows that
// a family prefix followed by a digit starts, instead of decoding and regex-
// scanning multi-megabyte projects. `offsetBase` is added to hit offsets;
// `requireUpperPrefix` (ROM image) rejects mixed-case junk such as "dCM2".
inline void collectEcuFamilyHits(const uint8_t* d, size_t n, size_t offsetBase,
                                 bool requireUpperPrefix, std::vector<FamilyHit>& hits) {
  if (!d || n == 0) return;
  // Only real ECU-family model prefixes. Manufacturer names (Denso, Keihin,
  // Temic, Kefico, Transtron) are not families. A digit must follow the
  // prefix (optionally after one separator) so short prefixes such as ME
  // cannot match prose or random tokens such as "Me-b1G".
  static const std::wregex re(LR"(\b(?:ME|MED|EDC|DCM|SID|SIMOS|MD1|MG1|EMS|PCR|PSG|DCU|MSS|DDE|MSD|MSV|MJD|IAW|MOTRONIC|MS4)\s*[-.]?\s*\d[A-Z0-9.]{0,12}(?:[.-][A-Z0-9]{1,8}){0,2}\b)",std::regex::ECMAScript|std::regex::icase);
  static const char* const prefixes[] = {"ME", "MED", "EDC", "DCM", "SID", "SIMOS", "MD1", "MG1", "EMS",
                                         "PCR", "PSG", "DCU", "MSS", "DDE", "MSD", "MSV", "MJD", "IAW",
                                         "MOTRONIC", "MS4"};
  // NUL bytes are dropped by decode(); work on the compacted byte stream.
  std::string text;
  text.reserve(n);
  for (size_t i = 0; i < n; ++i)
    if (d[i] != 0) text += static_cast<char>(d[i]);
  const size_t m = text.size();
  auto up = [](unsigned char c) -> unsigned char { return (c >= 'a' && c <= 'z') ? c - 32 : c; };
  // Superset of what std::regex may treat as \s / \d after CP1252 decoding.
  auto space = [](unsigned char c) { return c == ' ' || (c >= 9 && c <= 13) || c == 0xa0; };
  auto digit = [](unsigned char c) { return (c >= '0' && c <= '9') || c == 0xb2 || c == 0xb3 || c == 0xb9; };
  size_t resumeAt = 0;  // like regex_iterator: matches never overlap
  for (size_t i = 0; i < m; ++i) {
    if (i < resumeAt) continue;
    const unsigned char c0 = up(static_cast<unsigned char>(text[i]));
    if (c0 != 'M' && c0 != 'E' && c0 != 'D' && c0 != 'S' && c0 != 'P' && c0 != 'I') continue;
    size_t digitAt = std::string::npos;
    for (const char* prefix : prefixes) {
      size_t k = 0, j = i;
      while (prefix[k] && j < m && up(static_cast<unsigned char>(text[j])) == (unsigned char)prefix[k]) { ++k; ++j; }
      if (prefix[k]) continue;
      while (j < m && space(static_cast<unsigned char>(text[j]))) ++j;
      if (j < m && (text[j] == '-' || text[j] == '.')) ++j;
      while (j < m && space(static_cast<unsigned char>(text[j]))) ++j;
      if (j < m && digit(static_cast<unsigned char>(text[j]))) { digitAt = std::max(digitAt == std::string::npos ? 0 : digitAt, j); }
    }
    if (digitAt == std::string::npos) continue;
    // Decode a window: one char of look-behind for \b, the longest possible tail.
    const size_t from = i > 0 ? i - 1 : 0;
    const size_t to = std::min(m, digitAt + 64);
    ws window = decode(reinterpret_cast<const uint8_t*>(text.data()) + from, to - from);
    std::wsmatch match;
    auto flags = std::regex_constants::match_continuous;
    if (i > 0) flags |= std::regex_constants::match_prev_avail;
    if (!std::regex_search(window.cbegin() + (i - from), window.cend(), match, re, flags)) continue;
    resumeAt = i + (size_t)match.length(0);
    if (match.length(0) == 0) resumeAt = i + 1;
    ws x = jsTrim(match[0].str());
    // A source filename such as "EDC15P.ols" matches with its extension; cut
    // trailing ".<letters>" segments (families only have digits after dots,
    // e.g. ME7.5.10, MED17.7.2).
    for (size_t dot = x.rfind(L'.'); dot != ws::npos && dot > 0; dot = x.rfind(L'.')) {
      const ws ext = x.substr(dot + 1);
      const bool letters = ext.size() >= 2 && std::all_of(ext.begin(), ext.end(), [](wchar_t c) {
        return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
      });
      if (!letters) break;
      x.resize(dot);
    }
    // Reject alphabetic prose such as German "Meine", which can match the "ME" prefix.
    if(x.size()<4||x.size()>32||x.find_first_of(L"0123456789")==ws::npos)continue;
    // Short ME-family hits with a digit immediately followed by a letter
    // (e.g. "Me-6P") are not real families. Longer families such as EDC17CP02
    // are unaffected.
    ws lowX=lower(x);
    if(lowX.rfind(L"me",0)==0&&lowX.rfind(L"med",0)!=0&&x.size()<=8){
      bool digitThenLetter=false;
      for(size_t q=0;q+1<x.size();++q){
        if((x[q]>=L'0'&&x[q]<=L'9')&&
           ((x[q+1]>=L'A'&&x[q+1]<=L'Z')||(x[q+1]>=L'a'&&x[q+1]<=L'z'))){
          digitThenLetter=true;break;
        }
      }
      if(digitThenLetter)continue;
    }
    if (requireUpperPrefix) {
      bool upper = true;
      for (wchar_t ch : x) {
        if (!((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z'))) break;
        if (ch >= L'a' && ch <= L'z') { upper = false; break; }
      }
      if (!upper) continue;
    }
    ws nrm=lowX;
    int score=10;
    if(nrm.find(L'.')!=ws::npos)score+=4;
    if(nrm.find(L'-')!=ws::npos)score+=1;
    static const std::vector<ws> fp={L"motronic",L"simos",L"med",L"edc",L"dcm",L"sid",L"md1",L"mg1",L"ems",L"pcr",L"psg",L"dcu",L"mss",L"dde",L"msd",L"msv",L"mjd",L"iaw",L"ms4",L"me"};
    for(const auto& prefix:fp)if(nrm.rfind(prefix,0)==0){score+=(int)prefix.size();break;}
    hits.push_back({x,offsetBase+i,score});
  }
}
inline ws bestEcuFamily(std::vector<FamilyHit>& hits) {
  std::sort(hits.begin(),hits.end(),[](const FamilyHit&a,const FamilyHit&b) {
    if(a.score!=b.score)return a.score>b.score;
    if(a.offset!=b.offset)return a.offset<b.offset;
    return a.value.size()>b.value.size();
  });
  return hits.empty()?ws():hits.front().value;
}
inline ws inferEcuFamily(const uint8_t* d,size_t n) {
  std::vector<FamilyHit> hits;
  collectEcuFamilyHits(d,n,0,false,hits);
  return bestEcuFamily(hits);
}
// ECU family named in a short metadata string such as the original project
// filename ("EDC15P.ols", "BMW ME9.ols", "ms43.ols"). The user typed it, so it
// outranks family-looking text elsewhere in the project (pasted notes, web
// pages). An all-lower-case hit is printed in capitals ("ms43" -> "MS43").
inline ws ecuFamilyFromName(const ws& name) {
  std::string bytes;
  for (wchar_t c : name) bytes += (c > 0 && c < 0x100) ? (char)c : ' ';
  std::vector<FamilyHit> hits;
  collectEcuFamilyHits(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), 0, false, hits);
  ws v = bestEcuFamily(hits);
  if (v.empty()) {
    // Three-character Motronic families (ME7, ME9) are below the general
    // scanner's minimum length; accept them only in a filename.
    static const std::wregex shortMe(LR"((?:^|[^A-Za-z0-9])(ME[0-9])(?=$|[^A-Za-z0-9.]|\.[A-Za-z]))",
                                     std::regex::ECMAScript | std::regex::icase);
    std::wsmatch m;
    if (std::regex_search(name, m, shortMe)) v = m[1].str();
  }
  bool upper = false;
  for (wchar_t c : v) if (c >= L'A' && c <= L'Z') upper = true;
  if (!upper) for (auto& c : v) if (c >= L'a' && c <= L'z') c = (wchar_t)(c - 32);
  return v;
}
// Map/ID area first, then the first embedded ROM image (strict upper-case prefix).
inline ws inferEcuFamily(const uint8_t* d,size_t headerEnd,size_t romBegin,size_t romEnd) {
  std::vector<FamilyHit> hits;
  collectEcuFamilyHits(d,headerEnd,0,false,hits);
  if(romEnd>romBegin)collectEcuFamilyHits(d+romBegin,romEnd-romBegin,headerEnd,true,hits);
  return bestEcuFamily(hits);
}
// `loaded` leading bytes of a `fileSize`-byte project are in memory (see
// requiredLoadLength). Map finding reads only the map/ID area before the ROM
// version array; identity fallbacks may also read the first ROM image.
inline Analysis analyzeOls(const uint8_t* d, size_t loaded, uint64_t fileSize) {
  const size_t n = loaded;
  Analysis a;
  a.fileSize = fileSize;
  a.bytesLoaded = loaded;
  a.proposed = proposedFilename(L"Unknown", L"Unknown", L"Unknown", L"", 0);
  auto fail = [&](const wchar_t* e) {
    a.ok = false;
    a.error = e;
    return a;
  };
  if (n >= 2 && d[0] == 0x50 && d[1] == 0x4b)
    return fail(L"This is a ZIP/Kennfeld pack, not a WinOLS project.");
  if (n < 24) return fail(L"File is too small to be an OLS project.");
  if (rd32(d, 0) != 0x0b) return fail(L"Missing the WinOLS length prefix.");
  if (std::memcmp(d + 4, "WinOLS File", 11) != 0) return fail(L"Signature is not \"WinOLS File\".");
  if (d[15] != 0) return fail(L"Header signature is not terminated.");

  uint32_t formatVersion=rd32(d,16);bool modern=formatVersion>=200;
  a.formatSizeConsistent=!modern||rd32(d,20)==fileSize;
  if(!a.formatSizeConsistent)a.warnings.push_back(L"Header size does not match file size; automatic renaming is disabled.");
  Meta meta=parseMetadata(d,n,modern?0x18:0x14,formatVersion,a.warnings);
  auto getRaw=[&](const char*k){auto it=meta.find(k);return it==meta.end()?ws():it->second;};
  // Identity fields never carry "DAMOS" into names (see stripDamos).
  auto get=[&](const char*k){
    static const std::set<std::string> identity={"make","model","type","engine_code","ecu_name","sw_number","hw_number","original_file_name"};
    ws v=getRaw(k);
    return identity.count(k)?stripDamos(v):v;
  };
  a.headerComplete=get("__header_complete")==L"1";
  if(!a.headerComplete)a.warnings.push_back(L"Required metadata header is incomplete.");
  for(const auto& kv:meta) {
    if(!kv.first.empty()&&kv.first[0]!='_')a.metadata.push_back({fromAscii(kv.first.c_str()),kv.second});
  }
  // Fast map finding: locate the boundary between the map/ID area and the
  // embedded ECU ROM images first, so every later scan can be bounded by it.
  VersionDirectory versionDirectory;
  a.versionDirectoryValid=locateVersionDirectory(d,n,fileSize,versionDirectory);
  if(!a.versionDirectoryValid&&n<fileSize) {
    // The boundary is unproven in the loaded prefix: the caller must load the
    // whole file (requiredLoadLength already asks for it; this is a guard).
    a.needsFullFile=true;
    return fail(L"Map area boundary not found in the loaded prefix; the whole file is required.");
  }
  size_t headerEnd=n, romBegin=0, romEnd=0;
  if(a.versionDirectoryValid) {
    headerEnd=std::min(n,versionDirectory.dataStart);
    romBegin=std::min(n,versionDirectory.dataStart+4);
    romEnd=(size_t)std::min<uint64_t>(n,(uint64_t)versionDirectory.dataStart+4u+versionDirectory.recordSize);
    a.mapAreaEnd=versionDirectory.dataStart;
    a.romImageStart=romBegin;
    a.romImageEnd=romEnd;
  }
  auto scanSource=[&](){return a.versionDirectoryValid?scanEmbeddedHexFilename(d,headerEnd,romBegin,romEnd):scanEmbeddedHexFilename(d,n);};
  auto scanFamily=[&](){return a.versionDirectoryValid?inferEcuFamily(d,headerEnd,romBegin,romEnd):inferEcuFamily(d,n);};
  ws original=get("original_file_name");
  ws scannedSource=softwareStemFromSourceFilename(original).empty()?scanSource():ws();
  ws embedded=selectSourceFilename(original,scannedSource);
  ws rawBrand=firstFilled({get("make")});
  ws inferredBrand=rawBrand.empty()?inferVehicleBrandFromText(embedded):ws(L"Unknown");
  // An empty make field ("Damos" removed, or never filled) with the brand
  // written in the model or type field: model "Ford Raptor".
  if(rawBrand.empty()&&(inferredBrand.empty()||inferredBrand==L"Unknown")){
    for(const char* k:{"model","type"}){
      ws fromField=inferVehicleBrandFromText(get(k));
      if(!fromField.empty()&&fromField!=L"Unknown"){inferredBrand=fromField;break;}
    }
  }
  ws brand=!rawBrand.empty()?normalizeVehicleBrand(rawBrand):(!inferredBrand.empty()&&inferredBrand!=L"Unknown"?inferredBrand:ws(L"Unknown"));
  ws model=firstFilled({get("model"),get("type"),get("engine_code")});if(model.empty())model=L"Unknown";
  ws explicitEcu=firstFilled({get("ecu_name")});
  ws inferredEcu;
  if(explicitEcu.empty()){
    // The filename's family wins over other project text, but when the text
    // spells the same family more fully (MED1733 vs MED17.3.3) use that form.
    const ws fromName=ecuFamilyFromName(original), fromText=scanFamily();
    inferredEcu=fromName;
    if(fromName.empty())inferredEcu=fromText;
    else if(alnumKey(fromName)==alnumKey(fromText)){
      const bool textDots=fromText.find(L'.')!=ws::npos, nameDots=fromName.find(L'.')!=ws::npos;
      const bool nameSpace=fromName.find(L' ')!=ws::npos, textSpace=fromText.find(L' ')!=ws::npos;
      if(textDots&&!nameDots)inferredEcu=fromText;          // MED1733 -> MED17.3.3
      else if(nameSpace&&!textSpace)inferredEcu=fromText;   // MSD 85 -> MSD85
    }
  }
  ws ecu=!explicitEcu.empty()?explicitEcu:(!inferredEcu.empty()?inferredEcu:ws(L"Unknown"));
  // When no ECU family is known, the hardware number is a useful identity
  // token (e.g. Bosch 038906012L, Denso 89663-26432). Prefer it over leaving
  // the ECU slot empty so the proposed name still carries a part identifier.
  ws rawHw=firstFilled({get("hw_number")});
  if(isUnknownNamePart(ecu)&&!isUnknownNamePart(rawHw)) {
    ws hw=jsTrim(rawHw);
    while(!hw.empty()&&(hw.back()==L'-'||hw.back()==L' '))hw.pop_back();
    if(!hw.empty()&&hw.find_first_of(L"0123456789")!=ws::npos&&hw.size()>=5&&hw.size()<=32)
      ecu=hw;
  }
  ws rawMaker=get("manufacturer");ws ecuManufacturer=identifyEcuManufacturer(rawMaker);
  bool knownMaker=false;for(const auto& m:ecuManufacturerCatalogue())if(catalogNorm(m)==catalogNorm(ecuManufacturer))knownMaker=true;
  if(!knownMaker) {
    ws fromFilename=inferEcuManufacturerFromText(embedded);
    if(fromFilename!=L"Unknown")ecuManufacturer=fromFilename;
    else {
      ws fromFamily=inferEcuManufacturerFromFamily(ecu);
      if(fromFamily!=L"Unknown")ecuManufacturer=fromFamily;
    }
    if(ecuManufacturer.empty())ecuManufacturer=L"Unknown";
  }
  // Prefer the dedicated original-source filename field; scan binary strings only as fallback.
  ws metadataSoftware=firstFilled({get("sw_number")});ws lowSw=lower(metadataSoftware);
  bool generic=metadataSoftware.empty()||lowSw==L"unknown"||lowSw==L"n/a"||lowSw==L"none"||lowSw==L"not available"||lowSw==L"not identified"||lowSw==L"original"||lowSw==L"stock"||lowSw==L"dump"||lowSw==L"hex"||lowSw==L"rom"||lowSw==L"software"||lowSw==L"sw"||lowSw==L"-"||lowSw==L"?"||lowSw==L"%"||lowSw==lower(ecu)||isDtcCode(metadataSoftware);
  static const std::wregex boschPartNumberRe(
      L"(0261S[0-9]{5}|0281[0-9]{6})", std::regex::ECMAScript | std::regex::icase);
  bool boschPart=std::regex_match(metadataSoftware,boschPartNumberRe)&&lower(ecuManufacturer).find(L"bosch")!=ws::npos;
  ws embeddedSoftware=softwareVersionFromSourceFilename(embedded,brand,model,ecu,get("production_no"),get("hw_number"));
  bool useFallback=!embeddedSoftware.empty()&&(generic||boschPart);
  ws software=useFallback?embeddedSoftware:metadataSoftware;
  if(a.versionDirectoryValid) {
    a.versionCount=versionDirectory.count;
    a.versionDataStart=versionDirectory.dataStart;
    a.versionRecordSize=versionDirectory.recordSize;
  } else {
    a.warnings.push_back(L"Could not validate the embedded ROM version directory (M1).");
  }
  Candidate ex=extractMaps(d,n,formatVersion,versionDirectory,a.versionDirectoryValid,a.warnings);
  a.ok=true;a.formatVersion=formatVersion;a.brand=brand;a.model=model;a.ecu=ecu;a.software=software;
  a.ecuManufacturer=ecuManufacturer;a.brandSource=!rawBrand.empty()?L"metadata":(inferredBrand!=L"Unknown"?L"inferred from source filename or model text":L"unknown");
  a.hardwareNumber=get("hw_number");a.productionNumber=get("production_no");
  a.engineCode=get("engine_code");a.vehicleYear=get("year");a.originalFileName=getRaw("original_file_name");a.embeddedHexFilename=embedded;
  a.softwareSource=software.empty()?L"unknown":(useFallback?L"embedded hex filename":L"metadata");
  a.ecuSource=!explicitEcu.empty()?L"metadata":(!inferredEcu.empty()?L"inferred from project text":(!isUnknownNamePart(ecu)?L"hardware number":L"unknown"));
  a.mapCount=ex.maps.size();
  for(const auto&m:ex.maps) {
    if(m.defined)a.definedCount++;
    a.mapDetails.push_back({m.name,m.description,m.defined});
  }
  a.skippedCount=(int)ex.maps.size()-a.definedCount;a.mapSource=ex.source;
  a.mapDirectoryOffset=ex.offset;
  a.proposed=proposedFilename(brand,model,ecu,software,a.definedCount);
  const bool hasAnyIdentity = !isUnknownNamePart(brand) || !isUnknownNamePart(model) || !isUnknownNamePart(ecu) || !isUnknownNamePart(software);
   const bool scannedMapEvidence = ex.source=="scanned map directory" && (a.mapCount>=10 || a.definedCount>0);
  // A partially read explicit map directory gives an undercount. Keep it
  // review-only even when enough labels were recovered to look plausible.
  a.renameSafe=a.headerComplete&&a.formatSizeConsistent&&a.versionDirectoryValid&&
      hasAnyIdentity&&!hasPartialMapDirectory(ex)&&
      (ex.source=="map directory"||ex.source=="compressed map directory"||scannedMapEvidence);
  if(!a.renameSafe)a.warnings.push_back(L"Identity or structured map-directory evidence is incomplete; automatic rename is disabled.");
  return a;
}

inline Analysis analyzeOls(const uint8_t* d, size_t n) { return analyzeOls(d, n, n); }

// Final proposed name for a project currently called `currentName`. When the
// identity is partial, the original stem is kept as a bracketed fallback.
inline ws finalProposedName(const Analysis& a, const ws& currentName) {
  if (!isUnknownNamePart(a.brand) && !isUnknownNamePart(a.model) &&
      !isUnknownNamePart(a.ecu))
    return a.proposed;

  ws originalStem = currentName;
  if (originalStem.size() >= 4 && lower(originalStem.substr(originalStem.size() - 4)) == L".ols")
    originalStem.resize(originalStem.size() - 4);
  ws normalizedOriginal = lower(jsTrim(stripDamos(originalStem)));
  if (normalizedOriginal.empty() || normalizedOriginal == L"project" ||
      normalizedOriginal == L"unknown" || normalizedOriginal == L"ols project" ||
      normalizedOriginal == L"untitled" || normalizedOriginal == L"new project")
    return a.proposed;

  // The parser cannot reliably assign words in an existing filename to missing
  // metadata slots. Keep the complete original stem as a labelled fallback
  // rather than silently replacing it with an "Unknown" component.
  ws proposed = a.proposed;
  size_t suffixAt = proposed.rfind(L" (");
  if (suffixAt == ws::npos || proposed.size() < 5 ||
      lower(proposed.substr(proposed.size() - 4)) != L".ols")
    return a.proposed;
  ws base = proposed.substr(0, suffixAt);
  ws suffix = proposed.substr(suffixAt);
  // A stem this tool produced earlier ends with its own map-count suffix,
  // e.g. "Chrysler Voyager 0281010814 (17)". Drop it before comparing, so a
  // re-scan does not nest the old name: "... [Chrysler Voyager ... (17)] (17)".
  {
    const size_t open = originalStem.rfind(L" (");
    if (open != ws::npos && originalStem.size() > open + 3 && originalStem.back() == L')') {
      const ws inner = originalStem.substr(open + 2, originalStem.size() - open - 3);
      const bool count = inner == L"Nil" ||
          (!inner.empty() && std::all_of(inner.begin(), inner.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }));
      if (count) originalStem.resize(open);
    }
  }
  ws fallback = stripDamos(originalStem);
  if (isUnknownNamePart(fallback)) return a.proposed;
  fallback = filenamePart(fallback);
  if (fallback.empty()) return a.proposed;
  // Already "<base> [<old stem>]" from an earlier run: keep it as it is.
  const ws lowBase = lower(base), lowFallback = lower(fallback);
  const bool alreadyTagged = !base.empty() && lowFallback.size() > lowBase.size() + 3 &&
                             lowFallback.compare(0, lowBase.size() + 2, lowBase + L" [") == 0 &&
                             lowFallback.back() == L']';
  if (base.empty() || alreadyTagged) base = fallback;
  else if (lowBase != lowFallback) base += L" [" + fallback + L"]";
  const size_t maxBase = 240 > suffix.size() ? 240 - suffix.size() : 1;
  if (base.size() > maxBase) base.resize(maxBase);
  while (!base.empty() && (base.back() == L' ' || base.back() == L'.')) base.pop_back();
  return base + suffix;
}


}  // namespace ols
