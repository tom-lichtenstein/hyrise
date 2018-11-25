#include "abstract_operator.hpp"

#if PAPI_SUPPORT
#include <papi.h>
#endif

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "abstract_read_only_operator.hpp"
#include "concurrency/transaction_context.hpp"
#include "global.hpp"
#include "jit_evaluation_helper.hpp"
#include "storage/table.hpp"
#include "utils/assert.hpp"
#include "utils/format_duration.hpp"
#include "utils/print_directed_acyclic_graph.hpp"
#include "utils/timer.hpp"
#include "utils/tracing/probes.hpp"

namespace opossum {

AbstractOperator::AbstractOperator(const OperatorType type, const std::shared_ptr<const AbstractOperator>& left,
                                   const std::shared_ptr<const AbstractOperator>& right,
                                   std::unique_ptr<OperatorPerformanceData> performance_data)
    : _type(type), _input_left(left), _input_right(right), _performance_data(std::move(performance_data)) {}

OperatorType AbstractOperator::type() const { return _type; }

const std::unordered_map<OperatorType, std::string> operator_type_to_string = {
    {OperatorType::Aggregate, "Aggregate"},
    {OperatorType::Alias, "Alias"},
    {OperatorType::Delete, "Delete"},
    {OperatorType::Difference, "Difference"},
    {OperatorType::ExportBinary, "ExportBinary"},
    {OperatorType::ExportCsv, "ExportCsv"},
    {OperatorType::GetTable, "GetTable"},
    {OperatorType::ImportBinary, "ImportBinary"},
    {OperatorType::ImportCsv, "ImportCsv"},
    {OperatorType::IndexScan, "IndexScan"},
    {OperatorType::Insert, "Insert"},
    {OperatorType::JitOperatorWrapper, "JitOperatorWrapper"},
    {OperatorType::JoinHash, "JoinHash"},
    {OperatorType::JoinIndex, "JoinIndex"},
    {OperatorType::JoinMPSM, "JoinMPSM"},
    {OperatorType::JoinNestedLoop, "JoinNestedLoop"},
    {OperatorType::JoinSortMerge, "JoinSortMerge"},
    {OperatorType::Limit, "Limit"},
    {OperatorType::Print, "Print"},
    {OperatorType::Product, "Product"},
    {OperatorType::Projection, "Projection"},
    {OperatorType::Sort, "Sort"},
    {OperatorType::TableScan, "TableScan"},
    {OperatorType::TableWrapper, "TableWrapper"},
    {OperatorType::UnionAll, "UnionAll"},
    {OperatorType::UnionPositions, "UnionPositions"},
    {OperatorType::Update, "Update"},
    {OperatorType::Validate, "Validate"},
    {OperatorType::CreateView, "CreateView"},
    {OperatorType::DropView, "DropView"},
    {OperatorType::ShowColumns, "ShowColumns"},
    {OperatorType::ShowTables, "ShowTables"},
    {OperatorType::Mock, "Mock"},
};

void AbstractOperator::execute() {
  DTRACE_PROBE1(HYRISE, OPERATOR_STARTED, name().c_str());
  DebugAssert(!_input_left || _input_left->get_output(), "Left input has not yet been executed");
  DebugAssert(!_input_right || _input_right->get_output(), "Right input has not yet been executed");
  DebugAssert(!_output, "Operator has already been executed");

  const bool old_deep_copy_exists = Global::get().deep_copy_exists;
  if (_type != OperatorType::JitOperatorWrapper) {
    Global::get().deep_copy_exists = true;
  }

#if PAPI_SUPPORT
  auto& result = JitEvaluationHelper::get().result();

  auto papi_events = JitEvaluationHelper::get().globals()["papi_events"];
  auto num_counters = papi_events.size();
  int32_t papi_event_ids[10];
  long long papi_values[10];  // NOLINT

  for (uint32_t i = 0; i < num_counters; ++i) {
    auto event_name = papi_events[i].get<std::string>();
    if (PAPI_event_name_to_code(event_name.c_str(), &papi_event_ids[i]) < 0)
      throw std::logic_error("PAPI_event_name_to_code: PAPI event name: " + event_name + " PAPI error " +
                             std::to_string(PAPI_event_name_to_code(event_name.c_str(), &papi_event_ids[i])));
  }
#endif

  Timer performance_timer;
#if PAPI_SUPPORT
  if (num_counters) {
    //  if (PAPI_assign_eventset_component(papi_event_ids, 0) < 0) throw std::logic_error("PAPI error");
    if (PAPI_start_counters(papi_event_ids, num_counters) < 0)
      throw std::logic_error("PAPI_start_counters: PAPI error " +
                             std::to_string(PAPI_start_counters(papi_event_ids, num_counters)));
  }
#endif
  _prepare();
#if PAPI_SUPPORT
  if (num_counters) {
    if (PAPI_stop_counters(papi_values, num_counters) < 0)
      throw std::logic_error("PAPI_stop_counters: PAPI error " +
                             std::to_string(PAPI_stop_counters(papi_values, num_counters)));
  }
#endif

  const auto preparation_time = performance_timer.lap();

#if PAPI_SUPPORT
  if (Global::get().jit_evaluate) {
    std::string name_prefix = old_deep_copy_exists ? "__" : "";
    nlohmann::json op = {{"name", name_prefix + name()}, {"prepare", true}, {"walltime", preparation_time.count()}};
    for (uint32_t i = 0; i < num_counters; ++i) {
      op[papi_events[i].get<std::string>()] = papi_values[i];
      papi_values[i] = 0;
    }

    result["operators"].push_back(op);
  }
#endif

  performance_timer.lap();

#if PAPI_SUPPORT
  if (num_counters) {
    if (PAPI_start_counters(papi_event_ids, num_counters) < 0)
      throw std::logic_error("PAPI_start_counters: PAPI error " +
                             std::to_string(PAPI_start_counters(papi_event_ids, num_counters)));
  }
#endif

  auto transaction_context = this->transaction_context();

  if (transaction_context) {
    /**
     * Do not execute Operators if transaction has been aborted.
     * Not doing so is crucial in order to make sure no other
     * tasks of the Transaction run while the Rollback happens.
     */
    if (transaction_context->aborted()) {
      return;
    }
    transaction_context->on_operator_started();
    _output = _on_execute(transaction_context);
    transaction_context->on_operator_finished();
  } else {
    _output = _on_execute(nullptr);
  }

  // release any temporary data if possible
  _on_cleanup();

#if PAPI_SUPPORT
  if (num_counters) {
    if (PAPI_stop_counters(papi_values, num_counters) < 0)
      throw std::logic_error("PAPI_stop_counters: PAPI error " +
                             std::to_string(PAPI_stop_counters(papi_values, num_counters)));
  }
#endif

  _performance_data->walltime = performance_timer.lap();

#if PAPI_SUPPORT
  if (Global::get().jit_evaluate) {
    std::string name_prefix = old_deep_copy_exists ? "__" : "";
    nlohmann::json op2 = {{"name", name_prefix + name()}, {"prepare", false}, {"walltime", _performance_data->walltime.count()}};
    for (uint32_t i = 0; i < num_counters; ++i) {
      op2[papi_events[i].get<std::string>()] = papi_values[i];
    }
    result["operators"].push_back(op2);
  }
#endif

  if (Global::get().use_times || Global::get().jit_evaluate) {
    auto& times = Global::get().times[name()];
    if (old_deep_copy_exists) {
      times.__preparation_time += preparation_time;
      times.__execution_time += _performance_data->walltime;
    } else {
      times.preparation_time += preparation_time;
      times.execution_time += _performance_data->walltime;
    }
  }

  Global::get().deep_copy_exists = old_deep_copy_exists;

  DTRACE_PROBE5(HYRISE, OPERATOR_EXECUTED, name().c_str(), _performance_data->walltime.count(),
                _output ? _output->row_count() : 0, _output ? _output->chunk_count() : 0,
                reinterpret_cast<uintptr_t>(this));
}

// returns the result of the operator
std::shared_ptr<const Table> AbstractOperator::get_output() const {
  DebugAssert(
      [&]() {
        if (_output == nullptr) return true;
        if (_output->chunk_count() <= ChunkID{1}) return true;
        for (auto chunk_id = ChunkID{0}; chunk_id < _output->chunk_count(); ++chunk_id) {
          if (_output->get_chunk(chunk_id)->size() < 1) return true;
        }
        return true;
      }(),
      "Empty chunk returned from operator " + description());

  DebugAssert(!_output || _output->column_count() > 0, "Operator " + description() + " did not output any columns");

  return _output;
}

void AbstractOperator::clear_output() { _output = nullptr; }

const std::string AbstractOperator::description(DescriptionMode description_mode) const { return name(); }

std::shared_ptr<AbstractOperator> AbstractOperator::deep_copy() const {
  std::unordered_map<const AbstractOperator*, std::shared_ptr<AbstractOperator>> copied_ops;
  return _deep_copy_impl(copied_ops);
}

std::shared_ptr<const Table> AbstractOperator::input_table_left() const { return _input_left->get_output(); }

std::shared_ptr<const Table> AbstractOperator::input_table_right() const { return _input_right->get_output(); }

bool AbstractOperator::transaction_context_is_set() const { return _transaction_context.has_value(); }

std::shared_ptr<TransactionContext> AbstractOperator::transaction_context() const {
  DebugAssert(!transaction_context_is_set() || !_transaction_context->expired(),
              "TransactionContext is expired, but SQL Query Executor should still own it (Operator: " + name() + ")");
  return transaction_context_is_set() ? _transaction_context->lock() : nullptr;
}

void AbstractOperator::set_transaction_context(const std::weak_ptr<TransactionContext>& transaction_context) {
  _transaction_context = transaction_context;
  _on_set_transaction_context(transaction_context);
}

void AbstractOperator::set_transaction_context_recursively(
    const std::weak_ptr<TransactionContext>& transaction_context) {
  set_transaction_context(transaction_context);

  if (_input_left != nullptr) mutable_input_left()->set_transaction_context_recursively(transaction_context);
  if (_input_right != nullptr) mutable_input_right()->set_transaction_context_recursively(transaction_context);
}

std::shared_ptr<AbstractOperator> AbstractOperator::mutable_input_left() const {
  return std::const_pointer_cast<AbstractOperator>(_input_left);
}

std::shared_ptr<AbstractOperator> AbstractOperator::mutable_input_right() const {
  return std::const_pointer_cast<AbstractOperator>(_input_right);
}

const OperatorPerformanceData& AbstractOperator::performance_data() const { return *_performance_data; }

std::shared_ptr<const AbstractOperator> AbstractOperator::input_left() const { return _input_left; }

std::shared_ptr<const AbstractOperator> AbstractOperator::input_right() const { return _input_right; }

void AbstractOperator::print(std::ostream& stream) const {
  const auto get_children_fn = [](const auto& op) {
    std::vector<std::shared_ptr<const AbstractOperator>> children;
    if (op->input_left()) children.emplace_back(op->input_left());
    if (op->input_right()) children.emplace_back(op->input_right());
    return children;
  };
  const auto node_print_fn = [& performance_data = *_performance_data](const auto& op, auto& stream) {
    stream << op->description();

    // If the operator was already executed, print some info about data and performance
    const auto output = op->get_output();
    if (output) {
      stream << " (" << output->row_count() << " row(s)/" << output->chunk_count() << " chunk(s)/"
             << output->column_count() << " column(s)/";

      stream << format_bytes(output->estimate_memory_usage());
      stream << "/";
      stream << performance_data.to_string(DescriptionMode::SingleLine) << ")";
    }
  };

  print_directed_acyclic_graph<const AbstractOperator>(shared_from_this(), get_children_fn, node_print_fn, stream);
}

void AbstractOperator::set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) {
  _on_set_parameters(parameters);
  if (input_left()) mutable_input_left()->set_parameters(parameters);
  if (input_right()) mutable_input_right()->set_parameters(parameters);
}

void AbstractOperator::_on_set_transaction_context(const std::weak_ptr<TransactionContext>& transaction_context) {}

void AbstractOperator::_on_cleanup() {}

std::shared_ptr<AbstractOperator> AbstractOperator::_deep_copy_impl(
    std::unordered_map<const AbstractOperator*, std::shared_ptr<AbstractOperator>>& copied_ops) const {
  const auto copied_ops_iter = copied_ops.find(this);
  if (copied_ops_iter != copied_ops.end()) return copied_ops_iter->second;

  const auto copied_input_left =
      input_left() ? input_left()->_deep_copy_impl(copied_ops) : std::shared_ptr<AbstractOperator>{};
  const auto copied_input_right =
      input_right() ? input_right()->_deep_copy_impl(copied_ops) : std::shared_ptr<AbstractOperator>{};

  const auto copied_op = _on_deep_copy(copied_input_left, copied_input_right);
  if (_transaction_context) copied_op->set_transaction_context(*_transaction_context);

  copied_ops.emplace(this, copied_op);

  return copied_op;
}

}  // namespace opossum
