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
#include "optimizer/strategy/between_composition_rule.hpp"
#include "optimizer/strategy/strategy_base_test.hpp"
#include "statistics/column_statistics.hpp"
#include "statistics/table_statistics.hpp"
#include "storage/chunk_encoder.hpp"
#include "storage/storage_manager.hpp"

#include "utils/assert.hpp"

#include "logical_query_plan/mock_node.hpp"

using namespace opossum::expression_functional;  // NOLINT

namespace opossum {

class BetweenCompositionTest : public StrategyBaseTest {
 protected:
  void SetUp() override {
    auto& storage_manager = StorageManager::get();
    storage_manager.add_table("compressed", load_table("src/test/tables/int_float2.tbl", 2u));
    storage_manager.add_table("long_compressed", load_table("src/test/tables/25_ints_sorted.tbl", 25u));
    storage_manager.add_table("run_length_compressed", load_table("src/test/tables/10_ints.tbl", 5u));
    storage_manager.add_table("string_compressed", load_table("src/test/tables/string.tbl", 3u));
    storage_manager.add_table("fixed_string_compressed", load_table("src/test/tables/string.tbl", 3u));

    ChunkEncoder::encode_all_chunks(storage_manager.get_table("compressed"), EncodingType::Dictionary);
    ChunkEncoder::encode_all_chunks(storage_manager.get_table("long_compressed"), EncodingType::Dictionary);
    ChunkEncoder::encode_all_chunks(storage_manager.get_table("run_length_compressed"), EncodingType::RunLength);
    ChunkEncoder::encode_all_chunks(storage_manager.get_table("string_compressed"), EncodingType::Dictionary);
    ChunkEncoder::encode_all_chunks(storage_manager.get_table("fixed_string_compressed"),
                                    EncodingType::FixedStringDictionary);
    _rule = std::make_shared<BetweenCompositionRule>();

    storage_manager.add_table("uncompressed", load_table("src/test/tables/int_float2.tbl", 10u));
  }

  std::shared_ptr<BetweenCompositionRule> _rule;
};

TEST_F(BetweenCompositionTest, DummyBetweenCompositionTest) {
  auto stored_table_node = std::make_shared<StoredTableNode>("compressed");

  auto root = std::make_shared<PredicateNode>(
      and_(greater_than_equals_(LQPColumnReference(stored_table_node, ColumnID{0}), 200),
           less_than_equals_(LQPColumnReference(stored_table_node, ColumnID{0}), 300)));

  auto composed = StrategyBaseTest::apply_rule(_rule, root);
  EXPECT_EQ(composed, root);
}

TEST_F(BetweenCompositionTest, ScanBetweenReplacementTest) {
  auto stored_table_node = std::make_shared<StoredTableNode>("compressed");

  const auto input_lqp = std::make_shared<PredicateNode>(
      and_(greater_than_equals_(LQPColumnReference(stored_table_node, ColumnID{0}), 200),
           less_than_equals_(LQPColumnReference(stored_table_node, ColumnID{0}), 300)));

  std::cout << "BetweenCompositionTest \n";

  input_lqp->print();

  const auto expected_lqp =
      std::make_shared<PredicateNode>(between_(LQPColumnReference(stored_table_node, ColumnID{0}), 200, 300));

  auto result_lqp = StrategyBaseTest::apply_rule(_rule, input_lqp);
  EXPECT_EQ(result_lqp, expected_lqp);
}

}  // namespace opossum

