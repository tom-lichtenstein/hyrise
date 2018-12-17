#pragma once

#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "abstract_read_only_operator.hpp"

namespace opossum {

/**
 * operator to materialize the table with its data
 */
class Materialize : public AbstractReadOnlyOperator {
public:
  explicit Materialize(const std::shared_ptr<const AbstractOperator>& in, const size_t number_of_columns = 1);

  const std::string name() const override;

protected:
  std::shared_ptr<const Table> _on_execute() override;
  std::shared_ptr<AbstractOperator> _on_deep_copy(
          const std::shared_ptr<AbstractOperator>& copied_input_left,
          const std::shared_ptr<AbstractOperator>& copied_input_right) const override;
  void _on_set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) override;

private:
  const size_t _number_of_columns;

};
}  // namespace opossum
