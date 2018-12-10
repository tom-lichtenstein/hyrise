#include "jit_read_value.hpp"

#include "constant_mappings.hpp"
#include "jit_segment_reader.hpp"

namespace opossum {

std::string JitReadValue::description() const {
  std::stringstream desc;
  desc << "[ReadValue] x" << _input_column.tuple_value.tuple_index() << " = Col#" << _input_column.column_id << ", ";
  return desc.str();
}

void JitReadValue::_consume(JitRuntimeContext& context) const {
#if JIT_READER_WRAPPER
  _input_segment_wrapper->read_value(context);
#else
  context.inputs[_reader_index]->read_value(context);
#endif
  _emit(context);
}

}  // namespace opossum
