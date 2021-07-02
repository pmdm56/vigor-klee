#pragma once

#include "../modules/module.h"

#include <typeindex>
#include <unordered_map>

namespace synapse {

class MemoryBankTypeBase {
public:
  template <typename T> inline bool contains(Target target, int key) const;
  template <typename T> inline const T &read(Target target, int key) const;
  template <typename T> inline void write(Target target, int key, T value);
};

template <typename T> class MemoryBankType {
private:
  std::unordered_map<Target, std::map<int, T>> banks;

public:
  bool contains(Target target, int key) const {
    auto bank_it = banks.find(target);

    if (bank_it == banks.end()) {
      return false;
    }

    auto value_it = bank_it->second.find(key);

    if (value_it == bank_it->second.end()) {
      return false;
    }

    return true;
  }

  T read(Target target, int key) const {
    assert(contains(target, key));
    return banks.at(target).at(key);
  }

  void write(Target target, int key, T value) { banks[target][key] = value; }
};

class MemoryBank {
private:
  MemoryBankType<int> int_mb;
  MemoryBankType<unsigned> unsigned_mb;

public:
  template <typename T> bool contains(Target target, int key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> T read(Target target, int key) const {
    assert(false && "MemoryBank: I dont know this type");
  }
  template <typename T> void write(Target target, int key, T value) {
    assert(false && "MemoryBank: I dont know this type");
  }
};

} // namespace synapse
