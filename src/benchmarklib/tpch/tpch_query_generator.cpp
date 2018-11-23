#include "tpch_query_generator.hpp"

#include <algorithm>
#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <iomanip>
#include <numeric>
#include <random>
#include <sstream>

#include "tpch_dbgen.h"
#include "tpch_queries.hpp"
#include "utils/assert.hpp"

namespace opossum {

TPCHQueryGenerator::TPCHQueryGenerator(float scale_factor) : _scale_factor(scale_factor) {
  _generate_names();
  _selected_queries.resize(22);
  std::iota(_selected_queries.begin(), _selected_queries.end(), QueryID{0});
}

TPCHQueryGenerator::TPCHQueryGenerator(float scale_factor, const std::vector<QueryID>& selected_queries)
    : _scale_factor(scale_factor) {
  _generate_names();
  _selected_queries = selected_queries;
}

void TPCHQueryGenerator::_generate_names() {
  _query_names.reserve(22);
  for (auto i = 0; i < 22; ++i) {
    _query_names.emplace_back(std::string("TPC-H ") + std::to_string(i + 1));
  }
}

std::string TPCHQueryGenerator::setup_queries() const {
  std::stringstream sql;
  for (auto query_id = QueryID{0}; query_id < 22; ++query_id) {
    if (query_id + 1 == 15) {
      // We cannot prepare query 15, because the SELECT relies on a view that is generated in the first step. We'll have
      // to manually build this query once we start randomizing the parameters.
      continue;
    }
    sql << tpch_queries.find(query_id + 1)->second;
  }
  return sql.str();
}

std::string TPCHQueryGenerator::build_query(const QueryID query_id) {
  static std::random_device random_device;
  // Preferring a fast random engine over one with high-quality randomness
  static std::minstd_rand random_engine(random_device());

  std::stringstream sql;
  sql << "EXECUTE TPCH" << (query_id + 1) << " (";

  static std::vector materials{"TIN", "NICKEL", "BRASS", "STEEL", "COPPER"};

  // Random distributions for all strings defined by the TPC-H benchmark
  static std::uniform_int_distribution<> material_dist{0, static_cast<int>(materials.size() - 1)};
  static std::uniform_int_distribution<> region_dist{0, regions.count - 1};
  static std::uniform_int_distribution<> segment_dist{0, c_mseg_set.count - 1};
  static std::uniform_int_distribution<> nation_dist{0, nations.count - 1};
  static std::uniform_int_distribution<> type_dist{0, p_types_set.count - 1};
  static std::uniform_int_distribution<> color_dist{0, colors.count - 1};
  static std::uniform_int_distribution<> shipmode_dist{0, l_smode_set.count - 1};
  static std::uniform_int_distribution<> brand_char_dist{1, 5};
  static std::uniform_int_distribution<> container_dist{0, p_cntr_set.count - 1};

  // This is not nice, but initializing this statically would require external methods and make it harder to
  // follow in the end. It's not like this list will ever change...
  static const auto sizes =
      std::vector{1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
                  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50};
  static const auto country_codes =
      std::vector{10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34};

  switch (query_id) {
    // Writing `1-1` to make people aware that this is zero-indexed while TPC-H query names are not
    case 1 - 1: {
      static std::uniform_int_distribution<> date_diff_dist{60, 120};
      const auto date = _calculate_date(boost::gregorian::date{1998, 12, 01}, 0, -date_diff_dist(random_engine));

      sql << "'" << date << "'";
      break;
    }

    case 2 - 1: {
      static std::uniform_int_distribution<> size_dist{1, 50};
      const auto size = size_dist(random_engine);
      const auto material = materials[material_dist(random_engine)];
      const auto region = regions.list[region_dist(random_engine)].text;

      sql << size << ", '%" << material << "', '" << region << "', '" << region << "'";
      break;
    }

    case 3 - 1: {
      const auto segment = c_mseg_set.list[segment_dist(random_engine)].text;
      static std::uniform_int_distribution<> date_diff_dist{0, 30};
      const auto date = _calculate_date(boost::gregorian::date{1995, 03, 01}, 0, date_diff_dist(random_engine));

      sql << "'" << segment << "', '" << date << "', '" << date << "'";
      break;
    }

    case 4 - 1: {
      static std::uniform_int_distribution<> date_diff_dist{0, 9};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff + 3);

      sql << "'" << begin_date << "', '" << end_date << "'";
      break;
    }

    case 5 - 1: {
      const auto region = regions.list[region_dist(random_engine)].text;

      static std::uniform_int_distribution<> date_diff_dist{0, 4};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, (diff + 1) * 12);

      sql << "'" << region << "', '" << begin_date << "', '" << end_date << "'";
      break;
    }

    case 6 - 1: {
      static std::uniform_int_distribution<> date_diff_dist{0, 4};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, (diff + 1) * 12);

      static std::uniform_real_distribution<> discount_dist{0.02f, 0.09f};
      const auto discount = discount_dist(random_engine);

