#pragma once

#include <string>

#include "abstract_read_only_operator.hpp"
#include "jit_operator/jit_types.hpp"

namespace opossum {

/* The JitOperatorWrapper wraps a number of jittable operators and exposes them through Hyrise's default
 * operator interface. This allows a number of jit operators to be seamlessly integrated with
 * the existing operator pipeline.
 * The JitOperatorWrapper is responsible for chaining the operators it contains, compiling code for the operators at
 * runtime, creating and managing the runtime context and calling hooks (before/after processing a chunk or the entire
 * query) on the its operators.
 */
class JitOptimalScanOperator : public AbstractReadOnlyOperator {
public:
  JitOptimalScanOperator() : AbstractReadOnlyOperator{OperatorType::JitOperatorWrapper} {}

  const std::string name() const final;

protected:
  std::shared_ptr<const Table> _on_execute() override;

  std::shared_ptr<AbstractOperator> _on_deep_copy(
          const std::shared_ptr<AbstractOperator>& copied_input_left,
          const std::shared_ptr<AbstractOperator>& copied_input_right) const override {
    return std::make_shared<JitOptimalScanOperator>();
  }
  void _on_set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) override {}
  void _on_set_transaction_context(const std::weak_ptr<TransactionContext>& transaction_context) override {}

  __attribute__((noinline))
  std::shared_ptr<JitRuntimeContext> _get_context();
};

}  // namespace opossum
