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

#ifndef BUILDCACHE_MSVC_WRAPPER_HPP_
#define BUILDCACHE_MSVC_WRAPPER_HPP_

#include <base/string_utils.hpp>
#include <sys/filetracker.hpp>
#include <wrappers/program_wrapper.hpp>

#include <array>
#include <memory>

namespace bcache {
struct cmdline_parser_t;

class version_t {
public:
  version_t() {
  }
  version_t(uint16_t major, uint16_t minor, uint16_t build = 0, uint16_t qfe = 0)
      : m_major(major), m_minor(minor), m_build(build), m_qfe(qfe) {
  }
  version_t(uint64_t rhs) {
    m_major = static_cast<uint16_t>(rhs >> 48);
    m_minor = static_cast<uint16_t>(rhs >> 32);
    m_build = static_cast<uint16_t>(rhs >> 16);
    m_qfe = static_cast<uint16_t>(rhs);
  }
  version_t(const std::string& rhs) {
    *this = {};
    const auto dsts = std::array<uint16_t*, 4>{&m_major, &m_minor, &m_build, &m_qfe};
    const auto vals = split(rhs, '.');
    const auto count = std::min(dsts.size(), vals.size());
    for (size_t i = 0; i < count; i++) {
      *dsts[i] = static_cast<uint16_t>(std::atoi(vals[i].c_str()));
    }
  }
  operator uint64_t() const {
    return (static_cast<uint64_t>(m_major) << 48) | (static_cast<uint64_t>(m_minor) << 32) |
           (static_cast<uint64_t>(m_build) << 16) | m_qfe;
  }
  bool operator<(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) < static_cast<uint64_t>(rhs);
  }
  bool operator<=(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) <= static_cast<uint64_t>(rhs);
  }
  bool operator>(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) > static_cast<uint64_t>(rhs);
  }
  bool operator>=(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) >= static_cast<uint64_t>(rhs);
  }
  bool operator!=(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) != static_cast<uint64_t>(rhs);
  }
  bool operator==(const version_t& rhs) const {
    return static_cast<uint64_t>(*this) == static_cast<uint64_t>(rhs);
  }
  std::string as_string(size_t num_components = 4) const {
    const auto srcs = std::array<uint16_t, 4>{m_major, m_minor, m_build, m_qfe};
    const auto count = std::min(srcs.size(), num_components);
    std::stringstream ss;
    for (size_t i = 0; i < count; i++) {
      if (i > 0) {
        ss << ".";
      }
      ss << srcs[i];
    }
    return ss.str();
  }

private:
  uint16_t m_major{};
  uint16_t m_minor{};
  uint16_t m_build{};
  uint16_t m_qfe{};
};

struct tool_version_t {
  std::string host_arch;
  std::string target_arch;
  version_t vc_version;
};

/// @brief A program wrapper MS Visual Studio.
class msvc_wrapper_t : public program_wrapper_t {
public:
  msvc_wrapper_t(const string_list_t& args);

  bool can_handle_command() override;

private:
  void resolve_args() override;
  string_list_t get_capabilities() override;
  pp_sources_t preprocess_source() override;
  string_list_t get_relevant_arguments() override;
  std::map<std::string, std::string> get_relevant_env_vars() override;
  std::string get_program_id() override;
  build_files_t get_build_files(const pp_key_t& key) override;
  bool filter_cache_hit(const cache_entry_t& entry) override;
  sys::run_result_t run_for_miss(miss_infos_t& miss_infos) override;

  string_list_t get_preprocess_options(const std::string& output_dir) const;
  sys::run_result_t run_with_response_file(const string_list_t& args, bool quiet = true) const;

  bool is_system_include(const std::string& path) const;

  std::unique_ptr<cmdline_parser_t> m_parser;
  tool_version_t m_tool_version;
  filetracker::tracking_log_t m_tlog;
  std::vector<std::string> m_env_include_paths;
  dependency_records_t m_dependencies;

  bool get_dependency_digest(hasher_t::hash_t *digest, const std::string& path) {
    const auto it = m_dependencies.find(path);
    if (it == m_dependencies.end()) {
      return false;
    }
    *digest = it->second;
    return true;
  }
  void set_dependency_digest(const std::string&path, const hasher_t::hash_t& digest) {
    m_dependencies[path] = digest;
  }
};
}  // namespace bcache
#endif  // BUILDCACHE_MSVC_WRAPPER_HPP_
