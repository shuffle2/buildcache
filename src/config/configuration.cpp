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

#include <config/configuration.hpp>

#include <base/env_utils.hpp>
#include <base/file_utils.hpp>

#include <cjson/cJSON.h>

#include <algorithm>
#include <sstream>
#include <stdexcept>

namespace bcache {
namespace {
// Various constants.
const std::string ROOT_FOLDER_NAME = ".buildcache";
const std::string CONFIGURATION_FILE_NAME = "config.json";
const int64_t DEFAULT_MAX_CACHE_SIZE = 5368709120L;        // 5 GiB
const int64_t DEFAULT_MAX_LOCAL_ENTRY_SIZE = 134217728L;   // 128 MiB
const int64_t DEFAULT_MAX_REMOTE_ENTRY_SIZE = 134217728L;  // 128 MiB

// Configuration options.
std::string s_dir;
std::string s_config_file;
string_list_t s_lua_paths;
std::string s_prefix;
std::string s_impersonate;
std::string s_remote;
std::string s_s3_access;
std::string s_s3_secret;
int64_t s_max_cache_size = DEFAULT_MAX_CACHE_SIZE;
int64_t s_max_local_entry_size = DEFAULT_MAX_LOCAL_ENTRY_SIZE;
int64_t s_max_remote_entry_size = DEFAULT_MAX_REMOTE_ENTRY_SIZE;
int32_t s_debug = -1;
std::string s_log_file;
bool s_hard_links = false;
bool s_cache_link_commands = false;
bool s_compress = false;
int32_t s_compress_level = -1;
bool s_perf = false;
bool s_disable = false;
bool s_read_only = false;
bool s_local_locks = false;
config::cache_accuracy_t s_accuracy = config::cache_accuracy_t::DEFAULT;
config::compress_format_t s_compress_format = config::compress_format_t::DEFAULT;

std::string to_lower(const std::string& str) {
  std::string str_lower(str.size(), ' ');
  std::transform(str.begin(), str.end(), str_lower.begin(), ::tolower);
  return str_lower;
}

std::string get_dir() {
  // Is the environment variable BUILDCACHE_DIR defined?
  {
    const env_var_t dir_env("BUILDCACHE_DIR");
    if (dir_env) {
      return dir_env.as_string();
    }
  }

  // Use the user home directory if possible.
  {
    auto home = file::get_user_home_dir();
    if (!home.empty()) {
      // TODO(m): Should use ".cache/buildcache".
      return file::append_path(home, ROOT_FOLDER_NAME);
    }
  }

  // We failed.
  throw std::runtime_error("Unable to determine a home directory for BuildCache.");
}

std::string get_config_file(const std::string& dir) {
  return file::append_path(dir, CONFIGURATION_FILE_NAME);
}

config::cache_accuracy_t to_cache_accuracy(const std::string& str) {
  const auto s = to_lower(str);
  if (s == "strict") {
    return config::cache_accuracy_t::STRICT;
  } else if (s == "sloppy") {
    return config::cache_accuracy_t::SLOPPY;
  } else {
    return config::cache_accuracy_t::DEFAULT;
  }
}

config::compress_format_t to_compress_format(const std::string& str) {
  const auto s = to_lower(str);
  if (s == "lz4") {
    return config::compress_format_t::LZ4;
  } else if (s == "zstd") {
    return config::compress_format_t::ZSTD;
  } else {
    return config::compress_format_t::DEFAULT;
  }
}

void load_from_file(const std::string& file_name) {
  // Load the configuration file.
  if (!file::file_exists(file_name)) {
    // Nothing to do.
    return;
  }
  const auto data = file::read(file_name);

  // Parse the JSON data.
  auto* root = cJSON_Parse(data.data());
  if (root == nullptr) {
    std::ostringstream ss;
    ss << "Configuration file JSON parse error before:\n";
    const auto* json_error = cJSON_GetErrorPtr();
    if (json_error != nullptr) {
      ss << json_error;
    } else {
      ss << "(N/A)";
    }
    throw std::runtime_error(ss.str());
  }

  // Get "lua_paths".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "lua_paths");
    cJSON* child_node;
    cJSON_ArrayForEach(child_node, node) {
      const auto str = std::string(child_node->valuestring);
      s_lua_paths += str;
    }
  }

