#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/public.h"
#include "optlib/random.h"
#include "optlib/sched.h"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Object {
  std::int64_t cost;
  std::int64_t weight;
  long double normalized_cost = 0;
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

  long double total_cost = 0;
  for (const Object &object : instance.objects) {
    total_cost += object.cost;
  }
  if (total_cost <= 0) {
    throw std::runtime_error("object costs must have a positive total");
  }
  const long double scale =
      static_cast<long double>(instance.objects.size()) / total_cost;
  for (Object &object : instance.objects) {
    object.normalized_cost = object.cost * scale;
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
    long double total_cost = 0;
    for (std::size_t object_index = 0; object_index < taken.size();
         ++object_index) {
      if (taken[object_index]) {
        total_cost += instance->objects[object_index].normalized_cost;
      }
    }
    return -static_cast<double>(total_cost);
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

std::int64_t RemainingCapacity(const KnapsackState &state) {
  std::int64_t remaining_capacity = state.instance->capacity;
  for (std::size_t object_index = 0; object_index < state.taken.size();
       ++object_index) {
    if (state.taken[object_index]) {
      remaining_capacity -= state.instance->objects[object_index].weight;
    }
  }
  return remaining_capacity;
}

class AddCandidate : public Candidate {
public:
  AddCandidate(KnapsackState &state, std::size_t object_index)
      : state_(state), object_index_(object_index) {
    if (object_index >= state_.taken.size()) {
      throw std::out_of_range("object index out of range");
    }
    if (state_.taken[object_index]) {
      throw std::logic_error("cannot add an object already in the knapsack");
    }
    if (state_.instance->objects[object_index].weight >
        RemainingCapacity(state_)) {
      throw std::logic_error("cannot add an object beyond knapsack capacity");
    }
    state_.taken[object_index] = 1;
  }

  void Accept() override {}

  void Reject() override { state_.taken[object_index_] = 0; }

private:
  KnapsackState &state_;
  std::size_t object_index_;
};

class RemoveCandidate : public Candidate {
public:
  RemoveCandidate(KnapsackState &state, std::size_t object_index)
      : state_(state), object_index_(object_index) {
    if (object_index >= state_.taken.size()) {
      throw std::out_of_range("object index out of range");
    }
    if (!state_.taken[object_index]) {
      throw std::logic_error("cannot remove an object outside the knapsack");
    }
    state_.taken[object_index] = 0;
  }

  void Accept() override {}

  void Reject() override { state_.taken[object_index_] = 1; }

private:
  KnapsackState &state_;
  std::size_t object_index_;
};

class KnapsackSpace : public StateSpace {
public:
  explicit KnapsackSpace(KnapsackInstance instance, std::uint32_t seed = 5489u)
      : instance_(std::move(instance)),
        state_(std::make_unique<KnapsackState>(instance_)), random_(seed) {
    GreedyInitialize();
  }

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    legal_objects_.clear();
    const std::int64_t remaining_capacity = RemainingCapacity(*state_);
    for (std::size_t object_index = 0; object_index < instance_.objects.size();
         ++object_index) {
      if (state_->taken[object_index] ||
          instance_.objects[object_index].weight <= remaining_capacity) {
        legal_objects_.push_back(object_index);
      }
    }
    if (legal_objects_.empty()) {
      throw std::logic_error("knapsack state has no legal candidates");
    }

    const std::size_t object_index = RandomChoice(legal_objects_, random_);
    if (state_->taken[object_index]) {
      pending_ = std::make_unique<RemoveCandidate>(*state_, object_index);
    } else {
      pending_ = std::make_unique<AddCandidate>(*state_, object_index);
    }
    return pending_.get();
  }

  bool IsValid(const KnapsackState &state) const {
    return ::IsValid(instance_, state);
  }

  void WriteSolution(std::ostream &output, const KnapsackState &state) const {
    std::int64_t score = 0;
    std::size_t object_count = 0;
    for (std::size_t object_index = 0; object_index < state.taken.size();
         ++object_index) {
      if (state.taken[object_index]) {
        score += instance_.objects[object_index].cost;
        ++object_count;
      }
    }

    output << "score " << score << '\n';
    output << "object_count " << object_count << '\n';
    output << "objects";
    for (std::size_t object_index = 0; object_index < state.taken.size();
         ++object_index) {
      if (state.taken[object_index]) {
        output << ' ' << object_index;
      }
    }
    output << '\n';
  }

private:
  void GreedyInitialize() {
    std::vector<std::size_t> object_indices(instance_.objects.size());
    std::iota(object_indices.begin(), object_indices.end(), 0);
    const auto utility = [this](std::size_t object_index) {
      const Object &object = instance_.objects[object_index];
      if (object.weight == 0) {
        return object.cost > 0 ? std::numeric_limits<long double>::infinity()
                               : 0.0L;
      }
      return static_cast<long double>(object.cost) / object.weight;
    };
    std::stable_sort(object_indices.begin(), object_indices.end(),
                     [&utility](std::size_t left, std::size_t right) {
                       return utility(left) > utility(right);
                     });

    std::int64_t remaining_capacity = instance_.capacity;
    for (std::size_t object_index : object_indices) {
      const std::int64_t weight = instance_.objects[object_index].weight;
      if (weight <= remaining_capacity) {
        state_->taken[object_index] = 1;
        remaining_capacity -= weight;
      }
    }
  }

  KnapsackInstance instance_;
  std::unique_ptr<KnapsackState> state_;
  std::unique_ptr<Candidate> pending_;
  std::vector<std::size_t> legal_objects_;
  std::mt19937 random_;
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: knapsack <instance-file> [solution-file] [--seed N]\n";
    return 1;
  }

  int argument = 2;
  std::string solution_path;
  if (argument < argc && std::string_view(argv[argument]) != "--seed") {
    solution_path = argv[argument++];
  }
  std::uint32_t seed = 5489u;
  if (argument < argc) {
    if (argument + 2 != argc || std::string_view(argv[argument]) != "--seed") {
      std::cerr
          << "usage: knapsack <instance-file> [solution-file] [--seed N]\n";
      return 1;
    }
    try {
      seed = static_cast<std::uint32_t>(std::stoul(argv[argument + 1]));
    } catch (const std::exception &) {
      std::cerr << "invalid seed\n";
      return 1;
    }
  }
  std::cerr << "seed=" << seed << '\n';

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  std::ofstream output_file;
  std::ostream *output = &std::cout;
  if (!solution_path.empty()) {
    output_file.open(solution_path);
    if (!output_file) {
      std::cerr << "cannot write solution file: " << solution_path << '\n';
      return 1;
    }
    output = &output_file;
  }

  try {
    auto space = std::make_unique<KnapsackSpace>(ParseInstance(input), seed);
    KnapsackSpace *space_view = space.get();
    Annealer annealer(
        std::move(space),
        std::make_unique<ExpDecayScheduler>(1.0L, 0.99999L, 0.0001L),
        std::make_unique<MetropolisAcceptPolicy>(seed));
    std::unique_ptr<State> solution = annealer.Run();
    const AnnealStats &stats = annealer.Stats();
    std::cerr << "anneal stats: iterations=" << stats.iterations
              << " accepted=" << stats.accepted
              << " rejected=" << stats.rejected
              << " best_updates=" << stats.best_updates
              << " initial_score=" << stats.initial_score
              << " final_score=" << stats.final_score
              << " best_score=" << stats.best_score << '\n';

    const auto *knapsack_solution =
        dynamic_cast<const KnapsackState *>(solution.get());
    if (knapsack_solution == nullptr ||
        !space_view->IsValid(*knapsack_solution)) {
      throw std::runtime_error("annealer produced an invalid solution");
    }
    space_view->WriteSolution(*output, *knapsack_solution);
  } catch (const std::exception &error) {
    std::cerr << "invalid knapsack instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
