#pragma once

#include "../jit_types.hpp"
#include "abstract_jittable.hpp"
#include "storage/chunk.hpp"
#include "storage/segment_iterables/create_iterable_from_attribute_vector.hpp"
#include "storage/table.hpp"
#include "jit_segment_reader.hpp"

namespace opossum {

class AbstractExpression;
class JitExpression;

// data_type and tuple_value._data_type are different for value id columns as data_type describes the actual type of the
// column and tuple_value._data_type describes the data type in the jit code which is DataTypeValueID for value ids.
struct JitInputColumn {
  ColumnID column_id;
  DataType data_type;
  JitTupleValue tuple_value;
  bool use_value_id;
};

struct JitInputLiteral {
  AllTypeVariant value;
  JitTupleValue tuple_value;
  bool use_value_id;
};

struct JitInputParameter {
  ParameterID parameter_id;
  JitTupleValue tuple_value;
  bool use_value_id;
};

struct JitValueIDPredicate {
  const size_t input_column_index;
  const JitExpressionType expression_type;
  const std::optional<size_t> input_literal_index;
  const std::optional<size_t> input_parameter_index;
};

class JitReadValue;

/* JitReadTuples must be the first operator in any chain of jit operators.
 * It is responsible for:
 * 1) storing literal values to the runtime tuple before the query is executed
 * 2) reading data from the the input table to the runtime tuple
 * 3) advancing the segment iterators
 * 4) keeping track of the number of values in the runtime tuple. Whenever
 *    another operator needs to store a temporary value in the runtime tuple,
 *    it can request a slot in the tuple from JitReadTuples.
 */
class JitReadTuples : public AbstractJittable {
 public:
  explicit JitReadTuples(const bool has_validate = false,
                         const std::shared_ptr<AbstractExpression>& row_count_expression = nullptr,
                         const bool use_load_atomic = true);

  std::string description() const final;

  virtual void before_query(const Table& in_table, const std::vector<AllTypeVariant>& parameter_values,
                            JitRuntimeContext& context);
  virtual bool before_chunk(const Table& in_table, const ChunkID chunk_id,
                            const std::vector<AllTypeVariant>& parameter_values, JitRuntimeContext& context);

  JitTupleValue add_input_column(const DataType data_type, const bool is_nullable, const ColumnID column_id,
                                 const bool use_value_id = false);
  JitTupleValue add_literal_value(const AllTypeVariant& value, const bool use_value_id = false);
  JitTupleValue add_parameter_value(const DataType data_type, const bool is_nullable, const ParameterID parameter_id,
                                    const bool use_value_id = false);
  void add_value_id_predicate(const JitExpression& jit_expression);
  size_t add_temporary_value();

  void set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters);

  const std::vector<std::shared_ptr<BaseJitSegmentReaderWrapper>>& input_wrappers() const;
  const std::vector<JitInputColumn>& input_columns() const;
  const std::vector<JitInputLiteral>& input_literals() const;
  const std::vector<JitInputParameter>& input_parameters() const;
  const std::vector<JitValueIDPredicate>& value_id_predicates() const;

  void create_default_input_wrappers();

  std::optional<ColumnID> find_input_column(const JitTupleValue& tuple_value) const;
  std::optional<AllTypeVariant> find_literal_value(const JitTupleValue& tuple_value) const;

  void execute(JitRuntimeContext& context) const;

  std::shared_ptr<AbstractExpression> row_count_expression() const;

  void add_input_segment_iterators(JitRuntimeContext& context, const Table& in_table, const Chunk& in_chunk, const bool prepare_wrapper);

 protected:
  uint32_t _num_tuple_values{0};
  std::vector<std::shared_ptr<BaseJitSegmentReaderWrapper>> _input_wrappers;
  std::map<DataType, uint32_t> _num_values;
  std::vector<JitInputColumn> _input_columns;
  std::vector<JitInputLiteral> _input_literals;
  std::vector<JitInputParameter> _input_parameters;
  std::vector<JitValueIDPredicate> _value_id_predicates;

 private:
  void _consume(JitRuntimeContext& context) const final {}
  const bool _has_validate;
  const std::shared_ptr<AbstractExpression> _row_count_expression;
  const bool _use_load_atomic;
};

}  // namespace opossum
