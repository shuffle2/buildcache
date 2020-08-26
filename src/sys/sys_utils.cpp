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
#include <base/unicode_utils.hpp>
#include <config/configuration.hpp>
#include <sys/sys_utils.hpp>

#include <cstdio>
#include <cstdlib>

#include <iostream>
#include <stdexcept>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <thread>
#undef ERROR
#undef log
#else
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <thread>
#endif

namespace bcache {
namespace sys {
namespace {
const std::string TEMP_FOLDER_NAME = "tmp";

// This is a workaround for buggy compiler identification in ICECC: It will not properly identify
// C++ compilers that do not end with "++", for instance.
std::string make_exe_path_suitable_for_icecc(const std::string& path) {
  const auto exe_name = file::get_file_part(path, false);
  const auto exe_ext = file::get_extension(path);

  // Handle g++ with "-*" suffixes.
  if ((exe_name == "g++-4") || (exe_name == "g++-5") || (exe_name == "g++-6") ||
      (exe_name == "g++-7")) {
    auto candidate = file::append_path(file::get_dir_part(path), "g++");
    if (!file::file_exists(candidate)) {
      candidate = candidate + exe_ext;
    }
    if (file::file_exists(candidate)) {
      return candidate;
    }
  }

  return path;
}

#if !defined(_WIN32)
bool try_start_editor(const std::string& program, const std::string& file) {
  try {
    const auto& real_path = file::find_executable(program);
    execl(real_path.c_str(), program.c_str(), file.c_str(), (char*)0);
    return true;
  } catch (...) {
  }
  return false;
}
#endif  // !_WIN32

#if defined(_WIN32)
bool print_raw(const char* str, const DWORD len, HANDLE handle) {
  // TODO(m): Handle bytes_written != bytes_read?
  DWORD bytes_written;
  if (!WriteFile(handle, str, len, &bytes_written, nullptr)) {
    return false;
  }
  return true;
}
#endif  // _WIN32

// Helper function for reading data from a child process pipe.
#if defined(_WIN32)
bool read_from_pipe(HANDLE pipe_handle, std::string& data, const bool quiet, HANDLE& out_stream) {
  std::vector<char> buf(4096);
  auto success = true;
  auto has_more_data = true;
  while (has_more_data && success) {
    DWORD bytes_read = 0;
    if (!ReadFile(pipe_handle, buf.data(), static_cast<DWORD>(buf.size()), &bytes_read, nullptr)) {
      const auto err = GetLastError();
      // ERROR_BROKEN_PIPE indicates that we're done (normal exit path).
      if (err != ERROR_BROKEN_PIPE) {
        debug::log(debug::ERROR) << "Error reading output from child process";
        success = false;
      }
      has_more_data = false;
    } else if (bytes_read > 0) {
      if (!quiet && out_stream != nullptr) {
        if (!print_raw(buf.data(), bytes_read, out_stream)) {
          debug::log(debug::ERROR) << "Error printing output from child process";
          success = false;
          has_more_data = false;
        }
      }
      data.append(buf.data(), static_cast<std::string::size_type>(bytes_read));
    }
  }
  return success;
}
#else
bool read_from_pipe(const int pipe_fd,
                    std::string& data,
                    const bool quiet,
                    std::ostream& out_stream) {
  std::vector<char> buf(4096);
  auto success = true;
  auto has_more_data = true;
  while (has_more_data && success) {
    const auto bytes_read = read(pipe_fd, buf.data(), buf.size());
    if (bytes_read == -1) {
      if (errno != EINTR) {
        debug::log(debug::ERROR) << "Error reading output from child process (errno: " << errno
                                 << ")";
        success = false;
      }
    } else if (bytes_read == 0) {
      has_more_data = false;
    } else {
      if (!quiet) {
        out_stream.write(buf.data(), static_cast<std::streamsize>(bytes_read));
      }
      data += std::string(buf.data(), static_cast<std::string::size_type>(bytes_read));
    }
  }
  return success;
};
#endif  // _WIN32
}  // namespace

run_result_t run(const string_list_t& args, const bool quiet) {
  // Initialize the run result.
  run_result_t result;

  auto successfully_launched_program = false;
#if defined(_WIN32)
  const bool escape_args = false;
#else
  const bool escape_args = true;
#endif
  const auto cmd = args.join(" ", escape_args);
  debug::log(debug::DEBUG) << "Invoking: " << cmd;

#if defined(_WIN32)
  HANDLE std_out_read_handle = nullptr;
  HANDLE std_out_write_handle = nullptr;
  HANDLE std_err_read_handle = nullptr;
  HANDLE std_err_write_handle = nullptr;
  HANDLE std_in_read_handle = nullptr;
  HANDLE std_in_write_handle = nullptr;

  try {
    SECURITY_ATTRIBUTES security_attr;
    ZeroMemory(&security_attr, sizeof(security_attr));
    security_attr.nLength = sizeof(SECURITY_ATTRIBUTES);
    security_attr.bInheritHandle = TRUE;
    security_attr.lpSecurityDescriptor = nullptr;

    // Create a pipe for the child process's stdout.
    if (!CreatePipe(&std_out_read_handle, &std_out_write_handle, &security_attr, 0)) {
      throw std::runtime_error("Unable to create stdout pipe.");
    }
    if (!SetHandleInformation(std_out_read_handle, HANDLE_FLAG_INHERIT, 0)) {
      throw std::runtime_error("Unable to create stdout pipe.");
    }

    // Create a pipe for the child process's stderr.
    if (!CreatePipe(&std_err_read_handle, &std_err_write_handle, &security_attr, 0)) {
      throw std::runtime_error("Unable to create stderr pipe.");
    }
    if (!SetHandleInformation(std_err_read_handle, HANDLE_FLAG_INHERIT, 0)) {
      throw std::runtime_error("Unable to create stderr pipe.");
    }

    // Create a pipe for the child process's stdin.
    if (!CreatePipe(&std_in_read_handle, &std_in_write_handle, &security_attr, 0)) {
      throw std::runtime_error("Unable to create stdin pipe.");
    }
    if (!SetHandleInformation(std_in_write_handle, HANDLE_FLAG_INHERIT, 0)) {
      throw std::runtime_error("Unable to create stdin pipe.");
    }

    // Get the standard I/O handles of the BuildCache process.
    auto stdout_handle = GetStdHandle(STD_OUTPUT_HANDLE);
    if (stdout_handle == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("Unable to get the stdout handle.");
    }
    auto stderr_handle = GetStdHandle(STD_ERROR_HANDLE);
    if (stderr_handle == INVALID_HANDLE_VALUE) {
      throw std::runtime_error("Unable to get the stderr handle.");
    }

    // Set up process information.
    STARTUPINFOW startup_info;
    ZeroMemory(&startup_info, sizeof(startup_info));
    startup_info.cb = sizeof(STARTUPINFOW);
    startup_info.hStdOutput = std_out_write_handle;
    startup_info.hStdError = std_err_write_handle;
    startup_info.hStdInput = std_in_read_handle;
    startup_info.dwFlags |= STARTF_USESTDHANDLES;

    // Note: The cmdw string may be modified by CreateProcessW (!) so it must not be const.
    auto cmdw = utf8_to_ucs2(cmd);

    // Start the child process.
    PROCESS_INFORMATION process_info;
    ZeroMemory(&process_info, sizeof(process_info));
    if (!CreateProcessW(nullptr,
                        &cmdw[0],
                        nullptr,
                        nullptr,
                        TRUE,
                        0,
                        nullptr,
                        nullptr,
                        &startup_info,
                        &process_info)) {
      throw std::runtime_error("Unable to create child process.");
    }

    // Close the write ends of the pipes (so the child process stops reading).
    CloseHandle(std_out_write_handle);
    std_out_write_handle = nullptr;
    CloseHandle(std_err_write_handle);
    std_err_write_handle = nullptr;
    CloseHandle(std_in_write_handle);
    std_in_write_handle = nullptr;

    // Read stdout in a separate thread.
    auto stdout_read_success = false;
    std::thread stdout_reader_thread(
        [&stdout_read_success, &std_out_read_handle, &result, quiet, &stdout_handle]() {
          try {
            stdout_read_success =
                read_from_pipe(std_out_read_handle, result.std_out, quiet, stdout_handle);
          } catch (...) {
            stdout_read_success = false;
          }
        });

    // Read stderr in the current thread (concurrently with reading stdout).
    const auto stderr_read_success =
        read_from_pipe(std_err_read_handle, result.std_err, quiet, stderr_handle);

    // Wait for stdout reading to be finished.
    stdout_reader_thread.join();
    const auto read_pipes_failed = !stdout_read_success || !stderr_read_success;

    // Wait for the process to finish.
    if (WaitForSingleObject(process_info.hProcess, INFINITE) == WAIT_OBJECT_0) {
      // Get process return code.
      DWORD exit_code = 1;
      if (GetExitCodeProcess(process_info.hProcess, &exit_code)) {
        result.return_code = static_cast<int>(exit_code);
        successfully_launched_program = true;
      } else {
        result.return_code = 1;
      }
    } else {
      result.return_code = 1;
    }

    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);
  } catch (...) {
    successfully_launched_program = false;
  }

