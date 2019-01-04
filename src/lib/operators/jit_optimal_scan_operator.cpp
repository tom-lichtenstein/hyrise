#include "jit_optimal_scan_operator.hpp"

#include <functional>

#include "abstract_read_only_operator.hpp"
#include "concurrency/transaction_context.hpp"
#include "operators/jit_operator/jit_operations.hpp"
#include "operators/jit_operator/operators/jit_aggregate.hpp"
#include "operators/jit_operator/operators/jit_read_value.hpp"
#include "operators/jit_operator/operators/jit_read_tuples.hpp"
#include "operators/jit_operator/operators/jit_write_offset.hpp"
#include "storage/dictionary_segment/dictionary_segment_iterable.hpp"
#include "storage/reference_segment.hpp"
#include "storage/storage_manager.hpp"
#include "validate.hpp"
#include "storage/value_segment/value_segment_iterable.hpp"
#include "jit_evaluation_helper.hpp"
#include "utils/timer.hpp"

namespace opossum {

const std::string JitOptimalScanOperator::name() const { return "JitOperatorWrapper"; }

std::shared_ptr<const Table> JitOptimalScanOperator::_on_execute() {
  // std::cerr << "Using custom jit scan operator" << std::endl;

  // SELECT A FROM TABLE_SCAN WHERE A < 50000

  const auto table = StorageManager::get().get_table("TABLE_SCAN");


  using OwnJitSegmentReader = JitSegmentReader<ValueSegmentIterable<int32_t>::NonNullIterator, int32_t, false>;
  // using DictIterator = DictionarySegmentIterable<int32_t, pmr_vector<int>>::Iterator<std::vector<uint32_t>::const_iterator>;
  // using OwnDictionaryReader = JitSegmentReader<DictIterator, JitValueID, false>;
  using OwnDictionaryReader = JitSegmentReader<AttributeVectorIterable::Iterator<std::vector<uint32_t>::const_iterator>, JitValueID, false>;

  const auto col_a = table->column_id_by_name("A");
  bool dict_segment = false;
  if (!table->chunks().empty()) {
    const auto segment = table->get_chunk(ChunkID(0))->get_segment(col_a);
    dict_segment = static_cast<bool>(std::dynamic_pointer_cast<const BaseEncodedSegment>(segment));
  }

  {
    JitRuntimeContext context;
    if (transaction_context_is_set()) {
      context.transaction_id = transaction_context()->transaction_id();
      context.snapshot_commit_id = transaction_context()->snapshot_commit_id();
    }
    auto read_tuples = JitReadTuples(true);
    const auto input_col_tuple = read_tuples.add_input_column(DataType::Int, false, col_a, dict_segment);
    // const auto col_x = right_table->column_id_by_name("X100000");
    // constexpr auto a_id = 0;
    constexpr int32_t val = 50000;
    const auto variant = AllTypeVariant(val);
    const auto input_literal_tuple = read_tuples.add_literal_value(variant, dict_segment);
    constexpr auto l_id = 1;
    const auto tmp = read_tuples.add_temporary_value();

    if (dict_segment) {
      const auto jit_expression =
              std::make_shared<JitExpression>(std::make_shared<JitExpression>(input_col_tuple), JitExpressionType::LessThan,
                                              std::make_shared<JitExpression>(input_literal_tuple, variant, dict_segment), tmp);

      read_tuples.add_value_id_predicate(*jit_expression);

    }

    // constexpr auto tmp_id = 2;
    // const auto tpl =

    // const auto l_id = read_tuples.add_literal_value(0);
    read_tuples.before_query(*table, std::vector<AllTypeVariant>(), context);

    // auto expected_entries = table->row_count();

    auto write = JitWriteOffset();
    JitOutputReferenceColumn left_ref_col{"A", DataType::Int, false, col_a};
    write.add_output_column(left_ref_col);
    auto out_table = write.create_output_table(table->max_chunk_size());
    write.before_query(*table, *out_table, context);

    // Timer timer;

    if (dict_segment) {
      for (opossum::ChunkID chunk_id{0}; chunk_id < table->chunk_count(); ++chunk_id) {
        read_tuples.before_chunk(*table, chunk_id, std::vector<AllTypeVariant>(), context);

        const auto chunk_size = context.chunk_size;
        auto& chunk_offset = context.chunk_offset;
        const auto _chunk_id = context.chunk_id;

        auto casted_ptr = static_cast<OwnDictionaryReader*>(context.inputs.front().get());
        const int32_t value = context.tuple.get<int>(l_id);

        auto output_pos_list = context.output_pos_list;

        for (; chunk_offset < chunk_size; ++chunk_offset) {
          // static_cast dynamic_cast
          // const auto casted_ptr = context.inputs.front().get();
          if (casted_ptr->read_and_get_value(context, int32_t()).value < value) {
            output_pos_list->emplace_back(_chunk_id, chunk_offset);
          }
        }

        write.after_chunk(table, *out_table, context);
      }
    } else {
      for (opossum::ChunkID chunk_id{0}; chunk_id < table->chunk_count(); ++chunk_id) {
        read_tuples.before_chunk(*table, chunk_id, std::vector<AllTypeVariant>(), context);

        const auto chunk_size = context.chunk_size;
        auto& chunk_offset = context.chunk_offset;
        const auto _chunk_id = context.chunk_id;

        auto casted_ptr = static_cast<OwnJitSegmentReader*>(context.inputs.front().get());
        auto output_pos_list = context.output_pos_list;

        for (; chunk_offset < chunk_size; ++chunk_offset) {
          // static_cast dynamic_cast
          if (casted_ptr->read_and_get_value(context, int32_t()).value < val) {
            output_pos_list->emplace_back(_chunk_id, chunk_offset);
          }
        }

        write.after_chunk(table, *out_table, context);
      }
    }

    // std::chrono::nanoseconds scan = timer.lap();

    write.after_query(*out_table, context);

    /*
    auto& operators = JitEvaluationHelper::get().result()["operators"];
    auto add_time = [&operators](const std::string& name, const auto& time) {
      const auto micro_s = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
      if (micro_s > 0) {
        nlohmann::json jit_op = {{"name", name}, {"prepare", false}, {"walltime", micro_s}};
        operators.push_back(jit_op);
      }
    };

    add_time("_table_scan", scan);
     */

    return out_table;
  }
}

}  // namespace opossum
