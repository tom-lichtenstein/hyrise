#include "jit_aware_lqp_translator.hpp"

#if HYRISE_JIT_SUPPORT

#include <boost/range/adaptors.hpp>
#include <boost/range/combine.hpp>

#include <queue>
#include <unordered_set>
#include <expression/between_expression.hpp>

#include "constant_mappings.hpp"
#include "expression/abstract_predicate_expression.hpp"
#include "expression/arithmetic_expression.hpp"
#include "expression/logical_expression.hpp"
#include "expression/lqp_column_expression.hpp"
#include "expression/binary_predicate_expression.hpp"
#include "expression/parameter_expression.hpp"
#include "expression/value_expression.hpp"
#include "global.hpp"
#include "jit_evaluation_helper.hpp"
#include "logical_query_plan/aggregate_node.hpp"
#include "logical_query_plan/limit_node.hpp"
#include "logical_query_plan/lqp_utils.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/projection_node.hpp"
#include "logical_query_plan/stored_table_node.hpp"
#include "operators/jit_operator/operators/jit_aggregate.hpp"
#include "operators/jit_operator/operators/jit_compute.hpp"
#include "operators/jit_operator/operators/jit_filter.hpp"
#include "operators/jit_operator/operators/jit_limit.hpp"
#include "operators/jit_operator/operators/jit_read_tuples.hpp"
#include "operators/jit_operator/operators/jit_validate.hpp"
#include "operators/jit_operator/operators/jit_write_offset.hpp"
#include "operators/jit_operator/operators/jit_write_tuples.hpp"
#include "operators/operator_scan_predicate.hpp"
#include "resolve_type.hpp"
#include "statistics/table_statistics.hpp"
#include "storage/storage_manager.hpp"
#include "types.hpp"
#include "expression/expression_utils.hpp"

#include "operators/get_table.hpp"
#include "operators/table_scan.hpp"

#include "sql/sql_query_cache.hpp"
#include "expression/expression_functional.hpp"

using namespace std::string_literals;  // NOLINT

using namespace opossum::expression_functional;  // NOLINT

