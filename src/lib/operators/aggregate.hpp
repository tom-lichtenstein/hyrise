#pragma once

#include <boost/container/pmr/polymorphic_allocator.hpp>
#include <boost/container/scoped_allocator.hpp>
#include <boost/container/small_vector.hpp>
#include <boost/functional/hash.hpp>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "abstract_read_only_operator.hpp"
#include "expression/aggregate_expression.hpp"
#include "resolve_type.hpp"
#include "storage/abstract_segment_visitor.hpp"
#include "storage/reference_segment.hpp"
#include "storage/value_segment.hpp"
#include "types.hpp"
#include "utils/assert.hpp"

namespace opossum {

struct GroupByContext;

/**
 * Aggregates are defined by the column (ColumnID for Operators, LQPColumnReference in LQP) they operate on and the aggregate
 * function they use. COUNT() is the exception that doesn't use a column, which is why column is optional
 * Optionally, an alias can be specified to use as the output name.
 */
struct AggregateColumnDefinition final {
  AggregateColumnDefinition(const std::optional<ColumnID>& column, const AggregateFunction function)
      : column(column), function(function) {}

  std::optional<ColumnID> column;
  AggregateFunction function;
};

/*
Operator to aggregate columns by certain functions, such as min, max, sum, average, and count. The output is a table
 with reference segments. As with most operators we do not guarantee a stable operation with regards to positions -
 i.e. your sorting order.

For implementation details, please check the wiki: https://github.com/hyrise/hyrise/wiki/Aggregate-Operator
*/

/*
Current aggregated value and the number of rows that were used.
The latter is used for AVG and COUNT.
*/
template <typename AggregateType>
struct AggregateResult {
  std::optional<AggregateType> current_aggregate;
  size_t aggregate_count = 0;
  RowID row_id;
};

/*
The key type that is used for the aggregation map.
*/
using AggregateKeyEntry = uint64_t;

using AggregateKey = boost::container::small_vector<AggregateKeyEntry, 2>;

template <typename AggregateType>
using ResultMapAllocator = PolymorphicAllocator<std::pair<const AggregateKey, AggregateResult<AggregateType>>>;

template <typename AggregateType>
using AggregateResultMap = std::unordered_map<AggregateKey, AggregateResult<AggregateType>, std::hash<AggregateKey>,
                                              std::equal_to<AggregateKey>, ResultMapAllocator<AggregateType>>;

using AggregateKeys = pmr_vector<AggregateKey>;

using KeysPerChunk = pmr_vector<AggregateKeys>;

/**
 * Types that are used for the special COUNT(*) and DISTINCT implementations. Using 'small' types for the
 * Distinct*Types might save a handful of bytes somewhere, but increases the number of template instantiations
 */
using CountColumnType = int32_t;
using CountAggregateType = int64_t;
using DistinctColumnType = int32_t;
using DistinctAggregateType = int32_t;

/**
 * Note: Aggregate does not support null values at the moment
 */
class Aggregate : public AbstractReadOnlyOperator {
 public:
  Aggregate(const std::shared_ptr<AbstractOperator>& in, const std::vector<AggregateColumnDefinition>& aggregates,
            const std::vector<ColumnID>& groupby_column_ids);

  const std::vector<AggregateColumnDefinition>& aggregates() const;
  const std::vector<ColumnID>& groupby_column_ids() const;

  const std::string name() const override;
  const std::string description(DescriptionMode description_mode) const override;

  // write the aggregated output for a given aggregate column
  template <typename ColumnType, AggregateFunction function>
  void write_aggregate_output(ColumnID column_index);

 protected:
  std::shared_ptr<const Table> _on_execute() override;

  std::shared_ptr<AbstractOperator> _on_deep_copy(
      const std::shared_ptr<AbstractOperator>& copied_input_left,
      const std::shared_ptr<AbstractOperator>& copied_input_right) const override;

  void _on_set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) override;

  void _on_cleanup() override;

  template <typename ColumnType>
  void _write_aggregate_output(boost::hana::basic_type<ColumnType> type, ColumnID column_index,
                               AggregateFunction function);

  void _write_groupby_output(PosList& pos_list);

  template <typename ColumnType, AggregateFunction function>
  void _aggregate_segment(ChunkID chunk_id, ColumnID column_index, const BaseSegment& base_segment,
                          const KeysPerChunk& keys_per_chunk);

  std::shared_ptr<SegmentVisitorContext> _create_aggregate_context(const DataType data_type,
                                                                   const AggregateFunction function) const;

  const std::vector<AggregateColumnDefinition> _aggregates;
  const std::vector<ColumnID> _groupby_column_ids;

  TableColumnDefinitions _output_column_definitions;
  Segments _output_segments;

  pmr_vector<std::shared_ptr<BaseValueSegment>> _groupby_segments;
  std::vector<std::shared_ptr<SegmentVisitorContext>> _contexts_per_column;
};

}  // namespace opossum

namespace std {
template <>
struct hash<opossum::AggregateKey> {
  size_t operator()(const opossum::AggregateKey& key) const { return boost::hash_range(key.begin(), key.end()); }
};

}  // namespace std
