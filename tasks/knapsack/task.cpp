#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/fenwick.h"
#include "optlib/public.h"
#include "optlib/sched.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
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

class KnapsackSpace;

class NoOpCandidate : public Candidate {
public:
  void Accept() override {}
  void Reject() override {}
};

class AddCandidate : public Candidate {
public:
  AddCandidate(KnapsackSpace &space, std::size_t object_index);

  void Accept() override {}

  void Reject() override;

private:
  KnapsackSpace &space_;
  std::size_t object_index_;
};

class DpAddCandidate : public Candidate {
public:
  DpAddCandidate(KnapsackSpace &space,
                 std::vector<std::size_t> object_indices);

  void Accept() override {}

  void Reject() override;

private:
  KnapsackSpace &space_;
  std::vector<std::size_t> object_indices_;
};

class RemoveCandidate : public Candidate {
public:
  RemoveCandidate(KnapsackSpace &space, std::size_t object_index);

  void Accept() override;

  void Reject() override;

private:
  KnapsackSpace &space_;
  std::size_t object_index_;
};

class KnapsackSpace : public StateSpace {
public:
  explicit KnapsackSpace(KnapsackInstance instance, std::uint32_t seed = 5489u)
      : instance_(std::move(instance)),
        state_(std::make_unique<KnapsackState>(instance_)), random_(seed) {
    GreedyInitialize();
    InitializeSamplers();
  }

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    ++iteration_;
    ExpireBans();
    if (add_candidate_count_ == 0 && remove_candidate_count_ == 0) {
      pending_ = std::make_unique<NoOpCandidate>();
      return pending_.get();
    }

    bool add;
    if (remove_candidate_count_ == 0) {
      add = true;
    } else if (add_candidate_count_ == 0) {
      add = false;
    } else {
      std::uniform_int_distribution<std::size_t> choose_move(
          0, add_candidate_count_ + remove_candidate_count_ - 1);
      add = choose_move(random_) < add_candidate_count_;
    }

