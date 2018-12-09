#if HYRISE_JIT_SUPPORT

#include <json.hpp>

#if PAPI_SUPPORT
#include <papi.h>
#endif

#include <iostream>
#include <algorithm>


#include "jit/jit_table_generator.hpp"
#include "jit_evaluation_helper.hpp"
#include "operators/jit_operator/specialization/jit_repository.hpp"
#include "visualization/lqp_visualizer.hpp"
#include "visualization/sql_query_plan_visualizer.hpp"
#include "resolve_type.hpp"
#include "scheduler/current_scheduler.hpp"
#include "sql/sql_pipeline.hpp"
#include "storage/chunk_encoder.hpp"
#include "storage/vector_compression/fixed_size_byte_aligned/fixed_size_byte_aligned_vector.hpp"
#include "tpch/tpch_db_generator.hpp"

#include "global.hpp"
#include "sql/sql_pipeline_builder.hpp"
#include "sql/sql_query_cache.hpp"
#include "types.hpp"
#include "logical_query_plan/abstract_lqp_node.hpp"

const size_t cache_line = 64;

template <typename Vector>
void remove_vector_from_cache(Vector& vector) {
  for (auto& value : vector) {
    const auto address = reinterpret_cast<size_t>(&value);
    if (address % cache_line == 0) {
      asm volatile("clflush (%0)\n\t" : : "r"(&value) : "memory");
    }
  }
  asm volatile("sfence\n\t" : : : "memory");
}

void remove_table_from_cache(opossum::Table& table) {
  for (opossum::ChunkID chunk_id{0}; chunk_id < table.chunk_count(); ++chunk_id) {
    auto chunk = table.get_chunk(chunk_id);
    for (opossum::ColumnID column_id{0}; column_id < table.column_count(); ++column_id) {
      auto segment = chunk->get_segment(column_id);
      const auto data_type = table.column_data_type(column_id);
      opossum::resolve_data_type(data_type, [&](auto type) {
        using ColumnDataType = typename decltype(type)::type;

        if (auto value_column = std::dynamic_pointer_cast<const opossum::ValueSegment<ColumnDataType>>(segment)) {
          remove_vector_from_cache(value_column->values());
          if (table.column_is_nullable(column_id)) {
#if CONCURRENT_VECTOR
            remove_vector_from_cache(value_column->null_values());
#endif
          }
        } else if (auto dict_column =
                       std::dynamic_pointer_cast<const opossum::DictionarySegment<ColumnDataType>>(segment)) {
          remove_vector_from_cache(*dict_column->dictionary());
          auto base_attribute_vector = dict_column->attribute_vector();

          if (auto attribute_vector = std::dynamic_pointer_cast<const opossum::FixedSizeByteAlignedVector<uint8_t>>(
                  base_attribute_vector)) {
            remove_vector_from_cache(attribute_vector->data());
          } else if (auto attribute_vector =
                         std::dynamic_pointer_cast<const opossum::FixedSizeByteAlignedVector<uint16_t>>(
                             base_attribute_vector)) {
            remove_vector_from_cache(attribute_vector->data());
          } else if (auto attribute_vector =
                         std::dynamic_pointer_cast<const opossum::FixedSizeByteAlignedVector<uint32_t>>(
                             base_attribute_vector)) {
            remove_vector_from_cache(attribute_vector->data());
          } else {
            throw std::logic_error("could not flush cache, unknown attribute vector type");
          }
        } else {
          throw std::logic_error("could not flush cache, unknown column type");
        }
      });
    }
  }
}

void lqp(const std::string& query_string) {
  auto& experiment = opossum::JitEvaluationHelper::get().experiment();
  const bool optimize = experiment["optimize"];
  const std::string lqp_file = experiment["lqp_file"];
  bool mvcc = experiment["mvcc"];

  opossum::SQLPipeline pipeline =
      opossum::SQLPipelineBuilder(query_string).with_mvcc(opossum::UseMvcc(mvcc)).create_pipeline();
  const auto plans = optimize ? pipeline.get_optimized_logical_plans() : pipeline.get_unoptimized_logical_plans();

  opossum::LQPVisualizer visualizer;
  visualizer.visualize(plans, lqp_file + ".dot", lqp_file + ".png");
}

