#include "jit_optimal_scan_operator.hpp"

#include <functional>

#include "abstract_read_only_operator.hpp"
#include "concurrency/transaction_context.hpp"
#include "operators/jit_operator/jit_operations.hpp"
#include "operators/jit_operator/operators/jit_aggregate.hpp"
#include "operators/jit_operator/operators/jit_read_value.hpp"
#include "operators/jit_operator/operators/jit_read_tuples.hpp"
#include "operators/jit_operator/operators/jit_write_offset.hpp"
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

  std::chrono::nanoseconds scan{0};

  using OwnJitSegmentReader = JitSegmentReader<ValueSegmentIterable<int32_t>::NonNullIterator, int32_t, false>;

  {
    JitRuntimeContext context;
    if (transaction_context_is_set()) {
      context.transaction_id = transaction_context()->transaction_id();
      context.snapshot_commit_id = transaction_context()->snapshot_commit_id();
    }
    auto read_tuples = JitReadTuples(true);
    const auto col_a = table->column_id_by_name("A");
    read_tuples.add_input_column(DataType::Int, false, col_a, false);
    // const auto col_x = right_table->column_id_by_name("X100000");
    // constexpr auto a_id = 0;
    constexpr int32_t val = 50000;
    read_tuples.add_literal_value(AllTypeVariant(val));
    // constexpr auto l_id = 1;
    read_tuples.add_temporary_value();
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

    Timer timer;

    for (opossum::ChunkID chunk_id{0}; chunk_id < table->chunk_count(); ++chunk_id) {
      read_tuples.before_chunk(*table, chunk_id, std::vector<AllTypeVariant>(), context);

      for (; context.chunk_offset < context.chunk_size; ++context.chunk_offset) {
        /*
        for (const auto input : context.inputs) {
          input->read_value(context);
        }
        */
        // context.inputs.front()->read_value(context);
        // static_cast<OwnJitSegmentReader*>(context.inputs.front().get())->read_value(context);
        /*

        const auto row_tid = context.mvcc_data->tids[context.chunk_offset].load();
        const auto begin_cid = context.mvcc_data->begin_cids[context.chunk_offset];
        const auto end_cid = context.mvcc_data->end_cids[context.chunk_offset];
        const auto our_tid = context.transaction_id;
        const auto snapshot_commit_id = context.snapshot_commit_id;
        bool is_visible = Validate::is_row_visible(our_tid, snapshot_commit_id, row_tid, begin_cid, end_cid);
        if (!is_visible) {
          continue;
        }
        */
        //context.tuple.set<int>(tmp_id, context.tuple.get<int>(a_id) < context.tuple.get<int>(l_id));
        //if (!context.tuple.get<int>(tmp_id)) {
        int32_t value = 50000;  // context.tuple.get<int>(l_id)
        if (! (static_cast<OwnJitSegmentReader*>(context.inputs.front().get())->read_and_get_value(context, int32_t()).value < value)) {
          continue;
        }

        context.output_pos_list->emplace_back(context.chunk_id, context.chunk_offset);
      }

      write.after_chunk(table, *out_table, context);
    }

    scan = timer.lap();

    write.after_query(*out_table, context);

    auto& operators = JitEvaluationHelper::get().result()["operators"];
    auto add_time = [&operators](const std::string& name, const auto& time) {
      const auto micro_s = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
      if (micro_s > 0) {
        nlohmann::json jit_op = {{"name", name}, {"prepare", false}, {"walltime", micro_s}};
        operators.push_back(jit_op);
      }
    };

    add_time("_table_scan", scan);

    return out_table;
  }
}

}  // namespace opossum
