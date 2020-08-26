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
#include <base/file_utils.hpp>
#include <base/string_utils.hpp>
#include <base/unicode_utils.hpp>
#include <config/configuration.hpp>
#include <wrappers/ti_common_wrapper.hpp>

#include <algorithm>
#include <regex>

// Relevant documentation for different TI compilers:
//
// * TI TMS320C6000 Optimizing Compiler:
//    - https://www.ti.com/lit/ug/sprui04b/sprui04b.pdf
// * TI ARM Optimizing C/C++ compiler:
//    - http://www.ti.com/lit/ug/spnu151v/spnu151v.pdf
// * TI ARP32 Optimizing C/C++ compiler:
//    - spruh24a.pdf (part of PROCESSOR SDK-VISION)
//    - http://software-dl.ti.com/processor-sdk-vision/esd/TDAx/vision-sdk/latest/index_FDS.html

namespace bcache {
namespace {
bool has_debug_symbols(const string_list_t& args) {
  // The default behavior is to enable debugging.
  //   C6x default (sprui04b.pdf, 3.3.6):   --symdebug:dwarf
  //   ARM default (spnu151v.pdf, 2.3.5):   --symdebug:dwarf
  //   ARP32 default (spruh24a.pdf, 2.3.5): --symdebug:skeletal
  bool result = true;
  for (auto arg : args) {
    if (starts_with(arg, "--symdebug:")) {
      result = (arg != "--symdebug:none");
    } else if (arg == "-g") {
      result = true;
    }
  }
  return result;
}

string_list_t make_preprocessor_cmd(const string_list_t& args,
                                    const std::string& preprocessed_file) {
  string_list_t preprocess_args;

  // Drop arguments that we do not want/need.
  bool drop_next_arg = false;
  for (const auto& arg : args) {
    bool drop_this_arg = drop_next_arg;
    drop_next_arg = false;
    if ((arg == "--compile_only") || starts_with(arg, "--output_file=") ||
        starts_with(arg, "-pp") || starts_with(arg, "--preproc_")) {
      drop_this_arg = true;
    }
    if (!drop_this_arg) {
      preprocess_args += arg;
    }
  }

  // Should we inhibit line info in the preprocessed output?
  const bool debug_symbols_required =
      has_debug_symbols(args) && (config::accuracy() >= config::cache_accuracy_t::STRICT);
  const bool inhibit_line_info = !debug_symbols_required;

  // Append the required arguments for producing preprocessed output.
  if (inhibit_line_info) {
    preprocess_args += "--preproc_only";
  } else {
    preprocess_args += "--preproc_with_line";
  }
  preprocess_args += ("--output_file=" + preprocessed_file);

  return preprocess_args;
}

void hash_link_cmd_file(const std::string& path, hasher_t& hasher) {
  // We need to parse *.cmd files, since they contain lines on the form:
  // -l"/foo/.../bar.ext". These lines are files that should be hashed (instead of hashing
  // their paths, which is not what we want).
  const auto data = file::read(path);
  const auto lines = string_list_t(data, "\n");
  for (const auto& line : lines) {
    if (starts_with(line, "-l")) {
      auto file_name = line.substr(2);
      if (file_name.size() > 2 && file_name[0] == '"') {
        file_name = file_name.substr(1, file_name.size() - 2);
      }
      hasher.update_from_file_deterministic(file_name);
    } else {
      hasher.update(line);
    }
  }
}
}  // namespace

ti_common_wrapper_t::ti_common_wrapper_t(const string_list_t& args) : program_wrapper_t(args) {
}

void ti_common_wrapper_t::resolve_args() {
  // Iterate over all args and load any response files that we encounter.
  m_resolved_args.clear();
  for (const auto& arg : m_args) {
    std::string response_file;
    if (starts_with(arg, "--cmd_file=")) {
      response_file = arg.substr(arg.find("=") + 1);
    } else if (starts_with(arg, "-@")) {
      response_file = arg.substr(2);
    }
    if (!response_file.empty()) {
      append_response_file(response_file);
    } else {
      m_resolved_args += arg;
    }
  }
}

pp_sources_t ti_common_wrapper_t::preprocess_source() {
  // Check what kind of compilation command this is.
  bool is_object_compilation = false;
  bool is_link = false;
  bool has_output_file = false;
  for (const auto& arg : m_resolved_args) {
    if (arg == "--compile_only") {
      is_object_compilation = true;
    } else if (arg == "--run_linker") {
      if (!bcache::config::cache_link_commands()) {
        throw std::runtime_error("Caching link commands is disabled.");
      }
      is_link = true;
    } else if (starts_with(arg, "--output_file=")) {
      has_output_file = true;
    } else if (starts_with(arg, "--cmd_file=") || starts_with(arg, "-@")) {
      throw std::runtime_error("Recursive response files are not supported.");
    }
  }

  if (is_object_compilation && has_output_file) {
    // Run the preprocessor step.
    file::tmp_file_t preprocessed_file(sys::get_local_temp_folder(), ".i");
    const auto preprocessor_args = make_preprocessor_cmd(m_resolved_args, preprocessed_file.path());
    auto result = sys::run(preprocessor_args);
    if (result.return_code != 0) {
      throw std::runtime_error("Preprocessing command was unsuccessful.");
    }

    // Read and return the preprocessed file.
    return {{{}, file::read(preprocessed_file.path())}};
  } else if (is_link && has_output_file) {
    // Hash all the input files.
    hasher_t hasher;
    for (size_t i = 1; i < m_resolved_args.size(); ++i) {
      const auto& arg = m_resolved_args[i];
      if (arg.size() > 0 && arg[0] != '-' && file::file_exists(arg)) {
        if (lower_case(file::get_extension(arg)) == ".cmd") {
          debug::log(debug::DEBUG) << "Hashing cmd-file " << arg;
          hash_link_cmd_file(arg, hasher);
        } else {
          hasher.update_from_file_deterministic(arg);
        }
      }
    }
    return {{{}, hasher.final().as_string()}};
  }

  throw std::runtime_error("Unsupported complation command.");
}

string_list_t ti_common_wrapper_t::get_relevant_arguments() {
  string_list_t filtered_args;

  // The first argument is the compiler binary without the path.
  filtered_args += file::get_file_part(m_resolved_args[0]);

  // Note: We always skip the first arg since we have handled it already.
  bool skip_next_arg = true;
  for (auto arg : m_resolved_args) {
    if (arg.size() > 0 && !skip_next_arg) {
      // Generally unwanted argument (things that will not change how we go from preprocessed code
      // to binary object files)?
      const auto first_two_chars = arg.substr(0, 2);
      const bool is_unwanted_arg = (first_two_chars == "-I") || starts_with(arg, "--include") ||
                                   starts_with(arg, "--preinclude=") || (first_two_chars == "-D") ||
                                   starts_with(arg, "--define=") || starts_with(arg, "--c_file=") ||
                                   starts_with(arg, "--cpp_file=") ||
                                   starts_with(arg, "--output_file=") ||
                                   starts_with(arg, "--map_file=") || starts_with(arg, "-ppd=") ||
                                   starts_with(arg, "--preproc_dependency=");
      if (!is_unwanted_arg) {
        // We don't want to include input file paths as part of the command line, since they may
        // contain absolute paths. Input files are hashed as part of the preprocessing step.
        const bool is_input_file = (arg[0] != '-') && file::file_exists(arg);
        if (!is_input_file) {
          filtered_args += arg;
        }
      }
    }
    skip_next_arg = false;
  }

  debug::log(debug::DEBUG) << "Filtered arguments: " << filtered_args.join(" ", true);

  return filtered_args;
}

std::string ti_common_wrapper_t::get_program_id() {
  // TODO(m): Add things like executable file size too.

  // Get the help string from the compiler (it includes the version string).
  string_list_t version_args;
  version_args += m_resolved_args[0];
  version_args += "--help";
  const auto result = sys::run(version_args);
  if (result.return_code != 0) {
    throw std::runtime_error("Unable to get the compiler version information string.");
  }

  return result.std_out;
}

build_files_t ti_common_wrapper_t::get_build_files(const pp_key_t& key) {
  build_files_t files;
  std::string output_file;
  std::string dep_file;
  std::string map_file;
  bool is_object_compilation = false;
  bool is_link = false;
  for (const auto& arg : m_resolved_args) {
    if (arg == "--compile_only") {
      is_object_compilation = true;
    } else if (arg == "--run_linker") {
      is_link = true;
    } else if (starts_with(arg, "--output_file=")) {
      if (!output_file.empty()) {
        throw std::runtime_error("Only a single target file can be specified.");
      }
      output_file = arg.substr(arg.find("=") + 1);
    } else if (starts_with(arg, "-ppd=") || starts_with(arg, "--preproc_dependency=")) {
      if (!dep_file.empty()) {
        throw std::runtime_error("Only a single dependency file can be specified.");
      }
      dep_file = arg.substr(arg.find("=") + 1);
    } else if (starts_with(arg, "--map_file=")) {
      if (!map_file.empty()) {
        throw std::runtime_error("Only a single map file can be specified.");
      }
      map_file = arg.substr(arg.find("=") + 1);
    }
  }
  if (output_file.empty()) {
    throw std::runtime_error("Unable to get the output file.");
  }

  if (is_object_compilation) {
    // Note: --compile_only overrides --run_linker.
    files["object"] = {output_file, true};
  } else if (is_link) {
    files["linktarget"] = {output_file, true};
  } else {
    throw std::runtime_error("Unrecognized compilation type.");
  }

  if (!dep_file.empty()) {
    files["dep"] = {dep_file, true};
  }
  if (!map_file.empty()) {
    files["map"] = {map_file, true};
  }

  return files;
}

void ti_common_wrapper_t::append_response_file(const std::string& response_file) {
  auto args_string = file::read(response_file);
  std::replace(args_string.begin(), args_string.end(), '\n', ' ');
  m_resolved_args += string_list_t::split_args(args_string);
}

}  // namespace bcache