std::vector<std::pair<std::string, std::string>> get_query_string(const std::string& query_id) {
  const auto& query = opossum::JitEvaluationHelper::get().queries()[query_id];
  auto query_string = query["query"].get<std::string>();
  if (!query.count("parameters")) {
    return {std::make_pair(query_id, query_string)};
  }
  std::vector<std::vector<int>> parameters;
  const size_t size = query["parameters"].size();
  for (size_t i = 0; i < size; ++i) {
    parameters.emplace_back(query["parameters"][i].get<std::vector<int>>());
  }
  std::vector<std::pair<std::string, std::string>> query_strings;
  size_t n = std::count(query_string.begin(), query_string.end(), '?');
  if (size != n) std::cerr << "Parameter count mismatch for query " << query_string << " with id: " << query_id << std::endl;
  if (n == 0) return {std::make_pair(query_id, query_string)};
  size_t num_iterations = parameters.front().size();
  for (size_t i = 0; i < num_iterations; ++i) {
    std::string new_query_string = query_string;
    std::string id = query_id;
    for (size_t parameter_id = 0; parameter_id < n; ++parameter_id) {
      const auto value = std::to_string(parameters[parameter_id][i]);
      id += "_" + value;
      boost::replace_first(new_query_string, std::string("?"), value);
    }
    query_strings.push_back(std::make_pair(id, new_query_string));
  }
  return query_strings;
}

void pqp(const std::string& query_string) {
  auto& experiment = opossum::JitEvaluationHelper::get().experiment();
  const std::string pqp_file = experiment["pqp_file"];
  bool mvcc = experiment["mvcc"];

  opossum::Global::get().jit_evaluate = true;
  auto& result = opossum::JitEvaluationHelper::get().result();

  result = nlohmann::json::object();
  if (experiment.at("engine") == "jit") {
    opossum::JitEvaluationHelper::get().result()["dynamic_resolved"] = 0;
    opossum::JitEvaluationHelper::get().result()["static_resolved"] = 0;
    opossum::JitEvaluationHelper::get().result()["resolved_vtables"] = 0;
    opossum::JitEvaluationHelper::get().result()["not_resolved_vtables"] = 0;
    opossum::JitEvaluationHelper::get().result()["inlined_functions"] = 0;
    opossum::JitEvaluationHelper::get().result()["replaced_values"] = 0;
  }
  opossum::SQLPipeline pipeline = opossum::SQLPipelineBuilder(query_string)
                                      .with_mvcc(opossum::UseMvcc(mvcc))
                                      .dont_cleanup_temporaries()
                                      .create_pipeline();
  pipeline.get_result_table();
  opossum::SQLQueryPlan query_plan(opossum::CleanupTemporaries::No);
  const auto plans = pipeline.get_query_plans();
  opossum::Global::get().jit_evaluate = false;
  for (const auto& plan : plans) {
    query_plan.append_plan(*plan);
  }

  opossum::SQLQueryPlanVisualizer visualizer;
  visualizer.visualize(query_plan, pqp_file + ".dot", pqp_file + ".png");
}

