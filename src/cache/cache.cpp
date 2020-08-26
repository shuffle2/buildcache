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

#include <base/debug_utils.hpp>
#include <base/file_utils.hpp>
#include <cache/cache.hpp>
#include <config/configuration.hpp>
#include <sys/perf_utils.hpp>
#include <sys/sys_utils.hpp>

#include <iostream>

namespace bcache {
namespace {
// Return the total size (uncompressed bytes) for a cache entry.
int64_t get_total_entry_size(const cache_entry_t& entry,
                             const std::map<std::string, expected_file_t>& file_paths) {
  int64_t total_size =
      static_cast<int64_t>(entry.std_out().size()) + static_cast<int64_t>(entry.std_err().size());
  for (const auto& item : file_paths) {
    const auto& expected_file = item.second;
    try {
      total_size += file::get_file_info(expected_file.path()).size();
    } catch (std::exception& e) {
      if (expected_file.required()) {
        throw e;
      }
    }
  }
  return total_size;
}
}  // namespace

bool cache_t::lookup(const hasher_t::hash_t hash,
                     const std::map<std::string, expected_file_t>& expected_files,
                     lookup_filter_t filter,
                     const bool allow_hard_links,
                     const bool create_target_dirs,
                     int& return_code) {
  // First try the local cache.
  if (lookup_in_local_cache(
          hash, expected_files, filter, allow_hard_links, create_target_dirs, return_code)) {
    return true;
  }

  // Then try the remote cache.
  if (lookup_in_remote_cache(
          hash, expected_files, filter, allow_hard_links, create_target_dirs, return_code)) {
    return true;
  }

  return false;
}

void cache_t::add(const hasher_t::hash_t hash,
                  const cache_entry_t& entry,
                  const std::map<std::string, expected_file_t>& expected_files,
                  const bool allow_hard_links) {
  PERF_START(ADD_TO_CACHE);

  // We need the size of the cache entry for checking against the configured limits.
  const auto size = get_total_entry_size(entry, expected_files);

  // Add the entry to the local cache.
  const auto max_local_size = config::max_local_entry_size();
  if (size < max_local_size || max_local_size <= 0) {
    m_local_cache.add(hash, entry, expected_files, allow_hard_links);
  } else {
    debug::log(debug::INFO) << "Cache entry too large for the local cache: " << size << " bytes";
  }

  // Add the entry to the remote cache.
  if (m_remote_cache.is_connected()) {
    const auto max_remote_size = config::max_remote_entry_size();
    if (size < max_remote_size || max_remote_size <= 0) {
      // Note: We always compress entries for the remote cache.
      const cache_entry_t remote_entry(entry.file_ids(),
                                       entry.dependency_records(),
                                       cache_entry_t::comp_mode_t::ALL,
                                       entry.std_out(),
                                       entry.std_err(),
                                       entry.return_code());
      m_remote_cache.add(hash, remote_entry, expected_files);
    } else {
      debug::log(debug::INFO) << "Cache entry too large for the remote cache: " << size << " bytes";
    }
  }

  PERF_STOP(ADD_TO_CACHE);
}

bool cache_t::lookup_in_local_cache(const hasher_t::hash_t hash,
                                    const std::map<std::string, expected_file_t>& expected_files,
                                    lookup_filter_t filter,
                                    const bool allow_hard_links,
                                    const bool create_target_dirs,
                                    int& return_code) {
  PERF_START(CACHE_LOOKUP);
  // Note: The lookup will give us a lock file that is locked until we go out of scope.
  auto lookup_result = m_local_cache.lookup(hash);
  const auto& cached_entry = lookup_result.first;
  PERF_STOP(CACHE_LOOKUP);

  if (!cached_entry || !filter(cached_entry)) {
    return false;
  }

  // Copy all files from the cache to their respective target paths.
  // Note: If there is a mismatch in the expected (target) files and the actual (cached)
  // files, this will throw an exception (i.e. fall back to full program execution).
  PERF_START(RETRIEVE_CACHED_FILES);
  for (const auto& file_id : cached_entry.file_ids()) {
    const auto& target_path = expected_files.at(file_id).path();
    debug::log(debug::INFO) << "Cache hit (" << hash.as_string() << "): " << file_id << " => "
                            << target_path;

    if (create_target_dirs) {
      file::create_dir_with_parents(file::get_dir_part(target_path));
    }

    const auto is_compressed = (cached_entry.compression_mode() == cache_entry_t::comp_mode_t::ALL);
    m_local_cache.get_file(hash, file_id, target_path, is_compressed, allow_hard_links);
  }
  PERF_STOP(RETRIEVE_CACHED_FILES);

  // Return/print the cached program results.
  sys::print_raw_stdout(cached_entry.std_out());
  sys::print_raw_stderr(cached_entry.std_err());
  return_code = cached_entry.return_code();

  return true;
}

bool cache_t::lookup_in_remote_cache(const hasher_t::hash_t hash,
                                     const std::map<std::string, expected_file_t>& expected_files,
                                     lookup_filter_t filter,
                                     const bool allow_hard_links,
                                     const bool create_target_dirs,
                                     int& return_code) {
  // Start by trying to connect to the remote cache.
  if (!m_remote_cache.connect()) {
    return false;
  }

  PERF_START(CACHE_LOOKUP);
  const auto cached_entry = m_remote_cache.lookup(hash);
  PERF_STOP(CACHE_LOOKUP);

  if (!cached_entry || !filter(cached_entry)) {
    m_local_cache.update_stats(hash, cache_stats_t::remote_miss());
    return false;
  }

  // Copy all files from the cache to their respective target paths.
  // Note: If there is a mismatch in the expected (target) files and the actual (cached)
  // files, this will throw an exception (i.e. fall back to full program execution).
  PERF_START(RETRIEVE_CACHED_FILES);
  for (const auto& file_id : cached_entry.file_ids()) {
    const auto& target_path = expected_files.at(file_id).path();
    debug::log(debug::INFO) << "Remote cache hit (" << hash.as_string() << "): " << file_id
                            << " => " << target_path;

    if (create_target_dirs) {
      file::create_dir_with_parents(file::get_dir_part(target_path));
    }

    const auto is_compressed = (cached_entry.compression_mode() == cache_entry_t::comp_mode_t::ALL);
    m_remote_cache.get_file(hash, file_id, target_path, is_compressed);
  }
  PERF_STOP(RETRIEVE_CACHED_FILES);

  // Return/print the cached program results.
  sys::print_raw_stdout(cached_entry.std_out());
  sys::print_raw_stderr(cached_entry.std_err());
  return_code = cached_entry.return_code();

  // Add the remote entry to the local cache (for faster cache hits and reduced network traffic).
  PERF_START(ADD_TO_CACHE);
  try {
    const auto size = get_total_entry_size(cached_entry, expected_files);
    const auto max_local_size = config::max_local_entry_size();
    if (size < max_local_size || max_local_size <= 0) {
      // Remote entries are likely to be compressed. We only turn on compression for the local cache
      // if the configuration tells us to.
      const cache_entry_t entry(
          cached_entry.file_ids(),
          cached_entry.dependency_records(),
          config::compress() ? cache_entry_t::comp_mode_t::ALL : cache_entry_t::comp_mode_t::NONE,
          cached_entry.std_out(),
          cached_entry.std_err(),
          cached_entry.return_code());
      m_local_cache.add(hash, entry, expected_files, allow_hard_links);
      m_local_cache.update_stats(hash, cache_stats_t::remote_hit());
    } else {
      debug::log(debug::INFO) << "Cache entry too large for the local cache: " << size << " bytes";
    }
  } catch (std::exception& e) {
    debug::log(debug::ERROR) << "Unable to add remote entry to the local cache: " << e.what();
  }
  PERF_STOP(ADD_TO_CACHE);

  return true;
}
}  // namespace bcache