      static std::uniform_int_distribution<> quantity_dist{24, 25};
      const auto quantity = quantity_dist(random_engine);

      sql << "'" << begin_date << "', '" << end_date << "', " << (discount - 0.01f) << ", " << (discount + 0.01f)
          << ", " << quantity;
      break;
    }

    case 7 - 1: {
      const auto nation1 = nations.list[nation_dist(random_engine)].text;
      const auto nation2 = nations.list[nation_dist(random_engine)].text;

      sql << "'" << nation1 << "', '" << nation2 << "', '" << nation2 << "', '" << nation1 << "'";
      break;
    }

    case 8 - 1: {
      const auto nation_id = nation_dist(random_engine);
      const auto nation = nations.list[nation_id].text;

      // No idea why the field is called "weight", but it corresponds to the region of a given nation
      const auto region = regions.list[nations.list[nation_id].weight].text;

      const auto type = p_types_set.list[type_dist(random_engine)].text;

      sql << "'" << nation << "', '" << region << "', '" << type << "'";
      break;
    }

    case 9 - 1: {
      const auto color = colors.list[color_dist(random_engine)].text;

      sql << "'%" << color << "%'";
      break;
    }

    case 10 - 1: {
      static std::uniform_int_distribution<> date_diff_dist{0, 23};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, (diff + 3));

      sql << "'" << begin_date << "', '" << end_date << "'";
      break;
    }

    case 11 - 1: {
      const auto nation = nations.list[nation_dist(random_engine)].text;
      const auto fraction = 0.0001 / (_scale_factor > 0 ? _scale_factor : 1);

      sql << "'" << nation << "', " << fraction << ", '" << nation << "'";
      break;
    }

    case 12 - 1: {
      const auto shipmode1 = l_smode_set.list[shipmode_dist(random_engine)].text;
      std::string shipmode2;
      do {
        shipmode2 = l_smode_set.list[shipmode_dist(random_engine)].text;
      } while (shipmode1 == shipmode2);

      static std::uniform_int_distribution<> date_diff_dist{0, 4};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, (diff + 1) * 12);

      sql << "'" << shipmode1 << "', '" << shipmode2 << "', '" << begin_date << "', '" << end_date << "'";
      break;
    }

    case 13 - 1: {
      const auto words1 = std::vector{"special", "pending", "unusual", "express"};
      const auto words2 = std::vector{"packages", "requests", "accounts", "deposits"};

      static std::uniform_int_distribution<> word_dist{0, 3};

      sql << "'%" << words1[word_dist(random_engine)] << '%' << words2[word_dist(random_engine)] << "%'";
      break;
    }

    case 14 - 1: {
      static std::uniform_int_distribution<> date_diff_dist{0, 47};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff + 1);

      sql << "'" << begin_date << "', '" << end_date << "'";
      break;
    }

    case 15 - 1: {
      auto query_15 = std::string{tpch_queries.at(15)};

      static std::uniform_int_distribution<> date_diff_dist{0, 4};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12 + 1);

      // This is bloody disgusting. But since we cannot use prepared statements in TPC-H 15 and want to keep all
      // queries in a readable form in tpch_queries.cpp, I cannot come up with anything better. At least we can
      // assert that nobody tampered with the string over there.
      static constexpr auto begin_date_offset = 156;
      static constexpr auto end_date_offset = 192;
      DebugAssert((std::string_view{&query_15[begin_date_offset], 10} == "1996-01-01" &&
                   std::string_view{&query_15[end_date_offset], 10} == "1996-04-01"),
                  "TPC-H 15 string has been modified");
      query_15.replace(begin_date_offset, 10, begin_date);
      query_15.replace(end_date_offset, 10, end_date);

      static size_t view_id = 0;
      boost::replace_all(query_15, std::string("revenueview"), std::string("revenue") + std::to_string(view_id++));
      std::cout << query_15 << std::endl;
      return query_15;
    }

    case 16 - 1: {
      const auto brand = brand_char_dist(random_engine) * 10 + brand_char_dist(random_engine);

      const auto full_type = std::string{p_types_set.list[type_dist(random_engine)].text};
      const auto partial_type = std::string(full_type, 0, full_type.find_last_of(' '));

      auto sizes_copy = sizes;
      std::shuffle(sizes_copy.begin(), sizes_copy.end(), random_engine);

      sql << "'Brand#" << brand << "', '" << partial_type << "%'";
      for (auto i = 0; i < 8; ++i) sql << ", " << sizes_copy[i];
      break;
    }

    case 17 - 1: {
      const auto brand = brand_char_dist(random_engine) * 10 + brand_char_dist(random_engine);
      const auto container = p_cntr_set.list[nation_dist(random_engine)].text;

      sql << "'Brand#" << brand << "', '" << container << "'";
      break;
    }

    case 18 - 1: {
      static std::uniform_int_distribution<> quantity_dist{312, 315};
      const auto quantity = quantity_dist(random_engine);

      sql << quantity;
      break;
    }

    case 19 - 1: {
      static std::uniform_int_distribution<> quantity1_dist{1, 10};
      static std::uniform_int_distribution<> quantity2_dist{10, 20};
      static std::uniform_int_distribution<> quantity3_dist{20, 30};
      const auto quantity1 = quantity1_dist(random_engine);
      const auto quantity2 = quantity2_dist(random_engine);
      const auto quantity3 = quantity3_dist(random_engine);
      const auto brand1 = brand_char_dist(random_engine) * 10 + brand_char_dist(random_engine);
      const auto brand2 = brand_char_dist(random_engine) * 10 + brand_char_dist(random_engine);
      const auto brand3 = brand_char_dist(random_engine) * 10 + brand_char_dist(random_engine);

      sql << "'Brand#" << brand1 << "', " << quantity1 << ", " << quantity1 << ", 'Brand#" << brand2 << "', "
          << quantity2 << ", " << quantity2 << ", 'Brand#" << brand3 << "', " << quantity3 << ", " << quantity3;
      break;
    }

    case 20 - 1: {
      const auto color = colors.list[color_dist(random_engine)].text;
      static std::uniform_int_distribution<> date_diff_dist{0, 4};
      const auto diff = date_diff_dist(random_engine);
      const auto begin_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, diff * 12);
      const auto end_date = _calculate_date(boost::gregorian::date{1993, 01, 01}, (diff + 1) * 12);
      const auto nation = nations.list[nation_dist(random_engine)].text;

      sql << "'" << color << "', '" << begin_date << "', '" << end_date << "', '" << nation << "'";
      break;
    }

    case 21 - 1: {
      const auto nation = nations.list[nation_dist(random_engine)].text;

      sql << "'" << nation << "'";
      break;
    }

    case 22 - 1: {
      auto country_codes_copy = country_codes;
      std::shuffle(country_codes_copy.begin(), country_codes_copy.end(), random_engine);

      for (auto i = 0; i < 7; ++i) sql << country_codes_copy[i] << ", ";
      for (auto i = 0; i < 7; ++i) sql << country_codes_copy[i] << (i < 6 ? ", " : "");
      break;
    }

    default:
      Fail("There are only 22 TPC-H queries");
  }

  sql << ")";

  std::cout << sql.str() << std::endl;

  return sql.str();
}

