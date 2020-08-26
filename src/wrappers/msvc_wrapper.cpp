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
#include <base/env_utils.hpp>
#include <base/file_utils.hpp>
#include <base/string_utils.hpp>
#include <base/unicode_utils.hpp>
#include <config/configuration.hpp>
#include <sys/filetracker.hpp>
#include <sys/perf_utils.hpp>
#include <sys/sys_utils.hpp>
#include <wrappers/msvc_wrapper.hpp>

#include <cjson/cJSON.h>
#include <codecvt>
#include <cstdlib>

#include <algorithm>
#include <fstream>
#include <functional>
#include <locale>
#include <memory>
#include <mutex>
#include <stdexcept>

namespace bcache {
namespace {
// Tick this to a new number if the format has changed in a non-backwards-compatible way.
const std::string HASH_VERSION = "1";

// When cl.exe is started from Visual Studio, it explicitly sends certain output to the IDE
// process. This prevents capturing output otherwise written to stderr or stdout. The
// redirection is controlled by the VS_UNICODE_OUTPUT environment variable.
const std::string ENV_VS_OUTPUT_REDIRECTION = "VS_UNICODE_OUTPUT";

// cl.exe prepends/appends contents of these variables to the command line it interprets.
const char* const ENV_CL_PREFIX = "CL";
const char* const ENV_CL_POSTFIX = "_CL_";
// cl.exe searches paths given in this variable for system #includes.
const std::string ENV_CL_INCLUDE = "INCLUDE";

// Apparently some cl.exe arguments can be specified with an optional colon separator (e.g.
// both "/Fooutput.obj" and "/Fo:output.obj" are valid).
std::string drop_leading_colon(const std::string& s) {
  if (s.length() > 0 && s[0] == ':') {
    return s.substr(1);
  } else {
    return s;
  }
}

void read_lines(const std::string& path, std::function<void(const std::string&)> callback) {
  auto file_buf = file::read(path);
  const auto& data = file_buf.data();
  const auto wchar_len = sizeof(std::wstring::value_type);
  if (file_buf.size() > 2 && (file_buf.size() % wchar_len) == 0 && data[0] == '\xff' &&
      data[1] == '\xfe') {
    std::wstring file_buf_w(reinterpret_cast<const std::wstring::value_type*>(&file_buf[2]),
                            (file_buf.size() - 2) / wchar_len);
    file_buf = ucs2_to_utf8(file_buf_w);
  } else if (file_buf.size() > 3 && data[0] == '\xef' && data[1] == '\xbb' && data[2] == '\xbf') {
    file_buf = file_buf.substr(3);
  }

  std::stringstream filestream(std::move(file_buf));
  std::string line;
  while (std::getline(filestream, line)) {
    if (line.back() == '\r') {
      line.pop_back();
    }
    callback(line);
  }
}

tool_version_t get_tool_version(const std::string& compiler_path) {
  tool_version_t version;
  const auto path_split = split(compiler_path, '\\');
  const auto path_num_parts = path_split.size();
  bool path_valid = false;
  // Try to get host/target architectures from env vars (only populated if in vcvars-like env).
  // Fallback to trying to parse from the executable path.
  env_var_t vscmd_host_arch("VSCMD_ARG_HOST_ARCH");
  if (vscmd_host_arch) {
    version.host_arch = vscmd_host_arch.as_string();
  } else if (path_num_parts >= 3 && starts_with(path_split[path_num_parts - 3], "Host")) {
    version.host_arch = path_split[path_num_parts - 3].substr(4);
    path_valid = true;
  }
  env_var_t vscmd_target_arch("VSCMD_ARG_TGT_ARCH");
  if (vscmd_target_arch) {
    version.target_arch = vscmd_target_arch.as_string();
  } else if (path_valid) {
    version.target_arch = path_split[path_num_parts - 2];
  }
  if (version.host_arch.empty() || version.target_arch.empty()) {
    throw std::runtime_error("Failed to get compiler host/target architecture.");
  }
  env_var_t vc_tools_version("VCToolsVersion");
  if (vc_tools_version) {
    version.vc_version = vc_tools_version.as_string();
  } else if (path_num_parts >= 5 && path_valid) {
    version.vc_version = path_split[path_num_parts - 5];
  } else {
    throw std::runtime_error("Failed to get VC version.");
  }
  return version;
}

string_list_t get_source_dependencies(const std::string& path) {
  struct cJSONDeleter {
    explicit cJSONDeleter(cJSON* node) : m_node(node) {
    }
    ~cJSONDeleter() {
      cJSON_Delete(m_node);
    }
    cJSON* m_node;
  };
  const auto file_data = file::read(path);
  auto root = cJSON_Parse(file_data.data());
  cJSONDeleter cleanup(root);
  if (!root) {
    throw std::runtime_error("Failed to parse dependency file for " + path);
  }

  std::string version;
  const auto version_node = cJSON_GetObjectItemCaseSensitive(root, "Version");
  if (cJSON_IsString(version_node) && version_node->valuestring != nullptr) {
    version = std::string(version_node->valuestring);
  }
  if (version != "1.0") {
    throw std::runtime_error("Unknown dependency file version: " + version);
  }

  const auto data = cJSON_GetObjectItemCaseSensitive(root, "Data");
  if (!cJSON_IsObject(data)) {
    throw std::runtime_error("Bad dependency file format.");
  }

  // TODO append module dependencies?
  string_list_t depedencies;

  const auto pch_node = cJSON_GetObjectItemCaseSensitive(data, "PCH");
  if (cJSON_IsString(pch_node) && pch_node->valuestring != nullptr) {
    depedencies += pch_node->valuestring;
  }

  const auto includes = cJSON_GetObjectItemCaseSensitive(data, "Includes");
  if (!cJSON_IsArray(includes)) {
    throw std::runtime_error("Bad dependency file format.");
  }
  const cJSON* include = nullptr;
  cJSON_ArrayForEach(include, includes) {
    if (!cJSON_IsString(include) || !include->valuestring) {
      throw std::runtime_error("Bad dependency file format.");
    }
    depedencies += include->valuestring;
  }
  return depedencies;
}
}  // namespace

enum class InputType {
  kUnknown,
  kObject,
  kC,
  kCpp,
};

InputType filename_to_type(const std::string& name) {
  auto ext = lower_case(file::get_extension(name));
  if (ext == ".c") {
    return InputType::kC;
  } else if (ext == ".cpp" || ext == ".cxx" || ext == ".cc") {
    return InputType::kCpp;
  }
  return InputType::kObject;
}

struct InputFile {
  InputFile(const std::string& name, InputType type) : name(name), type(type) {
  }
  std::string as_arg() const;
  std::string name;
  InputType type;
};

enum class DebugFormat {
  kNone,
  kObjectFile,
  kSeparateFile,
  kSeparateFileEditAndContinue,
};

// XXX this is a bit clumsy, but it works well enough for now.
enum class MergeMode {
  kAll,
  kSkipCoveredByPreprocess,
  kDirectModeCommonArgs,
  kSkipInputs,
};

/// @brief Parses the limited subset of cl.exe command line syntax needed to extract info and
/// rewrite compilation command.
/// @note We could technically have higher hit rate if we resolved concrete values of all arguments
/// (e.g. "/WX /WX- /WX" should match any other sequence where /WX is effectively enabled). Then,
/// the resulting parser context itself can be hashed instead of command line text. It seems likely
/// there is small ROI in the real world (invocations by a build system), though?
/// @note cl.exe options may implicitly modify other related option state. These behaviors may also
/// change between compiler versions.
struct cmdline_parser_t {
  struct flag_option_t {
    bool enabled{};
    std::string value;
  };
  struct pch_config_t {
    bool is_create() const;
    std::string output_path(const std::string& input_file, const std::string& default_name) const;

