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
    L"Mack",L"Western Star",L"General Motors",L"SAIC",L"Prodrive",L"TVR"
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
    L"Magic Motorsport",L"CMD Flash",L"Continental Powertrain",L"Continental SIMOS",L"Bosch Motorsport"
  }; return v;
}
inline ws normalizeVehicleBrand(const ws& raw) {
  ws n=catalogNorm(raw); if(n.empty())return L"Unknown";
  static const std::map<ws,ws> aliases={{L"alfa",L"Alfa Romeo"},{L"vw",L"Volkswagen"},
    {L"volkswagen ag",L"Volkswagen"},{L"mercedes",L"Mercedes Benz"},{L"landrover",L"Land Rover"},
    {L"rolls royce",L"Rolls Royce"},{L"gm",L"General Motors"},{L"psa",L"Peugeot"},
    {L"ssang yong",L"SsangYong"},{L"bmw ag",L"BMW"},{L"tata motors",L"Tata"}};
  auto a=aliases.find(n);if(a!=aliases.end())return a->second;
  for(const auto& b:vehicleBrandCatalogue())if(catalogNorm(b)==n)return b;
  return jsTrim(raw);
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
inline bool isPlaceholderLabel(const ws& raw) {
  static const std::set<ws> ENV = [] {
    std::set<ws> e;
    const wchar_t* list[] = {L"(damos)", L"ols 5.0 (windows)", L"hexdump", L"hex dump", L"hex-dump",
                             L"my maps", L"my map", L"meine kennfelder", L"eigene kennfelder",
                             L"potential maps", L"potential map", L"potentielle kennfelder",
                             L"potentielles kennfeld", L"m\x00f6gliche kennfelder",
                             L"moegliche kennfelder", L"original", L"passenger car", L"engine",
                             L"binary data", L"0 (original)", L"version 1", L"differences", L"text",
                             L"untitled", L"unnamed", L"new map", L"newmap", L"map", L"kennfeld",
                             L"kennlinie", L"einzelwert", L"value", L"curve", L"imported"};
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
  static const R ONLYNUM(L"^\\d+$");
  static const R DIMONLY(L"^\\d+\\s*[x\x00d7*]\\s*\\d+$", fl);
  static const R BIT(L"\\d+\\s*-?\\s*bits?\\b", fl);
  static const R DIM(L"\\d+\\s*[x\x00d7]\\s*\\d+");
  static const R ORDER(L"\\b(lohi|hilo|big[\\s-]?endian|little[\\s-]?endian)\\b", fl);
  static const R BITORDER(L"^\\d+\\s*-?\\s*bits?\\b[\\s,.-]*(lohi|hilo)\\b", fl);

  ws s = spaceNorm(jsTrim(removeNul(raw)));  // trim happens before normalising in JS; same result
  if (s.empty()) return true;
  if (s == L"-" || s == L"?" || s == L"%") return true;

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
  if (std::regex_search(s, GENERIC)) return true;
  if (std::regex_search(s, ONLYNUM)) return true;
  if (std::regex_search(s, DIMONLY)) return true;

  bool hasBit = std::regex_search(s, BIT);
  bool hasDim = std::regex_search(s, DIM);
  bool hasOrder = std::regex_search(s, ORDER);
  bool prod = std::regex_search(s, PRODUCER);
  if (hasBit && hasDim) return true;
  if (prod && hasBit) return true;
  if (prod && hasDim && s.size() < 80) return true;
  if (hasBit && hasOrder && s.size() < 80) return true;
  if (std::regex_search(s, BITORDER) && s.size() < 48) return true;
  return false;
}

inline bool isHexdump(const ws& description) {
  static const std::wregex R(L"^hex\\s*-?\\s*dump$", std::regex::ECMAScript | std::regex::icase);
  return std::regex_search(spaceNorm(jsTrim(removeNul(description))), R);
}

inline bool isDefinedMap(const ws& name, const ws& description) {
  if (isHexdump(description)) return false;
  for (const ws* label : {&name, &description}) {
    ws t = jsTrim(removeNul(*label));
    if (!t.empty() && !isPlaceholderLabel(t)) return true;
  }
  return false;
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

inline ws firstFilled(const std::vector<ws>& values) {
  for (const ws& v : values) {
    ws t = jsTrim(removeNul(v));
    if (!t.empty() && t != L"-" && t != L"?" && t != L"%") return t;
  }
  return L"";
}

inline ws proposedFilename(const ws& brand, const ws& model, const ws& ecu, const ws& software,
                           int definedMaps) {
  ws prefix=filenamePart(brand)+L" "+filenamePart(model)+L" "+filenamePart(ecu);
  ws sw=firstFilled({software});
  if(!sw.empty()&&lower(filenamePart(sw))!=lower(filenamePart(ecu)))prefix+=L" "+filenamePart(sw);
  ws countLabel=definedMaps==0?ws(L"Nil"):std::to_wstring(definedMaps);
  ws suffix=L" ("+countLabel+L").ols";
  size_t cap=240>suffix.size()?240-suffix.size():1;
  if(prefix.size()>cap)prefix.resize(cap);
  while(!prefix.empty()&&(prefix.back()==L' '||prefix.back()==L'.'))prefix.pop_back();
  return prefix+suffix;
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

struct Analysis {
  bool ok = false, headerComplete = false, formatSizeConsistent = true, renameSafe = false;
  ws error;
  std::vector<ws> warnings;
  uint32_t formatVersion = 0;
  ws brand = L"Unknown", model = L"Unknown", ecu = L"Unknown", software;
  ws ecuManufacturer = L"Unknown", hardwareNumber, productionNumber, engineCode, vehicleYear;
  ws originalFileName, embeddedHexFilename, softwareSource = L"unknown", ecuSource = L"unknown";
  std::vector<std::pair<ws, ws>> metadata;
  size_t mapCount = 0;
  int definedCount = 0, skippedCount = 0;
  ws proposed;
  std::string mapSource = "unread";
};

inline uint32_t rd32(const uint8_t* d, size_t p) {
  return (uint32_t)d[p] | ((uint32_t)d[p + 1] << 8) | ((uint32_t)d[p + 2] << 16) |
         ((uint32_t)d[p + 3] << 24);
}
inline uint64_t rd64(const uint8_t* d, size_t p) {
  return (uint64_t)rd32(d, p) | ((uint64_t)rd32(d, p + 4) << 32);
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
};

struct Marker {
  size_t at;
  uint8_t compressed;
  uint32_t count;
};

inline std::vector<Marker> findMarkers(const uint8_t* d, size_t n) {
  std::vector<Marker> out;
  int64_t limit = std::min<int64_t>((int64_t)n - 9, 8 * 1024 * 1024);
  for (int64_t i = 0x40; i <= limit; i++) {
    if (d[i] == 0x77 && d[i + 1] == 0x33 && d[i + 2] == 0x88 && d[i + 3] == 0x11) {
      uint8_t compressed = d[i + 4];
      uint32_t count = rd32(d, i + 5);
      if (compressed > 1) continue;
      if (count == 0 || count > 200000) continue;
      out.push_back({(size_t)i + 9, compressed, count});
      if (out.size() >= 8) break;
    }
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
  size_t plausible = 0;
  for (uint32_t i = 0; i < count; i++) {
    MapRow r;
    seq.map(r.name, r.description);
    r.defined = isDefinedMap(r.name, r.description);
    if (looksLikeRealMap(r.name, r.description)) plausible++;
    c.maps.push_back(std::move(r));
    if (i == 29 && c.maps.size() >= 30 && (double)plausible / (double)c.maps.size() < 0.2)
      throw Err("map directory does not look like named records");
  }
  return c;
}

inline Candidate extractMaps(const uint8_t* d, size_t n, uint32_t version,
                             std::vector<ws>& warnings) {
  auto markers = findMarkers(d, n);
  bool haveBest = false;
  Candidate best;
  for (const auto& marker : markers) {
    try {
      Bytes block;
      const uint8_t* bp = d;
      size_t bn = n;
      if (marker.compressed) {
        Bytes raw;
        if (!unzlib(d + marker.at, n - marker.at, raw)) throw Err("inflate failed");
        block.assign(d, d + marker.at);
        block.insert(block.end(), raw.begin(), raw.end());
        bp = block.data();
        bn = block.size();
      }
      Candidate cand = readMapBlock(bp, bn, marker.at, marker.count, version);
      bool full = cand.maps.size() == marker.count;
      size_t plausible = 0;
      for (const auto& m : cand.maps)
        if (looksLikeRealMap(m.name, m.description)) plausible++;
      double ratio = cand.maps.empty() ? 0 : (double)plausible / (double)cand.maps.size();
      if (full || ratio >= 0.45 || plausible >= 8) {
        if (marker.compressed) cand.source = "compressed map directory";
        if (!full)
          cand.warning = L"Read " + std::to_wstring(cand.maps.size()) + L" of " +
                         std::to_wstring(marker.count) +
                         L" map records before the directory ended.";
        if (!haveBest || cand.maps.size() > best.maps.size()) {
          best = cand;
          haveBest = true;
        }
        if (full && ratio >= 0.45) return cand;
      }
    } catch (const std::exception&) {
      // try the next marker
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
inline ws scanEmbeddedHexFilename(const uint8_t* d,size_t n) {
  static const std::vector<ws> exts={L".bin",L".hex",L".rom",L".mot",L".s19",L".srec",L".ori",L".eep",L".epr",L".dat",L".frf",L".sgo",L".odx",L".vbf",L".flash"};
  std::vector<std::pair<int,ws>> candidates;size_t p=0;
  while(p<n) {
    while(p<n&&(d[p]<0x20||d[p]>0x7e))++p;
    size_t start=p;while(p<n&&d[p]>=0x20&&d[p]<=0x7e)++p;
    if(p<=start||p-start>8192)continue;
    ws run=decode(d+start,p-start);
    for(size_t i=0;i<run.size();++i) {
      if(run[i]!=L'.')continue; // Candidate extensions can only start at a dot.
      for(const auto& ext:exts) {
      if(i+ext.size()>run.size()||lower(run.substr(i,ext.size()))!=ext)continue;
      size_t b=i;while(b>0&&((run[b-1]>=L'a'&&run[b-1]<=L'z')||(run[b-1]>=L'A'&&run[b-1]<=L'Z')||(run[b-1]>=L'0'&&run[b-1]<=L'9')||run[b-1]==L'_'||run[b-1]==L' '||run[b-1]==L'('||run[b-1]==L')'||run[b-1]==L'.'||run[b-1]==L'+'||run[b-1]==L'-'||run[b-1]==L'\\'||run[b-1]==L'/'))--b;
      ws base=jsTrim(run.substr(b,i+ext.size()-b));size_t sep=base.find_last_of(L"\\/");if(sep!=ws::npos)base=base.substr(sep+1);
      ws stem=softwareStemFromSourceFilename(base);if(stem.empty())continue;
      int score=1+(stem.find_first_of(L"0123456789")!=ws::npos?2:0)+(stem.find_first_of(L"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ")!=ws::npos?1:0);
      candidates.push_back({score,base});
      }
    }
  }
  std::sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.first!=b.first?a.first>b.first:a.second.size()<b.second.size();});
  return candidates.empty()?ws():candidates.front().second;
}
inline ws inferEcuFamily(const uint8_t* d,size_t n) {
  ws text=decode(d,n);
  static const std::wregex re(LR"(\b(?:ME|MED|EDC|DCM|SID|SIMOS|MD1|MG1|EMS|PCR|PSG|DCU|MSS|DDE|MSD|MSV|MJD|IAW|MOTRONIC|KEFICO|TRANSTRON|KEIHIN|DENSO|TEMIC)\s*[-.]?\s*[A-Z0-9]{1,8}(?:[.-][A-Z0-9]{1,8}){0,3}\b)",std::regex::ECMAScript|std::regex::icase);
  std::vector<ws> v;for(std::wsregex_iterator it(text.begin(),text.end(),re),end;it!=end;++it){ws x=jsTrim((*it)[0].str());if(x.size()>=4&&x.size()<=32)v.push_back(x);}
  std::sort(v.begin(),v.end(),[](const ws&a,const ws&b){return a.size()<b.size();});
  return v.empty()?ws():v.front();
}
inline Analysis analyzeOls(const uint8_t* d, size_t n) {
  Analysis a;
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
  a.formatSizeConsistent=!modern||rd32(d,20)==n;
  if(!a.formatSizeConsistent)a.warnings.push_back(L"Header size does not match file size; automatic renaming is disabled.");
  Meta meta=parseMetadata(d,n,modern?0x18:0x14,formatVersion,a.warnings);
  auto get=[&](const char*k){auto it=meta.find(k);return it==meta.end()?ws():it->second;};
  a.headerComplete=get("__header_complete")==L"1";
  if(!a.headerComplete)a.warnings.push_back(L"Required metadata header is incomplete.");
  for(const auto& kv:meta) {
    if(!kv.first.empty()&&kv.first[0]!='_')a.metadata.push_back({fromAscii(kv.first.c_str()),kv.second});
  }
  ws rawBrand=firstFilled({get("make")});ws brand=normalizeVehicleBrand(rawBrand);
  ws model=firstFilled({get("model"),get("type"),get("engine_code")});if(model.empty())model=L"Unknown";
  ws explicitEcu=firstFilled({get("ecu_name")});ws inferredEcu=explicitEcu.empty()?inferEcuFamily(d,n):ws();
  ws ecu=!explicitEcu.empty()?explicitEcu:(!inferredEcu.empty()?inferredEcu:ws(L"Unknown"));
  ws ecuManufacturer=identifyEcuManufacturer(get("manufacturer"));if(ecuManufacturer==L"Unknown")ecuManufacturer=inferEcuManufacturerFromFamily(ecu);
  ws original=get("original_file_name");
  // Prefer the dedicated original-source filename field; scan binary strings only as fallback.
  ws embedded=softwareStemFromSourceFilename(original).empty()?scanEmbeddedHexFilename(d,n):original;
  ws embeddedStem=softwareStemFromSourceFilename(embedded);
  ws metadataSoftware=firstFilled({get("sw_number")});ws lowSw=lower(metadataSoftware);
  bool generic=metadataSoftware.empty()||lowSw==L"unknown"||lowSw==L"n/a"||lowSw==L"none"||lowSw==L"not available"||lowSw==L"not identified"||lowSw==L"original"||lowSw==L"stock"||lowSw==L"dump"||lowSw==L"hex"||lowSw==L"rom"||lowSw==L"software"||lowSw==L"sw"||lowSw==L"-"||lowSw==L"?"||lowSw==L"%"||lowSw==lower(ecu);
  bool boschPart=std::regex_match(metadataSoftware,std::wregex(L"(0261S[0-9]{5}|0281[0-9]{6})",std::regex::ECMAScript|std::regex::icase))&&lower(ecuManufacturer).find(L"bosch")!=ws::npos;
  bool useFallback=!embeddedStem.empty()&&(generic||boschPart);
  ws software=useFallback?embeddedStem:metadataSoftware;
  Candidate ex=extractMaps(d,n,formatVersion,a.warnings);
  a.ok=true;a.formatVersion=formatVersion;a.brand=brand;a.model=model;a.ecu=ecu;a.software=software;
  a.ecuManufacturer=ecuManufacturer;a.hardwareNumber=get("hw_number");a.productionNumber=get("production_no");
  a.engineCode=get("engine_code");a.vehicleYear=get("year");a.originalFileName=original;a.embeddedHexFilename=embedded;
  a.softwareSource=software.empty()?L"unknown":(useFallback?L"embedded hex filename":L"metadata");
  a.ecuSource=!explicitEcu.empty()?L"metadata":(!inferredEcu.empty()?L"inferred from project text":L"unknown");
  a.mapCount=ex.maps.size();for(const auto&m:ex.maps)if(m.defined)a.definedCount++;
  a.skippedCount=(int)ex.maps.size()-a.definedCount;a.mapSource=ex.source;
  a.proposed=proposedFilename(brand,model,ecu,software,a.definedCount);
  a.renameSafe=a.headerComplete&&a.formatSizeConsistent&&!rawBrand.empty()&&!get("model").empty()&&!explicitEcu.empty()&&(ex.source=="map directory"||ex.source=="compressed map directory");
  if(!a.renameSafe)a.warnings.push_back(L"Identity or structured map-directory evidence is incomplete; automatic rename is disabled.");
  return a;
}

}  // namespace ols
