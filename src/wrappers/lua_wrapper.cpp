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

#include <base/debug_utils.hpp>
#include <base/file_utils.hpp>
#include <config/configuration.hpp>
#include <sys/perf_utils.hpp>
#include <sys/sys_utils.hpp>
#include <wrappers/lua_wrapper.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
#include <lualib.h>
}

#include <algorithm>
#include <map>
#include <regex>
#include <stdexcept>

namespace bcache {
namespace {
int panic_handler(lua_State* state) {
  debug::log(debug::FATAL) << "Unprotected error in call to Lua API (" << lua_tostring(state, -1)
                           << ")";
  return 0;  // Return to Lua to abort.
}

void assert_state_initialized(const lua_State* state) {
  if (state == nullptr) {
    throw std::runtime_error("Lua state has not been initialized (should not happen)!");
  }
}

bool pop_bool(lua_State* state) {
  assert_state_initialized(state);
  if (lua_isboolean(state, -1) == 0) {
    throw std::runtime_error("Expected a boolean value on the stack.");
  }
  return (lua_toboolean(state, -1) != 0);
}

int pop_int(lua_State* state, bool keep_value_on_the_stack = false) {
  assert_state_initialized(state);
  if (lua_isinteger(state, -1) == 0) {
    throw std::runtime_error("Expected an integer value on the stack.");
  }
  return static_cast<int>(lua_tointeger(state, -1));
}

std::string pop_string(lua_State* state, bool keep_value_on_the_stack = false) {
  assert_state_initialized(state);
  if (lua_isstring(state, -1) == 0) {
    throw std::runtime_error("Expected a string value on the stack.");
  }
  size_t str_size;
  const auto* str = lua_tolstring(state, -1, &str_size);
  if (!keep_value_on_the_stack) {
    lua_pop(state, 1);
  }
  return std::string(str, str_size);
}

string_list_t pop_string_list(lua_State* state, bool keep_value_on_the_stack = false) {
  assert_state_initialized(state);
  if (lua_istable(state, -1) == 0) {
    throw std::runtime_error("Expected a table on the stack.");
  }
  string_list_t result;
  const auto len = static_cast<lua_Integer>(lua_rawlen(state, -1));
  for (lua_Integer i = 1; i <= len; ++i) {
    lua_rawgeti(state, -1, i);
    result += pop_string(state);
  }
  if (!keep_value_on_the_stack) {
    lua_pop(state, 1);
  }
  return result;
}

std::map<std::string, std::string> pop_map(lua_State* state, bool keep_value_on_the_stack = false) {
  assert_state_initialized(state);
  if (lua_istable(state, -1) == 0) {
    throw std::runtime_error("Expected a table on the stack.");
  }
  std::map<std::string, std::string> result;
  lua_pushnil(state);  // Make sure lua_next starts at beginning.
  while (lua_next(state, -2) != 0) {
    const auto value = pop_string(state);
    const auto key = pop_string(state, true);
    result[key] = value;
  }
  if (!keep_value_on_the_stack) {
    lua_pop(state, 1);
  }
  return result;
}

sys::run_result_t pop_run_result(lua_State* state, bool keep_value_on_the_stack = false) {
  assert_state_initialized(state);
  if (lua_istable(state, -1) == 0) {
    throw std::runtime_error("Expected a table on the stack.");
  }

  sys::run_result_t result;
  lua_getfield(state, -1, "std_out");
  result.std_out = pop_string(state);
  lua_getfield(state, -1, "std_err");
  result.std_err = pop_string(state);
  lua_getfield(state, -1, "return_code");
  result.return_code = pop_int(state);

  if (!keep_value_on_the_stack) {
    lua_pop(state, 1);
  }
  return result;
}

void push(lua_State* state, const bool data) {
  assert_state_initialized(state);
  lua_pushboolean(state, data ? 1 : 0);
}

void push(lua_State* state, const std::string& data) {
  assert_state_initialized(state);
  lua_pushlstring(state, data.c_str(), data.size());
}

void push(lua_State* state, const string_list_t& data) {
  // Push the data as an array of strings.
  assert_state_initialized(state);
  lua_createtable(state, 0, static_cast<int>(data.size()));
  lua_Integer key = 1;
  for (const auto& arg : data) {
    lua_pushlstring(state, arg.c_str(), arg.size());
    lua_rawseti(state, -2, key);
    ++key;
  }
}

void push(lua_State* state, const sys::run_result_t& data) {
  // Push the result as a string-indexed table (i.e. a map).
  assert_state_initialized(state);
  lua_createtable(state, 0, 3);
  lua_pushlstring(state, data.std_out.c_str(), data.std_out.size());
  lua_setfield(state, -2, "std_out");
  lua_pushlstring(state, data.std_err.c_str(), data.std_err.size());
  lua_setfield(state, -2, "std_err");
  lua_pushinteger(state, data.return_code);
  lua_setfield(state, -2, "return_code");
}

void push(lua_State* state, const file::file_info_t& data) {
  // Push the information as a string-indexed table (i.e. a map).
  assert_state_initialized(state);
  lua_createtable(state, 0, 5);
  lua_pushlstring(state, data.path().c_str(), data.path().size());
  lua_setfield(state, -2, "path");
  lua_pushinteger(state, static_cast<lua_Integer>(data.modify_time()));
  lua_setfield(state, -2, "modify_time");
  lua_pushinteger(state, static_cast<lua_Integer>(data.access_time()));
  lua_setfield(state, -2, "access_time");
  lua_pushinteger(state, static_cast<lua_Integer>(data.size()));
  lua_setfield(state, -2, "size");
  lua_pushboolean(state, data.is_dir() ? 1 : 0);
  lua_setfield(state, -2, "is_dir");
}

//--------------------------------------------------------------------------------------------------
// Begin: The Lua side "bcache" library.
//--------------------------------------------------------------------------------------------------

int l_split_args(lua_State* state) {
  push(state, string_list_t::split_args(pop_string(state)));
  return 1;
}

int l_run(lua_State* state) {
  // Get arguments (in reverse order).
  auto quiet = true;
  if (lua_isboolean(state, -1) != 0) {
    quiet = (lua_toboolean(state, -1) != 0);
    lua_pop(state, 1);
  }
  const auto cmd = pop_string_list(state);

  // Call the C++ function and push the result.
  push(state, sys::run(cmd, quiet));
  return 1;
}

int l_dir_exists(lua_State* state) {
  push(state, file::dir_exists(pop_string(state)));
  return 1;
}

int l_file_exists(lua_State* state) {
  push(state, file::file_exists(pop_string(state)));
  return 1;
}

int l_get_extension(lua_State* state) {
  push(state, file::get_extension(pop_string(state)));
  return 1;
}

int l_get_file_part(lua_State* state) {
  // Get arguments.
  const auto path = pop_string(state);
  auto include_ext = true;
  if (lua_gettop(state) > 0) {
    include_ext = pop_bool(state);
  }

  // Call the C++ function and push the result.
  push(state, file::get_file_part(path, include_ext));
  return 1;
}

int l_get_dir_part(lua_State* state) {
  push(state, file::get_dir_part(pop_string(state)));
  return 1;
}

int l_get_file_info(lua_State* state) {
  push(state, file::get_file_info(pop_string(state)));
  return 1;
}

static const luaL_Reg BCACHE_LIB_FUNCS[] = {{"split_args", l_split_args},
                                            {"run", l_run},
                                            {"dir_exists", l_dir_exists},
                                            {"file_exists", l_file_exists},
                                            {"get_extension", l_get_extension},
                                            {"get_file_part", l_get_file_part},
                                            {"get_dir_part", l_get_dir_part},
                                            {"get_file_info", l_get_file_info},
                                            {NULL, NULL}};

int luaopen_bcache(lua_State* state) {
  luaL_newlib(state, BCACHE_LIB_FUNCS);
  return 1;
}

//--------------------------------------------------------------------------------------------------
// End: The Lua side "bcache" library.
//--------------------------------------------------------------------------------------------------

int l_require_std(lua_State* state) {
  // Get the module name from the stack.
  size_t arg_len;
  const auto* arg = luaL_checklstring(state, 1, &arg_len);
  const auto module_name = std::string(arg, arg_len);

  // Load the selected standard library module.
  if (module_name == "package") {
    luaL_requiref(state, module_name.c_str(), luaopen_package, 1);
  } else if (module_name == "coroutine") {
    luaL_requiref(state, module_name.c_str(), luaopen_coroutine, 1);
  } else if (module_name == "table") {
    luaL_requiref(state, module_name.c_str(), luaopen_table, 1);
  } else if (module_name == "io") {
    luaL_requiref(state, module_name.c_str(), luaopen_io, 1);
  } else if (module_name == "os") {
    luaL_requiref(state, module_name.c_str(), luaopen_os, 1);
  } else if (module_name == "string") {
    luaL_requiref(state, module_name.c_str(), luaopen_string, 1);
  } else if (module_name == "math") {
    luaL_requiref(state, module_name.c_str(), luaopen_math, 1);
  } else if (module_name == "utf8") {
    luaL_requiref(state, module_name.c_str(), luaopen_utf8, 1);
  } else if (module_name == "debug") {
    luaL_requiref(state, module_name.c_str(), luaopen_debug, 1);
  } else if (module_name == "bcache") {
    luaL_requiref(state, module_name.c_str(), luaopen_bcache, 1);
  } else if (module_name == "*") {
    // Load all standard libraries.
    luaL_openlibs(state);

    // Load the "bcache" library.
    luaL_requiref(state, "bcache", luaopen_bcache, 1);
    lua_pop(state, 1);  // Remove lib.

    return 0;
  } else {
    return luaL_error(state, "Invalid standard library: \"%s\".", module_name.c_str());
  }

  return 1;  // Number of results.
}

bool is_program_match(const std::string& script_str, const std::string& program_path) {
  // We require that a program wrapper script starts with "-- match(...)".
  if (script_str.size() < 10 || !std::equal(&script_str[0], &script_str[0] + 9, "-- match(")) {
    return false;
  }

  // Extract the program match expression.
  const auto end_expr_pos = script_str.find_last_of(')', script_str.find('\n'));
  if (end_expr_pos == std::string::npos) {
    return false;
  }
  const auto expr = script_str.substr(9, end_expr_pos - 9);

  // Do a regular expression match.
  const auto program_exe = file::get_file_part(program_path, false);
  const std::regex expr_regex(expr);
  const auto match = std::regex_match(program_exe, expr_regex);

  debug::log(debug::DEBUG) << "Evaluating regex \"" << expr << "\": " << (!match ? "no " : "")
                           << "match";

  return match;
}

build_files_t to_expected_files_map(const std::map<std::string, std::string>& files) {
  build_files_t expected_files;
  for (const auto& file : files) {
    // Right now we simply assume that all files are required.
    // TODO(m): Make it possible to specify files as optional in Lua.
    expected_files[file.first] = {file.second, true};
  }
  return expected_files;
}

std::string make_lua_path() {
  // Construct a LUA_PATH compatible string that includes the BuildCache Lua paths. This way it
  // is possible to "require" modules from the BuildCache Lua path.
  string_list_t lua_paths;
  for (const auto& path : config::lua_paths()) {
    lua_paths += file::append_path(path, "?.lua");
  }
  {
    env_var_t lua_path_evn("LUA_PATH");
    if (lua_path_evn) {
      lua_paths += string_list_t(lua_path_evn.as_string(), ";");
    }
  }
  return lua_paths.join(";");
}

}  // namespace

lua_wrapper_t::runner_t::runner_t(const std::string& script_path, const string_list_t& args)
    : m_script_path(script_path), m_args(args), m_lua_path_env("LUA_PATH", make_lua_path()) {
  try {
    m_script = file::read(m_script_path);
  } catch (...) {
    // We'll leave m_script empty and throw an exception later (we don't want to throw in the
    // constructor).
  }
}

lua_wrapper_t::runner_t::~runner_t() {
  if (m_state != nullptr) {
    lua_close(m_state);
  }
}

void lua_wrapper_t::runner_t::init_lua_state() {
  // As we use lazy initialization of the Lua state, we allow multiple calls to this routine.
  if (m_state != nullptr) {
    return;
  }

  // If the script file failed to load, we can't execute.
  if (m_script.empty()) {
    throw std::runtime_error("Missing script file.");
  }

  // Init Lua.
  PERF_START(LUA_INIT);
  m_state = luaL_newstate();
  (void)lua_atpanic(m_state, panic_handler);
  setup_lua_libs_and_globals();
  PERF_STOP(LUA_INIT);

  // Load the script.
  {
    PERF_START(LUA_LOAD_SCRIPT);
    const auto success = (luaL_loadstring(m_state, m_script.c_str()) == 0);
    PERF_STOP(LUA_LOAD_SCRIPT);
    if (!success) {
      bail("Couldn't load file.");
    }
  }

  // Prime run of the script (define functions etc).
  {
    PERF_START(LUA_RUN);
    const auto success = (lua_pcall(m_state, 0, 0, 0) == 0);
    PERF_STOP(LUA_RUN);
    if (!success) {
      bail("Couldn't run script.");
    }
  }
}

void lua_wrapper_t::runner_t::setup_lua_libs_and_globals() {
  // To minimize startup overhead when loading Lua scripts, we only pre-load a minimal environment.
  // Standard libraries can be loaded by Lua scripts on an op-in basis using the require_std()
  // function.

  // Load the basic library.
  luaL_requiref(m_state, "_G", luaopen_base, 1);
  lua_pop(m_state, 1);

  // We provide a custom function, require_std(), that can be used for loading standard libraries.
  lua_pushcfunction(m_state, l_require_std);
  lua_setglobal(m_state, "require_std");

  // Add a global variable, "ARGS", that contains the program arguments (this mimics the m_args
  // member of the C++ program_wrapper_t base class).
  lua_newtable(m_state);
  for (size_t i = 0; i < m_args.size(); ++i) {
    (void)lua_pushlstring(m_state, m_args[i].c_str(), m_args[i].size());
    lua_rawseti(m_state, -2, static_cast<lua_Integer>(i + 1));
  }
  lua_setglobal(m_state, "ARGS");
}

[[noreturn]] void lua_wrapper_t::runner_t::bail(const std::string& message) {
  debug::log(debug::ERROR) << m_script_path << " " << lua_tostring(m_state, -1);
  lua_close(m_state);
  m_state = nullptr;
  throw std::runtime_error(message);
}

bool lua_wrapper_t::runner_t::call(const std::string& func) {
  init_lua_state();

  lua_getglobal(m_state, func.c_str());
  if (!lua_isfunction(m_state, -1)) {
    return false;
  }
  debug::log(debug::DEBUG) << "Calling Lua function: " << func;
  PERF_START(LUA_RUN);
  const auto success = (lua_pcall(m_state, 0, 1, 0) == 0);
  PERF_STOP(LUA_RUN);
  if (!success) {
    bail("Lua error");
  }
  return true;
}

lua_wrapper_t::lua_wrapper_t(const string_list_t& args, const std::string& lua_script_path)
    : program_wrapper_t(args), m_runner(lua_script_path, args) {
}

bool lua_wrapper_t::can_handle_command() {
  auto result = false;
  try {
    // First check: regex match against program name.
    if (is_program_match(m_runner.script(), m_args[0])) {
      // Second check: Call can_handle_command(), if defined.
      if (m_runner.call("can_handle_command")) {
        result = pop_bool(m_runner.state());
      } else {
        result = true;
      }
    }
  } catch (...) {
    // If anything went wrong when running this function, we can not be trusted to handle this
    // command.
    result = false;
  }
  return result;
}

void lua_wrapper_t::resolve_args() {
  if (!m_runner.call("resolve_args")) {
    program_wrapper_t::resolve_args();
  }
}

string_list_t lua_wrapper_t::get_capabilities() {
  if (m_runner.call("get_capabilities")) {
    return pop_string_list(m_runner.state());
  } else {
    return program_wrapper_t::get_capabilities();
  }
}

pp_sources_t lua_wrapper_t::preprocess_source() {
  if (m_runner.call("preprocess_source")) {
    return {{{}, pop_string(m_runner.state())}};
  } else {
    return program_wrapper_t::preprocess_source();
  }
}

string_list_t lua_wrapper_t::get_relevant_arguments() {
  if (m_runner.call("get_relevant_arguments")) {
    return pop_string_list(m_runner.state());
  } else {
    return program_wrapper_t::get_relevant_arguments();
  }
}

std::map<std::string, std::string> lua_wrapper_t::get_relevant_env_vars() {
  if (m_runner.call("get_relevant_env_vars")) {
    return pop_map(m_runner.state());
  } else {
    return program_wrapper_t::get_relevant_env_vars();
  }
}

std::string lua_wrapper_t::get_program_id() {
  if (m_runner.call("get_program_id")) {
    return pop_string(m_runner.state());
  } else {
    return program_wrapper_t::get_program_id();
  }
}

build_files_t lua_wrapper_t::get_build_files(const pp_key_t& key) {
  if (m_runner.call("get_build_files")) {
    const auto files_map = pop_map(m_runner.state());
    return to_expected_files_map(files_map);
  } else {
    return program_wrapper_t::get_build_files(key);
  }
}

sys::run_result_t lua_wrapper_t::run_for_miss(miss_infos_t& miss_infos) {
  if (m_runner.call("run_for_miss")) {
    return pop_run_result(m_runner.state());
  } else {
    return program_wrapper_t::run_for_miss(miss_infos);
  }
}

}  // namespace bcache
