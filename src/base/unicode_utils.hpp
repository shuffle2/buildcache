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

#ifndef BUILDCACHE_UNICODE_UTILS_HPP_
#define BUILDCACHE_UNICODE_UTILS_HPP_

#include <string>

namespace bcache {
/// @brief Convert a UCS-2 string to a UTF-8 string.
/// @param str16 The UCS-2 encoded wide character string.
/// @returns a UTF-8 encoded string.
std::string ucs2_to_utf8(const std::wstring& str16);

/// @brief Convert a UTF-8 string to a UCS-2 string.
/// @param str8 The UTF-8 encoded string.
/// @returns a UCS-2 encoded wide charater string.
std::wstring utf8_to_ucs2(const std::string& str8);

/// @brief Convert the string to lower case.
/// @param str The string to convert.
/// @returns a lower case version of the input string.
/// @note This function currently only works on the ASCII subset of Unicode.
std::string lower_case(const std::string& str);

/// @brief Convert the string to upper case.
/// @param str The string to convert.
/// @returns an upper case version of the input string.
/// @note This function currently only works on the ASCII subset of Unicode.
std::string upper_case(const std::string& str);
}  // namespace bcache

#endif  // BUILDCACHE_UNICODE_UTILS_HPP_