  if (std_out_read_handle != nullptr) {
    CloseHandle(std_out_read_handle);
  }
  if (std_out_write_handle != nullptr) {
    CloseHandle(std_out_write_handle);
  }
  if (std_err_read_handle != nullptr) {
    CloseHandle(std_err_read_handle);
  }
  if (std_err_write_handle != nullptr) {
    CloseHandle(std_err_write_handle);
  }
  if (std_in_read_handle != nullptr) {
    CloseHandle(std_in_read_handle);
  }
  if (std_in_write_handle != nullptr) {
    CloseHandle(std_in_write_handle);
  }
#else
  // Create pipes for stdout and stderr.
  int pipe_stdout[2], pipe_stderr[2];
  if (pipe(pipe_stdout) == -1) {
    throw std::runtime_error("Error creating stdout pipe.");
  }
  if (pipe(pipe_stderr) == -1) {
    close(pipe_stdout[0]);
    close(pipe_stdout[1]);
    throw std::runtime_error("Error creating stderr pipe.");
  }

  // Create the child process.
  auto child_pid = fork();
  if (child_pid == 0) {
    try {
      // Redirect stdout & stderr to the pipes.
      auto dup2_or_throw = [](int fildes, int fildes2) {
        while (true) {
          if (dup2(fildes, fildes2) != -1) {
            break;
          } else if (errno != EINTR) {
            throw std::runtime_error("Could not redirect stdout/stderr");
          }
        }
      };
      dup2_or_throw(pipe_stdout[1], STDOUT_FILENO);
      dup2_or_throw(pipe_stderr[1], STDERR_FILENO);

      // The child process will not use the pipes directly, so close them.
      close(pipe_stdout[0]);
      close(pipe_stdout[1]);
      close(pipe_stderr[0]);
      close(pipe_stderr[1]);

      // Build a NULL-terminated argv array from args.
      std::vector<char*> argv;
      for (auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
      }
      argv.push_back(nullptr);

      // Call the command.
      execv(argv[0], &argv[0]);

      // If execv returns, it must have failed.
      throw std::runtime_error("Unable to launch command via execv()");
    } catch (std::exception& e) {
      // TODO(m): Signal this error to the parent process somehow.
      std::cerr << "*** BuildCache error: " << e.what() << "\n";
      std::exit(1);
    }
  } else if (child_pid != -1) {
    // The parent has no need for the entrances of the pipes, so close them.
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    // Read stdout in a separate thread.
    auto stdout_read_success = false;
    std::thread stdout_reader_thread([&stdout_read_success, quiet, &pipe_stdout, &result]() {
      try {
        stdout_read_success = read_from_pipe(pipe_stdout[0], result.std_out, quiet, std::cout);
      } catch (...) {
        stdout_read_success = false;
      }
    });

    // Read stderr in the current thread (concurrently with reading stdout).
    const auto stderr_read_success =
        read_from_pipe(pipe_stderr[0], result.std_err, quiet, std::cerr);

    // Wait for stdout reading to be finished.
    stdout_reader_thread.join();
    const auto read_pipes_failed = !stdout_read_success || !stderr_read_success;

    // We're done with the pipes in the parent process.
    close(pipe_stdout[0]);
    close(pipe_stderr[0]);

    // Wait for the child process to terminate.
    int child_status;
    while (true) {
      const auto pid = waitpid(child_pid, &child_status, 0);
      if (pid == child_pid) {
        if (WIFEXITED(child_status)) {
          result.return_code = WEXITSTATUS(child_status);
        } else if (WIFSIGNALED(child_status)) {
          result.return_code = WTERMSIG(child_status);
          debug::log(debug::INFO) << "Child process " << child_pid
                                  << " terminated (signal: " << WTERMSIG(child_status) << ")";
        }
        successfully_launched_program = true;
        break;
      } else if (pid == -1 && errno == EINTR) {
        // Our wait was interrupted. Try again.
        continue;
      } else {
        debug::log(debug::ERROR) << "Unexpected error waiting for pid " << child_pid
                                 << " (errno: " << errno << ")";
        result.return_code = 1;
        break;
      }
    }

    // If we didn't get all the stdout/stderr data from the child process, we can't guarantee
    // correct operation.
    if (read_pipes_failed) {
      successfully_launched_program = false;
    }
  } else {
    debug::log(debug::ERROR) << "Could not create child process (errno: " << errno << ")";
  }
#endif  // _WIN32

