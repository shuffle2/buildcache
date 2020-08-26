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

#ifndef BUILDCACHE_FILETRACKER_HPP_
#define BUILDCACHE_FILETRACKER_HPP_

#include <base/string_list.hpp>
#include <wrappers/program_wrapper.hpp>

namespace bcache {
namespace filetracker {
/// @brief Re-enable FileTracker monitoring. Should be used before performing a fallback action
/// which may produce outputs that the build system needs to be aware of.
void release_suppression();

class tracking_log_t {
public:
  tracking_log_t();
  bool enabled() const {
    return m_enabled;
  };
  build_files_t get_build_files(const std::string& filename) const;
  void add_source(const std::string& path);
  void finalize_sources();
  void write_logs(const std::string& source,
                  const build_files_t& build_files,
                  const string_list_t& dependencies) const;

private:
  std::string fullpath(const std::string& path) const;

  bool m_enabled;
  std::string m_intermediate_dir;
  std::string m_toolchain;
  string_list_t m_sources;
  std::string m_root;
};
}  // namespace filetracker
}  // namespace bcache

#endif  // BUILDCACHE_FILETRACKER_HPP_
