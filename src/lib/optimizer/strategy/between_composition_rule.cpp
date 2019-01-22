#include "between_composition_rule.hpp"

#include <algorithm>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "constant_mappings.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"
#include "logical_query_plan/lqp_utils.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "statistics/table_statistics.hpp"
#include "utils/assert.hpp"


using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

std::string BetweenCompositionRule::name() const { return "Between Composition Rule"; }

ColumnBoundary get_boundary(const std::shared_ptr<BinaryPredicateExpression>& expression, const std::shared_ptr<PredicateNode>& node) {
  // BUG: Can recognize: x <= 4, but can not recognize: 4 <= x
  const auto left_column_expression = std::static_pointer_cast<LQPColumnExpression>(expression->left_operand());
  const auto right_value_expression = std::static_pointer_cast<ValueExpression>(expression->right_operand());

  if (left_column_expression != nullptr && right_value_expression != nullptr) {
    return {
        node,
        left_column_expression,
        right_value_expression,
        expression->predicate_condition == PredicateCondition::LessThanEquals,
        expression->predicate_condition == PredicateCondition::GreaterThanEquals,
    };
  }

  const auto left_value_expression = std::static_pointer_cast<ValueExpression>(expression->left_operand());
  const auto right_column_expression = std::static_pointer_cast<LQPColumnExpression>(expression->right_operand());

  if (left_value_expression != nullptr && right_column_expression != nullptr) {
    std::cout << "LEFT VALUE EXPRESSION\n";
    return {
        node,
        right_column_expression,
        left_value_expression,
        expression->predicate_condition == PredicateCondition::GreaterThanEquals,
        expression->predicate_condition == PredicateCondition::LessThanEquals,
    };
  }
  return {nullptr, nullptr, nullptr, false, false};
}

/*
std::vector<ColumnBoundary> get_column_boundaries(const std::shared_ptr<AbstractLQPNode>& node) {
  if (node == nullptr) {
    return std::vector<ColumnBoundary>();
  }


  auto left_children = get_column_boundaries(node->left_input());
  const auto right_children = get_column_boundaries(node->right_input());

  left_children.insert(std::end(left_children), std::begin(right_children), std::end(right_children));

  if (node->type == LQPNodeType::Predicate) {
    const auto predicate_node = std::static_pointer_cast<PredicateNode>(node);
    const auto expression = std::static_pointer_cast<BinaryPredicateExpression>(predicate_node->predicate());
    if (expression != nullptr && (expression->predicate_condition == PredicateCondition::GreaterThanEquals ||
                                  expression->predicate_condition == PredicateCondition::LessThanEquals)) {
      const auto boundary = get_boundary(expression, predicate_node);
      if (boundary.upper_bound || boundary.lower_bound) {
        left_children.push_back(boundary);
      }
    }
  }

  lqp_remove_node(node);
  node->print();

  return left_children;
}

bool column_boundary_comparator(ColumnBoundary a, ColumnBoundary b) {
  return a.column_expression->as_column_name() < b.column_expression->as_column_name();
}

void replace_boundaries(std::vector<ColumnBoundary> boundaries, const std::shared_ptr<AbstractLQPNode>& root) {
  std::sort(boundaries.begin(), boundaries.end(), column_boundary_comparator);
  std::shared_ptr<opossum::LQPColumnExpression> last_column_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> lower_bound_value_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> upper_bound_value_expression = nullptr;
  std::vector<std::shared_ptr<PredicateNode>> node_scope = std::vector<std::shared_ptr<PredicateNode>>();
  for (ColumnBoundary boundary : boundaries) {
    if (last_column_expression == nullptr ||
        last_column_expression->as_column_name() != boundary.column_expression->as_column_name()) {
      if (lower_bound_value_expression != nullptr && upper_bound_value_expression != nullptr) {
        const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
            last_column_expression, lower_bound_value_expression, upper_bound_value_expression));
        for (std::shared_ptr<PredicateNode> node : node_scope) {
          if (node->left_input() == nullptr) {
            lqp_replace_node(node, between_node);
            break;
          } else if (node->right_input() == nullptr) {
            node->set_left_input(node->right_input());
            node->left_input() = nullptr;
            lqp_replace_node(node, between_node);
            break;
          }
        }
        // TODO(tom): Delete predicate expressions and insert between node
      }
      upper_bound_value_expression = nullptr;
      lower_bound_value_expression = nullptr;
      node_scope = std::vector<std::shared_ptr<PredicateNode>>();
      last_column_expression = boundary.column_expression;
    }

    if (boundary.upper_bound && !boundary.lower_bound) {
      if (upper_bound_value_expression == nullptr ||
        upper_bound_value_expression->value > boundary.value_expression->value) {
        upper_bound_value_expression = boundary.value_expression;
        node_scope.push_back(boundary.node);
      }
    } else if (boundary.lower_bound && !boundary.upper_bound) {
      if (lower_bound_value_expression == nullptr ||
          lower_bound_value_expression->value < boundary.value_expression->value) {
        lower_bound_value_expression = boundary.value_expression;
        node_scope.push_back(boundary.node);
      }
    }
  }

  for (std::shared_ptr<PredicateNode> expression : between_expressions) {
    std::cout << "Add: ";
    expression->print();
    lqp_insert_node(root, LQPInputSide::Left, expression);
  }

  const auto boundaries = get_column_boundaries(node);
  std::cout << "Filtered for binary predicate expressions\n";
  replace_boundaries(boundaries, node);
}*/

bool column_boundary_comparator(ColumnBoundary a, ColumnBoundary b) {
  return a.column_expression->as_column_name() < b.column_expression->as_column_name();
}