  if (!successfully_launched_program) {
    throw std::runtime_error("Unable to start the child process.");
  }

  return result;
}

run_result_t run_with_prefix(const string_list_t& args, const bool quiet) {
  // Prepend the argument list with a prefix, if any.
  bool is_icecc_prefix = false;
  string_list_t prefixed_args;
  if (!config::prefix().empty()) {
    prefixed_args += config::prefix();

    // Are we prefixed by ICECC?
    is_icecc_prefix = (file::get_file_part(config::prefix(), false) == "icecc");
  }
  prefixed_args += args;

  // This is a hack to work around ICECC bugs.
  if (is_icecc_prefix) {
    prefixed_args[1] = make_exe_path_suitable_for_icecc(prefixed_args[1]);
  }

  // Run the command.
  return run(prefixed_args, quiet);
}

void open_in_default_editor(const std::string& path) {
#if defined(_WIN32)
  const auto path_w = utf8_to_ucs2(path);
  const auto result_code = reinterpret_cast<intptr_t>(
      ShellExecuteW(nullptr, L"edit", path_w.c_str(), nullptr, nullptr, SW_SHOW));
  if (result_code <= 32) {
    // Something went wrong (e.g. there was no default association for the given file type), so let
    // the user decide.
    OPENASINFO oi;
    ZeroMemory(&oi, sizeof(oi));
    oi.pcszFile = path_w.c_str();
    oi.oaifInFlags = OAIF_EXEC;
    if (SHOpenWithDialog(nullptr, &oi) != S_OK) {
      throw std::runtime_error("Unable to open file " + path);
    }
  }
#else
  const auto pid = fork();
  if (pid == 0) {
    bool started_editor = false;

#if defined(__APPLE__) && defined(__MACH__)
    // For macOS we try "open" first.
    if (try_start_editor("open", path)) {
      started_editor = true;
    }
#endif

    // Try an X11 based GUI editor.
    if (!started_editor) {
      if (env_defined("DISPLAY")) {
        if (try_start_editor("xdg-open", path)) {
          started_editor = true;
        } else if (try_start_editor("gvfs-open", path)) {
          started_editor = true;
        } else if (try_start_editor("kde-open", path)) {
          started_editor = true;
        }
      }
    }

    // Fall back to the user configured console editor.
    if (!started_editor) {
      if (try_start_editor("sensible-editor", path)) {
        started_editor = true;
      }
      if (!started_editor) {
        const auto env_editor = get_env("EDITOR");
        if (!env_editor.empty() && try_start_editor(env_editor, path)) {
          started_editor = true;
        }
      }
      if (!started_editor) {
        if (try_start_editor("nano", path)) {
          started_editor = true;
        } else if (try_start_editor("vim", path)) {
          started_editor = true;
        } else if (try_start_editor("vi", path)) {
          started_editor = true;
        }
      }
    }

    std::exit(started_editor ? 0 : 1);
  } else {
    int exit_code = 1;
    (void)waitpid(pid, &exit_code, 0);
    if (exit_code != 0) {
      throw std::runtime_error("Unable to open an editor for file " + path);
    }
  }
#endif
}

void print_raw_stdout(const std::string& str) {
#if defined(_WIN32)
  auto handle = GetStdHandle(STD_OUTPUT_HANDLE);
  if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
    throw std::runtime_error("Unable to get the stdout handle.");
  }
  if (!print_raw(str.data(), static_cast<DWORD>(str.size()), handle)) {
    throw std::runtime_error("Unable to get print to stdout.");
  }
#else
  std::cout << str;
#endif
}

void print_raw_stderr(const std::string& str) {
#if defined(_WIN32)
  auto handle = GetStdHandle(STD_ERROR_HANDLE);
  if (handle == INVALID_HANDLE_VALUE || handle == NULL) {
    throw std::runtime_error("Unable to get the stderr handle.");
  }
  if (!print_raw(str.data(), static_cast<DWORD>(str.size()), handle)) {
    throw std::runtime_error("Unable to get print to stderr.");
  }
#else
  std::cerr << str;
#endif
}

const std::string get_local_temp_folder() {
  const auto tmp_path = file::append_path(config::dir(), TEMP_FOLDER_NAME);
  file::create_dir_with_parents(tmp_path);
  return tmp_path;
}

}  // namespace sys
}  // namespace bcache
