#include "binary_predicate_expression.hpp"

#include <sstream>

#include "boost/functional/hash.hpp"
#include "constant_mappings.hpp"
#include "types.hpp"

namespace opossum {

BinaryPredicateExpression::BinaryPredicateExpression(const PredicateCondition predicate_condition,
                                                         const std::shared_ptr<AbstractExpression>& left_operand,
                                                         const std::shared_ptr<AbstractExpression>& right_operand):
AbstractPredicateExpression(predicate_condition, {left_operand, right_operand}) {}

const std::shared_ptr<AbstractExpression>& BinaryPredicateExpression::left_operand() const {
  return arguments[0];
}

const std::shared_ptr<AbstractExpression>& BinaryPredicateExpression::right_operand() const {
  return arguments[1];
}

std::shared_ptr<BinaryPredicateExpression> BinaryPredicateExpression::flipped() const {
  const auto flipped_predicate_condition = flip_predicate_condition(predicate_condition);
  return std::make_shared<BinaryPredicateExpression>(flipped_predicate_condition, right_operand(), left_operand());
}

std::shared_ptr<AbstractExpression> BinaryPredicateExpression::deep_copy() const {
  return std::make_shared<BinaryPredicateExpression>(predicate_condition, left_operand()->deep_copy(), right_operand()->deep_copy());
}

std::string BinaryPredicateExpression::as_column_name() const {
  std::stringstream stream;

  stream << left_operand()->as_column_name() << " ";
  stream << predicate_condition_to_string.left.at(predicate_condition) << " ";
  stream << right_operand()->as_column_name();

  return stream.str();
}

bool BinaryPredicateExpression::_shallow_equals(const AbstractExpression& expression) const {
  return predicate_condition == static_cast<const BinaryPredicateExpression&>(expression).predicate_condition;
}

}  // namespace opossum
