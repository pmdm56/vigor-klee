#pragma once

#include <iostream>
#include <map>
#include <vector>

namespace synapse {

class Score {
public:
  // The order of the elements in this enum matters.
  // It defines a lexicographic order.
  enum Category {
    SentToController,
    NumberOfReorderedNodes,
    NumberOfNodes,
    Depth,
  };

private:
  const Category FIRST_CATEGORY = SentToController;
  const Category LAST_CATEGORY = NumberOfNodes;

private:
  std::map<Category, int> values;

public:
  Score() {
    for (int category_int = FIRST_CATEGORY; category_int <= LAST_CATEGORY;
         category_int++) {
      auto category = static_cast<Category>(category_int);
      set(category, 0);
    }
  }

  Score(const Score &score) : values(score.values) {}

  void set(Category category, int value) { values[category] = value; }
  int get(Category category) const { return values.at(category); }

  inline bool operator<(const Score &other) {
    for (int category_int = FIRST_CATEGORY; category_int <= LAST_CATEGORY;
         category_int++) {
      auto category = static_cast<Category>(category_int);
      if (get(category) >= other.get(category)) {
        return false;
      }
    }

    return true;
  }

  inline bool operator>(const Score &other) { return (*this) < other; }
  inline bool operator<=(const Score &other) { return !((*this) > other); }
  inline bool operator>=(const Score &other) { return !((*this) < other); }
  inline bool operator==(const Score &other) {
    for (int category_int = FIRST_CATEGORY; category_int <= LAST_CATEGORY;
         category_int++) {
      auto category = static_cast<Category>(category_int);
      if (get(category) != other.get(category)) {
        return false;
      }
    }

    return true;
  }

  inline bool operator!=(const Score &other) { return !((*this) == other); }

  friend std::ostream &operator<<(std::ostream &os, const Score &dt);
};

inline std::ostream &operator<<(std::ostream &os, const Score &score) {
  os << "<";

  for (int category_int = score.FIRST_CATEGORY;
       category_int <= score.LAST_CATEGORY; category_int++) {
    auto category = static_cast<Score::Category>(category_int);

    if (category != score.FIRST_CATEGORY) {
      os << ",";
    }

    os << score.get(category);
  }

  os << ">";
  return os;
}
}