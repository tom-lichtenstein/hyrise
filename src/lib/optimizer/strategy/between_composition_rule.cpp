#include "between_composition_rule.hpp"

#include <algorithm>
#include <iostream>

#include "all_parameter_variant.hpp"
#include "constant_mappings.hpp"
#include "expression/expression_functional.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/lqp_utils.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/stored_table_node.hpp"
#include "operators/operator_scan_predicate.hpp"
#include "statistics/chunk_statistics/chunk_statistics.hpp"
#include "storage/storage_manager.hpp"
#include "storage/table.hpp"
#include "utils/assert.hpp"

using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

std::string BetweenCompositionRule::name() const { return "Between Composition Rule"; }

ColumnBoundary get_boundary(const std::shared_ptr<BinaryPredicateExpression>& expression) {
  const auto left_column_expression = std::static_pointer_cast<LQPColumnExpression>(expression->left_operand());
  const auto right_value_expression = std::static_pointer_cast<ValueExpression>(expression->right_operand());

  if (left_column_expression != nullptr && right_value_expression != nullptr) {
    return {
        left_column_expression,
        right_value_expression,
        expression->predicate_condition == PredicateCondition::LessThanEquals,
        expression->predicate_condition == PredicateCondition::GreaterThanEquals,
    };
  }

  const auto left_value_expression = std::static_pointer_cast<ValueExpression>(expression->left_operand());
  const auto right_column_expression = std::static_pointer_cast<LQPColumnExpression>(expression->right_operand());

  if (left_value_expression != nullptr && right_column_expression != nullptr) {
    return {
        right_column_expression,
        left_value_expression,
        expression->predicate_condition == PredicateCondition::GreaterThanEquals,
        expression->predicate_condition == PredicateCondition::LessThanEquals,
    };
  }
  return {nullptr, nullptr, false, false};
}

void BetweenCompositionRule::apply_to(const std::shared_ptr<AbstractLQPNode>& node) const {

  if (node->type != LQPNodeType::Predicate) {
    _apply_to_inputs(node);
    return;
  }
  auto predicate_node = std::static_pointer_cast<PredicateNode>(node);
  auto expression = std::static_pointer_cast<LogicalExpression>(predicate_node->predicate());

  if (expression == nullptr || expression->logical_operator != LogicalOperator::And) {
    _apply_to_inputs(node);
    return;
  }

  const auto left_operand = std::static_pointer_cast<BinaryPredicateExpression>(expression->left_operand());
  const auto right_operand = std::static_pointer_cast<BinaryPredicateExpression>(expression->right_operand());

  if (left_operand == nullptr || right_operand == nullptr) {
    _apply_to_inputs(node);
    return;
  }

  const auto left_boundary = get_boundary(left_operand);
  const auto right_boundary = get_boundary(right_operand);
  if (left_boundary.column_expression->as_column_name() == right_boundary.column_expression->as_column_name()) {
    if (left_boundary.upper_bound && right_boundary.lower_bound) {
      const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
          left_boundary.column_expression, right_boundary.value_expression, left_boundary.value_expression));
      lqp_replace_node(predicate_node, between_node);
    } else if (left_boundary.lower_bound && right_boundary.upper_bound) {
      const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
          left_boundary.column_expression, left_boundary.value_expression, right_boundary.value_expression));
      lqp_replace_node(predicate_node, between_node);
    }
  }
  _apply_to_inputs(node);
  return;
}

}  // namespace opossum