namespace {

using namespace opossum;  // NOLINT

const std::unordered_map<PredicateCondition, JitExpressionType> predicate_condition_to_jit_expression_type = {
    {PredicateCondition::Equals, JitExpressionType::Equals},
    {PredicateCondition::NotEquals, JitExpressionType::NotEquals},
    {PredicateCondition::LessThan, JitExpressionType::LessThan},
    {PredicateCondition::LessThanEquals, JitExpressionType::LessThanEquals},
    {PredicateCondition::GreaterThan, JitExpressionType::GreaterThan},
    {PredicateCondition::GreaterThanEquals, JitExpressionType::GreaterThanEquals},
    {PredicateCondition::Between, JitExpressionType::Between},
    {PredicateCondition::Like, JitExpressionType::Like},
    {PredicateCondition::NotLike, JitExpressionType::NotLike},
    {PredicateCondition::IsNull, JitExpressionType::IsNull},
    {PredicateCondition::IsNotNull, JitExpressionType::IsNotNull},
    {PredicateCondition::In, JitExpressionType::In}};

const std::unordered_map<ArithmeticOperator, JitExpressionType> arithmetic_operator_to_jit_expression_type = {
    {ArithmeticOperator::Addition, JitExpressionType::Addition},
    {ArithmeticOperator::Subtraction, JitExpressionType::Subtraction},
    {ArithmeticOperator::Multiplication, JitExpressionType::Multiplication},
    {ArithmeticOperator::Division, JitExpressionType::Division},
    {ArithmeticOperator::Modulo, JitExpressionType::Modulo}};

const std::unordered_map<LogicalOperator, JitExpressionType> logical_operator_to_jit_expression = {
    {LogicalOperator::And, JitExpressionType::And}, {LogicalOperator::Or, JitExpressionType::Or}};

bool requires_computation(const std::shared_ptr<AbstractLQPNode> &node) {
  // do not count trivial projections without computations
  if (const auto projection_node = std::dynamic_pointer_cast<ProjectionNode>(node)) {
    for (const auto& expression : projection_node->expressions) {
      if (expression->type != ExpressionType::LQPColumn) return true;
    }
    return false;
  }
  return true;
}

bool can_translate_predicate_to_predicate_value_id_expression(const AbstractExpression& expression,
                                                              const std::shared_ptr<AbstractLQPNode>& input_node) {
  if (!Global::get().use_value_id) return false;
  // input node must be a stored table node
  if (input_node && input_node->type != LQPNodeType::StoredTable) return false;

  const auto* predicate_expression = dynamic_cast<const AbstractPredicateExpression*>(&expression);
  // value ids can only be used in compare expressions
  switch (predicate_expression->predicate_condition) {
    case PredicateCondition::In:
    case PredicateCondition::NotIn:
    case PredicateCondition::Like:
    case PredicateCondition::NotLike:
      return false;
    default:
      break;
  }

  // predicates with value ids only work on exactly one input column
  bool found_input_column = false;

  for (const auto& argument : expression.arguments) {
    switch (argument->type) {
      case ExpressionType::Value:
      case ExpressionType::Parameter:
        break;
      case ExpressionType::LQPColumn: {
        if (found_input_column) return false;

        // Check if column references a stored table
        const auto column = std::dynamic_pointer_cast<const LQPColumnExpression>(argument);
        const auto column_reference = column->column_reference;

        const auto stored_table_node =
                std::dynamic_pointer_cast<const StoredTableNode>(column_reference.original_node());
        if (!stored_table_node) return false;

        // Check if column is dictionary compressed
        const auto table = StorageManager::get().get_table(stored_table_node->table_name);
        if (table->chunks().empty()) return false;
        const auto segment = table->get_chunk(ChunkID(0))->get_segment(column_reference.original_column_id());
        const auto dict_segment = std::dynamic_pointer_cast<const BaseEncodedSegment>(segment);
        if (!dict_segment) return false;

        found_input_column = true;
        break;
      }
      default:
        return false;
    }
  }
  return found_input_column;
}

bool expression_is_complex(const std::shared_ptr<AbstractExpression> &expression) {
  switch (expression->type) {
    case ExpressionType::Value:
    case ExpressionType::Parameter:
    case ExpressionType::LQPColumn:
    case ExpressionType::Aggregate:
    case ExpressionType::Function:
      return false;
    default:
      return true;
  }
}

bool expressions_are_complex(const std::vector<std::shared_ptr<AbstractExpression>>& expressions) {
  for (const auto& expression : expressions) {
    if (expression_is_complex(expression)) return true;
  }
  return false;
}

float compute_weigth(const std::shared_ptr<AbstractLQPNode> &node, const std::shared_ptr<AbstractLQPNode>& input_node, const bool first_node) {
  if (const auto projection_node = std::dynamic_pointer_cast<ProjectionNode>(node)) {
    float counter = 0;
    for (const auto& expression : projection_node->expressions) {
      if (expression->type != ExpressionType::LQPColumn) counter += 1;
    }
    if (input_node && counter) return counter;
    return counter == 1 ? .5f : counter;
  } else if (const auto aggregate_node = std::dynamic_pointer_cast<AggregateNode>(node)) {
    auto group_by_count = aggregate_node->group_by_expressions.size();
    float weight = 0; // group_by_count > 2 ? 1 : 0;
    size_t string_expressions = 0;
    for (const auto& expression : aggregate_node->group_by_expressions) {
      const auto input_node_column_id = input_node->find_column_id(*expression);
      if (!input_node_column_id) {
        weight += .9;  //
        if (false) expressions_are_complex(expression->arguments);  // ? 1. : 0.9;
      }
      if (expression->data_type() == DataType::String) ++string_expressions;
    }
    for (const auto& expression : aggregate_node->aggregate_expressions) {
      const auto aggregate_expression = std::static_pointer_cast<AggregateExpression>(expression);
      if (const auto argument = aggregate_expression->argument()) {
        const auto input_node_column_id = input_node->find_column_id(*argument);
        if (argument->data_type() == DataType::String) ++string_expressions;
        if (!input_node_column_id) {
          weight += .9; //expressions_are_complex(expression->arguments) ? 1. : 0.9;
        }

      }
    }
    // weight -= .1 * string_expressions;
    return group_by_count > 3 ? std::max(weight, 1.9f) : weight;
  } else if (const auto predicate_node = std::dynamic_pointer_cast<PredicateNode>(node)) {
    float weight = 0;
    size_t complexity = 0;
    visit_expression(predicate_node->predicate, [&](const auto& sub_expression) {
      if (!expression_is_complex(sub_expression)) {
        return ExpressionVisitation::DoNotVisitArguments;
      }
      ++complexity;
      if (sub_expression->arguments[0]->data_type() == DataType::String) {
        if (!can_translate_predicate_to_predicate_value_id_expression(*sub_expression, input_node)) {
          weight -= 1.6;
        }
      }
      weight += 1;
      return ExpressionVisitation::VisitArguments;
    });
    if (weight < 0 || complexity > 1) return weight;
    float selectivity = -1;
    try {
      if ( input_node->type == LQPNodeType::StoredTable) {
        auto& cache = SQLQueryCache<float, std::shared_ptr<PredicateNode>>::get();
        if (const auto entry = cache.try_get(predicate_node)) {
          selectivity = *entry;
        } else {
          const auto stored_table_node = std::dynamic_pointer_cast<StoredTableNode>(input_node);
          auto get_table = std::make_shared<GetTable>(stored_table_node->table_name);
          get_table->execute();
          const auto input_table = get_table->get_output();
          const auto& operator_predicates = OperatorScanPredicate::from_expression(*predicate_node->predicate, *input_node);
          const auto predicate_vars = operator_predicates->front();
          std::shared_ptr<AbstractPredicateExpression> new_predicate;

          const auto& column_definition = input_table->column_definitions().at(predicate_vars.column_id);
          const std::shared_ptr<AbstractExpression> column_expression = expression_functional::pqp_column_(predicate_vars.column_id, column_definition.data_type, column_definition.nullable, column_definition.name);
          if (predicate_vars.predicate_condition == PredicateCondition::Between) {
            if (is_variant(predicate_vars.value) && is_variant(*predicate_vars.value2)) {
              new_predicate = std::make_shared<BetweenExpression>(column_expression, value_(boost::get<AllTypeVariant>(predicate_vars.value)), value_(boost::get<AllTypeVariant>(*predicate_vars.value2)));
            }
          } else {
            if (is_variant(predicate_vars.value)) {
              new_predicate = std::make_shared<BinaryPredicateExpression>(predicate_vars.predicate_condition, column_expression, expression_functional::value_(boost::get<AllTypeVariant>(predicate_vars.value)));
            }
          }
          if (new_predicate) {
            auto scan = std::make_shared<TableScan>(get_table, new_predicate);
            scan->execute();
            if (const auto input_size = input_table->row_count()) {
              const auto output_size = scan->get_output()->row_count();
              selectivity = 1.f * output_size / input_size;
            }
          }
          if (cache.size() != 1000) {
            cache.resize(1000);
          }
          cache.set(predicate_node, selectivity);
        }
      }

      if (selectivity < 0) {
        if (const auto input_row_count = node->left_input()->get_statistics()->row_count()) {
          selectivity = node->get_statistics()->row_count() / input_row_count;
        }
      }
    } catch (std::logic_error err) {
      // Not all nodes support statistics yet
      selectivity = 1;
    }
    // std::cout << "selectivity: " << selectivity << std::endl;
    // node->print(std::cout);
    if (first_node) {
      if (selectivity < .3) return -2;
      if (selectivity < .4) return 0;
      if (selectivity > .7) return std::max(weight, 1.9f);
    }
    if (selectivity > .6) return std::max(weight, 1.f);
    return 0.1;
    //return selectivity * std::min(weight, 1.f);
  } else if (node->type == LQPNodeType::Validate) {
    return first_node ? 1.8 : 0.1;
  } else if (node->type == LQPNodeType::Limit) {
    return 1;
  }
  return 1;
}

}  // namespace