    flag_option_t create;
    flag_option_t use;
    std::string path;
    bool ignore{};
  };

  void parse_list(const string_list_t& list);
  void parse_line(const std::string& line);
  void parse_file(const std::string& name);
  void parse(const string_list_t& cmdline);

  string_list_t merge(MergeMode mode = MergeMode::kAll) const;

  bool get_option(const std::string& item, std::string* option) const;
  void append_file(const std::string& name, InputType type = InputType::kUnknown);
  InputFile& input_file_by_name(const std::string& name);
  InputType effective_file_type(const InputFile& file) const;

  bool obj_path_is_dir() const;

  int m_command_file_depth{};
  bool m_compile_only{};
  InputType m_default_input_type{InputType::kObject};
  DebugFormat m_debug_format{DebugFormat::kNone};
  string_list_t m_includes;
  string_list_t m_defines;
  string_list_t m_options;
  std::string m_pdb_path;
  std::string m_object_path;
  pch_config_t m_pch_config;
  std::vector<InputFile> m_input_files;
};

bool cmdline_parser_t::get_option(const std::string& item, std::string* option) const {
  if (item.size() < 1) {
    return false;
  }
  if (!(item[0] == '/' || item[0] == '-')) {
    return false;
  }
  *option = item.substr(1);
  return true;
}

void cmdline_parser_t::append_file(const std::string& name, InputType type) {
  m_input_files.emplace_back(name, type);
}

InputFile& cmdline_parser_t::input_file_by_name(const std::string& name) {
  auto it = std::find_if(m_input_files.begin(),
                         m_input_files.end(),
                         [&name](const InputFile& file) { return file.name == name; });
  if (it == m_input_files.end()) {
    throw std::runtime_error("Failed to lookup " + name);
  }
  return *it;
}

void cmdline_parser_t::parse_list(const string_list_t& list) {
  auto it = list.cbegin();
  auto next_item = [&]() {
    if (++it == list.cend()) {
      throw std::runtime_error("Expected another item.");
    }
    return *it;
  };
  auto retrieve_arg = [&](const std::string& item, bool uses_colon = false) {
    auto arg = uses_colon ? drop_leading_colon(item) : item;
    if (!arg.empty()) {
      return arg;
    }
    // There is subtle behavior here: if the command supports colon, and colon was not provided,
    // then there must be no spaces between command and argument (i.e. it cannot be the next item).
    if (uses_colon) {
      throw std::runtime_error("Expected another item.");
    }
    return next_item();
  };
  auto sanitize_path = [](const std::string& path) {
    // If the path begins with a drive letter, normalize it to upper case
    // This just improves cache hit rate, not required for proper operation.
    if (path.size() > 2 && path[1] == ':') {
      return upper_case(path.substr(0, 1)) + path.substr(1);
    }
    return path;
  };
  while (it != list.cend()) {
    const auto& item = *it;

    std::string option;
    if (get_option(item, &option)) {
      if (option == "link") {
        // Do not add /link or any following items from this line
        break;
      } else if (option == "c") {
        m_compile_only = true;
      } else if (starts_with(option, "D")) {
        m_defines += retrieve_arg(option.substr(1));
      } else if (starts_with(option, "Fd")) {
        m_pdb_path = sanitize_path(retrieve_arg(option.substr(2), true));
      } else if (starts_with(option, "Fo")) {
        m_object_path = sanitize_path(retrieve_arg(option.substr(2), true));
      } else if (starts_with(option, "Fp")) {
        m_pch_config.path = sanitize_path(retrieve_arg(option.substr(2), true));
      } else if (starts_with(option, "I")) {
        m_includes += sanitize_path(retrieve_arg(option.substr(1)));
      } else if (option == "TC") {
        m_default_input_type = InputType::kC;
      } else if (option == "TP") {
        m_default_input_type = InputType::kCpp;
      } else if (starts_with(option, "Tc") || starts_with(option, "Tp")) {
        InputType file_type = starts_with(option, "Tc") ? InputType::kC : InputType::kCpp;
        append_file(sanitize_path(retrieve_arg(option.substr(2))), file_type);
      } else if (option == "Y-") {
        m_pch_config.ignore = true;
      } else if (starts_with(option, "Yc")) {
        m_pch_config.create.enabled = true;
        m_pch_config.create.value = sanitize_path(option.substr(2));
      } else if (starts_with(option, "Yu")) {
        m_pch_config.use.enabled = true;
        m_pch_config.use.value = sanitize_path(option.substr(2));
      } else if (option == "Z7") {
        m_debug_format = DebugFormat::kObjectFile;
      } else if (option == "Zi") {
        m_debug_format = DebugFormat::kSeparateFile;
      } else if (option == "ZI") {
        m_debug_format = DebugFormat::kSeparateFileEditAndContinue;
      } else {
        // Not something we specially handle
        m_options += option;
      }
    } else if (starts_with(item, "@")) {
      // Inline the file. The command-file option itself is not tracked
      parse_file(item.substr(1));
    } else {
      // TODO Is it worth checking if |item| here is not a valid, existing file path?
      // Such a case likely means the previous item was parsed incorrectly.
      append_file(item);
    }

    it++;
  }
}

void cmdline_parser_t::parse_line(const std::string& line) {
  parse_list(string_list_t::split_args(line));
}

void cmdline_parser_t::parse_file(const std::string& name) {
  m_command_file_depth++;
  // This is an arbitrary amount, actual limit used by cl.exe is unknown.
  if (m_command_file_depth > 100) {
    throw std::runtime_error("Command file nesting too deep.");
  }
  read_lines(name, [&](const std::string& line) { parse_line(line); });
  m_command_file_depth--;
}

void cmdline_parser_t::parse(const string_list_t& argv) {
  const char* env_var = std::getenv(ENV_CL_PREFIX);
  if (env_var) {
    parse_line(env_var);
  }
  if (argv.size() > 1) {
    parse_list({argv.cbegin() + 1, argv.cend()});
  }
  env_var = std::getenv(ENV_CL_POSTFIX);
  if (env_var) {
    parse_line(env_var);
  }
}

std::string InputFile::as_arg() const {
  if (type == InputType::kC) {
    return "/Tc" + name;
  } else if (type == InputType::kCpp) {
    return "/Tp" + name;
  }
  return name;
}

InputType cmdline_parser_t::effective_file_type(const InputFile& file) const {
  if (file.type != InputType::kUnknown) {
    return file.type;
  }
  switch (m_default_input_type) {
    case InputType::kC:
    case InputType::kCpp:
      return m_default_input_type;
    default:
      return filename_to_type(file.name);
  }
}

string_list_t cmdline_parser_t::merge(MergeMode mode) const {
  string_list_t cmdline;
  if (m_compile_only) {
    cmdline += "/c";
  }
  if (mode != MergeMode::kDirectModeCommonArgs) {
    switch (m_default_input_type) {
      case InputType::kC:
        cmdline += "/TC";
        break;
      case InputType::kCpp:
        cmdline += "/TP";
        break;
    }
  }
  switch (m_debug_format) {
    case DebugFormat::kObjectFile:
      cmdline += "/Z7";
      break;
    case DebugFormat::kSeparateFile:
      cmdline += "/Zi";
      break;
    case DebugFormat::kSeparateFileEditAndContinue:
      cmdline += "/ZI";
      break;
  }
  for (const auto& option : m_options) {
    cmdline += "/" + option;
  }
  if (!m_pdb_path.empty()) {
    cmdline += "/Fd:" + m_pdb_path;
  }
  if (mode != MergeMode::kSkipCoveredByPreprocess) {
    for (const auto& option : m_includes) {
      cmdline += "/I" + option;
    }
    for (const auto& option : m_defines) {
      cmdline += "/D " + option;
    }
    if (!m_object_path.empty()) {
      cmdline += "/Fo:" + m_object_path;
    }
  }
  if (m_pch_config.create.enabled) {
    cmdline += "/Yc" + m_pch_config.create.value;
  }
  if (m_pch_config.use.enabled) {
    cmdline += "/Yu" + m_pch_config.use.value;
  }
  if (m_pch_config.ignore) {
    cmdline += "/Y-";
  }
  if (!m_pch_config.path.empty()) {
    cmdline += "/Fp:" + m_pch_config.path;
  }
  if (mode == MergeMode::kAll) {
    for (const auto& file : m_input_files) {
      cmdline += file.as_arg();
    }
  }
  return cmdline;
}

bool cmdline_parser_t::obj_path_is_dir() const {
  if (m_object_path.empty()) {
    // Current directory
    return true;
  }
  auto last = m_object_path.back();
  return last == '\\' || last == '/';
}

bool cmdline_parser_t::pch_config_t::is_create() const {
  if (ignore) {
    return false;
  }
  return create.enabled;
}

std::string cmdline_parser_t::pch_config_t::output_path(const std::string& input_file,
                                                        const std::string& default_name) const {
  if (path.empty()) {
    return file::change_extension(input_file, ".pch");
  } else {
    const auto last = path.back();
    if (last == '\\' || last == '/') {
      // The pch path is a directory. Formulate the default filename.
      return file::append_path(path, default_name);
    }
    return file::change_extension(path, ".pch");
  }
}

msvc_wrapper_t::msvc_wrapper_t(const string_list_t& args) : program_wrapper_t(args) {
  // Version 1.0 of the source dependencies json stores all paths in lowercase, with backslash
  // separator. Preproces so simple string compare can be used.
  for (auto path : split(get_env(ENV_CL_INCLUDE), ';')) {
    if (path.empty()) {
      continue;
    }
    m_env_include_paths.emplace_back(lower_case(path));
  }

  m_tool_version = get_tool_version(m_args[0]);
}

void msvc_wrapper_t::resolve_args() {
  m_parser = std::unique_ptr<cmdline_parser_t>(new cmdline_parser_t);
  m_parser->parse(m_args);

  // This only checks for /c. While other options also inhibit compilation + linking, they represent
  // cl.exe incovations that buildcache doesn't provide any caching for (for example, preprocessed
  // output itself).
  if (!m_parser->m_compile_only) {
    throw std::runtime_error("Cannot handle invocation with chained link.");
  }

  // This is a general command line error, which cl.exe will error on as well.
  if (m_parser->m_input_files.size() > 1 && !m_parser->obj_path_is_dir()) {
    throw std::runtime_error("Single object file path specified for multiple inputs.");
  }

  // PDB outputs of Zi and ZI may contain contents merged from multiple objects (including objects
  // not produced during this invocation of buildcache). Users should configure their environment to
  // use Z7 for debug format instead (there is no downside to doing so). Buildcache could force
  // override with Z7, but allow user to fix it instead.
  if (m_parser->m_debug_format == DebugFormat::kSeparateFile ||
      m_parser->m_debug_format == DebugFormat::kSeparateFileEditAndContinue) {
    throw std::runtime_error("Cannot handle invocation with shared pdb file. Use /Z7 instead.");
  }

  if (m_tool_version.vc_version < version_t{14, 27}) {
    throw std::runtime_error("VC Tools >= 14.27 is required for /sourceDependencies support.");
  }
}

bool msvc_wrapper_t::can_handle_command() {
  // Is this the right compiler?
  const auto cmd = lower_case(file::get_file_part(m_args[0], false));
  return (cmd == "cl");
}

string_list_t msvc_wrapper_t::get_capabilities() {
  // We can use hard links with MSVC since it will never overwrite already existing files.
  return string_list_t{"hard_links"};
}

string_list_t msvc_wrapper_t::get_preprocess_options(const std::string& output_dir) const {
  // /P takes precedence no matter the location in the argument list.
  string_list_t options{"/P"};

  // Set the directory preprocessed output files will be written to.
  options += "/Fi:" + output_dir + "/";
  // The directory must exist.
  file::create_dir_with_parents(output_dir);
  return options;
}

pp_sources_t msvc_wrapper_t::preprocess_source() {
  /* Don't use preprocessor for "direct mode"
  file::tmp_file_t preproc_dir(file::get_temp_dir(), "");
  // Run the preprocessor step.
  string_list_t preproc_args = m_parser->merge();
  preproc_args += get_preprocess_options(preproc_dir.path());
  auto result = run_with_response_file(preproc_args);
  if (result.return_code != 0) {
    throw std::runtime_error("Preprocessing command was unsuccessful.");
  }

  // Read preprocessor output and remove newlines
  pp_sources_t preprocessed;
  for (const auto& file : m_parser->m_input_files) {
    // Need to remove any leading directory parts.
    const auto basename = file::get_file_part(file.name, false);
    auto path = file::append_path(preproc_dir.path(), basename + ".i");
    // read-at-once is significantly faster
    std::string source = file::read(path);
    // read_lines(path, [&](const std::string& line) { source += line; });
    preprocessed.emplace(file.name, source);
  }
  // Return the preprocessed files.
  return preprocessed;
  */

  // Direct mode: just provide content of input file
  pp_sources_t preprocessed;
  for (const auto& file : m_parser->m_input_files) {
    // In case _only_ a source filename changed, the cache will still have a hit and place the
    // outputs at the correct location. This is normally acceptable - except when filename change
    // would change language mode used by compiler. So, account for effective language mode in the
    // index.
    const auto file_type = m_parser->effective_file_type(file);
    std::string type_name;
    switch (file_type) {
      case InputType::kC:
        type_name = "c";
        break;
      case InputType::kCpp:
        type_name = "cpp";
        break;
      case InputType::kObject:
        type_name = "object";
        break;
      case InputType::kUnknown:
        type_name = "unknown";
        break;
    }
    preprocessed.emplace(file.name, type_name + file::read(file.name));
  }
  return preprocessed;
}

string_list_t msvc_wrapper_t::get_relevant_arguments() {
  string_list_t filtered_args;

  // Append any state from command line parser which wouldn't already be accounted for via
  // preprocess_source.
  // filtered_args = m_parser->merge(MergeMode::kSkipCoveredByPreprocess); // not for direct mode
  filtered_args += m_parser->merge(MergeMode::kDirectModeCommonArgs);

  debug::log(debug::DEBUG) << "Filtered arguments: " << filtered_args.join(" ");

  return filtered_args;
}

std::map<std::string, std::string> msvc_wrapper_t::get_relevant_env_vars() {
  // Return the full, original version of INCLUDE in case we haven't parsed it correctly.
  const std::string env_var_names[] = {ENV_CL_INCLUDE};
  std::map<std::string, std::string> env_vars;
  for (auto& var_name : env_var_names) {
    env_vars[var_name] = get_env(var_name);
  }
  return env_vars;
}

std::string msvc_wrapper_t::get_program_id() {
  // TODO(m): Add things like executable file size too.
  return HASH_VERSION + m_tool_version.host_arch + m_tool_version.target_arch +
         m_tool_version.vc_version.as_string();
}

build_files_t msvc_wrapper_t::get_build_files(const pp_key_t& key) {
  build_files_t files;
  std::string object_path;

  if (!m_parser->obj_path_is_dir()) {
    // Non-directory object path indicates there is only a single input file, and the object name is
    // constructed from object path instead of input file.
    object_path = m_parser->m_object_path;
    if (file::get_extension(object_path).empty()) {
      object_path += ".obj";
    }
  } else {
    // If the object path is a directory, all output object filenames are automatically constructed
    // from input filenames.
    const auto basename = file::get_file_part(key, false);
    const auto object_dir = m_parser->m_object_path;
    object_path = object_dir + basename + ".obj";
  }
  files["object"] = {object_path, true};

  if (m_parser->m_pch_config.is_create()) {
    const auto default_name = "vc" + m_tool_version.vc_version.as_string(1) + "0.pch";
    files["pch"] = {m_parser->m_pch_config.output_path(key, default_name), true};
  }

  // Inform cache about filetracker tlog files if needed.
  const auto tlog_files = m_tlog.get_build_files(key);
  files.insert(tlog_files.cbegin(), tlog_files.cend());
  return files;
}

bool msvc_wrapper_t::filter_cache_hit(const cache_entry_t& entry) {
  for (auto& dependency : entry.dependency_records()) {
    const auto& include = dependency.first;
    hasher_t::hash_t digest;
    if (!get_dependency_digest(&digest, include)) {
      try {
        // Calculate and record the current digest of the file, in case another source file depends
        // on the same file.
        hasher_t hasher;
        hasher.update_from_file(include);
        digest = hasher.final();
        set_dependency_digest(include, digest);
      } catch (...) {
        // If there was some problem (e.g. file to hash no longer exists), ensure the cached result
        // won't be used.
        return false;
      }
    }
    // If the current digest of the dependency differs from the cache entry, consider it a cache
    // miss.
    if (digest != dependency.second) {
      return false;
    }
  }
  return true;
}

sys::run_result_t msvc_wrapper_t::run_with_response_file(const string_list_t& args,
                                                         bool quiet) const {
  // Clean environment variables which cl.exe will use as extra command line inputs.
  // It is expected |args| already contains content of these environment variables, if their values
  // are desired.
  scoped_unset_env_t scoped_off_pre(ENV_CL_PREFIX);
  scoped_unset_env_t scoped_off_post(ENV_CL_POSTFIX);
  // Disable unwanted printing of source file name in Visual Studio.
  scoped_unset_env_t scoped_off_vs(ENV_VS_OUTPUT_REDIRECTION);

  string_list_t args_to_exec{m_args[0]};

  auto cmdline = args.join(" ");
  if (cmdline.size() > 8000) {
    debug::log(debug::DEBUG) << "command file:" << cmdline;
    file::tmp_file_t tmp_file(file::get_temp_dir(), ".rsp");
    file::write(cmdline, tmp_file.path());
    args_to_exec += "@" + tmp_file.path();
    return sys::run(args_to_exec, quiet);
  }

  args_to_exec += args;
  return sys::run(args_to_exec, quiet);
}

bool msvc_wrapper_t::is_system_include(const std::string& path) const {
  for (const auto& include_path : m_env_include_paths) {
    if (starts_with(path, include_path)) {
      return true;
    }
  }
  return false;
}

sys::run_result_t msvc_wrapper_t::run_for_miss(miss_infos_t& miss_infos) {
  // Run the original command, but only for items that caused cache miss.
  auto args = m_parser->merge(MergeMode::kSkipInputs);
  for (const auto& miss_info : miss_infos) {
    const auto& file_name = std::get<0>(miss_info);
    const auto& input_file = m_parser->input_file_by_name(file_name);
    args += input_file.as_arg();
    m_tlog.add_source(input_file.name);
  }
  m_tlog.finalize_sources();

  // Append command to generate dependency information.
  file::tmp_file_t preproc_dir(file::get_temp_dir(), "");
  args += "/sourceDependencies " + preproc_dir.path();
  // For sourceDependencies, cl.exe actually checks if a dir exists at the given location. Else it's
  // treated as filename.
  file::create_dir_with_parents(preproc_dir.path());

  // Capture printed source file name (stdout) in cache entry.
  // TODO removed. Is it needed?
  auto result = run_with_response_file(args, false);
  result.std_err = {};
  result.std_out = {};

  // Read and process dependency information.
  for (auto& miss_info : miss_infos) {
    const auto& file_name = std::get<0>(miss_info);
    // Need to remove any leading directory parts.
    const auto basename = file::get_file_part(file_name, true);
    const auto json_path = file::append_path(preproc_dir.path(), basename + ".json");
    // Get the list of includes the compiler claims are dependencies of this input file.
    dependency_records_t dependencies;
    const auto src_deps = get_source_dependencies(json_path);
    for (const auto& include : src_deps) {
      // Check if we've already hashed the file
      hasher_t::hash_t digest;
      if (get_dependency_digest(&digest, include)) {
        dependencies[include] = digest;
        continue;
      }
      // Just ignore any system-provided includes
      if (is_system_include(include)) {
        continue;
      }
      // We hit some new file, hash it.
      hasher_t hasher;
      hasher.update_from_file(include);
      digest = hasher.final();
      dependencies[include] = digest;
      set_dependency_digest(include, digest);
    }
    std::get<3>(miss_info) = dependencies;

    m_tlog.write_logs(file_name, std::get<2>(miss_info), src_deps);
  }
  return result;
}

}  // namespace bcache
