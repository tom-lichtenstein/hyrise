#pragma once

#include <chrono>
#include <map>

#include "utils/singleton.hpp"

namespace opossum {

struct OperatorTimes {
  OperatorTimes() : preparation_time{0}, execution_time{0}, __preparation_time{0}, __execution_time{0} {}
  std::chrono::microseconds preparation_time;
  std::chrono::microseconds execution_time;
  std::chrono::microseconds __preparation_time;
  std::chrono::microseconds __execution_time;
};

struct Global : public Singleton<Global> {
  bool jit = false;
  bool lazy_load = true;
  bool jit_validate = true;
  bool deep_copy_exists = false;
  bool jit_evaluate = false;
  bool interpret = false;
  bool use_times = false;
  bool disable_string_compare = false;
  bool use_limit_in_subquery = false;
  bool jit_limit = true;
  std::map<std::string, OperatorTimes> times;

 private:
  Global() = default;

  friend class Singleton;
};

}  // namespace opossum
