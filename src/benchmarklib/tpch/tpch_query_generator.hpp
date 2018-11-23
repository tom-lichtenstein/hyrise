#pragma once

#include <boost/date_time/gregorian/gregorian.hpp>

#include "abstract_query_generator.hpp"

namespace opossum {

class TPCHQueryGenerator : public AbstractQueryGenerator {
 public:
  TPCHQueryGenerator(float scale_factor);
  explicit TPCHQueryGenerator(float scale_factor, const std::vector<QueryID>& selected_queries);
  std::string setup_queries() const override;
  std::string build_query(const QueryID query_id) override;
  std::string build_deterministic_query(const QueryID query_id) override;

 protected:
  void _generate_names();
  void _generate_preparation_queries();

  // adds (or subtracts) specified number of months and days
  std::string _calculate_date(boost::gregorian::date date, int months, int days = 0);

  float _scale_factor;
};

}  // namespace opossum
