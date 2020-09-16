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

#ifndef BUILDCACHE_CONFIGURATION_HPP_
#define BUILDCACHE_CONFIGURATION_HPP_

#include <base/string_list.hpp>

#include <cstdint>

#include <map>
#include <string>

namespace bcache {
namespace config {

/// @brief The cache accuracy.
enum class cache_accuracy_t {
  SLOPPY,   ///< Maximize cache hit ratio, but may produce incorrect results for certain use cases.
  DEFAULT,  ///< For most users.
  STRICT    ///< Be as strict as possible.
};

/// @brief The compression format.
enum class compress_format_t {
  LZ4,     ///< Utilize LZ4 compression (faster compression, larger cache sizes)
  ZSTD,    ///< Utilize ZSTD compression (slower compression, smaller cache sizes)
  DEFAULT  ///< Utilize LZ4 compression
};

/// @brief Convert a cache accuracy enum value to a string.
/// @param accuracy The cache accuracy.
/// @returns an upper case string representing the cache accuracy.
std::string to_string(const cache_accuracy_t accuracy);

/// @brief Convert a compression format enum value to a string.
/// @param format The compression format.
/// @returns an upper case string representing the compression format.
std::string to_string(const compress_format_t format);

/// @brief Initialize the configuration based on environment variables etc.
void init();

/// @returns the BuildCache home directory.
const std::string& dir();

/// @returns the BuildCache configuration file.
const std::string& config_file();

/// @returns the Lua search paths.
const string_list_t& lua_paths();

/// @returns the compiler execution prefix command.
const std::string& prefix();

/// @returns the executable to impersonate.
const std::string& impersonate();

/// @returns the remote cache service address.
const std::string& remote();

/// @returns the S3 access key for the remote cache.
const std::string& s3_access();

/// @returns the S3 secret key for the remote cache.
const std::string& s3_secret();

/// @returns the maximum cache size (in bytes).
int64_t max_cache_size();

/// @returns the maximum local cache entry size (in bytes).
int64_t max_local_entry_size();

/// @returns the maximum remote cache entry size (in bytes).
int64_t max_remote_entry_size();

/// @returns the debug level (-1 for no debugging).
int32_t debug();

/// @returns the log file (empty string for stdout).
const std::string& log_file();

/// @returns true if BuildCache should use hard links when possible.
bool hard_links();

/// @returns true if BuildCache should cache link commands.
bool cache_link_commands();

/// @returns true if BuildCache should compress data in the cache.
bool compress();

/// @returns the compression format.
compress_format_t compress_format();

/// @returns the compression level.
int32_t compress_level();

/// @returns true if performance profiling output is enabled.
bool perf();

/// @returns true if BuildCache is disabled.
bool disable();

/// @returns true if the readonly mode is enabled.
bool read_only();

/// @returns true if lock_file_t may use implementation optimized for sharing on local machine only.
bool local_locks();

/// @returns the cache accuracy.
cache_accuracy_t accuracy();

}  // namespace config
}  // namespace bcache

#endif  // BUILDCACHE_CONFIGURATION_HPP_
