#ifndef PROCESS_STATUS_HPP
#define PROCESS_STATUS_HPP

namespace coding {

enum class RunnerStatus {
  Success,
  SystemError,
  RuntimeError,
  TimeLimitExceeded,
  MemoryLimitExceeded
};

} // namespace coding

#endif // PROCESS_STATUS_HPP