  // Get "prefix".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "prefix");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_prefix = std::string(node->valuestring);
    }
  }

  // Get "impersonate".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "impersonate");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_impersonate = std::string(node->valuestring);
    }
  }

  // Get "remote".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "remote");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_remote = std::string(node->valuestring);
    }
  }

  // Get "s3_access".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "s3_access");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_s3_access = std::string(node->valuestring);
    }
  }

  // Get "s3_secret".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "s3_secret");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_s3_secret = std::string(node->valuestring);
    }
  }

  // Get "max_cache_size".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "max_cache_size");
    if (cJSON_IsNumber(node)) {
      s_max_cache_size = static_cast<int64_t>(node->valuedouble);
    }
  }

  // Get "max_local_entry_size".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "max_local_entry_size");
    if (cJSON_IsNumber(node)) {
      s_max_local_entry_size = static_cast<int64_t>(node->valuedouble);
    }
  }

  // Get "max_remote_entry_size".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "max_remote_entry_size");
    if (cJSON_IsNumber(node)) {
      s_max_remote_entry_size = static_cast<int64_t>(node->valuedouble);
    }
  }

  // Get "debug".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "debug");
    if (cJSON_IsNumber(node)) {
      s_debug = static_cast<int32_t>(node->valueint);
    }
  }

  // Get "log_file".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "log_file");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_log_file = std::string(node->valuestring);
    }
  }

  // Get "hard_links".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "hard_links");
    if (cJSON_IsBool(node)) {
      s_hard_links = cJSON_IsTrue(node);
    }
  }

  // Get "cache_link_commands".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "cache_link_commands");
    if (cJSON_IsBool(node)) {
      s_cache_link_commands = cJSON_IsTrue(node);
    }
  }

  // Get "compress".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "compress");
    if (cJSON_IsBool(node)) {
      s_compress = cJSON_IsTrue(node);
    }
  }

  // Get "compression_level".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "compress_level");
    if (cJSON_IsNumber(node)) {
      s_compress_level = static_cast<int32_t>(node->valueint);
    }
  }

  // Get "perf".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "perf");
    if (cJSON_IsBool(node)) {
      s_perf = cJSON_IsTrue(node);
    }
  }

  // Get "disable".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "disable");
    if (cJSON_IsBool(node)) {
      s_disable = cJSON_IsTrue(node);
    }
  }

  // Get "accuracy".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "accuracy");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_accuracy = to_cache_accuracy(std::string(node->valuestring));
    }
  }

  // Get "compress_format".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "compress_format");
    if (cJSON_IsString(node) && node->valuestring != nullptr) {
      s_compress_format = to_compress_format(std::string(node->valuestring));
    }
  }

  // Get "read_only".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "read_only");
    if (cJSON_IsBool(node)) {
      s_read_only = cJSON_IsTrue(node);
    }
  }

  // Get "local_locks".
  {
    const auto* node = cJSON_GetObjectItemCaseSensitive(root, "local_locks");
    if (cJSON_IsBool(node)) {
      s_local_locks = cJSON_IsTrue(node);
    }
  }

  cJSON_Delete(root);
}
}  // namespace

