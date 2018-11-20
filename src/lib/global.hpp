#pragma once

#include <chrono>
#include <map>

#include "utils/singleton.hpp"

namespace opossum {

struct OperatorTimes {
  std::chrono::microseconds preparation_time;
  std::chrono::microseconds execution_time;
  std::chrono::microseconds __preparation_time;
  std::chrono::microseconds __execution_time;
};

enum class OperatorType;

struct Global : public Singleton<Global> {
  bool jit = false;
  bool lazy_load = true;
  bool jit_validate = true;
  bool deep_copy_exists = false;
  bool jit_evaluate = false;
  bool interpret = false;
  bool use_times = false;
  bool disable_string_compare = false;
  std::map<OperatorType, OperatorTimes> times;

 private:
  Global() = default;

  friend class Singleton;
};

}  // namespace opossum
