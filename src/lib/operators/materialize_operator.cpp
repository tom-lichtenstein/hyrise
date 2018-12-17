#include "materialize_operator.hpp"

#include "storage/create_iterable_from_segment.hpp"
#include "global.hpp"

namespace opossum {

Materialize::Materialize(const std::shared_ptr<const AbstractOperator>& in, const size_t number_of_columns)
        : AbstractReadOnlyOperator(OperatorType::Print, in), _number_of_columns(number_of_columns) {}

const std::string Materialize::name() const { return "Materialize\nnumber of columns: " + std::to_string(_number_of_columns); }

std::shared_ptr<AbstractOperator> Materialize::_on_deep_copy(
        const std::shared_ptr<AbstractOperator>& copied_input_left,
        const std::shared_ptr<AbstractOperator>& copied_input_right) const {
  return std::make_shared<Materialize>(copied_input_left, _number_of_columns);
}

void Materialize::_on_set_parameters(const std::unordered_map<ParameterID, AllTypeVariant>& parameters) {}

std::shared_ptr<const Table> Materialize::_on_execute() {
  const auto& table = input_table_left();

  size_t counter{0};

  for (const auto& chunk : table->chunks()) {
    const auto& segments = chunk->segments();
    for (ColumnID column_id{0}; column_id < _number_of_columns; ++column_id) {
      const auto& segment = segments[column_id];
      resolve_data_and_segment_type(*segment, [&](auto type, auto& typed_segment) {
        using ColumnDataType = typename decltype(type)::type;
        ColumnDataType dummy = ColumnDataType();
        create_iterable_from_segment<ColumnDataType>(typed_segment).with_iterators([&](auto it, auto end) {
          for(; it != end; ++it) {
            if (it->value() == dummy) ++counter;
          }
        });
      });
    }
  }

  if (counter == 16) Global::get().materialize = _number_of_columns;

  return table;
}

}  // namespace opossum
