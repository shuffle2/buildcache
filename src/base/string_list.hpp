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

#ifndef BUILDCACHE_STRING_LIST_HPP_
#define BUILDCACHE_STRING_LIST_HPP_

#include <initializer_list>
#include <string>
#include <vector>

namespace bcache {
class string_list_t {
public:
  typedef typename std::vector<std::string>::iterator iterator;
  typedef typename std::vector<std::string>::const_iterator const_iterator;

  /// @brief Construct an empty list.
  string_list_t() {
  }

  /// @brief Construct a list from an initializer list.
  /// @param list The strings to add to the string list object.
  string_list_t(std::initializer_list<std::string> list) : m_args(list) {
  }

  /// @brief Construct a list from an const_iterator.
  /// @param list The range to add to the string list object.
  string_list_t(const_iterator first, const_iterator last) : m_args(first, last) {
  }

  /// @brief Construct a list from command line arguments.
  /// @param argc Argument count.
  /// @param argv Argument array.
  /// @note This is useful for handling command line arguments from a program main() function.
  string_list_t(const int argc, const char** argv) {
    for (int i = 0; i < argc; ++i) {
      m_args.emplace_back(std::string(argv[i]));
    }
  }

  /// @brief Construct a list from a delimited string.
  /// @param str The string.
  /// @param delimiter The delimiter.
  /// @note This is useful for splitting a string into a list of strings (e.g. the PATH environment
  /// variable).
  string_list_t(const std::string& str, const std::string& delimiter);

  /// @brief Construct a list of arguments from a string with a shell-like format.
  /// @param cmd The string.
  /// @note As far as possible this routine mimics the standard shell behaviour, e.g. w.r.t.
  /// escaping and quotation.
  static string_list_t split_args(const std::string& cmd);

  /// @brief Join all elements into a single string.
  /// @param separator The separator to use between strings (e.g. " ").
  /// @param escape Set this to true to escape each string.
  /// @returns a string containing all the strings of the list.
  /// @note When string escaping is enabled, the strings are escaped in a way that preserves
  /// command line argument information. E.g. strings that contain spaces are surrounded by quotes.
  std::string join(const std::string& separator, const bool escape = false) const;

  /// @brief Remove all the elements.
  void clear() {
    m_args.clear();
  }

  std::string& operator[](const size_t idx) {
    return m_args[idx];
  }

  const std::string& operator[](const size_t idx) const {
    return m_args[idx];
  }

  string_list_t& operator+=(const std::string& str) {
    m_args.emplace_back(str);
    return *this;
  }

  string_list_t& operator+=(const string_list_t& list) {
    for (const auto& str : list) {
      m_args.emplace_back(str);
    }
    return *this;
  }

  string_list_t operator+(const std::string& str) const {
    string_list_t result(*this);
    result += str;
    return result;
  }

  string_list_t operator+(const string_list_t& list) const {
    string_list_t result(*this);
    result += list;
    return result;
  }

  size_t size() const {
    return m_args.size();
  }

  bool empty() const {
    return m_args.size() == 0;
  }

  iterator begin() {
    return m_args.begin();
  }

  const_iterator begin() const {
    return m_args.begin();
  }

  const_iterator cbegin() const {
    return m_args.cbegin();
  }

  iterator end() {
    return m_args.end();
  }

  const_iterator end() const {
    return m_args.end();
  }

  const_iterator cend() const {
    return m_args.cend();
  }

private:
  static std::string escape_arg(const std::string& arg);

  static std::string unescape_arg(const std::string& arg);

  std::vector<std::string> m_args;
};
}  // namespace bcache

#endif  // BUILDCACHE_STRING_LIST_HPP_
