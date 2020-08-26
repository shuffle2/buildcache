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

#ifndef BUILDCACHE_PROGRAM_WRAPPER_HPP_
#define BUILDCACHE_PROGRAM_WRAPPER_HPP_

#include <base/file_utils.hpp>
#include <base/string_list.hpp>
#include <cache/cache.hpp>
#include <cache/expected_file.hpp>
#include <sys/sys_utils.hpp>

#include <map>
#include <string>
#include <tuple>

namespace bcache {
using build_files_t = std::map<std::string, expected_file_t>;

/// @brief Types relating to a set of preprocessing results. The meaning of the key is defined by
/// the derived implementation.
using pp_key_t = std::string;
using pp_sources_t = std::map<pp_key_t, std::string>;
using miss_info_t = std::tuple<pp_key_t, hasher_t::hash_t, build_files_t, dependency_records_t>;
using miss_infos_t = std::vector<miss_info_t>;

/// @brief The base class for all program wrappers.
///
/// Specialized program wrappers inherit from this class and implement the relevant virtual methods
/// that constitute the "wrapper API".
///
/// This class also implements the entire program wrapping mechanism, with cache lookups etc.
class program_wrapper_t {
public:
  virtual ~program_wrapper_t();

  /// @brief Try to wrap a program command.
  /// @param[out] return_code The command return code (if handled).
  /// @returns true if the command was recognized and handled.
  bool handle_command(int& return_code);

  /// @brief Check if this class implements a wrapper for the given command.
  /// @returns true if this wrapper can handle the command.
  virtual bool can_handle_command() = 0;

protected:
  // This constructor is called by derived classes.
  program_wrapper_t(const string_list_t& args);

  /// @brief Resolve arguments on the command line.
  ///
  /// This method is called after @c can_handle_command, but before calling other methods that rely
  /// on the command line arguments. This gives a wrapper the opportunity to resolve special
  /// arguments so that they can be properly interpretade by later steps.
  ///
  /// The primary use case of this method is to implement support for so called response files,
  /// which contain further command line arguments (this is common on Windows to work around the
  /// relativlely small command line length limit).
  ///
  /// @throws runtime_error if the request could not be completed.
  virtual void resolve_args();

  /// @brief Generate a list of supported capabilites.
  ///
  /// The list of capabilites can contain zero or more of the following (case sensitive) strings:
  ///
  /// | String     | Meaning                              |
  /// | ---------- | ------------------------------------ |
  /// | hard_links | Can use hard links for cached files  |
  ///
  /// @returns a list of supported capabilites.
  /// @throws runtime_error if the request could not be completed.
  /// @note Capabilites are "opt-in". If this method is not implemented or returns an empty list,
  /// all capabilites will be treated as "not supported".
  virtual string_list_t get_capabilities();

  /// @brief Generate the preprocessed source text.
  /// @returns the preprocessed source code file as a string, or an empty string if the operation
  /// failed.
  /// @throws runtime_error if the request could not be completed.
  virtual pp_sources_t preprocess_source();

  /// @brief Get relevant command line arguments for hashing.
  /// @returns filtered command line arguments.
  /// @throws runtime_error if the request could not be completed.
  ///
  /// @note The purpose of this function is to create a list of arguments that is suitable for
  /// hashing (i.e. uniquely identifying) the program options. As such, things like absolute file
  /// paths and compilation defines should be excluded (since they are resolved in the preprocess
  /// step).
  virtual string_list_t get_relevant_arguments();

  /// @brief Get relevant environment variables for hashing.
  /// @returns relevant environment variables and their values.
  /// @throws runtime_error if the request could not be completed.
  ///
  /// @note The purpose of this function is to create a list of environment variables that may
  /// affect program output.
  virtual std::map<std::string, std::string> get_relevant_env_vars();

  /// @brief Get a string that uniquely identifies the program.
  /// @returns a program ID string.
  /// @throws runtime_error if the request could not be completed.
  virtual std::string get_program_id();

  /// @brief Get the paths to the files that are to be generated by the command.
  /// @returns the files that are expected to be generated by this command.
  /// @throws runtime_error if the request could not be completed.
  virtual build_files_t get_build_files(const pp_key_t& key);

  virtual bool filter_cache_hit(const cache_entry_t& entry);

  /// @brief Run the actual command (when there is a cache miss).
  /// @returns the run result for the child process.
  virtual sys::run_result_t run_for_miss(miss_infos_t& miss_infos);

  const string_list_t& m_args;

private:
  std::string get_program_id_cached();

  cache_t m_cache;
};
}  // namespace bcache

#endif  // BUILDCACHE_PROGRAM_WRAPPER_HPP_