namespace opossum {

std::shared_ptr<AbstractOperator> JitAwareLQPTranslator::translate_node(
    const std::shared_ptr<AbstractLQPNode>& node) const {
  const auto jit_operator = _try_translate_sub_plan_to_jit_operators(node, Global::get().disable_string_compare);
  return jit_operator ? jit_operator : LQPTranslator::translate_node(node);
}

std::shared_ptr<JitOperatorWrapper> JitAwareLQPTranslator::_try_translate_sub_plan_to_jit_operators(
    const std::shared_ptr<AbstractLQPNode>& node, const bool use_value_id) const {
  auto jittable_node_count = size_t{0};

  auto input_nodes = std::unordered_set<std::shared_ptr<AbstractLQPNode>>{};

  bool use_validate = false;
  bool validate_after_filter = false;
  bool has_predicate = false;

  // Traverse query tree until a non-jittable nodes is found in each branch
  _visit(node, [&](auto& current_node) {
    const auto is_root_node = current_node == node;
    if (_node_is_jittable(current_node, use_value_id, is_root_node)) {
      use_validate |= current_node->type == LQPNodeType::Validate;
      has_predicate |= current_node->type == LQPNodeType::Predicate;
      validate_after_filter |= use_validate && current_node->type == LQPNodeType::Predicate;
      if (requires_computation(current_node)) ++jittable_node_count;
      return true;
    } else {
      input_nodes.insert(current_node);
      return false;
    }
  });

  // We use a really simple heuristic to decide when to introduce jittable operators:
  //   - If there is more than one input node, don't JIT
  //   - Always JIT AggregateNodes, as the JitAggregate is significantly faster than the Aggregate operator
  //   - Otherwise, JIT if there are two or more jittable nodes
  if (input_nodes.size() != 1 || jittable_node_count < 1) return nullptr;
  if (jittable_node_count == 1 && (node->type != LQPNodeType::Aggregate && !has_predicate))
    return nullptr;

  if (jittable_node_count == 1 && has_predicate && !Global::get().allow_single_predicate) {
    // do not jit for single simple table scan
    auto current_node = node;
    while (current_node->type != LQPNodeType::Predicate) {
      current_node = current_node->left_input();
    }
    const auto predicate_node = std::dynamic_pointer_cast<PredicateNode>(current_node);
    if (const auto predicate = std::dynamic_pointer_cast<BinaryPredicateExpression>(predicate_node->predicate)) {
      bool complex_expression = false;
      for (const auto expression : predicate->arguments) {
        switch (expression->type) {
          case ExpressionType::LQPColumn:
          case ExpressionType::Value:
          case ExpressionType::Parameter:
          case ExpressionType::Aggregate:
            continue;
          default:
            complex_expression = true;
            break;
        }
      }
      if (!complex_expression) return nullptr;
    }
  }

  // limit can only be the root node
  const bool use_limit = node->type == LQPNodeType::Limit;

  // The input_node is not being integrated into the operator chain, but instead serves as the input to the JitOperators
  const auto input_node = *input_nodes.begin();

  float weight = 0;
  if (Global::get().use_weight) {
    _visit(node, [&](auto& current_node) {
      const bool is_jittable = _node_is_jittable(current_node, use_value_id, node == current_node);
      if (is_jittable) {
        auto current_input_node = current_node->left_input();
        while (current_input_node->type == LQPNodeType::Projection && input_node != current_input_node) {
          current_input_node = current_input_node->left_input();
        }
        weight += compute_weigth(current_node, input_node, input_node == current_input_node);
      }
      return is_jittable;
    });
    if (weight < 2) return nullptr;
  }
  if constexpr (false) {
    std::cout << "Jit Translator: jittable_node_count " << jittable_node_count << " weight: " << weight << std::endl;
  }


  const auto jit_operator = std::make_shared<JitOperatorWrapper>(translate_node(input_node));
  const auto row_count_expression =
      use_limit ? std::static_pointer_cast<LimitNode>(node)->num_rows_expression : nullptr;
  const auto read_tuples = std::make_shared<JitReadTuples>(use_validate, row_count_expression, validate_after_filter);
  jit_operator->add_jit_operator(read_tuples);

  // "filter_node". The root node of the subplan computed by a JitFilter.
  auto filter_node = node;
  while (filter_node != input_node && filter_node->type != LQPNodeType::Predicate &&
         filter_node->type != LQPNodeType::Union) {
    filter_node = filter_node->left_input();
  }

  float selectivity = 0;
  try {
    if (const auto input_row_count = input_node->get_statistics()->row_count()) {
      selectivity = filter_node->get_statistics()->row_count() / input_row_count;
    }
  } catch (std::logic_error) {
    // Not all nodes support statistics yet
    selectivity = 1;
  }

  if (use_validate && !validate_after_filter) {
    jit_operator->add_jit_operator(std::make_shared<JitValidate>(TableType::Data, false));
  }

  // If we can reach the input node without encountering a UnionNode or PredicateNode,
  // there is no need to filter any tuples
  if (filter_node != input_node) {
    const auto boolean_expression =
        lqp_subplan_to_boolean_expression(filter_node, [&](const std::shared_ptr<AbstractLQPNode>& lqp) {
          return _node_is_jittable(lqp, use_value_id, false);
        });
    if (!boolean_expression) return nullptr;

    const auto jit_boolean_expression =
        _try_translate_expression_to_jit_expression(*boolean_expression, *read_tuples, input_node);
    if (!jit_boolean_expression) {
      // retry jitting current node without using value ids, i.e. without strings
      if (use_value_id) return _try_translate_sub_plan_to_jit_operators(node, false);
      return nullptr;
    }

    if (jit_boolean_expression->expression_type() != JitExpressionType::Column) {
      // make sure that the expression gets computed ...
#if LESS_JIT_CONTEXT
      jit_operator->add_jit_operator(std::make_shared<JitFilter>(jit_boolean_expression));
#else
      jit_operator->add_jit_operator(std::make_shared<JitCompute>(jit_boolean_expression));
      jit_operator->add_jit_operator(std::make_shared<JitFilter>(jit_boolean_expression->result()));
#endif
    } else {
      // and then filter on the resulting boolean.
      jit_operator->add_jit_operator(std::make_shared<JitFilter>(jit_boolean_expression->result()));
    }
  }

  if (use_validate && validate_after_filter) {
    jit_operator->add_jit_operator(std::make_shared<JitValidate>(TableType::Data, true));
  }

  if (node->type == LQPNodeType::Aggregate) {
    // Since aggregate nodes cause materialization, there is at most one JitAggregate operator in each operator chain
    // and it must be the last operator of the chain. The _node_is_jittable function takes care of this by rejecting
    // aggregate nodes that would be placed in the middle of an operator chain.
    const auto aggregate_node = std::static_pointer_cast<AggregateNode>(node);

    auto aggregate = std::make_shared<JitAggregate>();

    for (const auto& groupby_expression : aggregate_node->group_by_expressions) {
      const auto jit_expression =
          _try_translate_expression_to_jit_expression(*groupby_expression, *read_tuples, input_node);
      if (!jit_expression) return nullptr;
      // Create a JitCompute operator for each computed groupby column ...
      if (jit_expression->expression_type() != JitExpressionType::Column) {
        jit_operator->add_jit_operator(std::make_shared<JitCompute>(jit_expression));
      }
      // ... and add the column to the JitAggregate operator.
      aggregate->add_groupby_column(groupby_expression->as_column_name(), jit_expression->result());
    }

    for (const auto& expression : aggregate_node->aggregate_expressions) {
      const auto aggregate_expression = std::dynamic_pointer_cast<AggregateExpression>(expression);
      DebugAssert(aggregate_expression, "Expression is not a function.");

      if (aggregate_expression->arguments.empty()) {
        // count(*)
        aggregate->add_aggregate_column(aggregate_expression->as_column_name(), {DataType::Long, false, 0},
                                        aggregate_expression->aggregate_function);
      } else {
        const auto jit_expression =
            _try_translate_expression_to_jit_expression(*aggregate_expression->arguments[0], *read_tuples, input_node);
        if (!jit_expression) return nullptr;
        // Create a JitCompute operator for each aggregate expression on a computed value ...
        if (jit_expression->expression_type() != JitExpressionType::Column) {
          jit_operator->add_jit_operator(std::make_shared<JitCompute>(jit_expression));
        }

        // ... and add the aggregate expression to the JitAggregate operator.
        aggregate->add_aggregate_column(aggregate_expression->as_column_name(), jit_expression->result(),
                                        aggregate_expression->aggregate_function);
      }
    }

    jit_operator->add_jit_operator(aggregate);
  } else {
    if (use_limit) jit_operator->add_jit_operator(std::make_shared<JitLimit>());

    // check, if output has to be materialized
    const auto output_must_be_materialized = std::find_if(
        node->column_expressions().begin(), node->column_expressions().end(),
        [&input_node](const auto& column_expression) { return !input_node->find_column_id(*column_expression); });

    if (output_must_be_materialized != node->column_expressions().end() || !Global::get().reference_output) {
      // Add a compute operator for each computed output column (i.e., a column that is not from a stored table).
      auto write_table = std::make_shared<JitWriteTuples>();
      for (const auto& column_expression : node->column_expressions()) {
        const auto jit_expression =
            _try_translate_expression_to_jit_expression(*column_expression, *read_tuples, input_node);
        if (!jit_expression) return nullptr;
        // If the JitExpression is of type JitExpressionType::Column, there is no need to add a compute node, since it
        // would not compute anything anyway
        if (jit_expression->expression_type() != JitExpressionType::Column) {
          jit_operator->add_jit_operator(std::make_shared<JitCompute>(jit_expression));
        }

        write_table->add_output_column(column_expression->as_column_name(), jit_expression->result());
      }
      jit_operator->add_jit_operator(write_table);
    } else {
      auto write_table = std::make_shared<JitWriteOffset>(selectivity);

      for (const auto& column : node->column_expressions()) {
        const auto column_id = input_node->find_column_id(*column);
        DebugAssert(column_id, "Output column must reference an input column");
        write_table->add_output_column(
            {column->as_column_name(), column->data_type(), column->is_nullable(), *column_id});
      }

      jit_operator->add_jit_operator(write_table);
    }
  }


  /*
  if (weight < 2) {  // selectivity < 0.001
    // * /
    std::cout << "selectivity: " << selectivity << " weight: " << weight << std::endl;
    std::cout << jit_operator->description(DescriptionMode::MultiLine) << std::endl << std::flush;
    // / *
  } else {
    std::cout << "------------------ selectivity: " << selectivity << " weight: " << weight << std::endl;
    std::cout << jit_operator->description(DescriptionMode::MultiLine) << std::endl << std::flush;
    std::cout << "---------------------------------------------------------------------------------" << std::endl;
  }
  usleep(10);
   */

  return jit_operator;
}

std::shared_ptr<const JitExpression> JitAwareLQPTranslator::_try_translate_expression_to_jit_expression(
    const AbstractExpression& expression, JitReadTuples& jit_source, const std::shared_ptr<AbstractLQPNode>& input_node,
    bool use_value_id, const bool can_be_bool_column) const {
  const auto input_node_column_id = input_node->find_column_id(expression);
  if (input_node_column_id) {
    const auto tuple_value = jit_source.add_input_column(can_be_bool_column ? DataType::Bool : expression.data_type(),
                                                         expression.is_nullable(), *input_node_column_id, use_value_id);
    return std::make_shared<JitExpression>(tuple_value);
  }

  std::shared_ptr<const JitExpression> left, right;
  switch (expression.type) {
    case ExpressionType::Value: {
      const auto* value_expression = dynamic_cast<const ValueExpression*>(&expression);
      const auto tuple_value = jit_source.add_literal_value(value_expression->value, use_value_id);
      return std::make_shared<JitExpression>(tuple_value, value_expression->value, use_value_id);
    }
    case ExpressionType::Parameter: {
      const auto* parameter = dynamic_cast<const ParameterExpression*>(&expression);
      if (parameter->parameter_expression_type == ParameterExpressionType::External) {
        const auto tuple_value = jit_source.add_parameter_value(parameter->data_type(), parameter->is_nullable(),
                                                                parameter->parameter_id, use_value_id);
        return std::make_shared<JitExpression>(tuple_value);
      } else {
        DebugAssert(parameter->value(), "Value must be set");
        const auto tuple_value = jit_source.add_literal_value(*parameter->value(), use_value_id);
        return std::make_shared<JitExpression>(tuple_value, *parameter->value(), use_value_id);
      }
    }

    case ExpressionType::LQPColumn:
      // Column SHOULD have been resolved by `find_column_id()` call above the switch
      Fail("Column doesn't exist in input_node");

    case ExpressionType::Predicate: {
      use_value_id = can_translate_predicate_to_predicate_value_id_expression(expression, input_node);
    }
    case ExpressionType::Arithmetic:
    case ExpressionType::Logical: {
      std::vector<std::shared_ptr<const JitExpression>> jit_expression_arguments;
      for (const auto& argument : expression.arguments) {
        const auto jit_expression =
            _try_translate_expression_to_jit_expression(*argument, jit_source, input_node, use_value_id);
        if (!jit_expression) return nullptr;
        jit_expression_arguments.emplace_back(jit_expression);

        if (Global::get().disable_string_compare) {
          if (!use_value_id && jit_expression->result().data_type() == DataType::String) {
            return nullptr;  // string not supported without value ids
          }
        }

      }

      const auto jit_expression_type = _expression_to_jit_expression_type(expression);

      if (jit_expression_arguments.size() == 1) {
        return std::make_shared<JitExpression>(jit_expression_arguments[0], jit_expression_type,
                                               jit_source.add_temporary_value());
      } else if (jit_expression_arguments.size() == 2) {
        // An expression can handle strings only exclusively
        if ((jit_expression_arguments[0]->result().data_type() == DataType::String) !=
            (jit_expression_arguments[1]->result().data_type() == DataType::String)) {
          return nullptr;
        }
        const auto jit_expression =
            std::make_shared<JitExpression>(jit_expression_arguments[0], jit_expression_type,
                                            jit_expression_arguments[1], jit_source.add_temporary_value());
        if (use_value_id) jit_source.add_value_id_predicate(*jit_expression);
        return jit_expression;
      } else if (jit_expression_arguments.size() == 3) {
        DebugAssert(jit_expression_type == JitExpressionType::Between, "Only Between supported for 3 arguments");
        const auto lower_bound_check =
            std::make_shared<JitExpression>(jit_expression_arguments[0], JitExpressionType::GreaterThanEquals,
                                            jit_expression_arguments[1], jit_source.add_temporary_value());
        const auto upper_bound_check =
            std::make_shared<JitExpression>(jit_expression_arguments[0], JitExpressionType::LessThanEquals,
                                            jit_expression_arguments[2], jit_source.add_temporary_value());
        if (use_value_id) {
          jit_source.add_value_id_predicate(*lower_bound_check);
          jit_source.add_value_id_predicate(*upper_bound_check);
        }

        return std::make_shared<JitExpression>(lower_bound_check, JitExpressionType::And, upper_bound_check,
                                               jit_source.add_temporary_value());
      } else {
        Fail("Unexpected number of arguments, can't translate to JitExpression");
      }
    }

    default:
      return nullptr;
  }
}

namespace {
bool _expressions_are_jittable(const std::vector<std::shared_ptr<AbstractExpression>>& expressions,
                               const bool allow_string = false) {
  for (const auto& expression : expressions) {
    switch (expression->type) {
      case ExpressionType::Cast:
      case ExpressionType::Case:
      case ExpressionType::Exists:
      case ExpressionType::Extract:
      case ExpressionType::Function:
      case ExpressionType::List:
      case ExpressionType::PQPSelect:
      case ExpressionType::LQPSelect:
      case ExpressionType::UnaryMinus:
        return false;
      case ExpressionType::Predicate: {
        const auto predicate_expression = std::static_pointer_cast<AbstractPredicateExpression>(expression);
        switch (predicate_expression->predicate_condition) {
          case PredicateCondition::In:
          case PredicateCondition::NotIn:
          // case PredicateCondition::Like:
          // case PredicateCondition::NotLike:
            return false;
          default:
            break;
        }
        return _expressions_are_jittable(expression->arguments, can_translate_predicate_to_predicate_value_id_expression(*expression, nullptr));
      }
      case ExpressionType::Arithmetic:
      case ExpressionType::Logical:
        return _expressions_are_jittable(expression->arguments, allow_string);
      case ExpressionType::Value: {
        if (Global::get().disable_string_compare) {
          const auto value_expression = std::static_pointer_cast<const ValueExpression>(expression);
          if (!allow_string && data_type_from_all_type_variant(value_expression->value) == DataType::String) {
            return false;
          }
        }
        break;
      }
      case ExpressionType::Parameter: {
        const auto parameter = std::dynamic_pointer_cast<const ParameterExpression>(expression);
        // ParameterExpressionType::ValuePlaceholder used in prepared statements not supported as it does not provide
        // type information
        if (parameter->parameter_expression_type == ParameterExpressionType::ValuePlaceholder && !parameter->value())
          return false;
        break;
      }
      case ExpressionType::LQPColumn: {
        if (Global::get().disable_string_compare) {
          const auto column = std::dynamic_pointer_cast<const LQPColumnExpression>(expression);
          // Filter or computation on string columns is expensive
          if (!allow_string && column->data_type() == DataType::String) return false;
        }
        break;
      }
      default:
        break;
    }
  }
  return true;
}
}  // namespace

bool JitAwareLQPTranslator::_node_is_jittable(const std::shared_ptr<AbstractLQPNode>& node, const bool use_value_id,
                                              const bool is_root_node) const {
  bool jit_predicate = true;
  if (JitEvaluationHelper::get().experiment().count("jit_predicate")) {
    jit_predicate = JitEvaluationHelper::get().experiment()["jit_predicate"];
  }

  if (node->type == LQPNodeType::Aggregate) {
    // We do not support the count distinct function yet and thus need to check all aggregate expressions.
    auto aggregate_node = std::static_pointer_cast<AggregateNode>(node);
    auto aggregate_expressions = aggregate_node->aggregate_expressions;
    auto has_unsupported_aggregate =
        std::any_of(aggregate_expressions.begin(), aggregate_expressions.end(), [](auto& expression) {
          const auto aggregate_expression = std::dynamic_pointer_cast<AggregateExpression>(expression);
          Assert(aggregate_expression, "Expected AggregateExpression");
          // Right now, the JIT does not support CountDistinct
          return aggregate_expression->aggregate_function == AggregateFunction::CountDistinct;
        });
    return is_root_node && !has_unsupported_aggregate;
  }

  if (auto predicate_node = std::dynamic_pointer_cast<PredicateNode>(node)) {
    return _expressions_are_jittable({predicate_node->predicate}, use_value_id) && predicate_node->scan_type == ScanType::TableScan;
  }

  if (Global::get().jit_validate && node->type == LQPNodeType::Validate) {
    return true;
  }

  if (Global::get().jit_limit && is_root_node && node->type == LQPNodeType::Limit) {
    return true;
  }

  if (auto projection_node = std::dynamic_pointer_cast<ProjectionNode>(node)) {
    for (const auto expression : projection_node->expressions) {
      if (expression->type != ExpressionType::LQPColumn) {
        if (!_expressions_are_jittable({expression}, use_value_id)) return false;
      }
    }
    return true;
  }

  return node->type == LQPNodeType::Union && jit_predicate;
}

void JitAwareLQPTranslator::_visit(const std::shared_ptr<AbstractLQPNode>& node,
                                   const std::function<bool(const std::shared_ptr<AbstractLQPNode>&)>& func) const {
  std::unordered_set<std::shared_ptr<const AbstractLQPNode>> visited;
  std::queue<std::shared_ptr<AbstractLQPNode>> queue({node});

  while (!queue.empty()) {
    auto current_node = queue.front();
    queue.pop();

    if (!current_node || visited.count(current_node)) {
      continue;
    }
    visited.insert(current_node);

    if (func(current_node)) {
      queue.push(current_node->left_input());
      queue.push(current_node->right_input());
    }
  }
}

JitExpressionType JitAwareLQPTranslator::_expression_to_jit_expression_type(const AbstractExpression& expression) {
  switch (expression.type) {
    case ExpressionType::Arithmetic: {
      const auto* arithmetic_expression = dynamic_cast<const ArithmeticExpression*>(&expression);
      return arithmetic_operator_to_jit_expression_type.at(arithmetic_expression->arithmetic_operator);
    }

    case ExpressionType::Predicate: {
      const auto* predicate_expression = dynamic_cast<const AbstractPredicateExpression*>(&expression);
      return predicate_condition_to_jit_expression_type.at(predicate_expression->predicate_condition);
    }

    case ExpressionType::Logical: {
      const auto* logical_expression = dynamic_cast<const LogicalExpression*>(&expression);
      return logical_operator_to_jit_expression.at(logical_expression->logical_operator);
    }

    default:
      Fail("Expression "s + expression.as_column_name() + " is jit incompatible");
  }
}

}  // namespace opossum

#endif
