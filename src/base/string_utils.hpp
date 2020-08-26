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

#ifndef BUILDCACHE_STRING_UTILS_HPP_
#define BUILDCACHE_STRING_UTILS_HPP_

#include <string>
#include <vector>

namespace bcache {
inline bool starts_with(const std::string& str, const std::string& sub_str) {
  return str.substr(0, sub_str.size()) == sub_str;
}

inline std::vector<std::string> split(const std::string& str, char delimiter) {
  std::vector<std::string> result;
  for (std::string::size_type pos = 0, next = 0; next != std::string::npos;
       pos = next + sizeof(delimiter)) {
    next = str.find_first_of(delimiter, pos);
    result.emplace_back(str.substr(pos, next - pos));
  }
  return result;
}
}  // namespace bcache

#endif  // BUILDCACHE_STRING_UTILS_HPP_