void run(const std::string& query_string) {
  opossum::Global::get().jit_evaluate = true;
  auto& experiment = opossum::JitEvaluationHelper::get().experiment();
  const std::string query_id = experiment["query_id"];
  const auto query = opossum::JitEvaluationHelper::get().queries()[query_id];
  bool mvcc = experiment["mvcc"];

  const auto table_names = query["tables"];
  for (const auto& table_name : table_names) {
    auto table = opossum::StorageManager::get().get_table(table_name.get<std::string>());
    remove_table_from_cache(*table);
  }

  // Make sure all table statistics are generated and ready.
  opossum::SQLPipelineBuilder(query_string)
      .with_mvcc(opossum::UseMvcc(mvcc))
      .create_pipeline()
      .get_optimized_logical_plans();
  auto& result = opossum::JitEvaluationHelper::get().result();

  result = nlohmann::json::object();
  if (experiment.at("engine") == "jit") {
    result["dynamic_resolved"] = 0;
    result["static_resolved"] = 0;
    result["resolved_vtables"] = 0;
    result["not_resolved_vtables"] = 0;
    result["inlined_functions"] = 0;
    result["replaced_values"] = 0;
  }

  opossum::SQLPipeline pipeline =
          opossum::SQLPipelineBuilder(query_string).with_mvcc(opossum::UseMvcc(mvcc)).create_pipeline();
  const auto table = pipeline.get_result_table();

  if (experiment.count("print") && experiment["print"]) {
    opossum::Print::print(table, 0, std::cerr);
  }

  result["result_rows"] = table ? table->row_count() : 0;
  result["pipeline_compile_time"] = pipeline.metrics().statement_metrics.front()->sql_translate_time_nanos.count() / 1000 +
                                    pipeline.metrics().statement_metrics.front()->lqp_translate_time_nanos.count() / 1000;
  result["pipeline_execution_time"] = pipeline.metrics().statement_metrics.front()->execution_time_nanos.count() / 1000;
  result["pipeline_optimize_time"] = pipeline.metrics().statement_metrics.front()->optimize_time_nanos.count() / 1000;
  nlohmann::json instruction_counts;
  for (const auto& pair : opossum::Global::get().instruction_counts) {
    instruction_counts[pair.first] = pair.second;
  }
  result["instruction_counts"] = instruction_counts;
  opossum::Global::get().jit_evaluate = false;
}

nlohmann::json generate_input_different_selectivity(const bool use_jit) {
  nlohmann::json globals{
      {"scale_factor", 10}, {"use_other_tables", true}, {"use_tpch_tables", false}, {"dictionary_compress", true}};
  nlohmann::json queries;
  nlohmann::json experiments = nlohmann::json::array();
  const int repetitions = 50;
  const std::string table_name = "TABLE_AGGREGATE";
  const std::vector<std::string> column_names{"A", "B", "C", "D", "E", "F"};
  for (size_t filter_value = 0; filter_value <= 10000; filter_value += 1000) {
    for (size_t no_columns = 1; no_columns <= column_names.size(); ++no_columns) {
      std::string sql = "SELECT ID FROM " + table_name + " WHERE";
      for (size_t index = 0; index < no_columns; ++index) {
        if (index > 0) sql += " AND";
        sql += " " + column_names[index] + " >= " + std::to_string(filter_value);
      }
      sql += ";";
      std::string query_id =
          table_name + "_FILTER_VAL_" + std::to_string(filter_value) + "_NO_COL_" + std::to_string(no_columns);
      nlohmann::json query{{"query", sql}, {"tables", nlohmann::json::array()}};
      query["tables"].push_back(table_name);
      queries[query_id] = query;
      experiments.push_back({{"engine", use_jit ? "jit" : "opossum"},
                             {"repetitions", repetitions},
                             {"task", "run"},
                             {"query_id", query_id}});
    }
  }
  return {{"globals", globals}, {"queries", queries}, {"experiments", experiments}};
}

