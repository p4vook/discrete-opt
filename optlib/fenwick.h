#pragma once

#include <cstddef>
#include <vector>

class FenwickTree {
public:
  FenwickTree() = default;
  explicit FenwickTree(std::vector<long double> weights);

  void Reset(const std::vector<long double> &weights);

  void Set(std::size_t index, long double weight);

  long double Total() const;

  std::size_t IndexForCumulativeWeight(long double value) const;

private:
  void Add(std::size_t index, long double delta);

  std::vector<long double> weights_;
  std::vector<long double> tree_;
};
