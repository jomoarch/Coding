#include "compiler.hpp"
#include "handle.hpp"
#include "win_error.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

namespace {

std::string read_pipe(HANDLE hRead) {
  std::string out;
  char buf[4096];
  DWORD n = 0;
  while (ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) {
    out.append(buf, n);
  }
  return out;
}

std::wstring to_wide(const std::string &s) {
  if (s.empty())
    return {};
  int len = MultiByteToWideChar(CP_UTF8, 0, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(len, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                      w.data(), len);
  return w;
}

std::wstring quote_arg(const std::wstring &arg) {
  std::wstring out;
  out.reserve(arg.size() + 2);
  out.push_back(L'"');
  for (wchar_t c : arg) {
    if (c == L'"')
      out.push_back(L'\\');
    out.push_back(c);
  }
  out.push_back(L'"');
  return out;
}

} // namespace

CompilerResult compile_source(const CompilerOptions &opts) {
  CompilerResult r;
  r.success = false;

  std::error_code ec;
  if (!std::filesystem::exists(opts.source_path, ec)) {
    r.message = "Source file does not exist: " + opts.source_path.string();
    return r;
  }
  if (!std::filesystem::is_regular_file(opts.source_path, ec)) {
    r.message =
        "Source file is not a regular file: " + opts.source_path.string();
    return r;
  }

  if (!opts.output_path.parent_path().empty()) {
    std::filesystem::create_directories(opts.output_path.parent_path(), ec);
    if (ec) {
      r.message = "Failed to create output directory: " + ec.message();
      return r;
    }
  }

  if (std::filesystem::exists(opts.output_path, ec)) {
    std::filesystem::remove(opts.output_path, ec);
    if (ec) {
      r.message = "Failed to remove existing output file: " + ec.message();
      return r;
    }
  }

  std::wstring cmd;
  if (opts.args.empty()) {
    cmd = L"g++ -std=c++17";
  } else {
    cmd = quote_arg(to_wide(opts.args[0]));
    for (std::size_t i = 1; i < opts.args.size(); ++i) {
      cmd += L' ';
      cmd += quote_arg(to_wide(opts.args[i]));
    }
  }

  cmd += L' ';
  cmd += quote_arg(opts.source_path.wstring());
  cmd += L" -o ";
  cmd += quote_arg(opts.output_path.wstring());
  std::vector<wchar_t> cmdBuf(cmd.begin(), cmd.end());
  cmdBuf.push_back(L'\0');

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;

  HANDLE hReadRaw = INVALID_HANDLE_VALUE;
  HANDLE hWriteRaw = INVALID_HANDLE_VALUE;
  if (!CreatePipe(&hReadRaw, &hWriteRaw, &sa, 0)) {
    r.message = "CreatePipe failed: " + win::last_error_string();
    return r;
  }
  HandleGuard hRead(hReadRaw);
  HandleGuard hWirte(hWriteRaw);

  SetHandleInformation(hRead.get(), HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = si.hStdError = hWirte.get();

  PROCESS_INFORMATION pi{};
  if (!CreateProcessW(nullptr, cmdBuf.data(), nullptr, nullptr, TRUE,
                      CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
    r.message = "CreateProcessW failed: " + win::last_error_string();
    return r;
  }
  HandleGuard hProcess(pi.hProcess);
  HandleGuard hThead(pi.hThread);

  hWirte.reset();
  std::string output = read_pipe(hRead.get());

  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  const bool exe_exists = std::filesystem::exists(opts.output_path, ec);
  if (exit_code == 0 && exe_exists) {
    r.success = true;
    r.message = "Success";
  } else {
    if (output.empty()) {
      r.message = "Compiler exited: " + win::error_string(exit_code);
    } else {
      r.message = output;
    }
  }
  return r;
}