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

#ifndef BUILDCACHE_LUA_WRAPPER_HPP_
#define BUILDCACHE_LUA_WRAPPER_HPP_

#include <base/env_utils.hpp>
#include <wrappers/program_wrapper.hpp>

extern "C" {
typedef struct lua_State lua_State;
}

namespace bcache {
/// @brief A program wrapper that wraps Lua scripts.
///
/// This special wrapper class implements the wrapper API methods by redirecting the calls to the
/// corrsponding methods in a Lua script.
class lua_wrapper_t : public program_wrapper_t {
public:
  lua_wrapper_t(const string_list_t& args, const std::string& lua_script_path);

  bool can_handle_command() override;

private:
  // A helper class for managing the Lua state.
  class runner_t {
  public:
    runner_t(const std::string& script_path, const string_list_t& args);
    ~runner_t();

    bool call(const std::string& func);

    const std::string& script() const {
      return m_script;
    }

    lua_State* state() {
      return m_state;
    }

  private:
    void init_lua_state();
    void setup_lua_libs_and_globals();
    [[noreturn]] void bail(const std::string& message);

    lua_State* m_state = nullptr;
    const std::string m_script_path;
    const string_list_t m_args;
    scoped_set_env_t m_lua_path_env;
    std::string m_script;
  };

  void resolve_args() override;
  string_list_t get_capabilities() override;
  pp_sources_t preprocess_source() override;
  string_list_t get_relevant_arguments() override;
  std::map<std::string, std::string> get_relevant_env_vars() override;
  std::string get_program_id() override;
  build_files_t get_build_files(const pp_key_t& key) override;
  sys::run_result_t run_for_miss(miss_infos_t& miss_infos) override;

  runner_t m_runner;
};
}  // namespace bcache
#endif  // BUILDCACHE_LUA_WRAPPER_HPP_
