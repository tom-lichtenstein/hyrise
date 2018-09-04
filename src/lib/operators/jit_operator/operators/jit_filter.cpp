#include "jit_filter.hpp"

namespace opossum {

JitFilter::JitFilter(const JitTupleValue& condition) : _condition{condition} {
  DebugAssert(condition.data_type() == DataType::Bool || condition.data_type() == DataTypeBool,
              "Filter condition must be a boolean");
}

std::string JitFilter::description() const { return "[Filter] on x" + std::to_string(_condition.tuple_index()); }

JitTupleValue JitFilter::condition() { return _condition; }

void JitFilter::_consume(JitRuntimeContext& context) const {
#if JIT_MEASURE
  auto begin = std::chrono::high_resolution_clock::now();
#endif
  bool keep_tuple = !_condition.is_null(context) && _condition.get<bool>(context);
#if JIT_MEASURE
  auto end = std::chrono::high_resolution_clock::now();
  context.filter_time += end - begin;
#endif
  if (keep_tuple) {
    _emit(context);
  }
}

std::map<size_t, bool> JitFilter::accessed_column_ids() const {
  std::map<size_t, bool> column_ids;
  column_ids.emplace(_condition.tuple_index(), false);
  return column_ids;
}

}  // namespace opossum
