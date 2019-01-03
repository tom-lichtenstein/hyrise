#include "jit_compute.hpp"

#include <boost/preprocessor/seq/for_each_product.hpp>
#include <boost/preprocessor/tuple/elem.hpp>

#include <stack>

namespace opossum {

// Returns the enum value (e.g., DataType::Int, DataType::String) of a data type defined in the DATA_TYPE_INFO sequence
#define JIT_GET_ENUM_VALUE(index, s) APPEND_ENUM_NAMESPACE(_, _, BOOST_PP_TUPLE_ELEM(3, 1, BOOST_PP_SEQ_ELEM(index, s)))

// Returns the data type (e.g., int32_t, std::string) of a data type defined in the DATA_TYPE_INFO sequence
#define JIT_GET_DATA_TYPE(index, s) BOOST_PP_TUPLE_ELEM(3, 0, BOOST_PP_SEQ_ELEM(index, s))

#define JIT_COMPUTE_CASE_AND_GET(r, types)                                 \
  case JIT_GET_ENUM_VALUE(0, types): {                                        \
    const auto result = _expression->compute_and_get<JIT_GET_DATA_TYPE(0, types)>(context); \
    _expression->result().set<JIT_GET_DATA_TYPE(0, types)>(result.value, context); \
    if (_expression->result().is_nullable()) { \
      _expression->result().set_is_null(result.is_null, context); \
    } \
    break; \
  }

JitCompute::JitCompute(const std::shared_ptr<const JitExpression>& expression)
    : AbstractJittable(JitOperatorType::Compute), _expression{expression} {}

std::string JitCompute::description() const {
  return "[Compute] x" + std::to_string(_expression->result().tuple_index()) + " = " + _expression->to_string();
}

std::shared_ptr<const JitExpression> JitCompute::expression() { return _expression; }

std::map<size_t, bool> JitCompute::accessed_column_ids() const {
  std::map<size_t, bool> column_ids;
  std::stack<std::shared_ptr<const JitExpression>> stack;
  stack.push(_expression);
  bool has_or = false;
  while (!stack.empty()) {
    auto current = stack.top();
    stack.pop();
    if (auto right_child = current->right_child()) stack.push(right_child);
    if (auto left_child = current->left_child()) stack.push(left_child);
    if (current->expression_type() == JitExpressionType::Or) {
      has_or = true;
    }
    if (current->expression_type() == JitExpressionType::Column) {
      const auto tuple_index = current->result().tuple_index();
      if (has_or) {
        column_ids[tuple_index] = !column_ids.count(tuple_index);
      } else {
        column_ids[tuple_index] = true;
      }
    }
  }
  return column_ids;
}

void JitCompute::set_load_column(const size_t tuple_id, const std::shared_ptr<BaseJitSegmentReaderWrapper> _input_segment_wrapper, const bool also_set) const {
  std::stack<std::shared_ptr<const JitExpression>> stack;
  size_t counter = 0;
  stack.push(_expression);
  while (!stack.empty()) {
    auto current = stack.top();
    stack.pop();
    if (auto right_child = current->right_child()) stack.push(right_child);
    if (auto left_child = current->left_child()) stack.push(left_child);
    if (current->expression_type() == JitExpressionType::Column) {
      const auto tuple_index = current->result().tuple_index();
      if (tuple_id == tuple_index) {
        ++counter;
      }
    }
  }

  stack.push(_expression);
  while (!stack.empty()) {
    auto current = stack.top();
    stack.pop();
    if (auto right_child = current->right_child()) stack.push(right_child);
    if (auto left_child = current->left_child()) stack.push(left_child);
    if (current->expression_type() == JitExpressionType::Column) {
      const auto tuple_index = current->result().tuple_index();
      if (tuple_id == tuple_index) {
        current->set_load_column(_input_segment_wrapper, counter || also_set);
        return;
      }
    }
  }
}

void JitCompute::_consume(JitRuntimeContext& context) const {
#if LESS_JIT_CONTEXT
  switch (_expression->result().data_type()) {
    BOOST_PP_SEQ_FOR_EACH_PRODUCT(JIT_COMPUTE_CASE_AND_GET, (JIT_DATA_TYPE_INFO))
    case DataType::Null:
      break;
  }
#else
  _expression->compute(context);
#endif
  _emit(context);
}

#undef JIT_GET_ENUM_VALUE
#undef JIT_GET_DATA_TYPE
#undef JIT_COMPUTE_CASE_AND_GET

}  // namespace opossum