void BetweenCompositionRule::_replace_predicates(std::vector<std::shared_ptr<AbstractLQPNode>>& predicates) const {
  // Store original input and output
  auto input = predicates.back()->left_input();
  const auto outputs = predicates.front()->outputs();
  const auto input_sides = predicates.front()->get_input_sides();

  std::cout << "Input LQP\n";
  for (auto output : outputs) {
    output->print();

  }

  auto between_nodes = std::vector<std::shared_ptr<AbstractLQPNode>>();
  auto predicate_nodes = std::vector<std::shared_ptr<AbstractLQPNode>>();
  auto boundaries = std::vector<ColumnBoundary>();

  // Untie predicates from LQP, so we can freely retie them
  for (auto& predicate : predicates) {
    if (predicate->right_input() == nullptr) {
      lqp_remove_node(predicate);
      const auto predicate_node = std::static_pointer_cast<PredicateNode>(predicate);
      const auto expression = std::static_pointer_cast<BinaryPredicateExpression>(predicate_node->predicate());
      if (expression != nullptr && (expression->predicate_condition == PredicateCondition::GreaterThanEquals ||
                                    expression->predicate_condition == PredicateCondition::LessThanEquals)) {
        auto boundary = get_boundary(expression, predicate_node);
        if (boundary.upper_bound || boundary.lower_bound) {
          boundaries.push_back(boundary);
        }
      }
    } else {
      // TODO(Tom): Handle right input
    }
  }

  std::sort(boundaries.begin(), boundaries.end(), column_boundary_comparator);

  std::shared_ptr<opossum::LQPColumnExpression> last_column_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> lower_bound_value_expression = nullptr;
  std::shared_ptr<opossum::ValueExpression> upper_bound_value_expression = nullptr;
  auto node_scope = std::vector<std::shared_ptr<AbstractLQPNode>>();

  for (auto boundary : boundaries) {
    if (last_column_expression == nullptr ||
        last_column_expression->as_column_name() != boundary.column_expression->as_column_name()) {
      if (lower_bound_value_expression != nullptr && upper_bound_value_expression != nullptr) {
        const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
            last_column_expression, lower_bound_value_expression, upper_bound_value_expression));
        between_nodes.push_back(between_node);
      } else {
        predicate_nodes.insert(std::end(predicate_nodes), std::begin(node_scope), std::end(node_scope));
      }
      upper_bound_value_expression = nullptr;
      lower_bound_value_expression = nullptr;
      node_scope = std::vector<std::shared_ptr<AbstractLQPNode>>();
      last_column_expression = boundary.column_expression;
    }

    if (boundary.upper_bound && !boundary.lower_bound) {
      if (upper_bound_value_expression == nullptr ||
        upper_bound_value_expression->value > boundary.value_expression->value) {
        upper_bound_value_expression = boundary.value_expression;
        node_scope.push_back(boundary.node);
      }
    } else if (boundary.lower_bound && !boundary.upper_bound) {
      if (lower_bound_value_expression == nullptr ||
          lower_bound_value_expression->value < boundary.value_expression->value) {
        lower_bound_value_expression = boundary.value_expression;
        node_scope.push_back(boundary.node);
      }
    }
  }

  if (lower_bound_value_expression != nullptr && upper_bound_value_expression != nullptr) {
    const auto between_node = PredicateNode::make(std::make_shared<BetweenExpression>(
        last_column_expression, lower_bound_value_expression, upper_bound_value_expression));
    between_nodes.push_back(between_node);
  } else {
    predicate_nodes.insert(std::end(predicate_nodes), std::begin(node_scope), std::end(node_scope));
  }

  // Ensure that nodes are chained correctly
  predicates.back()->set_left_input(input);

  std::cout << "Between nodes\n";
  for (auto& node : between_nodes) {
    node->print();
  }

  std::cout << "Predicate nodes\n";
  for (auto& node : predicate_nodes) {
    node->print();
  }

  std::cout << "Remaining LQP\n";
  for (auto output : outputs) {
    output->print();
  }


  // TODO(tom): Insert nodes


  /* for (size_t output_idx = 0; output_idx < outputs.size(); ++output_idx) {
    outputs[output_idx]->set_input(input_sides[output_idx], predicates.front());
  }

  for (size_t predicate_index = 0; predicate_index < predicates.size() - 1; predicate_index++) {
    predicates[predicate_index]->set_left_input(predicates[predicate_index + 1]);
  }*/
}

void BetweenCompositionRule::apply_to(const std::shared_ptr<AbstractLQPNode>& node) const {
  // Validate can be seen as a Predicate on the MVCC column
  if (node->type == LQPNodeType::Predicate || node->type == LQPNodeType::Validate) {
    std::vector<std::shared_ptr<AbstractLQPNode>> predicate_nodes;

    // Gather adjacent PredicateNodes
    auto current_node = node;
    while (current_node->type == LQPNodeType::Predicate || current_node->type == LQPNodeType::Validate) {
      // Once a node has multiple outputs, we're not talking about a Predicate chain anymore
      if (current_node->outputs().size() > 1) {
        break;
      }

      predicate_nodes.emplace_back(current_node);
      if (current_node->left_input() == nullptr) {
        break;
      }
      current_node = current_node->left_input();

    }

    /**
     * A chain of predicates was found.
     * Sort PredicateNodes in descending order with regards to the expected row_count
     * Continue rule in deepest input
     */
    if (predicate_nodes.size() > 1) {
      _replace_predicates(predicate_nodes);
      _apply_to_inputs(predicate_nodes.back());
      return;
    }
  }

  _apply_to_inputs(node);











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
