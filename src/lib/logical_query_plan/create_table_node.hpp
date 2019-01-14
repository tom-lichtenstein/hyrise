#pragma once


#include "base_non_query_node.hpp" // NEEDEDINCLUDE
#include "storage/table_column_definition.hpp" // NEEDEDINCLUDE

namespace opossum {

/**
 * This node type represents the CREATE TABLE management command.
 */
class CreateTableNode : public EnableMakeForLQPNode<CreateTableNode>, public BaseNonQueryNode {
 public:
  CreateTableNode(const std::string& table_name, const TableColumnDefinitions& column_definitions);

  std::string description() const override;

  const std::string table_name;
  const TableColumnDefinitions column_definitions;

 protected:
  std::shared_ptr<AbstractLQPNode> _on_shallow_copy(LQPNodeMapping& node_mapping) const override;
  bool _on_shallow_equals(const AbstractLQPNode& rhs, const LQPNodeMapping& node_mapping) const override;
};

}  // namespace opossum