    if (add) {
      constexpr std::int64_t dp_capacity_threshold = 1'000;
      if (remaining_capacity_ <= dp_capacity_threshold) {
        std::vector<std::size_t> selection = DpSelection();
        if (!selection.empty()) {
          pending_ =
              std::make_unique<DpAddCandidate>(*this, std::move(selection));
          return pending_.get();
        }
      }
      pending_ = std::make_unique<AddCandidate>(*this, Sample(add_sampler_));
    } else {
      pending_ =
          std::make_unique<RemoveCandidate>(*this, Sample(remove_sampler_));
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
  friend class AddCandidate;
  friend class DpAddCandidate;
  friend class RemoveCandidate;

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
      if (instance_.objects[object_index].cost == 0) {
        continue;
      }
      const std::int64_t weight = instance_.objects[object_index].weight;
      if (weight <= remaining_capacity) {
        state_->taken[object_index] = 1;
        remaining_capacity -= weight;
      }
    }
    remaining_capacity_ = remaining_capacity;
  }

  void InitializeSamplers() {
    weight_order_.resize(instance_.objects.size());
    std::iota(weight_order_.begin(), weight_order_.end(), 0);
    std::stable_sort(weight_order_.begin(), weight_order_.end(),
                     [this](std::size_t left, std::size_t right) {
                       return instance_.objects[left].weight <
                              instance_.objects[right].weight;
                     });
    while (fitting_prefix_size_ < weight_order_.size() &&
           instance_.objects[weight_order_[fitting_prefix_size_]].weight <=
               remaining_capacity_) {
      ++fitting_prefix_size_;
    }

    std::vector<long double> add_weights(instance_.objects.size());
    std::vector<long double> remove_weights(instance_.objects.size());
    add_active_.assign(instance_.objects.size(), false);
    remove_active_.assign(instance_.objects.size(), false);
    ban_until_.assign(instance_.objects.size(), 0);
    for (std::size_t object_index = 0; object_index < instance_.objects.size();
         ++object_index) {
      if (state_->taken[object_index]) {
        remove_weights[object_index] = 1;
        remove_active_[object_index] = true;
        ++remove_candidate_count_;
      } else if (instance_.objects[object_index].weight <=
                 remaining_capacity_) {
        add_weights[object_index] = 1;
        add_active_[object_index] = true;
        ++add_candidate_count_;
      }
    }
    add_sampler_.Reset(add_weights);
    remove_sampler_.Reset(remove_weights);
  }

  void SetAddActive(std::size_t object_index, bool active) {
    active = active && !state_->taken[object_index] &&
             ban_until_[object_index] == 0 &&
             instance_.objects[object_index].weight <= remaining_capacity_;
    if (add_active_[object_index] == active) {
      return;
    }
    add_active_[object_index] = active;
    add_sampler_.Set(object_index, active ? 1 : 0);
    if (active) {
      ++add_candidate_count_;
    } else {
      --add_candidate_count_;
    }
  }

  void SetRemoveActive(std::size_t object_index, bool active) {
    active = active && state_->taken[object_index];
    if (remove_active_[object_index] == active) {
      return;
    }
    remove_active_[object_index] = active;
    remove_sampler_.Set(object_index, active ? 1 : 0);
    if (active) {
      ++remove_candidate_count_;
    } else {
      --remove_candidate_count_;
    }
  }

  void UpdateFittingPrefix() {
    while (fitting_prefix_size_ < weight_order_.size() &&
           instance_.objects[weight_order_[fitting_prefix_size_]].weight <=
               remaining_capacity_) {
      const std::size_t object_index = weight_order_[fitting_prefix_size_++];
      SetAddActive(object_index, true);
    }
    while (fitting_prefix_size_ > 0 &&
           instance_.objects[weight_order_[fitting_prefix_size_ - 1]].weight >
               remaining_capacity_) {
      const std::size_t object_index = weight_order_[--fitting_prefix_size_];
      SetAddActive(object_index, false);
    }
  }

  std::vector<std::size_t> DpSelection() const {
    const int capacity = static_cast<int>(remaining_capacity_);
    std::vector<std::size_t> eligible;
    eligible.reserve(fitting_prefix_size_);
    for (std::size_t position = 0; position < fitting_prefix_size_; ++position) {
      const std::size_t object_index = weight_order_[position];
      if (add_active_[object_index] &&
          instance_.objects[object_index].cost > 0) {
        eligible.push_back(object_index);
      }
    }

    std::vector<std::int64_t> best(capacity + 1, 0);
    std::vector<std::vector<char>> take(
        eligible.size(), std::vector<char>(capacity + 1, false));
    for (std::size_t item = 0; item < eligible.size(); ++item) {
      const Object &object = instance_.objects[eligible[item]];
      const int weight = static_cast<int>(object.weight);
      for (int current_capacity = capacity; current_capacity >= weight;
           --current_capacity) {
        const std::int64_t candidate =
            best[current_capacity - weight] + object.cost;
        if (candidate > best[current_capacity]) {
          best[current_capacity] = candidate;
          take[item][current_capacity] = true;
        }
      }
    }

    std::vector<std::size_t> selection;
    int current_capacity = capacity;
    for (std::size_t item = eligible.size(); item-- > 0;) {
      if (take[item][current_capacity]) {
        const std::size_t object_index = eligible[item];
        selection.push_back(object_index);
        current_capacity -=
            static_cast<int>(instance_.objects[object_index].weight);
      }
    }
    return selection;
  }

  void Add(std::size_t object_index, bool ignore_ban = false) {
    if (object_index >= state_->taken.size()) {
      throw std::out_of_range("object index out of range");
    }
    if (state_->taken[object_index]) {
      throw std::logic_error("cannot add an object already in the knapsack");
    }
    if (!ignore_ban && ban_until_[object_index] != 0) {
      throw std::logic_error("cannot add a banned object");
    }
    const std::int64_t weight = instance_.objects[object_index].weight;
    if (weight > remaining_capacity_) {
      throw std::logic_error("cannot add an object beyond knapsack capacity");
    }

    SetAddActive(object_index, false);
    state_->taken[object_index] = 1;
    SetRemoveActive(object_index, true);
    remaining_capacity_ -= weight;
    UpdateFittingPrefix();
  }

  void Remove(std::size_t object_index, bool record_ban) {
    if (object_index >= state_->taken.size()) {
      throw std::out_of_range("object index out of range");
    }
    if (!state_->taken[object_index]) {
      throw std::logic_error("cannot remove an object outside the knapsack");
    }

    SetRemoveActive(object_index, false);
    state_->taken[object_index] = 0;
    remaining_capacity_ += instance_.objects[object_index].weight;
    UpdateFittingPrefix();
    SetAddActive(object_index, true);
    if (record_ban) {
      Ban(object_index);
    }
  }

  void Ban(std::size_t object_index) {
    constexpr std::size_t ban_duration = 10'000;
    const std::size_t expiration = iteration_ + ban_duration + 1;
    ban_until_[object_index] = expiration;
    ban_expirations_.emplace_back(expiration, object_index);
    SetAddActive(object_index, false);
  }

  void ExpireBans() {
    while (!ban_expirations_.empty() &&
           ban_expirations_.front().first <= iteration_) {
      const auto [expiration, object_index] = ban_expirations_.front();
      ban_expirations_.pop_front();
      if (ban_until_[object_index] == expiration) {
        ban_until_[object_index] = 0;
        SetAddActive(object_index, true);
      }
    }
  }

  std::size_t Sample(FenwickTree &sampler) {
    const long double total = sampler.Total();
    std::uniform_real_distribution<long double> distribution(0, total);
    long double value = distribution(random_);
    if (value >= total) {
      value = std::nextafter(total, 0.0L);
    }
    return sampler.IndexForCumulativeWeight(value);
  }

  KnapsackInstance instance_;
  std::unique_ptr<KnapsackState> state_;
  std::unique_ptr<Candidate> pending_;
  std::vector<std::size_t> weight_order_;
  std::vector<char> add_active_;
  std::vector<char> remove_active_;
  FenwickTree add_sampler_;
  FenwickTree remove_sampler_;
  std::size_t fitting_prefix_size_ = 0;
  std::size_t add_candidate_count_ = 0;
  std::size_t remove_candidate_count_ = 0;
  std::deque<std::pair<std::size_t, std::size_t>> ban_expirations_;
  std::vector<std::size_t> ban_until_;
  std::size_t iteration_ = 0;
  std::int64_t remaining_capacity_ = 0;
  std::mt19937 random_;
};

AddCandidate::AddCandidate(KnapsackSpace &space, std::size_t object_index)
    : space_(space), object_index_(object_index) {
  space_.Add(object_index_);
}

void AddCandidate::Reject() { space_.Remove(object_index_, false); }

DpAddCandidate::DpAddCandidate(KnapsackSpace &space,
                               std::vector<std::size_t> object_indices)
    : space_(space), object_indices_(std::move(object_indices)) {
  for (std::size_t object_index : object_indices_) {
    space_.Add(object_index);
  }
}

void DpAddCandidate::Reject() {
  for (auto object = object_indices_.rbegin(); object != object_indices_.rend();
       ++object) {
    space_.Remove(*object, false);
  }
}

RemoveCandidate::RemoveCandidate(KnapsackSpace &space,
                                 std::size_t object_index)
    : space_(space), object_index_(object_index) {
  space_.Remove(object_index_, false);
}

void RemoveCandidate::Accept() { space_.Ban(object_index_); }

void RemoveCandidate::Reject() { space_.Add(object_index_); }

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
        std::make_unique<ExpDecayScheduler>(1.0L, 0.999999L, 0.0001L),
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