void generte_tables(const nlohmann::json& config, const float scale_factor) {
  opossum::StorageManager::get().reset();
  if (config["globals"]["use_tpch_tables"]) {
    std::cerr << "Generating TPCH tables with scale factor " << scale_factor << std::endl;
    opossum::TpchDbGenerator generator(scale_factor, opossum::ChunkID(100000));
    generator.generate_and_store();
  }

  if (config["globals"]["use_other_tables"]) {
    std::cerr << "Generating JIT tables with scale factor " << scale_factor << std::endl;
    opossum::JitTableGenerator generator(scale_factor, opossum::ChunkID(100000));
    generator.generate_and_store();
  }

  if (config["globals"]["dictionary_compress"]) {
    std::cerr << "Dictionary encoding tables" << std::endl;
    for (const auto& table_name : opossum::StorageManager::get().table_names()) {
      auto table = opossum::StorageManager::get().get_table(table_name);
      opossum::ChunkEncodingSpec chunk_spec;
      for (const auto& column_data_type : table->column_data_types()) {
        if (column_data_type == opossum::DataType::String) {
          chunk_spec.emplace_back(opossum::EncodingType::Dictionary);
        } else {
          chunk_spec.emplace_back(opossum::EncodingType::Unencoded);
        }
      }
      opossum::ChunkEncoder::encode_all_chunks(table, chunk_spec);
    }
  }

  std::cerr << "Table Information" << std::endl;
  for (const auto& table_name : opossum::StorageManager::get().table_names()) {
    auto table = opossum::StorageManager::get().get_table(table_name);
    std::cerr << table_name << ": " << table->row_count() << " rows, " << table->chunk_count() << " chunks, "
              << table->column_count() << " columns, " << table->estimate_memory_usage() << " bytes" << std::endl;
  }
}