namespace config {
std::string to_string(const cache_accuracy_t accuracy) {
  switch (accuracy) {
    case cache_accuracy_t::STRICT:
      return "STRICT";
    case cache_accuracy_t::DEFAULT:
      return "DEFAULT";
    case cache_accuracy_t::SLOPPY:
      return "SLOPPY";
    default:
      return "?";
  }
}

std::string to_string(const compress_format_t format) {
  switch (format) {
    case compress_format_t::LZ4:
      return "LZ4";
    case compress_format_t::ZSTD:
      return "ZSTD";
    case compress_format_t::DEFAULT:
      return "DEFAULT";
    default:
      return "?";
  }
}

void init() {
  // Guard: Only initialize once.
  static bool s_initialized = false;
  if (s_initialized) {
    return;
  }
  s_initialized = true;

  try {
    // Get the BuildCache home directory.
    s_dir = get_dir();

    // Get the Lua paths from the environment.
    // Note: We need do this before loading the configuration file, in order for the environment to
    // have priority over the JSON file.
    {
      const env_var_t lua_path_env("BUILDCACHE_LUA_PATH");
      if (lua_path_env) {
#ifdef _WIN32
        s_lua_paths += string_list_t(lua_path_env.as_string(), ";");
#else
        s_lua_paths += string_list_t(lua_path_env.as_string(), ":");
#endif
      }
    }

    // Load any paramaters from the user configuration file.
    // Note: We do this before reading the configuration from the environment, so that the
    // environment overrides the configuration file.
    s_config_file = get_config_file(s_dir);
    load_from_file(s_config_file);

    // We also look for Lua files in the cache root dir (e.g. ${BUILDCACHE_DIR}/lua).
    // Note: We need do this after loading the configuration file, to give the default Lua path the
    // lowest priority.
    s_lua_paths += file::append_path(s_dir, "lua");

    // Get the command prefix from the environment.
    {
      const env_var_t prefix_env("BUILDCACHE_PREFIX");
      if (prefix_env) {
        s_prefix = prefix_env.as_string();
      }
    }

    // Get the executable to impersonate from the environment.
    {
      const env_var_t impersonate_env("BUILDCACHE_IMPERSONATE");
      if (impersonate_env) {
        s_impersonate = impersonate_env.as_string();
      }
    }

    // Get the remote cache address from the environment.
    {
      const env_var_t remote_env("BUILDCACHE_REMOTE");
      if (remote_env) {
        s_remote = remote_env.as_string();
      }
    }

    // Get the S3 credentials from the environment.
    {
      const env_var_t s3_access_env("BUILDCACHE_S3_ACCESS");
      if (s3_access_env) {
        s_s3_access = s3_access_env.as_string();
      }
      const env_var_t s3_secret_env("BUILDCACHE_S3_SECRET");
      if (s3_secret_env) {
        s_s3_secret = s3_secret_env.as_string();
      }
    }

    // Get the max cache size from the environment.
    {
      const env_var_t max_cache_size_env("BUILDCACHE_MAX_CACHE_SIZE");
      if (max_cache_size_env) {
        try {
          s_max_cache_size = max_cache_size_env.as_int64();
        } catch (...) {
          // Ignore...
        }
      }
    }

    // Get the max local cache entry size from the environment.
    {
      const env_var_t max_local_entry_size_env("BUILDCACHE_MAX_LOCAL_ENTRY_SIZE");
      if (max_local_entry_size_env) {
        try {
          s_max_local_entry_size = max_local_entry_size_env.as_int64();
        } catch (...) {
          // Ignore...
        }
      }
    }

    // Get the max remote cache entry size from the environment.
    {
      const env_var_t max_remote_entry_size_env("BUILDCACHE_MAX_REMOTE_ENTRY_SIZE");
      if (max_remote_entry_size_env) {
        try {
          s_max_remote_entry_size = max_remote_entry_size_env.as_int64();
        } catch (...) {
          // Ignore...
        }
      }
    }

    // Get the debug level from the environment.
    {
      const env_var_t debug_env("BUILDCACHE_DEBUG");
      if (debug_env) {
        try {
          s_debug = static_cast<int32_t>(debug_env.as_int64());
        } catch (...) {
          // Ignore...
        }
      }
    }

    // Get the log file from the environment.
    {
      const env_var_t log_file_env("BUILDCACHE_LOG_FILE");
      if (log_file_env) {
        s_log_file = log_file_env.as_string();
      }
    }

    // Get the hard_links flag from the environment.
    {
      const env_var_t hard_links_env("BUILDCACHE_HARD_LINKS");
      if (hard_links_env) {
        s_hard_links = hard_links_env.as_bool();
      }
    }

    // Get the cache_link_commands flag from the environment.
    {
      const env_var_t cache_link_commands_env("BUILDCACHE_CACHE_LINK_COMMANDS");
      if (cache_link_commands_env) {
        s_cache_link_commands = cache_link_commands_env.as_bool();
      }
    }

    // Get the compress flag from the environment.
    {
      const env_var_t compress_env("BUILDCACHE_COMPRESS");
      if (compress_env) {
        s_compress = compress_env.as_bool();
      }
    }

    // Get the compression level flag from the environment.
    {
      const env_var_t compress_level_env("BUILDCACHE_COMPRESS_LEVEL");
      if (compress_level_env) {
        s_compress_level = static_cast<int32_t>(compress_level_env.as_int64());
      }
    }

    // Get the perf flag from the environment.
    {
      const env_var_t perf_env("BUILDCACHE_PERF");
      if (perf_env) {
        s_perf = perf_env.as_bool();
      }
    }

    // Get the disabled flag from the environment.
    {
      const env_var_t disable_env("BUILDCACHE_DISABLE");
      if (disable_env) {
        s_disable = disable_env.as_bool();
      }
    }

    // Get the cache accuracy from the environment.
    {
      const env_var_t accuracy_env("BUILDCACHE_ACCURACY");
      if (accuracy_env) {
        s_accuracy = to_cache_accuracy(accuracy_env.as_string());
      }
    }

    // Get the compression format from the environment.
    {
      const env_var_t format_env("BUILDCACHE_COMPRESS_FORMAT");
      if (format_env) {
        s_compress_format = to_compress_format(format_env.as_string());
      }
    }

    // Get the read only flag from the environment.
    {
      const env_var_t read_only_env("BUILDCACHE_READ_ONLY");
      if (read_only_env) {
        s_read_only = read_only_env.as_bool();
      }
    }

    // Get the local lock flag from the environment.
    {
      const env_var_t local_locks_env("BUILDCACHE_LOCAL_LOCKS");
      if (local_locks_env) {
        s_local_locks = local_locks_env.as_bool();
      }
    }
  } catch (...) {
    // If we could not initialize the configuration, we can't proceed. We need to disable the cache.
    s_disable = true;
    throw;
  }
}

const std::string& dir() {
  return s_dir;
}

const std::string& config_file() {
  return s_config_file;
}

const string_list_t& lua_paths() {
  return s_lua_paths;
}

const std::string& prefix() {
  return s_prefix;
}

const std::string& impersonate() {
  return s_impersonate;
}

const std::string& remote() {
  return s_remote;
}

const std::string& s3_access() {
  return s_s3_access;
}

const std::string& s3_secret() {
  return s_s3_secret;
}

int64_t max_cache_size() {
  return s_max_cache_size;
}

int64_t max_local_entry_size() {
  return s_max_local_entry_size;
}

int64_t max_remote_entry_size() {
  return s_max_remote_entry_size;
}

int32_t debug() {
  return s_debug;
}

const std::string& log_file() {
  return s_log_file;
}

bool hard_links() {
  return s_hard_links;
}

bool cache_link_commands() {
  return s_cache_link_commands;
}

bool compress() {
  return s_compress;
}

int32_t compress_level() {
  return s_compress_level;
}

bool perf() {
  return s_perf;
}

bool disable() {
  return s_disable;
}

cache_accuracy_t accuracy() {
  return s_accuracy;
}

compress_format_t compress_format() {
  return s_compress_format;
}

bool read_only() {
  return s_read_only;
}

bool local_locks() {
  return s_local_locks;
}

}  // namespace config
}  // namespace bcache
