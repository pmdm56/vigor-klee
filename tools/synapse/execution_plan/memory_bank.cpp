#include "memory_bank.h"

#define SPECIALIZE(value_type)                                                 \
  template <>                                                                  \
  bool MemoryBank::contains<value_type>(Target target, int key) const {        \
    return value_type##_mb.contains(target, key);                              \
  }                                                                            \
                                                                               \
  template <>                                                                  \
  value_type MemoryBank::read<value_type>(Target target, int key) const {      \
    return value_type##_mb.read(target, key);                                  \
  }                                                                            \
                                                                               \
  template <>                                                                  \
  void MemoryBank::write<value_type>(Target target, int key,                   \
                                     value_type value) {                       \
    value_type##_mb.write(target, key, value);                                 \
  }

namespace synapse {

template <typename T>
bool MemoryBankTypeBase::contains(Target target, int key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).contains(target, key);
}

template <typename T>
const T &MemoryBankTypeBase::read(Target target, int key) const {
  return dynamic_cast<const MemoryBankType<T> &>(*this).read(target, key);
}

template <typename T>
void MemoryBankTypeBase::write(Target target, int key, T value) {
  return dynamic_cast<MemoryBankType<T> &>(*this).write(target, key, value);
}

SPECIALIZE(int)
SPECIALIZE(unsigned)

} // namespace synapse