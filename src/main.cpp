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
#include <base/string_list.hpp>
#include <base/unicode_utils.hpp>
#include <cache/local_cache.hpp>
#include <config/configuration.hpp>
#include <sys/filetracker.hpp>
#include <sys/perf_utils.hpp>
#include <sys/sys_utils.hpp>
#include <wrappers/ccc_analyzer_wrapper.hpp>
#include <wrappers/gcc_wrapper.hpp>
#include <wrappers/ghs_wrapper.hpp>
#include <wrappers/lua_wrapper.hpp>
#include <wrappers/msvc_wrapper.hpp>
#include <wrappers/program_wrapper.hpp>
#include <wrappers/ti_arm_cgt_wrapper.hpp>
#include <wrappers/ti_arp32_wrapper.hpp>
#include <wrappers/ti_c6x_wrapper.hpp>

// These 3rd party includes are used for getting the version numbers.
#include "buildcache_version.h"

#include <cjson/cJSON.h>
#include <hiredis/hiredis.h>
#include <lua.h>
#include <lz4/lz4.h>
#include <xxHash/xxhash.h>
#include <zstd/zstd.h>

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

// For program binary identification (e.g. using the "strings", "ident" or "what" tools).
char PROGRAM_IDENTITY_1[] = "@(#)BuildCache version " BUILDCACHE_VERSION_STRING;
char PROGRAM_IDENTITY_2[] = "$BuildCacheVersion: " BUILDCACHE_VERSION_STRING " $";

