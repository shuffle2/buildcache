//--------------------------------------------------------------------------------------------------
// Copyright (c) 2018 Marcus Geelnard
//
// This software is provided 'as-is', without any express or implied warranty. In no event will the
// authors be held liable for any damages arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose, including commercial
// applications, and to alter it and redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not claim that you wrote
//     the original software. If you use this software in a product, an acknowledgment in the
//     product documentation would be appreciated but is not required.
//
//  2. Altered source versions must be plainly marked as such, and must not be misrepresented as
//     being the original software.
//
//  3. This notice may not be removed or altered from any source distribution.
//--------------------------------------------------------------------------------------------------

#include <base/unicode_utils.hpp>

// The codecvt routines are buggy on some systems, so we fall back to a custom routine instead if
// that is the case.
#if !(defined(__MINGW32__) || defined(__MINGW64__))
#define USE_CPP11_CODECVT
#endif

#ifdef USE_CPP11_CODECVT
#include <codecvt>
#include <locale>
#endif  // USE_CPP11_CODECVT

namespace bcache {
namespace {
#if !defined(USE_CPP11_CODECVT)
// These conversion routines are based on code by Roelof Berg.
// Original: https//github.com/RoelofBerg/Utf8Ucs2Converter

bool utf8_char_to_ucs2_char(const char* utf8_token, wchar_t& ucs2_char, uint32_t& utf8_token_len) {
  // We do math that relies on unsigned data types.
  const unsigned char* utf8_token_u = reinterpret_cast<const unsigned char*>(utf8_token);

  // Initialize return values for 'return false' cases.
  ucs2_char = L'?';
  utf8_token_len = 1;

  // Decode.
  if (0x80 > utf8_token_u[0]) {
    // Tokensize: 1 byte.
    ucs2_char = static_cast<const wchar_t>(utf8_token_u[0]);
  } else if (0xC0 == (utf8_token_u[0] & 0xE0)) {
    // Tokensize: 2 bytes.
    if (0x80 != (utf8_token_u[1] & 0xC0)) {
      return false;
    }
    utf8_token_len = 2;
    ucs2_char =
        static_cast<const wchar_t>((utf8_token_u[0] & 0x1F) << 6 | (utf8_token_u[1] & 0x3F));
  } else if (0xE0 == (utf8_token_u[0] & 0xF0)) {
    // Tokensize: 3 bytes.
    if ((0x80 != (utf8_token_u[1] & 0xC0)) || (0x80 != (utf8_token_u[2] & 0xC0))) {
      return false;
    }
    utf8_token_len = 3;
    ucs2_char = static_cast<const wchar_t>(
        (utf8_token_u[0] & 0x0F) << 12 | (utf8_token_u[1] & 0x3F) << 6 | (utf8_token_u[2] & 0x3F));
  } else if (0xF0 == (utf8_token_u[0] & 0xF8)) {
    // Tokensize: 4 bytes.
    utf8_token_len = 4;
    return false;  // Character exceeds the UCS-2 range (UCS-4 would be necessary)
  } else if (0xF8 == (utf8_token_u[0] & 0xFC)) {
    // Tokensize: 5 bytes.
    utf8_token_len = 5;
    return false;  // Character exceeds the UCS-2 range (UCS-4 would be necessary)
  } else if (0xFC == (utf8_token_u[0] & 0xFE)) {
    // Tokensize: 6 bytes.
    utf8_token_len = 6;
    return false;  // Character exceeds the UCS-2 range (UCS-4 would be necessary)
  } else {
    return false;
  }

  return true;
}

void ucs2_char_to_utf8_char(const wchar_t ucs2_char, char* utf8_token) {
  // We do math that relies on unsigned data types.
  // The standard doesn't specify the signed/unsignedness of wchar_t
  uint32_t ucs2_char_value = static_cast<uint32_t>(ucs2_char);
  unsigned char* utf8_token_u = reinterpret_cast<unsigned char*>(utf8_token);

  // Decode.
  if (0x80 > ucs2_char_value) {
    // Tokensize: 1 byte.
    utf8_token_u[0] = static_cast<unsigned char>(ucs2_char_value);
    utf8_token_u[1] = '\0';
  } else if (0x800 > ucs2_char_value) {
    // Tokensize: 2 bytes.
    utf8_token_u[2] = '\0';
    utf8_token_u[1] = static_cast<unsigned char>(0x80 | (ucs2_char_value & 0x3F));
    ucs2_char_value = (ucs2_char_value >> 6);
    utf8_token_u[0] = static_cast<unsigned char>(0xC0 | ucs2_char_value);
  } else {
    // Tokensize: 3 bytes.
    utf8_token_u[3] = '\0';
    utf8_token_u[2] = static_cast<unsigned char>(0x80 | (ucs2_char_value & 0x3F));
    ucs2_char_value = (ucs2_char_value >> 6);
    utf8_token_u[1] = static_cast<unsigned char>(0x80 | (ucs2_char_value & 0x3F));
    ucs2_char_value = (ucs2_char_value >> 6);
    utf8_token_u[0] = static_cast<unsigned char>(0xE0 | ucs2_char_value);
  }
}
#endif  // USE_CPP11_CODECVT
}  // namespace

#if defined(USE_CPP11_CODECVT)
std::string ucs2_to_utf8(const std::wstring& str16) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
  try {
    return conv.to_bytes(str16);
  } catch (...) {
    return std::string();
  }
}

std::wstring utf8_to_ucs2(const std::string& str8) {
  std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
  try {
    return conv.from_bytes(str8);
  } catch (...) {
    return std::wstring();
  }
}
#else
std::string ucs2_to_utf8(const std::wstring& str16) {
  std::string result;
  const auto* cursor = str16.c_str();
  const auto* const end = str16.c_str() + str16.length();
  while (end > cursor) {
    char utf8_sequence[] = {0, 0, 0, 0, 0};
    ucs2_char_to_utf8_char(*cursor, utf8_sequence);
    result.append(utf8_sequence);
    cursor++;
  }

  return result;
}

std::wstring utf8_to_ucs2(const std::string& str8) {
  std::wstring result;
  const char* cursor = str8.c_str();
  const char* const end = str8.c_str() + str8.length();
  while (end > cursor) {
    wchar_t ucs2_char;
    uint32_t utf8_token_len;
    if (!utf8_char_to_ucs2_char(cursor, ucs2_char, utf8_token_len)) {
      return std::wstring();
    }
    result.append(std::wstring(1, ucs2_char));
    cursor += utf8_token_len;
  }

  return result;
}
#endif  // USE_CPP11_CODECVT

std::string lower_case(const std::string& str) {
  std::string result(str.size(), ' ');
  for (std::string::size_type i = 0; i < str.size(); ++i) {
    auto in = str[i];
    if (('A' <= in) && (in <= 'Z')) {
      in += ('a' - 'A');
    }
    result[i] = in;
  }
  return result;
}

std::string upper_case(const std::string& str) {
  std::string result(str.size(), ' ');
  for (std::string::size_type i = 0; i < str.size(); ++i) {
    auto in = str[i];
    if (('a' <= in) && (in <= 'z')) {
      in -= ('a' - 'A');
    }
    result[i] = in;
  }
  return result;
}
}  // namespace bcache
