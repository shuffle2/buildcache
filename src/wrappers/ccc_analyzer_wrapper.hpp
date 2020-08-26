//--------------------------------------------------------------------------------------------------
// Copyright (c) 2020 Marcus Geelnard
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

#ifndef BUILDCACHE_CCC_ANALYZER_WRAPPER_HPP_
#define BUILDCACHE_CCC_ANALYZER_WRAPPER_HPP_

#include <wrappers/gcc_wrapper.hpp>

namespace bcache {
/// @brief This is a wrapper for the scan-build ccc-analyzer (a Clang-based static analyzer).
///
/// The analyzer runs GCC/Clang under the hood, so we inherit from the GCC wrapper.
class ccc_analyzer_wrapper_t : public gcc_wrapper_t {
public:
  ccc_analyzer_wrapper_t(const string_list_t& args);

  bool can_handle_command() override;

private:
  std::map<std::string, std::string> get_relevant_env_vars() override;
  build_files_t get_build_files(const pp_key_t& key) override;
  sys::run_result_t run_for_miss(miss_infos_t& miss_infos) override;

  static const int MAX_NUM_REPORTS = 10;
  std::string m_report_paths[MAX_NUM_REPORTS];
  file::tmp_file_t m_tmp_report_dir;
};
}  // namespace bcache

#endif  // BUILDCACHE_CCC_ANALYZER_WRAPPER_HPP_
