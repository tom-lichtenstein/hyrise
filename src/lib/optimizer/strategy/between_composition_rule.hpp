#pragma once

#include <memory>
#include <set>
#include <string>
#include <vector>

#include "abstract_rule.hpp"
#include "expression/expression_functional.hpp"

#include "types.hpp"

namespace opossum {

class AbstractLQPNode;
class ChunkStatistics;
class PredicateNode;

struct ColumnBoundary {
  std::shared_ptr<LQPColumnExpression> column_expression;
  std::shared_ptr<ValueExpression> value_expression;
  bool upper_bound;
  bool lower_bound;
};
/**
 * This rule determines which chunks can be excluded from table scans based on
 * the predicates present in the LQP and stores that information in the stored
 * table nodes.
 */
class BetweenCompositionRule : public AbstractRule {
 public:
  std::string name() const override;
  void apply_to(const std::shared_ptr<AbstractLQPNode>& node) const override;
};

}  // namespace opossum
