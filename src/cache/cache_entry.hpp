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

#ifndef BUILDCACHE_CACHE_ENTRY_HPP_
#define BUILDCACHE_CACHE_ENTRY_HPP_

#include <base/hasher.hpp>

#include <string>
#include <vector>

namespace bcache {
using dependency_records_t = std::map<std::string, hasher_t::hash_t>;

/// @brief Meta data for a single cache entry.
class cache_entry_t {
public:
  /// @brief Compression mode.
  enum class comp_mode_t {
    NONE = 0,  ///< Compress nothing (i.e. the entry is uncompressed in the cache).
    ALL = 1    ///< Compress all files and stdout + stderr.
  };

  /// @brief Construct an empty/invalid cache entry.
  cache_entry_t();

  /// @brief Construct a valid cache entry.
  /// @param file_ids ID:s of the cached files.
  /// @param compression_mode Compression mode.
  /// @param std_out stdout from the program run.
  /// @param std_err stderr from the program run.
  /// @param return_code Program return code (0 = success).
  cache_entry_t(const std::vector<std::string>& file_ids,
                const dependency_records_t& dependencies,
                const comp_mode_t compression_mode,
                const std::string& std_out,
                const std::string& std_err,
                const int return_code);

  /// @returns true if this object represents a valid cache entry. E.g. for a cache miss, the
  /// return value is false.
  operator bool() const {
    return m_valid;
  }

  /// @brief Serialize a cache entry.
  /// @returns a serialized data as a string object.
  std::string serialize() const;

  /// @brief Deserialize a cache entry.
  /// @param data The serialized data.
  /// @returns the deserialized cache entry.
  static cache_entry_t deserialize(const std::string& data);

  /// @returns the ID:s of the cached files.
  const std::vector<std::string>& file_ids() const {
    return m_file_ids;
  }

  /// @returns Records describing files which file_ids depend on.
  const dependency_records_t& dependency_records() const {
    return m_dependency_records;
  }

  /// @returns the compression mode.
  comp_mode_t compression_mode() const {
    return m_compression_mode;
  }

  /// @returns the stdout from the program run.
  const std::string& std_out() const {
    return m_std_out;
  }

  /// @returns the stderr from the program run.
  const std::string& std_err() const {
    return m_std_err;
  }

  /// @returns the program return code (0 = success).
  int return_code() const {
    return m_return_code;
  }

private:
  std::vector<std::string> m_file_ids;
  dependency_records_t m_dependency_records;
  comp_mode_t m_compression_mode = comp_mode_t::NONE;
  std::string m_std_out;
  std::string m_std_err;
  int m_return_code = 0;
  bool m_valid = false;  // true if this is a valid cache entry.
};
}  // namespace bcache

#endif  // BUILDCACHE_CACHE_ENTRY_HPP_