namespace {
// The name of the BuildCache executable (excluding the file extension).
const std::string BUILDCACHE_EXE_NAME = "buildcache";

bool is_lua_script(const std::string& script_path) {
  return (bcache::lower_case(bcache::file::get_extension(script_path)) == ".lua");
}

std::unique_ptr<bcache::program_wrapper_t> find_suitable_wrapper(
    const bcache::string_list_t& args) {
  const auto& true_exe_path = args[0];

  std::unique_ptr<bcache::program_wrapper_t> wrapper;

  // Try Lua wrappers first (so you can override internal wrappers).
  // Iterate over the existing Lua paths.
  for (const auto& lua_root_dir : bcache::config::lua_paths()) {
    if (bcache::file::dir_exists(lua_root_dir)) {
      // Find all .lua files in the given directory.
      const auto lua_files = bcache::file::walk_directory(lua_root_dir);
      for (const auto& file_info : lua_files) {
        const auto script_path = file_info.path();
        if ((!file_info.is_dir()) && is_lua_script(script_path)) {
          // Check if the given wrapper can handle this command (first match wins).
          wrapper.reset(new bcache::lua_wrapper_t(args, script_path));
          if (wrapper->can_handle_command()) {
            bcache::debug::log(bcache::debug::DEBUG)
                << "Found matching Lua wrapper for " << true_exe_path << ": " << script_path;
            break;
          } else {
            wrapper = nullptr;
          }
        }
      }
    }
    if (wrapper) {
      break;
    }
  }

  // If no Lua wrappers were found, try built in wrappers.
  if (!wrapper) {
    wrapper.reset(new bcache::gcc_wrapper_t(args));
    if (!wrapper->can_handle_command()) {
      wrapper.reset(new bcache::ghs_wrapper_t(args));
      if (!wrapper->can_handle_command()) {
        wrapper.reset(new bcache::msvc_wrapper_t(args));
        if (!wrapper->can_handle_command()) {
          wrapper.reset(new bcache::ti_c6x_wrapper_t(args));
          if (!wrapper->can_handle_command()) {
            wrapper.reset(new bcache::ti_arm_cgt_wrapper_t(args));
            if (!wrapper->can_handle_command()) {
              wrapper.reset(new bcache::ti_arp32_wrapper_t(args));
              if (!wrapper->can_handle_command()) {
                wrapper.reset(new bcache::ccc_analyzer_wrapper_t(args));
                if (!wrapper->can_handle_command()) {
                  wrapper = nullptr;
                }
              }
            }
          }
        }
      }
    }
  }

  return wrapper;
}

[[noreturn]] void clear_cache_and_exit() {
  int return_code = 0;
  try {
    bcache::local_cache_t cache;
    cache.clear();
  } catch (const std::exception& e) {
    std::cerr << "*** Unexpected error: " << e.what() << "\n";
    return_code = 1;
  } catch (...) {
    std::cerr << "*** Unexpected error.\n";
    return_code = 1;
  }
  std::exit(return_code);
}

[[noreturn]] void show_stats_and_exit() {
  int return_code = 0;
  try {
    bcache::local_cache_t cache;

    // Print the cache stats.
    std::cout << "Cache status:\n";
    cache.show_stats();

    {
// Print the configuration.
#ifdef _WIN32
      const std::string PATH_SEP = ";";
#else
      const std::string PATH_SEP = ":";
#endif

      std::cout << "\nConfiguration:\n";
      std::cout << "  Configuration file: " << bcache::config::config_file() << "\n\n";

      std::cout << "  BUILDCACHE_DIR:                    " << bcache::config::dir() << "\n";
      std::cout << "  BUILDCACHE_LUA_PATH:               "
                << bcache::config::lua_paths().join(PATH_SEP, false) << "\n";
      std::cout << "  BUILDCACHE_PREFIX:                 " << bcache::config::prefix() << "\n";
      std::cout << "  BUILDCACHE_REMOTE:                 "
                << (bcache::config::remote().empty() ? "(disabled)" : bcache::config::remote())
                << "\n";
      std::cout << "  BUILDCACHE_S3_ACCESS:              "
                << (bcache::config::s3_access().empty() ? "" : "*******") << "\n";
      std::cout << "  BUILDCACHE_S3_SECRET:              "
                << (bcache::config::s3_secret().empty() ? "" : "*******") << "\n";
      std::cout << "  BUILDCACHE_MAX_CACHE_SIZE:         " << bcache::config::max_cache_size()
                << " (" << bcache::file::human_readable_size(bcache::config::max_cache_size())
                << ")\n";
      std::cout << "  BUILDCACHE_MAX_LOCAL_ENTRY_SIZE:   " << bcache::config::max_local_entry_size()
                << " (" << bcache::file::human_readable_size(bcache::config::max_local_entry_size())
                << ")\n";
      std::cout << "  BUILDCACHE_MAX_REMOTE_ENTRY_SIZE:  "
                << bcache::config::max_remote_entry_size() << " ("
                << bcache::file::human_readable_size(bcache::config::max_remote_entry_size())
                << ")\n";
      std::cout << "  BUILDCACHE_DEBUG:                  " << bcache::config::debug() << "\n";
      std::cout << "  BUILDCACHE_LOG_FILE:               " << bcache::config::log_file() << "\n";
      std::cout << "  BUILDCACHE_HARD_LINKS:             "
                << (bcache::config::hard_links() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_CACHE_LINK_COMMANDS:    "
                << (bcache::config::cache_link_commands() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_COMPRESS:               "
                << (bcache::config::compress() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_COMPRESS_FORMAT:        "
                << to_string(bcache::config::compress_format()) << "\n";
      std::cout << "  BUILDCACHE_COMPRESS_LEVEL:         " << bcache::config::compress_level()
                << "\n";
      std::cout << "  BUILDCACHE_PERF:                   "
                << (bcache::config::perf() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_DISABLE:                "
                << (bcache::config::disable() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_READ_ONLY:              "
                << (bcache::config::read_only() ? "true" : "false") << "\n";
      std::cout << "  BUILDCACHE_ACCURACY:               " << to_string(bcache::config::accuracy())
                << "\n";
    }
  } catch (const std::exception& e) {
    std::cerr << "*** Unexpected error: " << e.what() << "\n";
    return_code = 1;
  } catch (...) {
    std::cerr << "*** Unexpected error.\n";
    return_code = 1;
  }
  std::exit(return_code);
}

[[noreturn]] void zero_stats_and_exit() {
  int return_code = 0;
  try {
    bcache::local_cache_t cache;
    cache.zero_stats();
  } catch (const std::exception& e) {
    std::cerr << "*** Unexpected error: " << e.what() << "\n";
    return_code = 1;
  } catch (...) {
    std::cerr << "*** Unexpected error.\n";
    return_code = 1;
  }
  std::exit(return_code);
}

[[noreturn]] void print_version_and_exit() {
  // Print the BuildCache version.
  std::cout << "BuildCache version " << BUILDCACHE_VERSION_STRING << "\n"
            << BUILDCACHE_COPYRIGHT_STRING << "\n";

  // Print the supported cache back ends.
  std::cout << "\nSupported back ends:\n";
  std::cout << "  local - Local file system based cache (level 1)\n";
  std::cout << "  Redis - Remote in-memory cache (level 2)\n";
#ifdef ENABLE_S3
  std::cout << "  S3    - Remote object storage based cache (level 2)\n";
#endif

  // Print a list of third party components.
  std::cout << "\nThird party components:\n";
#ifdef ENABLE_S3
  std::cout << "  cpp-base64 2.rc.04\n";
#endif
  std::cout << "  cJSON " << CJSON_VERSION_MAJOR << "." << CJSON_VERSION_MINOR << "."
            << CJSON_VERSION_PATCH << "\n";
  std::cout << "  hiredis " << HIREDIS_MAJOR << "." << HIREDIS_MINOR << "." << HIREDIS_PATCH
            << "\n";
#ifdef ENABLE_S3
  std::cout << "  HTTPRequest\n";
#endif
  std::cout << "  lua " << LUA_VERSION_MAJOR << "." << LUA_VERSION_MINOR << "."
            << LUA_VERSION_RELEASE << "\n";
  std::cout << "  lz4 " << LZ4_VERSION_STRING << "\n";
  std::cout << "  zstd " << ZSTD_VERSION_STRING << "\n";
  std::cout << "  xxhash " << XXH_VERSION_MAJOR << "." << XXH_VERSION_MINOR << "."
            << XXH_VERSION_RELEASE << "\n";
#ifdef USE_MINGW_THREADS
  std::cout << "  mingw-std-threads\n";
#endif

  std::exit(0);
}

[[noreturn]] void edit_config_and_exit() {
  int return_code = 0;
  try {
    // This will ensure that the local cache directory exists.
    bcache::local_cache_t cache;

    // Get the name of the config file, and create an empty file if it does not already exist.
    const auto config_file = bcache::config::config_file();
    if (!bcache::file::file_exists(config_file)) {
      bcache::file::write("{\n}\n", config_file);
    }

    // Open the editor.
    bcache::sys::open_in_default_editor(config_file);
  } catch (const std::exception& e) {
    std::cerr << "*** Unexpected error: " << e.what() << "\n";
    return_code = 1;
  } catch (...) {
    std::cerr << "*** Unexpected error.\n";
    return_code = 1;
  }
  std::exit(0);
}

[[noreturn]] void wrap_compiler_and_exit(int argc, const char** argv) {
  auto args = bcache::string_list_t(argc, argv);
  bool was_wrapped = false;
  int return_code = 0;

  PERF_START(TOTAL);

  try {
    if (args.size() < 1) {
      // Should never happen.
      throw std::runtime_error("Missing arguments.");
    }

    // Is the caching mechanism disabled?
    if (bcache::config::disable()) {
      bcache::filetracker::release_suppression();
      // Bypass all the cache logic and call the intended command directly.
      auto result = bcache::sys::run(args, false);
      return_code = result.return_code;
    } else {
      // Find the true path to the executable file. This affects things like if we can match the
      // compiler name or not, and what version string we get. We also want to avoid incorrectly
      // identifying other compiler accelerators (e.g. ccache) as actual compilers.
      // TODO(m): This call may throw an exception, which currently means that we will not even try
      // to run the original command. At the same time this is a protection against endless symlink
      // recursion. Figure something out!
      PERF_START(FIND_EXECUTABLE);
      const auto& exe_path =
          bcache::config::impersonate().empty() ? args[0] : bcache::config::impersonate();
      const auto true_exe_path = bcache::file::find_executable(exe_path, BUILDCACHE_EXE_NAME);
      PERF_STOP(FIND_EXECUTABLE);

      // Replace the command with the true exe path. Most of the following operations rely on having
      // a correct executable path. Also, this is important to avoid recursions when we are invoked
      // from a symlink, for instance.
      args[0] = true_exe_path;

      try {
        return_code = 1;

        // Select a matching compiler wrapper.
        PERF_START(FIND_WRAPPER);
        auto wrapper = find_suitable_wrapper(args);
        PERF_STOP(FIND_WRAPPER);

        // Run the wrapper, if any.
        if (wrapper) {
          was_wrapped = wrapper->handle_command(return_code);
        } else {
          bcache::debug::log(bcache::debug::INFO) << "No suitable wrapper for " << true_exe_path;
        }
      } catch (const std::exception& e) {
        bcache::debug::log(bcache::debug::ERROR) << "Unexpected error: " << e.what();
        return_code = 1;
      } catch (...) {
        bcache::debug::log(bcache::debug::ERROR) << "Unexpected error.";
        return_code = 1;
      }

      // Fall back to running the command as is.
      if (!was_wrapped) {
        bcache::filetracker::release_suppression();
        PERF_START(RUN_FOR_FALLBACK);
        auto result = bcache::sys::run_with_prefix(args, false);
        PERF_STOP(RUN_FOR_FALLBACK);
        return_code = result.return_code;
      }
    }
  } catch (const std::exception& e) {
    bcache::debug::log(bcache::debug::FATAL) << "Unexpected error: " << e.what();
    return_code = 1;
  } catch (...) {
    bcache::debug::log(bcache::debug::FATAL) << "Unexpected error.";
    return_code = 1;
  }

  PERF_STOP(TOTAL);

  // Report performance timings.
  if (!bcache::config::disable()) {
    bcache::perf::report();
  }

  std::exit(return_code);
}

bool compare_arg(const std::string& arg,
                 const std::string& short_form,
                 const std::string& long_form = "") {
  return (arg == short_form) || (arg == long_form);
}

void print_help(const char* program_name) {
  std::cout << "Usage:\n";
  std::cout << "    " << program_name << " [options]\n";
  std::cout << "    " << program_name << " compiler [compiler-options]\n";
  std::cout << "\n";
  std::cout << "Options:\n";
  std::cout << "    -C, --clear           clear the local cache (except configuration)\n";
  std::cout << "    -s, --show-stats      show statistics summary and configuration\n";
  std::cout << "    -z, --zero-stats      zero statistics counters\n";
  std::cout << "    -e, --edit-config     edit the configuration file\n";
  std::cout << "\n";
  std::cout << "    -h, --help            print this help text\n";
  std::cout << "    -V, --version         print version and copyright information\n";
  std::cout << "\n";
  std::cout << "See also https://github.com/mbitsnbites/buildcache\n";
}
}  // namespace

int main(int argc, const char** argv) {
  try {
    // Initialize the configuration.
    bcache::config::init();

    // Set up debug logging acccording to what is given by the configuration.
    bcache::debug::set_log_level(bcache::config::debug());
    bcache::debug::set_log_file(bcache::config::log_file());
  } catch (const std::exception& e) {
    bcache::debug::log(bcache::debug::FATAL) << e.what();
  } catch (...) {
    bcache::debug::log(bcache::debug::FATAL) << "An exception occurred.";
  }

  // Handle BUILDCACHE_IMPERSONATE invocation.
  const auto& impersonate = bcache::config::impersonate();
  if (!impersonate.empty()) {
    bcache::debug::log(bcache::debug::DEBUG) << "Impersonating: " << impersonate;
    argv[0] = impersonate.c_str();
    wrap_compiler_and_exit(argc, &argv[0]);
  }

  // Handle symlink invocation.
  if (bcache::lower_case(bcache::file::get_file_part(std::string(argv[0]), false)) !=
      BUILDCACHE_EXE_NAME) {
    bcache::debug::log(bcache::debug::DEBUG) << "Invoked as symlink: " << argv[0];
    wrap_compiler_and_exit(argc, &argv[0]);
  }

  if (argc < 2) {
    print_help(argv[0]);
    std::exit(1);
  }

  // Check if we are running any BuildCache commands.
  const std::string arg_str(argv[1]);
  if (compare_arg(arg_str, "-C", "--clear")) {
    clear_cache_and_exit();
  } else if (compare_arg(arg_str, "-s", "--show-stats")) {
    show_stats_and_exit();
  } else if (compare_arg(arg_str, "-z", "--zero-stats")) {
    zero_stats_and_exit();
  } else if (compare_arg(arg_str, "-V", "--version")) {
    print_version_and_exit();
  } else if (compare_arg(arg_str, "-e", "--edit-config")) {
    edit_config_and_exit();
  } else if (compare_arg(arg_str, "-h", "--help")) {
    print_help(argv[0]);
    std::exit(0);
  } else if (arg_str[0] == '-') {
    std::cerr << argv[0] << ": invalid option -- " << arg_str << "\n";
    print_help(argv[0]);
    std::exit(1);
  }

  // We got this far, so we're running as a compiler wrapper.
  wrap_compiler_and_exit(argc - 1, &argv[1]);
}
