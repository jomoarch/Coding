#ifndef PROCESS_STATUS_HPP
#define PROCESS_STATUS_HPP

enum class RunnerStatus {
  Success,
  SystemError,
  RuntimeError,
  TimeLimitExceeded,
  MemoryLimitExceeded
};

#endif // PROCESS_STATUS_HPP