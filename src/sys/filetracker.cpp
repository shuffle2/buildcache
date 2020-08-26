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
#include <sys/filetracker.hpp>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>

#ifdef _WIN32
// Note: The FileTracker API has been created from msdn documentation.

/// @brief Suspends tracking in the current context.
/// @returns An HRESULT with the SUCCEEDED bit set if tracking was suspended.
HRESULT WINAPI SuspendTracking(void);

/// @brief Resumes tracking in the current context.
/// @returns An HRESULT with the SUCCEEDED bit set if tracking was resumed. E_FAIL is returned if
/// tracking cannot be resumed because the context was not available.
HRESULT WINAPI ResumeTracking();
#endif

namespace bcache {
namespace filetracker {
#ifdef _WIN32
namespace {
HMODULE handle;
decltype(&::SuspendTracking) SuspendTracking;
decltype(&::ResumeTracking) ResumeTracking;

bool init() {
  if (handle) {
    return true;
  }
  env_var_t tracker_enabled("TRACKER_ENABLED");
  if (!tracker_enabled.as_bool()) {
    return false;
  }
  for (auto module_name : {L"FileTracker64", L"FileTracker32", L"FileTracker"}) {
    handle = GetModuleHandleW(module_name);
    if (handle) {
      break;
    }
  }
  if (!handle) {
    return false;
  }
#define FUNC_RESOLVE(x)                          \
  do {                                           \
    x = (decltype(x))GetProcAddress(handle, #x); \
    if (!x) {                                    \
      handle = nullptr;                          \
      return false;                              \
    }                                            \
  } while (0)
  FUNC_RESOLVE(SuspendTracking);
  FUNC_RESOLVE(ResumeTracking);
#undef FUNC_RESOLVE
  return true;
}

void suspend_tracking() {
  if (!init()) {
    return;
  }
  SuspendTracking();
}

void resume_tracking() {
  if (!init()) {
    return;
  }
  ResumeTracking();
}

// Note: SuspendTracking / ResumeTracking are not recursive (they don't track a refcount internally
// so whichever is called last is the effective state). Code outside of this file does not need real
// scoped wrapper support (which would be required to implement nesting support), so it is not
// implemented. This scoped wrapper is only intended for injecting SuspendTracking / ResumeTracking
// into the language init/deinit lists.
struct FileTrackerScopedSuppressor {
  FileTrackerScopedSuppressor() {
    suspend_tracking();
  }
  ~FileTrackerScopedSuppressor() {
    resume_tracking();
  }
};

// Ensure automatic suspend and resume of FileTracker for buildcache lifetime.
// Technically, it probably doesn't matter if it is resumed.
static FileTrackerScopedSuppressor filetracker_singleton;
}  // namespace
#endif

void release_suppression() {
#ifdef _WIN32
  resume_tracking();
#endif
}

tracking_log_t::tracking_log_t() {
  env_var_t enabled("TRACKER_ENABLED");
  m_enabled = enabled.as_bool();
  if (!m_enabled) {
    return;
  }
  m_intermediate_dir = get_env("TRACKER_INTERMEDIATE");
  m_toolchain = get_env("TRACKER_TOOLCHAIN");
}

build_files_t tracking_log_t::get_build_files(const std::string& filename) const {
  if (!enabled()) {
    return {};
  }

  build_files_t files;
  auto basename = file::get_file_part(filename);
  std::replace(basename.begin(), basename.end(), '.', '_');
  const auto tlog_name_read = m_toolchain + "." + basename + ".read.1.tlog";
  const auto tlog_name_write = m_toolchain + "." + basename + ".write.1.tlog";
  files["tlog_r"] = {file::append_path(m_intermediate_dir, tlog_name_read), true};
  files["tlog_w"] = {file::append_path(m_intermediate_dir, tlog_name_write), true};
  return files;
}

void tracking_log_t::add_source(const std::string& path) {
  if (!enabled()) {
    return;
  }
  m_sources += fullpath(path);
}

void tracking_log_t::finalize_sources() {
  if (!enabled()) {
    return;
  }
  std::sort(m_sources.begin(), m_sources.end());
  m_root = "^" + m_sources.join("|");
}

void tracking_log_t::write_logs(const std::string& source,
                                const build_files_t& build_files,
                                const string_list_t& dependencies) const {
  if (!enabled()) {
    return;
  }
  // Create per-input-file tlog records. This allows them to be cached per-file, and is compatible
  // with MSBuild (which will merge them automatically).
  const auto object_path = fullpath(build_files.at("object").path());
  {
    string_list_t tlog;
    tlog += m_root;
    tlog += fullpath(source);
    // Expect that dependencies are already absolute paths, but maybe not upper case
    tlog += dependencies;
    tlog += object_path;
    file::write(upper_case(tlog.join("\r\n")), build_files.at("tlog_r").path());
  }
  {
    string_list_t tlog;
    tlog += m_root;
    if (build_files.count("pch")) {
      tlog += build_files.at("pch").path();
    }
    tlog += object_path;
    file::write(tlog.join("\r\n"), build_files.at("tlog_w").path());
  }
}

std::string tracking_log_t::fullpath(const std::string& path) const {
  return upper_case(file::resolve_path(path));
}

}  // namespace filetracker
}  // namespace bcache
