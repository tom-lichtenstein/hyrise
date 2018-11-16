#include "jit_operator_wrapper.hpp"

#include "expression/expression_utils.hpp"
#include "global.hpp"
#include "operators/jit_operator/jit_constant_mappings.hpp"
#include "operators/jit_operator/operators/jit_aggregate.hpp"
#include "operators/jit_operator/operators/jit_compute.hpp"
#include "operators/jit_operator/operators/jit_read_value.hpp"
#include "operators/jit_operator/operators/jit_validate.hpp"
#include "operators/jit_operator/operators/jit_filter.hpp"
#include "utils/timer.hpp"

#include "jit_evaluation_helper.hpp"

namespace opossum {

JitOperatorWrapper::JitOperatorWrapper(
    const std::shared_ptr<const AbstractOperator>& left, const JitExecutionMode execution_mode,
    const std::vector<std::shared_ptr<AbstractJittable>>& jit_operators)
    : AbstractReadOnlyOperator{OperatorType::JitOperatorWrapper, left},
      _execution_mode{execution_mode},
      _specialized_function{std::make_shared<SpecializedFunction>()} {
  _specialized_function->jit_operators = jit_operators;
  if (JitEvaluationHelper::get().experiment().count("jit_use_jit")) {
    _execution_mode = JitEvaluationHelper::get().experiment().at("jit_use_jit") ? JitExecutionMode::Compile
                                                                                : JitExecutionMode::Interpret;
  }
}

JitOperatorWrapper::JitOperatorWrapper(
        const std::shared_ptr<const AbstractOperator>& left,
        const JitExecutionMode execution_mode,
        const std::shared_ptr<SpecializedFunction> specialized_function)
        : AbstractReadOnlyOperator{OperatorType::JitOperatorWrapper, left},
          _execution_mode{execution_mode},
          _specialized_function{specialized_function} {
  if (JitEvaluationHelper::get().experiment().count("jit_use_jit")) {
    _execution_mode = JitEvaluationHelper::get().experiment().at("jit_use_jit") ? JitExecutionMode::Compile
                                                                                : JitExecutionMode::Interpret;
  }
}
const std::string JitOperatorWrapper::name() const { return "JitOperatorWrapper"; }

const std::string JitOperatorWrapper::description(DescriptionMode description_mode) const {
  std::stringstream desc;
  const auto separator = description_mode == DescriptionMode::MultiLine ? "\n" : " ";
  desc << "[JitOperatorWrapper]" << separator;
  for (const auto& op : _specialized_function->jit_operators) {
    desc << op->description() << separator;
  }
  return desc.str();
}

void JitOperatorWrapper::add_jit_operator(const std::shared_ptr<AbstractJittable>& op) { _specialized_function->jit_operators.push_back(op); }

const std::vector<std::shared_ptr<AbstractJittable>>& JitOperatorWrapper::jit_operators() const { return _specialized_function->jit_operators; }

const std::shared_ptr<JitReadTuples> JitOperatorWrapper::_source() const {
  return std::dynamic_pointer_cast<JitReadTuples>(_specialized_function->jit_operators.front());
}

const std::shared_ptr<AbstractJittableSink> JitOperatorWrapper::_sink() const {
  return std::dynamic_pointer_cast<AbstractJittableSink>(_specialized_function->jit_operators.back());
}

void JitOperatorWrapper::insert_loads(const bool lazy) {
  const auto input_wrappers = _source()->input_wrappers();
  std::vector<std::shared_ptr<AbstractJittable>> jit_operators;
  if constexpr (!JIT_LAZY_LOAD) {
    const auto input_col_num = _source()->input_columns().size();
    const auto operators_size = _specialized_function->jit_operators.size();
    jit_operators.resize(operators_size + input_col_num);
    jit_operators[0] = _specialized_function->jit_operators[0];
    std::copy(_specialized_function->jit_operators.cbegin() + 1, _specialized_function->jit_operators.cbegin() + operators_size, jit_operators.begin() + input_col_num + 1);
    for (size_t index = 0; index < input_col_num; ++index) {
      jit_operators[index + 1] = std::make_shared<JitReadValue>(_source()->input_columns()[index], input_wrappers[index]);
    }
    _specialized_function->jit_operators = jit_operators;
    return;
  }
  std::map<size_t, size_t> inverted_input_columns;
  auto input_columns = _source()->input_columns();
  for (size_t input_column_index = 0; input_column_index < input_columns.size(); ++input_column_index) {
    inverted_input_columns[input_columns[input_column_index].tuple_value.tuple_index()] = input_column_index;
  }

  std::vector<std::map<size_t, bool>> accessed_column_ids;
  accessed_column_ids.reserve(jit_operators.size());
  std::map<size_t, bool> column_id_used_by_one_operator;
  for (const auto& jit_operator : _specialized_function->jit_operators) {
    auto col_ids = jit_operator->accessed_column_ids();
    accessed_column_ids.emplace_back(col_ids);
    for (const auto& pair : accessed_column_ids.back()) {
      if (inverted_input_columns.count(pair.first)) {
        column_id_used_by_one_operator[pair.first] = !column_id_used_by_one_operator.count(pair.first);
      }
    }
  }
  auto ids_itr = accessed_column_ids.begin();
  for (auto& jit_operator : _specialized_function->jit_operators) {
    for (const auto& pair : *ids_itr++) {
      if (column_id_used_by_one_operator.count(pair.first)) {
        if (pair.second && column_id_used_by_one_operator[pair.first]) {
          // insert within JitCompute operator
          auto compute_ptr = std::dynamic_pointer_cast<JitCompute>(jit_operator);
          if (compute_ptr) {
            compute_ptr->set_load_column(pair.first, input_wrappers[inverted_input_columns[pair.first]]);
          } else {
            auto filter_ptr = std::dynamic_pointer_cast<JitFilter>(jit_operator);
            filter_ptr->set_load_column(pair.first, input_wrappers[inverted_input_columns[pair.first]]);
          }
        } else {
          jit_operators.emplace_back(std::make_shared<JitReadValue>(
              _source()->input_columns()[inverted_input_columns[pair.first]], input_wrappers[inverted_input_columns[pair.first]]));
        }
        column_id_used_by_one_operator.erase(pair.first);
      }
    }
    jit_operators.push_back(jit_operator);
  }

  _specialized_function->jit_operators = jit_operators;
}

void JitOperatorWrapper::_prepare() {
  Assert(_source(), "JitOperatorWrapper does not have a valid source node.");
  Assert(_sink(), "JitOperatorWrapper does not have a valid sink node.");

  const auto& in_table = *input_left()->get_output();

  if (in_table.chunk_count() > 0 && _source()->input_wrappers().empty()) {
    JitRuntimeContext context;
    _source()->add_input_segment_iterators(context, in_table, *in_table.get_chunk(ChunkID(0)), true);
  }

  _choose_execute_func();
}

namespace {

TableType input_table_type(const std::shared_ptr<const AbstractOperator>& node) {
  if (const auto in_table = node->get_output()) {
    return in_table->type();
  }
  switch (node->type()) {
    case OperatorType::TableWrapper:
    case OperatorType::GetTable:
    case OperatorType::Aggregate:
      return TableType::Data;
    default:
      return TableType::References;
  }
}

}  // namespace


void JitOperatorWrapper::_choose_execute_func() {
  std::lock_guard<std::mutex> guard(_specialized_function->specialize_mutex);
  if (_specialized_function->execute_func) return;

  if (_source()->input_wrappers().empty()) _source()->create_default_input_wrappers();
  for (auto& jit_operator : _specialized_function->jit_operators) {
    if (auto jit_validate = std::dynamic_pointer_cast<JitValidate>(jit_operator)) {
      jit_validate->set_input_table_type(input_table_type(input_left()));
    }
  }

  // std::cout << "Before make loads lazy:" << std::endl << description(DescriptionMode::MultiLine) << std::endl;
  if (_specialized_function->insert_loads) {
    insert_loads(Global::get().lazy_load);
    _specialized_function->insert_loads = false;
  }
  // std::cout << "Specialising: " << (_execution_mode == JitExecutionMode::Compile ? "true" : "false") << std::endl;

  // Connect operators to a chain
  for (auto it = _specialized_function->jit_operators.begin(), next = ++_specialized_function->jit_operators.begin();
       it != _specialized_function->jit_operators.end() && next != _specialized_function->jit_operators.end(); ++it, ++next) {
    (*it)->set_next_operator(*(next));
  }

  // std::cerr << description(DescriptionMode::MultiLine) << std::endl;

  // We want to perform two specialization passes if the operator chain contains a JitAggregate operator, since the
  // JitAggregate operator contains multiple loops that need unrolling.
  auto two_specialization_passes = static_cast<bool>(std::dynamic_pointer_cast<JitAggregate>(_sink()));
  bool specialize = !Global::get().interpret;
  if (JitEvaluationHelper::get().experiment().count("jit_use_jit")) {
    specialize = JitEvaluationHelper::get().experiment().at("jit_use_jit");
  }
  // specialize = false;
  if (specialize) {
    // this corresponds to "opossum::JitReadTuples::execute(opossum::JitRuntimeContext&) const"
    _specialized_function->execute_func = _specialized_function->module.specialize_and_compile_function<void(const JitReadTuples*, JitRuntimeContext&)>(
        "_ZNK7opossum13JitReadTuples7executeERNS_17JitRuntimeContextE",
        std::make_shared<JitConstantRuntimePointer>(_source().get()), two_specialization_passes);
  } else {
    _specialized_function->execute_func = &JitReadTuples::execute;
  }
}

std::shared_ptr<const Table> JitOperatorWrapper::_on_execute() {
  const auto in_table = input_left()->get_output();
  auto out_table = _sink()->create_output_table(in_table->max_chunk_size());

  JitRuntimeContext context;
  if (transaction_context_is_set()) {
    context.transaction_id = transaction_context()->transaction_id();
    context.snapshot_commit_id = transaction_context()->snapshot_commit_id();
  }

  std::chrono::nanoseconds before_chunk_time{0};
  std::chrono::nanoseconds after_chunk_time{0};
  std::chrono::nanoseconds function_time{0};

  Timer timer;

  _source()->before_query(*in_table, _input_parameter_values, context);
  _sink()->before_query(*in_table, *out_table, context);
  auto before_query_time = timer.lap();

  // std::cout << "total chunks: " << in_table->chunk_count() << std::endl;
  for (opossum::ChunkID chunk_id{0}; chunk_id < in_table->chunk_count() && context.limit_rows; ++chunk_id) {
    /*
    if (chunk_id + 1 == in_table->chunk_count()) {
      std::cout << "last chunk, chunk no " << chunk_id << std::endl;
    } else {
      std::cout << "chunk no " << chunk_id << std::endl;
    }
     */
    const bool same_type = _source()->before_chunk(*in_table, chunk_id, _input_parameter_values, context);
    before_chunk_time += timer.lap();
    if (same_type) {
      _specialized_function->execute_func(_source().get(), context);
    } else {
      PerformanceWarning("Jit is interpreted as input reader types mismatch.")
      _source()->execute(context);
    }
    function_time += timer.lap();
    _sink()->after_chunk(in_table, *out_table, context);
    after_chunk_time += timer.lap();
  }

  _sink()->after_query(*out_table, context);
  auto after_query_time = timer.lap();

  if (Global::get().jit_evaluate) {
    auto& operators = JitEvaluationHelper::get().result()["operators"];
    auto add_time = [&operators](const std::string& name, const auto& time) {
      const auto micro_s = std::chrono::duration_cast<std::chrono::microseconds>(time).count();
      if (micro_s > 0) {
        nlohmann::json jit_op = {{"name", name}, {"prepare", false}, {"walltime", micro_s}};
        operators.push_back(jit_op);
      }
    };
    std::string name_prefix = Global::get().deep_copy_exists ? "__" : "";
    add_time(name_prefix + "_JitBeforeQuery", before_query_time);
    add_time(name_prefix + "_JitAfterQuery", after_query_time);
    add_time(name_prefix + "_JitBeforChunk", before_chunk_time);
    add_time(name_prefix + "_JitAfterChunk", after_chunk_time);
    add_time(name_prefix + "_Function", function_time);

#if JIT_MEASURE
    std::chrono::nanoseconds operator_total_time{0};
    for (size_t index = 0; index < JitOperatorType::Size; ++index) {
      add_time("_" + jit_operator_type_to_string.left.at(static_cast<JitOperatorType>(index)), context.times[index]);
      operator_total_time += context.times[index];
    }
    add_time("_Jit_OperatorsTotal", operator_total_time);
#endif
  }

  return out_table;
}

std::shared_ptr<AbstractOperator> JitOperatorWrapper::_on_deep_copy(
    const std::shared_ptr<AbstractOperator>& copied_input_left,
    const std::shared_ptr<AbstractOperator>& copied_input_right) const {
  if (Global::get().deep_copy_exists) {
    return std::make_shared<JitOperatorWrapper>(copied_input_left, _execution_mode, _specialized_function);
  } else {
    auto specialized_func =  std::make_shared<SpecializedFunction>(_specialized_function->jit_operators, _specialized_function->insert_loads);
    return std::make_shared<JitOperatorWrapper>(copied_input_left, _execution_mode, specialized_func);
  }
}

void JitOperatorWrapper::_on_set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) {
  const auto& input_parameters = _source()->input_parameters();
  _input_parameter_values.resize(input_parameters.size());
  auto itr = _input_parameter_values.begin();
  for (auto& parameter : input_parameters) {
    auto search = parameters.find(parameter.parameter_id);
    if (search != parameters.end()) {
      *itr = search->second;
    }
    ++itr;
  }
}

void JitOperatorWrapper::_on_set_transaction_context(const std::weak_ptr<TransactionContext>& transaction_context) {
  if (const auto row_count_expression = _source()->row_count_expression())
    expression_set_transaction_context(row_count_expression, transaction_context);
}

}  // namespace opossum
