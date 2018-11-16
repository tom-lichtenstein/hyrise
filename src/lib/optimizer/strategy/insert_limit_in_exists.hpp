#pragma once

#include <memory>
#include <string>

#include "abstract_rule.hpp"

namespace opossum {

class AbstractLQPNode;
class AbstractExpression;

// This optimizer rule is responsible for inserting limits in correlated subquerie with exists
class InsertLimitInExistsRule : public AbstractRule {
 public:
  std::string name() const override;
  bool apply_to(const std::shared_ptr<AbstractLQPNode>& node) const override;

 private:
  bool apply_to(const std::vector<std::shared_ptr<AbstractExpression>>& expressions) const;
};

}  // namespace opossum
