#pragma once

#include "abstract_jittable.hpp"

namespace opossum {

class JitExpression;
class BaseJitSegmentReaderWrapper;

/* The JitFilter operator filters on a single boolean value and only passes on
 * tuple, for which that value is non-null and true.
 */
class JitFilter : public AbstractJittable {
 public:
  explicit JitFilter(const JitTupleValue& condition);
  explicit JitFilter(const std::shared_ptr<const JitExpression>& expression);

  std::string description() const final;

  JitTupleValue condition();

  std::map<size_t, bool> accessed_column_ids() const final;
  void set_load_column(const size_t tuple_id, const std::shared_ptr<BaseJitSegmentReaderWrapper> input_segment_wrapper, const bool also_set) const;

 private:
  void _consume(JitRuntimeContext& context) const final;

  const JitTupleValue _condition;
  std::shared_ptr<const JitExpression> _expression;
};

}  // namespace opossum
