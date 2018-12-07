#include "jit_compute.hpp"

// #include <x86intrin.h>

#include "../jit_constant_mappings.hpp"
#include "../jit_operations.hpp"
#include "../jit_types.hpp"
#include "jit_read_tuples.hpp"
#include "jit_segment_reader.hpp"

namespace opossum {

JitExpression::JitExpression(const JitTupleValue& tuple_value, const AllTypeVariant& variant, const bool disable_variant)
    : _expression_type{JitExpressionType::Column}, _result_value{tuple_value}, _is_null(variant_is_null(variant)), _disable_variant(disable_variant) {
  if (!variant_is_null(variant)) {
    resolve_data_type(data_type_from_all_type_variant(variant), [&](const auto current_data_type_t) {
      using CurrentType = typename decltype(current_data_type_t)::type;
      set<CurrentType>(boost::get<CurrentType>(variant));
      using Bool = int32_t;
      if constexpr (std::is_same_v<CurrentType, Bool>) {
        set<bool>(boost::get<CurrentType>(variant));
      }
    });
  }
}

JitExpression::JitExpression(const std::shared_ptr<const JitExpression>& child, const JitExpressionType expression_type,
                             const size_t result_tuple_index)
    : _left_child{child},
      _expression_type{expression_type},
      _result_value{JitTupleValue(_compute_result_type(), result_tuple_index)} {}

JitExpression::JitExpression(const std::shared_ptr<const JitExpression>& left_child,
                             const JitExpressionType expression_type,
                             const std::shared_ptr<const JitExpression>& right_child, const size_t result_tuple_index)
    : _left_child{left_child},
      _right_child{right_child},
      _expression_type{expression_type},
      _result_value{JitTupleValue(_compute_result_type(), result_tuple_index)} {}

std::string JitExpression::to_string() const {
  if (_expression_type == JitExpressionType::Column) {
    std::string load_column;
#if JIT_LAZY_LOAD
    if (_load_column) {
#if JIT_READER_WRAPPER
      load_column = " (Using input reader #" + std::to_string(_input_segment_wrapper->reader_index) + ")";
#else
      load_column = " (Using input reader #" + std::to_string(_reader_index) + ")";
#endif
    };
#endif
    return "x" + std::to_string(_result_value.tuple_index()) + load_column;
  }

  const auto left = _left_child->to_string() + " ";
  const auto right = _right_child ? _right_child->to_string() + " " : "";
  return "(" + left + jit_expression_type_to_string.left.at(_expression_type) + " " + right + ")";
}

void JitExpression::compute(JitRuntimeContext& context) const {
  // We are dealing with an already computed value here, so there is nothing to do.
  if (_expression_type == JitExpressionType::Column) {
#if JIT_LAZY_LOAD
#if JIT_READER_WRAPPER
    if (_load_column) _input_segment_wrapper->read_value(context);
#else
    if (_load_column) context.inputs[_reader_index]->read_value(context);
#endif
#endif
    return;
  }

  _left_child->compute(context);

  if (!jit_expression_is_binary(_expression_type)) {
    switch (_expression_type) {
      case JitExpressionType::Not:
        jit_not(_left_child->result(), _result_value, context);
        break;
      case JitExpressionType::IsNull:
        jit_is_null(_left_child->result(), _result_value, context);
        break;
      case JitExpressionType::IsNotNull:
        jit_is_not_null(_left_child->result(), _result_value, context);
        break;
      default:
        Fail("Expression type is not supported.");
    }
    return;
  }

  // Check, whether right side can be pruned
  // AND: false and true/false/null = false
  // OR:  true  or  true/false/null = true
#if JIT_LOGICAL_PRUNING
  if (_expression_type == JitExpressionType::And && !_left_child->result().is_null(context) &&
      !_left_child->result().get<bool>(context)) {
    return jit_and(_left_child->result(), _right_child->result(), _result_value, context, true);
  } else if (_expression_type == JitExpressionType::Or && !_left_child->result().is_null(context) &&
             _left_child->result().get<bool>(context)) {
    return jit_or(_left_child->result(), _right_child->result(), _result_value, context, true);
  }
#endif

  _right_child->compute(context);

  if (_left_child->result().data_type() == DataType::String) {
    switch (_expression_type) {
      case JitExpressionType::Equals:
        jit_compute(jit_string_equals, _left_child->result(), _right_child->result(), _result_value, context);
        return;
      case JitExpressionType::NotEquals:
        jit_compute(jit_string_not_equals, _left_child->result(), _right_child->result(), _result_value, context);
        return;
      case JitExpressionType::GreaterThan:
        jit_compute(jit_string_greater_than, _left_child->result(), _right_child->result(), _result_value, context);
        return;
      case JitExpressionType::GreaterThanEquals:
        jit_compute(jit_string_greater_than_equals, _left_child->result(), _right_child->result(), _result_value,
                    context);
        return;
      case JitExpressionType::LessThan:
        jit_compute(jit_string_less_than, _left_child->result(), _right_child->result(), _result_value, context);
        return;
      case JitExpressionType::LessThanEquals:
        jit_compute(jit_string_less_than_equals, _left_child->result(), _right_child->result(), _result_value, context);
        return;
      default:
        break;
    }
  }
//  { __rdtsc();}
  switch (_expression_type) {
    case JitExpressionType::Addition:
      jit_compute(jit_addition, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Subtraction:
      jit_compute(jit_subtraction, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Multiplication:
      jit_compute(jit_multiplication, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Division:
      jit_compute(jit_division, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Modulo:
      jit_compute(jit_modulo, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Power:
      jit_compute(jit_power, _left_child->result(), _right_child->result(), _result_value, context);
      break;

    case JitExpressionType::Equals:
      jit_compute(jit_equals, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::NotEquals:
      jit_compute(jit_not_equals, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::GreaterThan:
      jit_compute(jit_greater_than, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::GreaterThanEquals:
      jit_compute(jit_greater_than_equals, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::LessThan:
      jit_compute(jit_less_than, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::LessThanEquals:
      jit_compute(jit_less_than_equals, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::Like:
      jit_compute(jit_like, _left_child->result(), _right_child->result(), _result_value, context);
      break;
    case JitExpressionType::NotLike:
      jit_compute(jit_not_like, _left_child->result(), _right_child->result(), _result_value, context);
      break;

    case JitExpressionType::And:
#if JIT_LOGICAL_PRUNING
      jit_and(_left_child->result(), _right_child->result(), _result_value, context, false);
#else
      jit_and(_left_child->result(), _right_child->result(), _result_value, context);
#endif
      break;
    case JitExpressionType::Or:
#if JIT_LOGICAL_PRUNING
      jit_or(_left_child->result(), _right_child->result(), _result_value, context, false);
#else
      jit_or(_left_child->result(), _right_child->result(), _result_value, context);
#endif
      break;
    default:
      Fail("Expression type is not supported.");
  }
//  {unsigned int dummy; __rdtscp(&dummy);}

}

std::pair<const DataType, const bool> JitExpression::_compute_result_type() {
  if (!jit_expression_is_binary(_expression_type)) {
    switch (_expression_type) {
      case JitExpressionType::Not:
        return std::make_pair(DataType::Bool, _left_child->result().is_nullable());
      case JitExpressionType::IsNull:
      case JitExpressionType::IsNotNull:
        return std::make_pair(DataType::Bool, false);
      default:
        Fail("Expression type not supported.");
    }
  }

  DataType result_data_type;
  switch (_expression_type) {
    case JitExpressionType::Addition:
      result_data_type =
          jit_compute_type(jit_addition, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Subtraction:
      result_data_type =
          jit_compute_type(jit_subtraction, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Multiplication:
      result_data_type =
          jit_compute_type(jit_multiplication, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Division:
      result_data_type =
          jit_compute_type(jit_division, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Modulo:
      result_data_type =
          jit_compute_type(jit_modulo, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Power:
      result_data_type =
          jit_compute_type(jit_power, _left_child->result().data_type(), _right_child->result().data_type());
      break;
    case JitExpressionType::Equals:
    case JitExpressionType::NotEquals:
    case JitExpressionType::GreaterThan:
    case JitExpressionType::GreaterThanEquals:
    case JitExpressionType::LessThan:
    case JitExpressionType::LessThanEquals:
    case JitExpressionType::Like:
    case JitExpressionType::NotLike:
    case JitExpressionType::And:
    case JitExpressionType::Or:
      result_data_type = DataType::Bool;
      break;
    default:
      Fail("Expression type not supported.");
  }

  return std::make_pair(result_data_type, _left_child->result().is_nullable() || _right_child->result().is_nullable());
}

template <typename T>
Value<T> JitExpression::compute_and_get(JitRuntimeContext& context) const {
  if (_expression_type == JitExpressionType::Column) {
#if JIT_LAZY_LOAD
    if (_load_column) {
#if JIT_READER_WRAPPER
      return _input_segment_wrapper->read_and_get_value(context, T());
#else
      return context.inputs[_reader_index]->read_and_get_value(context, T());
#endif
    }
#endif
    if (_result_value.data_type() == DataType::Null) return {true, T()};
    if (!_disable_variant) {
      return {_is_null, get<T>()};
    }
    return {_result_value.is_null(context), _result_value.get<T>(context)};
  }

  if (!jit_expression_is_binary(_expression_type)) {
    switch (_expression_type) {
      case JitExpressionType::Not:
        return jit_compute_unary_and_get<T>(jit_not_and_get, _left_child, context);
      case JitExpressionType::IsNull:
        return jit_compute_unary_and_get<T>(jit_is_null_and_get, _left_child, context);
      case JitExpressionType::IsNotNull:
        return jit_compute_unary_and_get<T>(jit_is_not_null_and_get, _left_child, context);
      default:
        Fail("Expression type is not supported.");
    }
  }

  if (_left_child->result().data_type() == DataType::String) {
    switch (_expression_type) {
      case JitExpressionType::Equals:
        return jit_compute_and_get<T>(jit_string_equals, _left_child, _right_child, context);
      case JitExpressionType::NotEquals:
        return jit_compute_and_get<T>(jit_string_not_equals, _left_child, _right_child, context);
      case JitExpressionType::GreaterThan:
        return jit_compute_and_get<T>(jit_string_greater_than, _left_child, _right_child, context);
      case JitExpressionType::GreaterThanEquals:
        return jit_compute_and_get<T>(jit_string_greater_than_equals, _left_child, _right_child, context);
      case JitExpressionType::LessThan:
        return jit_compute_and_get<T>(jit_string_less_than, _left_child, _right_child, context);
      case JitExpressionType::LessThanEquals:
        return jit_compute_and_get<T>(jit_string_less_than_equals, _left_child, _right_child, context);
      case JitExpressionType::Like:
        return jit_compute_and_get<T>(jit_like, _left_child, _right_child, context);
      case JitExpressionType::NotLike:
        return jit_compute_and_get<T>(jit_not_like, _left_child, _right_child, context);
      default:
        Fail("Expression type not supported.");
    }
  }

  switch (_expression_type) {
    case JitExpressionType::Addition:
      return jit_compute_and_get<T>(jit_addition, _left_child, _right_child, context);
    case JitExpressionType::Subtraction:
      return jit_compute_and_get<T>(jit_subtraction, _left_child, _right_child, context);
    case JitExpressionType::Multiplication:
      return jit_compute_and_get<T>(jit_multiplication, _left_child, _right_child, context);
    case JitExpressionType::Division:
      return jit_compute_and_get<T>(jit_division, _left_child, _right_child, context);
    case JitExpressionType::Modulo:
      return jit_compute_and_get<T>(jit_modulo, _left_child, _right_child, context);
    case JitExpressionType::Power:
      return jit_compute_and_get<T>(jit_power, _left_child, _right_child, context);
    case JitExpressionType::Equals:
     return jit_compute_and_get<T>(jit_equals, _left_child, _right_child, context);
    case JitExpressionType::NotEquals:
      return jit_compute_and_get<T>(jit_not_equals, _left_child, _right_child, context);
    case JitExpressionType::GreaterThan:
      return jit_compute_and_get<T>(jit_greater_than, _left_child, _right_child, context);
    case JitExpressionType::GreaterThanEquals:
      return jit_compute_and_get<T>(jit_greater_than_equals, _left_child, _right_child, context);
    case JitExpressionType::LessThan:
      return jit_compute_and_get<T>(jit_less_than, _left_child, _right_child, context);
    case JitExpressionType::LessThanEquals:
      return jit_compute_and_get<T>(jit_less_than_equals, _left_child, _right_child, context);

    case JitExpressionType::And:
      return jit_and_get<T>(_left_child, _right_child, context);
    case JitExpressionType::Or:
      return jit_or_get<T>(_left_child, _right_child, context);
    default:
      Fail("Expression type is not supported.");
  }
}

#define INSTANTIATE_FUNCTION(r, d, type) \
   template Value<BOOST_PP_TUPLE_ELEM(3, 0, type)> JitExpression::compute_and_get<BOOST_PP_TUPLE_ELEM(3, 0, type)>(JitRuntimeContext& context) const;
BOOST_PP_SEQ_FOR_EACH(INSTANTIATE_FUNCTION, _, JIT_DATA_TYPE_INFO)

}  // namespace opossum
