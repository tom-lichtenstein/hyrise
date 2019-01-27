#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "base_test.hpp"
#include "gtest/gtest.h"

#include "expression/expression_functional.hpp"
#include "logical_query_plan/join_node.hpp"
#include "logical_query_plan/lqp_translator.hpp"
#include "logical_query_plan/predicate_node.hpp"
#include "logical_query_plan/projection_node.hpp"
#include "logical_query_plan/sort_node.hpp"
#include "logical_query_plan/stored_table_node.hpp"
#include "logical_query_plan/union_node.hpp"
#include "operators/get_table.hpp"
#include "optimizer/strategy/like_composition_rule.hpp"
#include "optimizer/strategy/strategy_base_test.hpp"
#include "statistics/column_statistics.hpp"
#include "statistics/table_statistics.hpp"
#include "storage/chunk_encoder.hpp"
#include "storage/storage_manager.hpp"

#include "utils/assert.hpp"

#include "logical_query_plan/mock_node.hpp"

using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

class LikeCompositionTest : public StrategyBaseTest {
 protected:
  void SetUp() override {
    const auto table = load_table("resources/test_data/tbl/int_int_int.tbl");
    StorageManager::get().add_table("a", table);
    _rule = std::make_shared<LikeCompositionRule>();

    std::vector<std::shared_ptr<const BaseColumnStatistics>> column_statistics(
        {std::make_shared<ColumnStatistics<int32_t>>(0.0f, 20, 10, 100),
         std::make_shared<ColumnStatistics<int32_t>>(0.0f, 5, 50, 60),
         std::make_shared<ColumnStatistics<int32_t>>(0.0f, 2, 110, 1100)});

    auto table_statistics = std::make_shared<TableStatistics>(TableType::Data, 100, column_statistics);
    // Assumes 50% deleted rows
    table_statistics->increase_invalid_row_count(50);

    node = StoredTableNode::make("a");
    table->set_table_statistics(table_statistics);

    a = LQPColumnReference{node, ColumnID{0}};
    b = LQPColumnReference{node, ColumnID{1}};
    c = LQPColumnReference{node, ColumnID{2}};
  }

  std::shared_ptr<StoredTableNode> node;
  LQPColumnReference a, b, c;
  std::shared_ptr<LikeCompositionRule> _rule;
};

TEST_F(LikeCompositionTest, LikeCompositionTest) {
  const auto input_lqp = PredicateNode::make(like_(a, "RED%"), node);

  // clang-format off
  const auto expected_lqp = PredicateNode::make(
    greater_than_equals_(a, "RED"),
    PredicateNode::make(
      less_than_(b, "REE"),
      node));
  // clang-format on

  const auto result_lqp = StrategyBaseTest::apply_rule(_rule, input_lqp);

  EXPECT_LQP_EQ(result_lqp, expected_lqp);
}

}  // namespace opossum
