#pragma once

#include "abstract_jittable.hpp"
#include "jit_read_tuples.hpp"

namespace opossum {

class BaseJitSegmentReaderWrapper;

class JitReadValue : public AbstractJittable {
 public:
  explicit JitReadValue(const JitInputColumn input_column,
          std::shared_ptr<BaseJitSegmentReaderWrapper> input_segment_wrapper)
          : AbstractJittable(JitOperatorType::ReadValue)
      , _input_column(input_column),
        _input_segment_wrapper(input_segment_wrapper)
  {
  }

  std::string description() const final;

 private:
  void _consume(JitRuntimeContext& context) const final;
  const JitInputColumn _input_column;
  const std::shared_ptr<BaseJitSegmentReaderWrapper> _input_segment_wrapper;
};

}  // namespace opossum
