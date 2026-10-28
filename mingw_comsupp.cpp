// MinGW has no comsupp library; UnRAR's isnt.cpp (Windows version detection
// through WMI) uses _bstr_t, which needs these two helpers from it.
#ifdef _WIN32
#include <comutil.h>
#include <windows.h>

namespace _com_util {
BSTR WINAPI ConvertStringToBSTR(const char* src) {
  if (!src) return nullptr;
  const int n = MultiByteToWideChar(CP_ACP, 0, src, -1, nullptr, 0);
  BSTR out = SysAllocStringLen(nullptr, n > 0 ? n - 1 : 0);
  if (out && n > 0) MultiByteToWideChar(CP_ACP, 0, src, -1, out, n);
  return out;
}
char* WINAPI ConvertBSTRToString(BSTR src) {
  if (!src) return nullptr;
  const int n = WideCharToMultiByte(CP_ACP, 0, src, -1, nullptr, 0, nullptr, nullptr);
  char* out = new char[n > 0 ? n : 1];
  if (n > 0) WideCharToMultiByte(CP_ACP, 0, src, -1, out, n, nullptr, nullptr);
  else out[0] = 0;
  return out;
}
}  // namespace _com_util
#endif
