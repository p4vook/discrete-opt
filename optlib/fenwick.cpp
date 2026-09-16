#include "fenwick.h"

#include <stdexcept>
#include <utility>

FenwickTree::FenwickTree(std::vector<long double> weights)
    : weights_(std::move(weights)), tree_(weights_.size() + 1) {
  for (std::size_t index = 0; index < weights_.size(); ++index) {
    if (weights_[index] < 0) {
      throw std::invalid_argument("FenwickTree weights must be non-negative");
    }
    Add(index, weights_[index]);
  }
}

void FenwickTree::Set(std::size_t index, long double weight) {
  if (index >= weights_.size() || weight < 0) {
    throw std::invalid_argument("invalid FenwickTree weight update");
  }
  Add(index, weight - weights_[index]);
  weights_[index] = weight;
}

long double FenwickTree::Total() const {
  long double total = 0;
  for (std::size_t index = tree_.size() - 1; index > 0;
       index -= index & -index) {
    total += tree_[index];
  }
  return total;
}

std::size_t FenwickTree::IndexForCumulativeWeight(long double value) const {
  long double total = Total();
  if (value < 0 || value >= total) {
    throw std::invalid_argument("FenwickTree value is outside its total weight");
  }

  std::size_t index = 0;
  long double prefix = 0;
  std::size_t step = 1;
  while (step < tree_.size()) {
    step <<= 1;
  }
  for (step >>= 1; step > 0; step >>= 1) {
    std::size_t next = index + step;
    if (next < tree_.size() && prefix + tree_[next] <= value) {
      index = next;
      prefix += tree_[next];
    }
  }
  return index;
}

void FenwickTree::Add(std::size_t index, long double delta) {
  for (++index; index < tree_.size(); index += index & -index) {
    tree_[index] += delta;
  }
}
