#include "like_composition_rule.hpp"

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

std::string LikeCompositionRule::name() const { return "Like Composition Rule"; }

void LikeCompositionRule::apply_to(const std::shared_ptr<AbstractLQPNode>& node) const {
  if (node->type != LQPNodeType::Predicate) {
    _apply_to_inputs(node);
    return;
  }

  auto predicate_node = std::static_pointer_cast<PredicateNode>(node);
  auto expression = std::static_pointer_cast<BinaryPredicateExpression>(predicate_node->predicate());

  if (expression->predicate_condition != PredicateCondition::Like) {
    _apply_to_inputs(node);
    return;
  }

  auto value = std::static_pointer_cast<ValueExpression>(expression->right_operand());
  std::cout << "h1" << value->value << "\n";

  _apply_to_inputs(node);
  return;
}

}  // namespace opossum
