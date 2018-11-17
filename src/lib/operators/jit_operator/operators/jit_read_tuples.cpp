#include "jit_read_tuples.hpp"

#include <string>

#include "../jit_types.hpp"
#include "../jit_utils.hpp"
#include "constant_mappings.hpp"
#include "expression/evaluation/expression_evaluator.hpp"
#include "jit_expression.hpp"
#include "resolve_type.hpp"
#include "storage/create_iterable_from_segment.hpp"

namespace opossum {

JitReadTuples::JitReadTuples(const bool has_validate, const std::shared_ptr<AbstractExpression>& row_count_expression)
    : _has_validate(has_validate), _row_count_expression(row_count_expression) {}

std::string JitReadTuples::description() const {
  std::stringstream desc;
  desc << "[ReadTuple] ";
  for (const auto& input_column : _input_columns) {
    desc << "(" << (input_column.use_value_id ? "(V) " : "")
         << (input_column.data_type == DataType::Bool ? "Bool" : data_type_to_string.left.at(input_column.data_type))
         << " x" << input_column.tuple_value.tuple_index() << " = Column#" << input_column.column_id << "), ";
  }
  for (const auto& input_literal : _input_literals) {
    desc << (input_literal.use_value_id ? "(V) " : "")
         << (input_literal.tuple_value.data_type() == DataType::Null
                 ? "null"
                 : data_type_to_string.left.at(input_literal.tuple_value.data_type()))
         << " x" << input_literal.tuple_value.tuple_index() << " = " << input_literal.value << ", ";
  }
  for (const auto& input_parameter : _input_parameters) {
    desc << (input_parameter.use_value_id ? "(V) " : "")
         << (input_parameter.tuple_value.data_type() == DataType::Null
                 ? "null"
                 : data_type_to_string.left.at(input_parameter.tuple_value.data_type()))
         << " x" << input_parameter.tuple_value.tuple_index() << " = Par#" << input_parameter.parameter_id << ", ";
  }
  return desc.str();
}

void JitReadTuples::before_query(const Table& in_table, const std::vector<AllTypeVariant>& parameter_values,
                                 JitRuntimeContext& context) const {
  // Create a runtime tuple of the appropriate size
  context.tuple.resize(_num_tuple_values);
  if (_row_count_expression) {
    const auto num_rows_expression_result =
        ExpressionEvaluator{}.evaluate_expression_to_result<int64_t>(*_row_count_expression);
    context.limit_rows = static_cast<size_t>(num_rows_expression_result->value(0));
  } else {
    context.limit_rows = std::numeric_limits<size_t>::max();
  }

  const auto set_value_from_input = [&context](const JitTupleValue& tuple_value, const AllTypeVariant& value) {
    auto data_type = tuple_value.data_type();
    if (data_type == DataType::Null) {
      tuple_value.set_is_null(true, context);
    } else {
      resolve_data_type(data_type, [&](auto type) {
        using TupleDataType = typename decltype(type)::type;
        tuple_value.set<TupleDataType>(boost::get<TupleDataType>(value), context);
        if (tuple_value.is_nullable()) {
          tuple_value.set_is_null(variant_is_null(value), context);
        }
        // Non-jit operators store bool values as int values
        if constexpr (std::is_same_v<TupleDataType, Bool>) {
          tuple_value.set<bool>(boost::get<TupleDataType>(value), context);
        }
      });
    }
  };

  // Copy all input literals to the runtime tuple
  for (const auto& input_literal : _input_literals) {
    if (!input_literal.use_value_id) set_value_from_input(input_literal.tuple_value, input_literal.value);
  }
  // Copy all parameter values to the runtime tuple
  DebugAssert(_input_parameters.size() == parameter_values.size(), "Wrong number of parameters");
  auto parameter_value_itr = parameter_values.cbegin();
  for (const auto& input_parameter : _input_parameters) {
    if (!input_parameter.use_value_id) set_value_from_input(input_parameter.tuple_value, *parameter_value_itr++);
  }
}

void JitReadTuples::before_chunk(const Table& in_table, const ChunkID chunk_id,
                                 const std::vector<AllTypeVariant>& parameter_values,
                                 JitRuntimeContext& context) const {
  const auto& in_chunk = *in_table.get_chunk(chunk_id);
  context.inputs.clear();
  context.chunk_offset = 0;
  context.chunk_size = in_chunk.size();
  context.chunk_id = chunk_id;

  if (_has_validate) {
    if (in_chunk.has_mvcc_data()) {
      // materialize atomic transaction ids as specialization cannot handle atomics
      context.row_tids.resize(in_chunk.mvcc_data()->tids.size());
      auto itr = context.row_tids.begin();
      for (const auto& tid : in_chunk.mvcc_data()->tids) {
        *itr++ = tid.load();
      }
      // Lock MVCC data before accessing it.
      context.mvcc_data_lock =
          std::make_unique<SharedScopedLockingPtr<const MvccData>>(in_chunk.get_scoped_mvcc_data_lock());
      context.mvcc_data = in_chunk.mvcc_data();
    } else {
      DebugAssert(in_chunk.references_exactly_one_table(),
                  "Input to Validate contains a Chunk referencing more than one table.");
      const auto& ref_col_in = std::dynamic_pointer_cast<const ReferenceSegment>(in_chunk.get_segment(ColumnID{0}));
      context.referenced_table = ref_col_in->referenced_table();
      context.pos_list = ref_col_in->pos_list();
    }
  }

  // Create the segment iterator for each input segment and store them to the runtime context
  for (const auto& input_column : _input_columns) {
    const auto column_id = input_column.column_id;
    const auto segment = in_chunk.get_segment(column_id);
    const auto is_nullable = in_table.column_is_nullable(column_id);
    if (input_column.use_value_id) {
      const auto dictionary_segment = std::dynamic_pointer_cast<BaseDictionarySegment>(segment);
      DebugAssert(dictionary_segment, "Segment is not a dictionary");
      create_iterable_from_attribute_vector(*dictionary_segment).with_iterators([&](auto it, auto end) {
        using IteratorType = decltype(it);
        if (is_nullable) {
          context.inputs.push_back(
              std::make_shared<JitSegmentReader<IteratorType, JitValueID, true>>(it, input_column.tuple_value));
        } else {
          context.inputs.push_back(
              std::make_shared<JitSegmentReader<IteratorType, JitValueID, false>>(it, input_column.tuple_value));
        }
      });
    } else {
      if (input_column.tuple_value.data_type() == DataType::Bool) {
        DebugAssert(!is_nullable, "Bool column is null");
        resolve_segment_type<Bool>(*segment, [&](auto& typed_segment) {
          create_iterable_from_segment<Bool>(typed_segment).with_iterators([&](auto it, auto end) {
            using IteratorType = decltype(it);
            // If Data type is bool, the input column is a compute non-null int column
            context.inputs.push_back(
                std::make_shared<JitSegmentReader<IteratorType, bool, false>>(it, input_column.tuple_value));
          });
        });
      } else {
        resolve_data_and_segment_type(*segment, [&](auto type, auto& typed_segment) {
          using ColumnDataType = typename decltype(type)::type;
          create_iterable_from_segment<ColumnDataType>(typed_segment).with_iterators([&](auto it, auto end) {
            using IteratorType = decltype(it);
            if (is_nullable) {
              context.inputs.push_back(
                  std::make_shared<JitSegmentReader<IteratorType, ColumnDataType, true>>(it, input_column.tuple_value));
            } else {
              context.inputs.push_back(std::make_shared<JitSegmentReader<IteratorType, ColumnDataType, false>>(
                  it, input_column.tuple_value));
            }
          });
        });
      }
    }
  }

  for (const auto& value_id_predicate : _value_id_predicates) {
    const auto& input_column = _input_columns[value_id_predicate.input_column_index];
    const auto segment = in_chunk.get_segment(input_column.column_id);
    const auto dictionary = std::dynamic_pointer_cast<BaseDictionarySegment>(segment);
    AllTypeVariant value;
    size_t tuple_index;
    if (value_id_predicate.input_literal_index) {
      const auto& literal = _input_literals[*value_id_predicate.input_literal_index];
      value = literal.value;
      tuple_index = literal.tuple_value.tuple_index();
    } else {
      DebugAssert(value_id_predicate.input_parameter_index, "Neither input literal nor parameter index have been set.");
      const auto& parameter = _input_parameters[*value_id_predicate.input_parameter_index];
      value = parameter_values[*value_id_predicate.input_parameter_index];
      tuple_index = parameter.tuple_value.tuple_index();
    }
    const auto casted_value = cast_all_type_variant_to_type(value, input_column.data_type);

    ValueID value_id;
    switch (value_id_predicate.expression_type) {
      case JitExpressionType::Equals:
      case JitExpressionType::NotEquals:
        // check if value exists in segment
        if (dictionary->lower_bound(casted_value) == dictionary->upper_bound(casted_value)) {
          value_id = INVALID_VALUE_ID;
          break;
        }
      case JitExpressionType::LessThan:
      case JitExpressionType::GreaterThanEquals:
        value_id = dictionary->lower_bound(casted_value);
        break;
      case JitExpressionType::LessThanEquals:
      case JitExpressionType::GreaterThan:
        value_id = dictionary->upper_bound(casted_value);
        break;
      default:
        Fail("Unsupported expression type for binary value id predicate");
    }
    if (value_id == INVALID_VALUE_ID) {
      value_id = std::numeric_limits<JitValueID>::max();
    } else if (static_cast<ValueID::base_type>(value_id) >= std::numeric_limits<JitValueID>::max()) {
      Fail("ValueID used too high.");
    }
    context.tuple.set<JitValueID>(tuple_index, value_id);
  }
}

void JitReadTuples::execute(JitRuntimeContext& context) const {
  for (; context.chunk_offset < context.chunk_size; ++context.chunk_offset) {
#if JIT_LAZY_LOAD
    _emit(context);
    // We advance all segment iterators, after processing the tuple with the next operators.
    /*
    for (const auto& input : context.inputs) {
      input->increment();
    }
    */
#else
    for (const auto& input : context.inputs) {
      input->read_value(context);
    }
    _emit(context);
#endif
  }
}

std::shared_ptr<AbstractExpression> JitReadTuples::row_count_expression() const { return _row_count_expression; }

JitTupleValue JitReadTuples::add_input_column(const DataType data_type, const bool is_nullable,
                                              const ColumnID column_id, const bool use_value_id) {
  // There is no need to add the same input column twice.
  // If the same column is requested for the second time, we return the JitTupleValue created previously.
  const auto it =
      std::find_if(_input_columns.begin(), _input_columns.end(), [column_id, use_value_id](const auto& input_column) {
        return input_column.column_id == column_id && input_column.use_value_id == use_value_id;
      });
  if (it != _input_columns.end()) {
    return it->tuple_value;
  }

  const auto tuple_value = JitTupleValue(use_value_id ? DataTypeValueID : data_type, is_nullable, _num_tuple_values++);
  _input_columns.push_back({column_id, data_type, tuple_value, use_value_id});
  return tuple_value;
}

JitTupleValue JitReadTuples::add_literal_value(const AllTypeVariant& value, const bool use_value_id) {
  // Somebody needs a literal value. We assign it a position in the runtime tuple and store the literal value,
  // so we can initialize the corresponding tuple value to the correct literal value later.
  const auto it = std::find_if(_input_literals.begin(), _input_literals.end(), [&value](const auto& literal_value) {
    return literal_value.value == value && !literal_value.use_value_id;
  });
  if (it != _input_literals.end()) {
    return it->tuple_value;
  }

  const auto data_type = data_type_from_all_type_variant(value);
  const auto tuple_value = JitTupleValue(use_value_id ? DataTypeValueID : data_type, variant_is_null(value), _num_tuple_values++);
  _input_literals.push_back({value, tuple_value, use_value_id});
  return tuple_value;
}

JitTupleValue JitReadTuples::add_parameter_value(const DataType data_type, const bool is_nullable,
                                                 const ParameterID parameter_id, const bool use_value_id) {
  const auto it =
      std::find_if(_input_parameters.begin(), _input_parameters.end(), [&parameter_id](const auto& parameter) {
        return parameter.parameter_id == parameter_id && !parameter.use_value_id;
      });
  if (it != _input_parameters.end()) {
    return it->tuple_value;
  }

  const auto tuple_value = JitTupleValue(use_value_id ? DataTypeValueID : data_type, is_nullable, _num_tuple_values++);
  _input_parameters.push_back({parameter_id, tuple_value, use_value_id});
  return tuple_value;
}

void JitReadTuples::add_value_id_predicate(const JitExpression& jit_expression) {
  DebugAssert(jit_expression_is_binary(jit_expression.expression_type()), "Only binary predicates should be added");

  const auto find = [](const auto& vector, const JitTupleValue& tuple_value) -> std::optional<size_t> {
    // iterate backwards as the to be found items should have been inserted last
    const auto itr = std::find_if(vector.crbegin(), vector.crend(), [&tuple_value](const auto& item) {
      return item.tuple_value == tuple_value && item.use_value_id;
    });
    if (itr != vector.crend()) {
      return std::distance(itr, vector.crend()) - 1;
    } else {
      return {};
    }
  };
  std::optional<size_t> column_id = find(_input_columns, jit_expression.left_child()->result());
  const bool swap = !column_id;
  if (swap) column_id = find(_input_columns, jit_expression.right_child()->result());

  auto& non_column_tuple_value = swap ? jit_expression.left_child()->result() : jit_expression.right_child()->result();
  std::optional<size_t> literal_id = find(_input_literals, non_column_tuple_value);
  std::optional<size_t> parameter_id;
  if (!literal_id) parameter_id = find(_input_parameters, non_column_tuple_value);

  auto expression = swap ? swap_expression_type(jit_expression.expression_type()) : jit_expression.expression_type();

  DebugAssert(literal_id || parameter_id, "Neither input literal nor parameter index have been set.");

  if (expression == JitExpressionType::GreaterThan) {
    jit_expression.set_expression_type(swap ? JitExpressionType::LessThan : JitExpressionType::GreaterThanEquals);
  } else if (expression == JitExpressionType::LessThanEquals) {
    jit_expression.set_expression_type(swap ? JitExpressionType::GreaterThanEquals : JitExpressionType::LessThan);
  }

  _value_id_predicates.push_back({*column_id, expression, literal_id, parameter_id});
}

size_t JitReadTuples::add_temporary_value() {
  // Somebody wants to store a temporary value in the runtime tuple. We don't really care about the value itself,
  // but have to remember to make some space for it when we create the runtime tuple.
  return _num_tuple_values++;
}

const std::vector<JitInputColumn>& JitReadTuples::input_columns() const { return _input_columns; }

const std::vector<JitInputLiteral>& JitReadTuples::input_literals() const { return _input_literals; }

const std::vector<JitInputParameter>& JitReadTuples::input_parameters() const { return _input_parameters; }

const std::vector<JitValueIDPredicate>& JitReadTuples::value_id_predicates() const { return _value_id_predicates; }

std::optional<ColumnID> JitReadTuples::find_input_column(const JitTupleValue& tuple_value) const {
  const auto it = std::find_if(_input_columns.begin(), _input_columns.end(), [&tuple_value](const auto& input_column) {
    return input_column.tuple_value == tuple_value;
  });

  if (it != _input_columns.end()) {
    return it->column_id;
  } else {
    return {};
  }
}

std::optional<AllTypeVariant> JitReadTuples::find_literal_value(const JitTupleValue& tuple_value) const {
  const auto it = std::find_if(_input_literals.begin(), _input_literals.end(), [&tuple_value](const auto& literal) {
    return literal.tuple_value.tuple_index() == tuple_value.tuple_index();
  });

  if (it != _input_literals.end()) {
    return it->value;
  } else {
    return {};
  }
}

}  // namespace opossum
