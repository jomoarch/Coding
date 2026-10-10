#include "monitor/monitor.hpp"

#include <windows.h>

#include <psapi.h>

namespace coding {
namespace monitor {

Sample sample(void *process, void *job,
              std::chrono::steady_clock::time_point started) {
  Sample s;

  const HANDLE hProcess = static_cast<HANDLE>(process);
  const HANDLE hJob = static_cast<HANDLE>(job);

  s.wall = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now() - started);

  if (hProcess != nullptr) {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (GetProcessTimes(hProcess, &created, &exited, &kernel, &user)) {
      ULARGE_INTEGER k{};
      ULARGE_INTEGER u{};
      k.LowPart = kernel.dwLowDateTime;
      k.HighPart = kernel.dwHighDateTime;
      u.LowPart = user.dwLowDateTime;
      u.HighPart = user.dwHighDateTime;

      s.cpu = std::chrono::nanoseconds((k.QuadPart + u.QuadPart) * 100ULL);
    }

    PROCESS_MEMORY_COUNTERS mem{};
    if (K32GetProcessMemoryInfo(hProcess, &mem, sizeof(mem)))
      s.current_bytes = static_cast<std::size_t>(mem.WorkingSetSize);

    DWORD code = 0;
    if (GetExitCodeProcess(hProcess, &code)) {
      s.exit_code = code;
      s.alive = code == STILL_ACTIVE;
    }
  }

  if (hJob != nullptr) {
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    if (QueryInformationJobObject(hJob, JobObjectExtendedLimitInformation,
                                  &info, sizeof(info), nullptr))
      s.peak_bytes = static_cast<std::size_t>(info.PeakJobMemoryUsed);
  }

  return s;
}

} // namespace monitor
} // namespace coding
