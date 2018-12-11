#pragma once

#include <x86intrin.h>

// #include <boost/type_index.hpp>

#include "../jit_types.hpp"
#include "storage/segment_iterables/create_iterable_from_attribute_vector.hpp"

namespace opossum {

/* Base class for all segment readers.
 * We need this class, so we can store a number of JitSegmentReaders with different template
 * specializations in a common data structure.
 */

#define JIT_EXPLICIT_INSTANTIATION_GET_FUNCTION(r, _, type) \
virtual Value<BOOST_PP_TUPLE_ELEM(3, 0, type)> read_and_get_value(JitRuntimeContext& context, BOOST_PP_TUPLE_ELEM(3, 0, type)) { \
    Fail("Reading wrong datatype from segment reader"); \
  }

#define JIT_EXPLICIT_INSTANTIATION_GET_CONST_FUNCTION(r, _, type) \
  virtual Value<BOOST_PP_TUPLE_ELEM(3, 0, type)> read_and_get_value(JitRuntimeContext& context, BOOST_PP_TUPLE_ELEM(3, 0, type)) const { \
     return context.inputs[reader_index]->read_and_get_value(context, BOOST_PP_TUPLE_ELEM(3, 0, type)()); \
  }  // Used when input segments not available during specialization  Fail("Reading wrong datatype from segment reader wrapper");

class BaseJitSegmentReader {
 public:
  virtual ~BaseJitSegmentReader() = default;
  BOOST_PP_SEQ_FOR_EACH(JIT_EXPLICIT_INSTANTIATION_GET_FUNCTION, _, JIT_DATA_TYPE_INFO)
#if JIT_OLD_LAZY_LOAD
  virtual void increment() = 0;
  virtual void read_value(JitRuntimeContext& context) const = 0;
#else
  virtual void read_value(JitRuntimeContext& context) = 0;
#endif
};

class BaseJitSegmentReaderWrapper {
 public:
  BaseJitSegmentReaderWrapper(size_t reader_index) : reader_index(reader_index) {}
  virtual ~BaseJitSegmentReaderWrapper() = default;
  virtual void read_value(JitRuntimeContext& context) const {
    // Fail("Should not be executed.");
    context.inputs[reader_index]->read_value(context);
  }
  virtual bool same_type(JitRuntimeContext& context) = 0;
  /*
  {
    return true;
  }
  */
  BOOST_PP_SEQ_FOR_EACH(JIT_EXPLICIT_INSTANTIATION_GET_CONST_FUNCTION, _, JIT_DATA_TYPE_INFO)

#if JIT_OLD_LAZY_LOAD
  virtual void increment(JitRuntimeContext& context) const {
    context.inputs[reader_index]->increment();
  }
#endif

  const size_t reader_index;
};

/* JitSegmentReaders wrap the segment iterable interface used by most operators and makes it accessible
 * to the JitOperatorWrapper.
 *
 * Why we need this wrapper:
 * Most operators access data by creating a fixed number (usually one or two) of segment iterables and
 * then immediately use those iterators in a lambda. The JitOperatorWrapper, on the other hand, processes
 * data in a tuple-at-a-time fashion and thus needs access to an arbitrary number of segment iterators
 * at the same time.
 *
 * We solve this problem by introducing a template-free super class to all segment iterators. This allows us to
 * create an iterator for each input segment (before processing each chunk) and store these iterators in a
 * common vector in the runtime context.
 * We then use JitSegmentReader instances to access these iterators. JitSegmentReaders are templated with the
 * type of iterator they are supposed to handle. They are initialized with an input_index and a tuple value.
 * When requested to read a value, they will access the iterator from the runtime context corresponding to their
 * input_index and copy the value to their JitTupleValue.
 *
 * All segment readers have a common template-free base class. That allows us to store the segment readers in a
 * vector as well and access all types of segments with a single interface.
 */
template <typename Iterator, typename DataType, bool Nullable>
class JitSegmentReader : public BaseJitSegmentReader {
public:
  using DATA_TYPE = DataType;
  JitSegmentReader(const Iterator& iterator, const JitTupleValue& tuple_value)
          : _iterator{iterator}, _tuple_index{tuple_value.tuple_index()} {}

