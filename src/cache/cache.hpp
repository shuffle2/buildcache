//--------------------------------------------------------------------------------------------------
// Copyright (c) 2019 Marcus Geelnard
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

#ifndef BUILDCACHE_CACHE_HPP_
#define BUILDCACHE_CACHE_HPP_

#include <base/hasher.hpp>
#include <cache/cache_entry.hpp>
#include <cache/expected_file.hpp>
#include <cache/local_cache.hpp>
#include <cache/remote_cache.hpp>

#include <functional>
#include <map>
#include <string>

namespace bcache {
/// @brief Return true if the cache entry should be treated as a cache hit.
using lookup_filter_t = std::function<bool(const cache_entry_t&)>;

/// @brief An interface to the different caches.
class cache_t {
public:
  /// @brief Perform a cache lookup.
  /// @param hash The hash of the cache entry.
  /// @param expected_files Paths to the actual files in the local file system (map from file ID to
  /// an expected file descriptor).
  /// @param filter Allows the caller to inspect (and possibly reject) a cache entry while the lock
  /// is held.
  /// @param allow_hard_links True if we are allowed to use hard links.
  /// @param create_target_dirs True if the target directory of the cached file must be created.
  /// @param[out] return_code The return code of the program.
  /// @returns true if we had a cache hit, otherwise false.
  bool lookup(const hasher_t::hash_t hash,
              const std::map<std::string, expected_file_t>& expected_files,
              lookup_filter_t filter,
              const bool allow_hard_links,
              const bool create_target_dirs,
              int& return_code);

  /// @brief Add a new entry to the cache(s).
  /// @param hash The hash of the cache entry.
  /// @param entry The cache entry description.
  /// @param expected_files Paths to the actual files in the local file system (map from file ID to
  /// an expected file descriptor).
  /// @param allow_hard_links True if we are allowed to use hard links.
  void add(const hasher_t::hash_t hash,
           const cache_entry_t& entry,
           const std::map<std::string, expected_file_t>& expected_files,
           const bool allow_hard_links);

private:
  bool lookup_in_local_cache(const hasher_t::hash_t hash,
                             const std::map<std::string, expected_file_t>& expected_files,
                             lookup_filter_t filter,
                             const bool allow_hard_links,
                             const bool create_target_dirs,
                             int& return_code);

  bool lookup_in_remote_cache(const hasher_t::hash_t hash,
                              const std::map<std::string, expected_file_t>& expected_files,
                              lookup_filter_t filter,
                              const bool allow_hard_links,
                              const bool create_target_dirs,
                              int& return_code);

  local_cache_t m_local_cache;
  remote_cache_t m_remote_cache;
};
}  // namespace bcache

#endif  // BUILDCACHE_CACHE_HPP_
