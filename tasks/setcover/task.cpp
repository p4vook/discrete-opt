#include "optlib/public.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

struct SetCoverInstance {
  int element_count;
  std::vector<int64_t> costs;
  std::vector<std::vector<int>> sets;
};

SetCoverInstance ParseInstance(std::istream &input) {
  int element_count;
  int set_count;
  if (!(input >> element_count >> set_count) || element_count < 0 ||
      set_count < 0) {
    throw std::runtime_error("expected non-negative element and set counts");
  }

  std::string line;
  std::getline(input, line);

  SetCoverInstance instance{.element_count = element_count};
  instance.costs.reserve(set_count);
  instance.sets.reserve(set_count);

  for (int set_index = 0; set_index < set_count; ++set_index) {
    if (!std::getline(input, line)) {
      throw std::runtime_error("expected " + std::to_string(set_count) +
                               " set descriptions");
    }

    std::istringstream description(line);
    int64_t cost;
    if (!(description >> cost)) {
      throw std::runtime_error("missing cost for set " +
                               std::to_string(set_index));
    }

    std::vector<int> elements;
    for (int element; description >> element;) {
      if (element < 0 || element >= element_count) {
        throw std::runtime_error("element index out of range in set " +
                                 std::to_string(set_index));
      }
      elements.push_back(element);
    }
    if (!description.eof()) {
      throw std::runtime_error("invalid element index in set " +
                               std::to_string(set_index));
    }

    instance.costs.push_back(cost);
    instance.sets.push_back(std::move(elements));
  }

  return instance;
}

struct SetCover : State {
  std::vector<int> covered_by;
  std::unordered_set<int> used_sets;
  int64_t cost;

  SetCover(std::vector<int> covered_by_, int64_t cost_)
      : covered_by(covered_by_), cost(cost_) {}

  double Evaluate() const override { return cost; }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<SetCover>(covered_by, cost);
  }
};

// S1, S2, S3, S4
// x1, x2, x3, x4

// modification?
// let's do set removal?
// remove set -> use others to take its place
// how to choose from others?
// if we already have a set that covers it -> fine, use it
// if we don't -> then let's pick from the others at random

struct SetCoverSpace : StateSpace {
};

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: setcover <instance-file>\n";
    return 1;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  try {
    ParseInstance(input);
  } catch (const std::exception &error) {
    std::cerr << "invalid set-cover instance: " << error.what() << '\n';
    return 1;
  }
}
