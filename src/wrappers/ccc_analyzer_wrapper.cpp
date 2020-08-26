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

#include <base/debug_utils.hpp>
#include <base/env_utils.hpp>
#include <base/unicode_utils.hpp>
#include <wrappers/ccc_analyzer_wrapper.hpp>

#include <regex>
#include <sstream>

namespace bcache {

ccc_analyzer_wrapper_t::ccc_analyzer_wrapper_t(const string_list_t& args)
    : gcc_wrapper_t(args), m_tmp_report_dir(file::get_temp_dir(), "") {
}

bool ccc_analyzer_wrapper_t::can_handle_command() {
  const auto cmd = lower_case(file::get_file_part(m_args[0], true));

  // We recognize ccc-analyzer and c++-analyzer.
  const std::regex ccc_analyzer_re("c(\\+\\+|cc)-analyzer");
  if (std::regex_match(cmd, ccc_analyzer_re)) {
    return true;
  }

  return false;
}

std::map<std::string, std::string> ccc_analyzer_wrapper_t::get_relevant_env_vars() {
  auto env_vars = gcc_wrapper_t::get_relevant_env_vars();

  // The ccc-analyzer script uses environment variables on the form "CCC_ANALYZER_*" as arguments.
  static const std::string CCC_ANALYZER_ENV_VARS[] = {"CCC_ANALYZER_LOG",
                                                      "CCC_ANALYZER_ANALYSIS",
                                                      "CCC_ANALYZER_PLUGINS",
                                                      "CCC_ANALYZER_STORE_MODEL",
                                                      "CCC_ANALYZER_CONSTRAINTS_MODEL",
                                                      "CCC_ANALYZER_INTERNAL_STATS",
                                                      "CCC_ANALYZER_OUTPUT_FORMAT",
                                                      "CCC_ANALYZER_CONFIG",
                                                      "CCC_ANALYZER_VERBOSE",
                                                      "CCC_ANALYZER_LOG",
                                                      "CCC_ANALYZER_FORCE_ANALYZE_DEBUG_CODE"};
  for (const auto& key : CCC_ANALYZER_ENV_VARS) {
    env_var_t env_var(key);
    if (env_var) {
      env_vars[key] = env_var.as_string();
      debug::log(debug::log_level_t::DEBUG) << "ENV " << key << "=" << env_var.as_string();
    }
  }

  return env_vars;
}

build_files_t ccc_analyzer_wrapper_t::get_build_files(const pp_key_t& key) {
  auto files = gcc_wrapper_t::get_build_files(key);

  // Get the target report path.
  env_var_t report_dir("CCC_ANALYZER_HTML");
  if (!report_dir) {
    throw std::runtime_error("CCC_ANALYZER_HTML is not specified");
  }

  // We invent our own file names for the reports, since ccc-analyzer will create random file names
  // that we can not know beforehand.
  for (int i = 0; i < MAX_NUM_REPORTS; ++i) {
    const auto file_name = "report-" + file::get_unique_id() + ".html";
    m_report_paths[i] = file::append_path(report_dir.as_string(), file_name);

    std::ostringstream file_id;
    file_id << "ccc_analyzer_report_" << (i + 1);
    files[file_id.str()] = {m_report_paths[i], false};
  }

  return files;
}

sys::run_result_t ccc_analyzer_wrapper_t::run_for_miss(miss_infos_t& miss_infos) {
  // Create the temporary report dir (it will be deleted automatically when the wrapper object goes
  // out of scope).
  file::create_dir_with_parents(m_tmp_report_dir.path());

  // Run the command, and instruct ccc-analyzer to write its reports to the temporary dir.
  sys::run_result_t result;
  {
    scoped_set_env_t scoped_env("CCC_ANALYZER_HTML", m_tmp_report_dir.path());
    result = sys::run_with_prefix(m_args, false);
  }

  // Find the reports (we don't know what they are called) and move them to the target location.
  const auto files = file::walk_directory(m_tmp_report_dir.path());
  int num_reports = 0;
  for (const auto& file : files) {
    if (!file.is_dir()) {
      if (num_reports >= MAX_NUM_REPORTS) {
        throw std::runtime_error("Too many ccc-analyzer reports were found");
      }
      debug::log(debug::DEBUG) << "Found report: " << file.path() << " -> "
                               << m_report_paths[num_reports];
      file::copy(file.path(), m_report_paths[num_reports]);
      ++num_reports;
    }
  }

  return result;
}

}  // namespace bcache
