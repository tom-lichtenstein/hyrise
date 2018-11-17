#include "jit_filter.hpp"

#include <stack>

#include "jit_expression.hpp"

namespace opossum {

JitFilter::JitFilter(const JitTupleValue& condition)
    : AbstractJittable(JitOperatorType::Filter), _condition{condition} {
  DebugAssert(condition.data_type() == DataType::Bool || condition.data_type() == DataTypeBool,
              "Filter condition must be a boolean");
}

JitFilter::JitFilter(const std::shared_ptr<const JitExpression>& expression)
: AbstractJittable(JitOperatorType::Filter), _condition{expression->result()}, _expression{expression}{
  DebugAssert(_condition.data_type() == DataType::Bool || _condition.data_type() == DataTypeBool,
              "Filter condition must be a boolean");
}

std::string JitFilter::description() const {
  std::string expression = _expression ? " = " + _expression->to_string() : "";
  return "[Filter] on x" + std::to_string(_condition.tuple_index()) + expression;
}

JitTupleValue JitFilter::condition() { return _condition; }

void JitFilter::_consume(JitRuntimeContext& context) const {
  // if (_expression) {
    if (_expression->compute_and_get<bool>(context).value) {
      _emit(context);
    }
    return;
  // }
  /*
  if (!_condition.is_null(context) && _condition.get<bool>(context)) {
    _emit(context);
  } else {
#if JIT_MEASURE
    _end(context);
#endif
  }
  */
}

std::map<size_t, bool> JitFilter::accessed_column_ids() const {
  if (!_expression) {
    std::map<size_t, bool> column_ids;
    column_ids.emplace(_condition.tuple_index(), false);
    return column_ids;
  }
  std::map<size_t, bool> column_ids;
  std::stack<std::shared_ptr<const JitExpression>> stack;
  stack.push(_expression);
  while (!stack.empty()) {
    auto current = stack.top();
    stack.pop();
    if (auto right_child = current->right_child()) stack.push(right_child);
    if (auto left_child = current->left_child()) stack.push(left_child);
    if (current->expression_type() == JitExpressionType::Column) {
      const auto tuple_index = current->result().tuple_index();
      column_ids[tuple_index] = !column_ids.count(tuple_index);
    }
  }
  return column_ids;
}

void JitFilter::set_load_column(const size_t tuple_id, const std::shared_ptr<BaseJitSegmentReaderWrapper> _input_segment_wrapper) const {
  std::stack<std::shared_ptr<const JitExpression>> stack;
  stack.push(_expression);
  while (!stack.empty()) {
    auto current = stack.top();
    stack.pop();
    if (auto right_child = current->right_child()) stack.push(right_child);
    if (auto left_child = current->left_child()) stack.push(left_child);
    if (current->expression_type() == JitExpressionType::Column) {
      const auto tuple_index = current->result().tuple_index();
      if (tuple_id == tuple_index) {
        current->set_load_column(_input_segment_wrapper);
        return;
      }
    }
  }
}

}  // namespace opossum