std::string TPCHQueryGenerator::build_deterministic_query(const QueryID query_id) {
  DebugAssert(query_id < 22, "There are only 22 TPC-H queries");

  if (query_id == 14) {
    return tpch_queries.find(15)->second;
  }

  static std::vector<std::string> execute_statements = {
      "EXECUTE TPCH1  ('1998-09-02');",
      "EXECUTE TPCH2  (15, '%BRASS', 'EUROPE', 'EUROPE');",
      "EXECUTE TPCH3  ('BUILDING', '1995-03-15', '1995-03-15');",
      "EXECUTE TPCH4  ('1996-07-01', '1996-10-01');",
      "EXECUTE TPCH5  ('AMERICA', '1994-01-01', '1995-01-01');",
      "EXECUTE TPCH6  ('1994-01-01', '1995-01-01', .06, .06, 24);",
      "EXECUTE TPCH7  ('IRAN', 'IRAQ', 'IRAQ', 'IRAN');",
      "EXECUTE TPCH8  ('BRAZIL', 'AMERICA', 'ECONOMY ANODIZED STEEL');",
      "EXECUTE TPCH9  ('%green%');",
      "EXECUTE TPCH10 ('1993-10-01', '1994-01-01');",
      "EXECUTE TPCH11 ('GERMANY', 0.0001, 'GERMANY');",
      "EXECUTE TPCH12 ('MAIL', 'SHIP', '1994-01-01', '1995-01-01');",
      "EXECUTE TPCH13 ('%special%requests%');",
      "EXECUTE TPCH14 ('1995-09-01', '1995-10-01');",
      "",  // handled above.
      "EXECUTE TPCH16 ('Brand#45', 'MEDIUM POLISHED%', 49, 14, 23, 45, 19, 3, 36, 9);",
      "EXECUTE TPCH17 ('Brand#23', 'MED BOX');",
      "EXECUTE TPCH18 (300);",
      "EXECUTE TPCH19 ('Brand#12', 1, 1, 'Brand#23', 10, 10, 'Brand#34', 20, 20);",
      "EXECUTE TPCH20 ('forest%', '1995-01-01', '1994-01-01', 'CANADA');",
      "EXECUTE TPCH21 ('SAUDI ARABIA');",
      "EXECUTE TPCH22 ('13', '31', '23', '29', '30', '18', '17', '13', '31', '23', '29', '30', '18', '17');"};

  return execute_statements[query_id];
}

std::string TPCHQueryGenerator::_calculate_date(boost::gregorian::date date, int months, int days) {
  date = date + boost::gregorian::months(months) + boost::gregorian::days(days);

  std::stringstream output;
  output << date.year() << "-" << std::setw(2) << std::setfill('0') << date.month().as_number() << "-" << std::setw(2)
         << std::setfill('0') << date.day();
  return output.str();
}

}  // namespace opossum