  // Reads a value from the _iterator into the _tuple_value and increments the _iterator.
#if JIT_OLD_LAZY_LOAD
  __attribute__((always_inline)) void increment() final { ++_iterator; }
  __attribute__((always_inline)) void read_value(JitRuntimeContext& context) const final {
#else
  __attribute__((always_inline)) void read_value(JitRuntimeContext& context) final {
#if JIT_LAZY_LOAD && !JIT_OLD_LAZY_LOAD
    const size_t current_offset = context.chunk_offset;
    _iterator += current_offset - _chunk_offset;
    _chunk_offset = current_offset;
#endif
#endif
    const auto& value = *_iterator;
    // clang-format off
    if constexpr (Nullable) {
      context.tuple.set_is_null(_tuple_index, value.is_null());
      if (!value.is_null()) {
        context.tuple.set<DataType>(_tuple_index, value.value());
      }
    } else {
      context.tuple.set<DataType>(_tuple_index, value.value());
    }
    // clang-format on
#if !JIT_LAZY_LOAD
    ++_iterator;
#endif
  }

  /*
  __attribute__((always_inline))
  static void read_value(JitSegmentReader<Iterator, DataType, Nullable>& reader, JitRuntimeContext& context) {
#if JIT_LAZY_LOAD && !JIT_OLD_LAZY_LOAD
    const size_t current_offset = context.chunk_offset;
    // _iterator += current_offset - _chunk_offset;
    std::advance(reader._iterator, current_offset - reader._chunk_offset);
    reader._chunk_offset = current_offset;
#endif
    const auto& value = *reader._iterator;
    // clang-format off
    if constexpr (Nullable) {
      context.tuple.set_is_null(reader._tuple_index, value.is_null());
      if (!value.is_null()) {
        context.tuple.set<DataType>(reader._tuple_index, value.value());
      }
    } else {
      context.tuple.set<DataType>(reader._tuple_index, value.value());
    }
    // clang-format on
#if !JIT_LAZY_LOAD
    ++reader._iterator;
#endif
  }
   */

  __attribute__((always_inline))
  Value<DataType> read_and_get_value(JitRuntimeContext& context, DataType) final {
#if JIT_LAZY_LOAD
#if !JIT_OLD_LAZY_LOAD
    const size_t current_offset = context.chunk_offset;
    std::advance(_iterator, current_offset - _chunk_offset);
    _chunk_offset = current_offset;
#endif

    const auto& value = *_iterator;

    if constexpr (Nullable) {
      if (!value.is_null()) {
        return {false, static_cast<DataType>(value.value())};
      }
      return {true, DataType()};
    } else {
      return {false, static_cast<DataType>(value.value())};
    }
#else
    Fail("Function should not be used without lazy load");
#endif
  }

private:
  Iterator _iterator;
  const size_t _tuple_index;
#if JIT_LAZY_LOAD && !JIT_OLD_LAZY_LOAD
  size_t _chunk_offset = 0;
#endif
};

template <typename JitSegmentReader>
class JitSegmentReaderWrapper : public BaseJitSegmentReaderWrapper {
 public:
  using DATA_TYPE = typename JitSegmentReader::DATA_TYPE;
  using BaseJitSegmentReaderWrapper::BaseJitSegmentReaderWrapper;
  /*
  JitSegmentReaderWrapper(size_t reader_index)
  : BaseJitSegmentReaderWrapper(reader_index) {
    std::cout << "";
  }
   */

  // Reads a value from the _iterator into the _tuple_value and increments the _iterator.
  void read_value(JitRuntimeContext& context) const final {
    // DebugAssert(std::dynamic_pointer_cast<JitSegmentReader>(context.inputs[reader_index]), "Different reader type, no " + std::to_string(reader_index));
    if (use_cast) {
      //std::static_pointer_cast<JitSegmentReader>(context.inputs[reader_index])->read_value(context);
      static_cast<JitSegmentReader*>(context.inputs[reader_index].get())->read_value(context);
      // { __rdtsc();}  // rdtsc
      // JitSegmentReader::read_value(*static_cast<JitSegmentReader*>(context.inputs[reader_index].get()), context);
      // {unsigned int dummy; __rdtscp(&dummy);}
      // context.inputs[reader_index]->read_value(context);
    } else {
      context.inputs[reader_index]->read_value(context);
    }
  }

  Value<DATA_TYPE> read_and_get_value(JitRuntimeContext& context, DATA_TYPE) const final {
    if (use_cast) {
      //std::static_pointer_cast<JitSegmentReader>(context.inputs[reader_index])->read_value(context);
      return static_cast<JitSegmentReader*>(context.inputs[reader_index].get())->read_and_get_value(context, DATA_TYPE());
      // { __rdtsc();}  // rdtsc
      // JitSegmentReader::read_value(*static_cast<JitSegmentReader*>(context.inputs[reader_index].get()), context);
      // {unsigned int dummy; __rdtscp(&dummy);}
      // context.inputs[reader_index]->read_value(context);
    } else {
      return context.inputs[reader_index]->read_and_get_value(context, DATA_TYPE());
    }
  }

#if JIT_OLD_LAZY_LOAD
  void increment(JitRuntimeContext& context) const final {
    if (use_cast) {
    static_cast<JitSegmentReader*>(context.inputs[reader_index].get())->increment();
  } else {
    context.inputs[reader_index]->increment();
  }
}
#endif

  bool same_type(JitRuntimeContext& context) final {
    // std::cout << "Current type: " << boost::typeindex::type_id<JitSegmentReader>().pretty_name() << std::endl;
    use_cast = static_cast<bool>(std::dynamic_pointer_cast<JitSegmentReader>(context.inputs[reader_index]));
    return use_cast;
  }
  bool use_cast = true;
};

}  // namespace opossum