int main(int argc, char* argv[]) {
  std::cerr << "Starting the JIT benchmarking suite" << std::endl;

  const std::string output_file_name = (argc <= 1) ? "output.json" : argv[1];
  const std::string input_file_name = (argc <= 2) ? "input.json" : argv[2];

  nlohmann::json config;
  if (argc <= 3) {
    std::ifstream input_file{input_file_name};
    input_file >> config;
  } else {
    bool no_jit = argc >= 5 && (std::string(argv[4]) == "opossum");
    config = generate_input_different_selectivity(!no_jit);
  }
  opossum::JitEvaluationHelper::get().queries() = config["queries"];
  opossum::JitEvaluationHelper::get().globals() = config["globals"];

  const auto additional_scale_factor = 1.0;  // argc > 1 ? std::stod(argv[1]) : 1.0;
  const double scale_factor = config["globals"]["scale_factor"].get<double>() * additional_scale_factor;
  config["globals"]["scale_factor"] = scale_factor;

  double current_scale_factor = scale_factor;

  generte_tables(config, current_scale_factor);

  std::cerr << "Initializing JIT repository" << std::endl;
  opossum::JitRepository::get();

#if PAPI_SUPPORT
  std::cerr << "Initializing PAPI" << std::endl;
  if (PAPI_library_init(PAPI_VER_CURRENT) < 0) throw std::logic_error("PAPI error");
  std::cerr << "  supports " << PAPI_num_counters() << " event counters" << std::endl;
#endif
  nlohmann::json file_output{{"results", nlohmann::json::array()}};

  const size_t num_experiments = config["experiments"].size();
  for (size_t current_experiment = 0; current_experiment < num_experiments; ++current_experiment) {
    auto& experiment = config["experiments"][current_experiment];
    if (!experiment.count("mvcc")) experiment["mvcc"] = false;
    if (!experiment.count("optimize")) experiment["optimize"] = true;
    if (!experiment.count("hand_written")) experiment["hand_written"] = false;
    if (!experiment.count("scale_factor")) experiment["scale_factor"] = scale_factor;
    const auto experiment_scale_factor = experiment["scale_factor"].get<double>();
    if (current_scale_factor != experiment_scale_factor) {
      current_scale_factor = experiment_scale_factor;
      generte_tables(config, current_scale_factor);
    }
    if (!experiment.count("use_limit_in_subquery")) {
      experiment["use_limit_in_subquery"] = experiment.at("engine") == "jit";
    }
    opossum::Global::get().use_limit_in_subquery = experiment["use_limit_in_subquery"];
    if (experiment.at("engine") == "opossum") {
      opossum::Global::get().jit = false;
    } else if (experiment.at("engine") == "jit") {
      if (!experiment.count("lazy_load")) experiment["lazy_load"] = true;
      if (!experiment.count("jit_validate")) experiment["jit_validate"] = true;
      if (!experiment.count("jit_use_jit")) experiment["jit_use_jit"] = true;
      if (!experiment.count("jit_limit")) experiment["jit_limit"] = true;
      if (!experiment.count("allow_single_predicate")) experiment["allow_single_predicate"] = false;
      if (!experiment.count("use_value_id")) experiment["use_value_id"] = true;
      if (!experiment.count("reference_output")) experiment["reference_output"] = true;
      opossum::Global::get().jit = true;
      opossum::Global::get().lazy_load = experiment["lazy_load"];
      opossum::Global::get().jit_validate = experiment["jit_validate"];
      opossum::Global::get().jit_limit = experiment["jit_limit"];
      opossum::Global::get().allow_single_predicate = experiment["allow_single_predicate"];
      opossum::Global::get().use_value_id = experiment["use_value_id"];
      opossum::Global::get().reference_output = experiment["reference_output"];
    } else {
      opossum::Fail("unknown query engine parameter");
    }
    auto query_pairs = get_query_string(experiment["query_id"]);
    if (experiment["task"] != "run" && query_pairs.size() > 1) {
      query_pairs = {query_pairs.front()};
    }
    const size_t query_pairs_count = query_pairs.size();
    size_t current_query_pairs = 0;
    for (const auto& pair : query_pairs) {
      ++current_query_pairs;
      const auto& [query_id, query_string] = pair;
      opossum::JitEvaluationHelper::get().experiment() = experiment;
      nlohmann::json output{
              {"globals", config["globals"]}, {"experiment", experiment}, {"results", nlohmann::json::array()}};
      const uint32_t num_repetitions = experiment.count("repetitions") ? experiment["repetitions"].get<uint32_t>() : 1;
      uint32_t current_repetition = 0;
      for (uint32_t i = 0; i < num_repetitions; ++i) {
        opossum::SQLQueryCache<opossum::SQLQueryPlan>::get().clear();
        opossum::SQLQueryCache<std::shared_ptr<opossum::AbstractLQPNode>>::get().clear();
        current_repetition++;
        std::cerr << "Running experiment " << (current_experiment + 1) << "/" << num_experiments
        << " parameter combination " << (current_query_pairs) << "/" << query_pairs_count
        << " repetition " << current_repetition << "/" << num_repetitions << std::endl;

        opossum::JitEvaluationHelper::get().result() = nlohmann::json::object();
        opossum::Global::get().times.clear();
        opossum::Global::get().instruction_counts.clear();
        if (experiment["task"] == "lqp") {
          lqp(query_string);
          break;
        } else if (experiment["task"] == "pqp") {
          pqp(query_string);
          break;
        } else if (experiment["task"] == "run") {
          run(query_string);
        } else {
          throw std::logic_error("unknown task");
        }
        if constexpr (!PAPI_SUPPORT) {
          auto& result = opossum::JitEvaluationHelper::get().result();
          nlohmann::json operators = nlohmann::json::array();
          for (const auto pair : opossum::Global::get().times) {
            if (pair.second.execution_time.count() > 0) {
              operators.push_back({{"name", pair.first}, {"prepare", false}, {"walltime", pair.second.execution_time.count()}});
            };
            if (pair.second.__execution_time.count() > 0) {
              operators.push_back({{"name", "__" + pair.first}, {"prepare", false}, {"walltime", pair.second.__execution_time.count()}});
            };
            if (pair.second.preparation_time.count() > 0) {
              operators.push_back({{"name", pair.first}, {"prepare", true}, {"walltime", pair.second.preparation_time.count()}});
            };
            if (pair.second.__preparation_time.count() > 0) {
              operators.push_back({{"name", "__" + pair.first}, {"prepare", true}, {"walltime", pair.second.__preparation_time.count()}});
            };
          }
          result["operators"] = operators;
        }
        output["results"].push_back(opossum::JitEvaluationHelper::get().result());
      }
      if (experiment["task"] != "run") continue;
      output["query_id"] = query_id;
      output["query_string"] = query_string;
      file_output["results"].push_back(output);
    }
  }
  std::ofstream output_file{output_file_name};
  output_file << file_output;
  std::cerr << "Done" << std::endl;
}

#else
int main() {}
#endif
