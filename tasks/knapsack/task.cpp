#include "optlib/public.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

struct Object {
  std::int64_t cost;
  std::int64_t weight;
};

struct KnapsackInstance {
  std::int64_t capacity;
  std::vector<Object> objects;
};

KnapsackInstance ParseInstance(std::istream &input) {
  std::int64_t object_count;
  std::int64_t capacity;
  if (!(input >> object_count >> capacity) || object_count < 0 ||
      capacity < 0) {
    throw std::runtime_error(
        "expected a non-negative object count and knapsack capacity");
  }

  KnapsackInstance instance{.capacity = capacity, .objects = {}};
  instance.objects.reserve(static_cast<std::size_t>(object_count));

  for (std::int64_t object_index = 0; object_index < object_count;
       ++object_index) {
    Object object;
    if (!(input >> object.cost >> object.weight)) {
      throw std::runtime_error("expected " + std::to_string(object_count) +
                               " object descriptions");
    }
    if (object.cost < 0 || object.weight < 0) {
      throw std::runtime_error("negative cost or weight for object " +
                               std::to_string(object_index));
    }
    instance.objects.push_back(object);
  }

  std::string trailing_token;
  if (input >> trailing_token) {
    throw std::runtime_error("unexpected data after object descriptions");
  }

  return instance;
}

struct KnapsackState : State {
  std::vector<char> taken;
  const KnapsackInstance *instance;

  explicit KnapsackState(const KnapsackInstance &instance_)
      : taken(instance_.objects.size(), false), instance(&instance_) {}

  double Evaluate() const override {
    double total_cost = 0;
    for (std::size_t object_index = 0; object_index < taken.size();
         ++object_index) {
      if (taken[object_index]) {
        total_cost += static_cast<double>(instance->objects[object_index].cost);
      }
    }
    return -total_cost;
  }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<KnapsackState>(*this);
  }
};

bool IsValid(const KnapsackInstance &instance, const KnapsackState &state) {
  if (state.instance != &instance ||
      state.taken.size() != instance.objects.size()) {
    return false;
  }

  std::int64_t remaining_capacity = instance.capacity;
  for (std::size_t object_index = 0; object_index < state.taken.size();
       ++object_index) {
    if (state.taken[object_index] != 0 && state.taken[object_index] != 1) {
      return false;
    }
    if (state.taken[object_index]) {
      const std::int64_t weight = instance.objects[object_index].weight;
      if (weight > remaining_capacity) {
        return false;
      }
      remaining_capacity -= weight;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: knapsack <instance-file>\n";
    return 1;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  try {
    KnapsackInstance instance = ParseInstance(input);
    KnapsackState state(instance);
    if (!IsValid(instance, state)) {
      throw std::runtime_error("initial state is invalid");
    }
  } catch (const std::exception &error) {
    std::cerr << "invalid knapsack instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
