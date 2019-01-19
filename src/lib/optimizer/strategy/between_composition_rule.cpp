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
  // BUG: Can recognize: x <= 4, but can not recognize: 4 <= x
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

std::vector<ColumnBoundary> get_column_boundaries(const std::shared_ptr<AbstractLQPNode>& node) {
  if (node == nullptr) {
    return std::vector<ColumnBoundary>();
  }
  std::cout << "node\n";

  auto left_children = get_column_boundaries(node->left_input());
  const auto right_children = get_column_boundaries(node->right_input());

  left_children.insert(std::end(left_children), std::begin(right_children), std::end(right_children));

  if (node->type == LQPNodeType::Predicate) {
    const auto predicate_node = std::static_pointer_cast<PredicateNode>(node);
    const auto expression = std::static_pointer_cast<BinaryPredicateExpression>(predicate_node->predicate());
    if (expression != nullptr && (expression->predicate_condition == PredicateCondition::GreaterThanEquals ||
                                  expression->predicate_condition == PredicateCondition::LessThanEquals)) {
      const auto boundary = get_boundary(expression);
      if (boundary.upper_bound || boundary.lower_bound) {
        left_children.push_back(boundary);
      }
    }
  }

  return left_children;
}

bool column_boundary_comparator(ColumnBoundary a, ColumnBoundary b) {
  return a.column_expression->as_column_name() < b.column_expression->as_column_name();
}

void replace_boundaries(std::vector<ColumnBoundary> boundaries) {
  std::sort(boundaries.begin(), boundaries.end(), column_boundary_comparator);
  std::shared_ptr<opossum::LQPColumnExpression> last_column_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> lower_bound_value_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> upper_bound_value_expression = nullptr;
  for (ColumnBoundary boundary : boundaries) {
    if (last_column_expression == nullptr ||
        last_column_expression->as_column_name() != boundary.column_expression->as_column_name()) {
      if (lower_bound_value_expression != nullptr && upper_bound_value_expression != nullptr) {
        const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
            last_column_expression, lower_bound_value_expression, upper_bound_value_expression));
        between_node->print();
        // TODO(tom): Delete predicate expressions and insert between node
      }
      upper_bound_value_expression = nullptr;
      lower_bound_value_expression = nullptr;
      last_column_expression = boundary.column_expression;
    }

    if (boundary.upper_bound && !boundary.lower_bound) {
      if (upper_bound_value_expression == nullptr ||
          upper_bound_value_expression->value > boundary.value_expression->value) {
        upper_bound_value_expression = boundary.value_expression;
      }
    } else if (boundary.lower_bound && !boundary.upper_bound) {
      if (lower_bound_value_expression == nullptr ||
          lower_bound_value_expression->value < boundary.value_expression->value) {
        lower_bound_value_expression = boundary.value_expression;
      }
    }
  }
}

void BetweenCompositionRule::apply_to(const std::shared_ptr<AbstractLQPNode>& node) const {
  const auto boundaries = get_column_boundaries(node);
  std::cout << "Filtered for binary predicate expressions\n";
  replace_boundaries(boundaries);

  /*if (node->type != LQPNodeType::Predicate) {
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
  */
}

}  // namespace opossum
