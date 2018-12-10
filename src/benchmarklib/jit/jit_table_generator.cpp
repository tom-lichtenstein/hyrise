#include "jit_table_generator.hpp"

#include <cmath>

namespace opossum {

JitTableGenerator::JitTableGenerator(const float scale_factor, const opossum::ChunkOffset chunk_size)
    : AbstractBenchmarkTableGenerator(chunk_size), _scale_factor{scale_factor} {}

std::map<std::string, std::shared_ptr<opossum::Table>> JitTableGenerator::generate_all_tables() {
  std::map<std::string, std::shared_ptr<opossum::Table>> tables;
  RandomGenerator generator;
  auto cardinalities = std::make_shared<std::vector<size_t>>(
      std::initializer_list<size_t>{static_cast<size_t>(_scale_factor * 1000000)});

  {
    std::vector<Segments> columns_by_chunk;
    TableColumnDefinitions column_definitions;

    add_column<int32_t>(columns_by_chunk, column_definitions, "ID", cardinalities,
                        [&](std::vector<size_t> indices) { return indices[0]; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "A", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "B", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "C", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "D", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); });
    add_nullable_column<int32_t>(columns_by_chunk, column_definitions, "E", cardinalities,
                                 [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); },
                                 [&](std::vector<size_t> indices) { return false; });
    add_nullable_column<int32_t>(columns_by_chunk, column_definitions, "F", cardinalities,
                                 [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); },
                                 [&](std::vector<size_t> indices) { return false; });
    add_nullable_column<int32_t>(columns_by_chunk, column_definitions, "G", cardinalities,
                                 [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); },
                                 [&](std::vector<size_t> indices) { return false; });
    add_nullable_column<int32_t>(columns_by_chunk, column_definitions, "H", cardinalities,
                                 [&](std::vector<size_t> indices) { return generator.random_number(0, 100000); },
                                 [&](std::vector<size_t> indices) { return false; });

    add_column<int32_t>(columns_by_chunk, column_definitions, "X0", cardinalities,
                        [&](std::vector<size_t> indices) { return 0; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X5000", cardinalities,
                        [&](std::vector<size_t> indices) { return 5000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X10000", cardinalities,
                        [&](std::vector<size_t> indices) { return 10000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X15000", cardinalities,
                        [&](std::vector<size_t> indices) { return 15000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X20000", cardinalities,
                        [&](std::vector<size_t> indices) { return 20000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X25000", cardinalities,
                        [&](std::vector<size_t> indices) { return 25000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X30000", cardinalities,
                        [&](std::vector<size_t> indices) { return 30000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X35000", cardinalities,
                        [&](std::vector<size_t> indices) { return 35000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X40000", cardinalities,
                        [&](std::vector<size_t> indices) { return 40000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X45000", cardinalities,
                        [&](std::vector<size_t> indices) { return 45000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X50000", cardinalities,
                        [&](std::vector<size_t> indices) { return 50000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X55000", cardinalities,
                        [&](std::vector<size_t> indices) { return 55000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X60000", cardinalities,
                        [&](std::vector<size_t> indices) { return 60000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X65000", cardinalities,
                        [&](std::vector<size_t> indices) { return 65000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X70000", cardinalities,
                        [&](std::vector<size_t> indices) { return 70000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X75000", cardinalities,
                        [&](std::vector<size_t> indices) { return 75000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X80000", cardinalities,
                        [&](std::vector<size_t> indices) { return 80000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X85000", cardinalities,
                        [&](std::vector<size_t> indices) { return 85000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X90000", cardinalities,
                        [&](std::vector<size_t> indices) { return 90000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X95000", cardinalities,
                        [&](std::vector<size_t> indices) { return 95000; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X100000", cardinalities,
                        [&](std::vector<size_t> indices) { return 100000; });

    auto table = std::make_shared<Table>(column_definitions, TableType::Data, _chunk_size, UseMvcc::Yes);
    for (const auto& chunk_columns : columns_by_chunk) table->append_chunk(chunk_columns);
    tables.emplace("TABLE_SCAN", table);
  }

  {
    std::vector<Segments> columns_by_chunk;
    TableColumnDefinitions column_definitions;

    add_column<int32_t>(columns_by_chunk, column_definitions, "ID", cardinalities,
                        [&](std::vector<size_t> indices) { return indices[0]; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "A", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "B", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "C", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "D", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "E", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "F", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });  // 10
    add_column<int32_t>(columns_by_chunk, column_definitions, "X1", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 0); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X10", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X100", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 99); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X1000", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 999); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X10000", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 9999); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "X100000", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, 99999); });

    auto table = std::make_shared<Table>(column_definitions, TableType::Data, _chunk_size, UseMvcc::Yes);
    for (const auto& chunk_columns : columns_by_chunk) table->append_chunk(chunk_columns);
    tables.emplace("TABLE_AGGREGATE", table);
  }

  {
    std::vector<Segments> columns_by_chunk;
    TableColumnDefinitions column_definitions;

    constexpr size_t number_of_chars = 5;
    const size_t scale_factor = pow(10, number_of_chars);  // 100,000

    add_column<int32_t>(columns_by_chunk, column_definitions, "ID", cardinalities,
                        [&](std::vector<size_t> indices) { return indices[0]; });
    add_column<int32_t>(columns_by_chunk, column_definitions, "I1", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<int32_t>(columns_by_chunk, column_definitions, "I2", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<int64_t>(columns_by_chunk, column_definitions, "L1", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<int64_t>(columns_by_chunk, column_definitions, "L2", cardinalities,
                        [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<float>(columns_by_chunk, column_definitions, "F1", cardinalities,
                      [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<float>(columns_by_chunk, column_definitions, "F2", cardinalities,
                      [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<double>(columns_by_chunk, column_definitions, "D1", cardinalities,
                       [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<double>(columns_by_chunk, column_definitions, "D2", cardinalities,
                       [&](std::vector<size_t> indices) { return generator.random_number(0, scale_factor); });
    add_column<std::string>(columns_by_chunk, column_definitions, "S1", cardinalities,
                            [&](std::vector<size_t> indices) {
                                char str[number_of_chars];
                                for (size_t i = 0; i < number_of_chars; ++i) {
                                    str[i] = static_cast<char>(generator.random_number(65, 75));
                                }
                                return std::string(str, number_of_chars);
                            });
    add_column<std::string>(columns_by_chunk, column_definitions, "S2", cardinalities,
                            [&](std::vector<size_t> indices) {
                                char str[number_of_chars];
                                for (size_t i = 0; i < number_of_chars; ++i) {
                                    str[i] = static_cast<char>(generator.random_number(65, 75));
                                }
                                return std::string(str, number_of_chars);
                            });

    auto table = std::make_shared<Table>(column_definitions, TableType::Data, _chunk_size, UseMvcc::Yes);
    for (const auto& chunk_columns : columns_by_chunk) table->append_chunk(chunk_columns);
    tables.emplace("TABLE_TYPES", table);
  }
  return tables;
}

void JitTableGenerator::generate_and_store() {
  for (auto& pair : generate_all_tables()) {
    StorageManager::get().add_table(pair.first, pair.second);
  }
}

}  // namespace opossum
