#include "optlib/anneal.h"
#include "optlib/sched.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
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
  std::vector<int> set_usage;
  std::vector<int> active_sets;
  std::vector<int> active_positions;

  SetCover(std::vector<int> covered_by_, int set_count)
      : covered_by(std::move(covered_by_)), set_usage(set_count),
        active_positions(set_count, -1) {
    for (int set : covered_by) {
      ++set_usage[set];
    }
    for (int set = 0; set < set_count; ++set) {
      if (set_usage[set] > 0) {
        Activate(set);
      }
    }
  }

  double Evaluate() const override { return active_sets.size(); }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<SetCover>(*this);
  }

  bool IsActive(int set) const { return active_positions[set] != -1; }

  void Reassign(int element, int new_set) {
    int old_set = covered_by[element];
    if (old_set == new_set) {
      return;
    }

    if (--set_usage[old_set] == 0) {
      Deactivate(old_set);
    }
    if (set_usage[new_set]++ == 0) {
      Activate(new_set);
    }
    covered_by[element] = new_set;
  }

private:
  void Activate(int set) {
    active_positions[set] = active_sets.size();
    active_sets.push_back(set);
  }

  void Deactivate(int set) {
    int position = active_positions[set];
    int last_set = active_sets.back();
    active_sets[position] = last_set;
    active_positions[last_set] = position;
    active_sets.pop_back();
    active_positions[set] = -1;
  }
};

class SetRemovalCandidate : public Candidate {
public:
  SetRemovalCandidate(SetCover &state,
                      std::vector<std::pair<int, int>> changes)
      : state_(state), changes_(std::move(changes)) {}

  void Accept() override { changes_.clear(); }

  void Reject() override {
    for (auto it = changes_.rbegin(); it != changes_.rend(); ++it) {
      state_.Reassign(it->first, it->second);
    }
    changes_.clear();
  }

private:
  SetCover &state_;
  std::vector<std::pair<int, int>> changes_;
};

class SetCoverSpace : public StateSpace {
public:
  explicit SetCoverSpace(SetCoverInstance instance) {
    std::vector<int> order(instance.sets.size());
    std::iota(order.begin(), order.end(), 0);
    std::shuffle(order.begin(), order.end(), random_);

    sets_.reserve(order.size());
    for (int set : order) {
      sets_.push_back(std::move(instance.sets[set]));
    }

    sets_for_element_.resize(instance.element_count);
    std::vector<int> covered_by(instance.element_count, -1);
    for (int set = 0; set < static_cast<int>(sets_.size()); ++set) {
      for (int element : sets_[set]) {
        sets_for_element_[element].push_back(set);
        if (covered_by[element] == -1) {
          covered_by[element] = set;
        }
      }
    }
    for (int element = 0; element < instance.element_count; ++element) {
      if (covered_by[element] == -1) {
        throw std::runtime_error("element " + std::to_string(element) +
                                 " is not covered by any set");
      }
    }
    state_ = std::make_unique<SetCover>(std::move(covered_by), sets_.size());
  }

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    std::vector<int> removable_sets;
    for (int set : state_->active_sets) {
      if (CanRemove(set)) {
        removable_sets.push_back(set);
      }
    }
    if (removable_sets.empty()) {
      pending_ = std::make_unique<SetRemovalCandidate>(
          *state_, std::vector<std::pair<int, int>>{});
      return pending_.get();
    }

    int removed_set = Pick(removable_sets);
    std::vector<std::pair<int, int>> changes;
    for (int element = 0; element < static_cast<int>(state_->covered_by.size());
         ++element) {
      if (state_->covered_by[element] != removed_set) {
        continue;
      }

      int replacement = PickActiveCoveringSet(element, removed_set);
      if (replacement == -1) {
        replacement = PickDifferentCoveringSet(element, removed_set);
      }
      changes.emplace_back(element, removed_set);
      state_->Reassign(element, replacement);
    }
    pending_ =
        std::make_unique<SetRemovalCandidate>(*state_, std::move(changes));
    return pending_.get();
  }

private:
  int Pick(const std::vector<int> &choices) {
    std::uniform_int_distribution<size_t> distribution(0, choices.size() - 1);
    return choices[distribution(random_)];
  }

  bool CanRemove(int set) const {
    for (int element = 0; element < static_cast<int>(state_->covered_by.size());
         ++element) {
      if (state_->covered_by[element] != set) {
        continue;
      }
      bool has_replacement = false;
      for (int covering_set : sets_for_element_[element]) {
        if (covering_set != set) {
          has_replacement = true;
          break;
        }
      }
      if (!has_replacement) {
        return false;
      }
    }
    return true;
  }

  int PickActiveCoveringSet(int element, int excluded_set) {
    std::vector<int> choices;
    for (int set : sets_for_element_[element]) {
      if (set != excluded_set && state_->IsActive(set)) {
        choices.push_back(set);
      }
    }
    return choices.empty() ? -1 : Pick(choices);
  }

  int PickDifferentCoveringSet(int element, int excluded_set) {
    std::vector<int> choices;
    for (int set : sets_for_element_[element]) {
      if (set != excluded_set) {
        choices.push_back(set);
      }
    }
    return Pick(choices);
  }

  std::vector<std::vector<int>> sets_;
  std::vector<std::vector<int>> sets_for_element_;
  std::unique_ptr<SetCover> state_;
  std::unique_ptr<SetRemovalCandidate> pending_;
  std::mt19937 random_{std::random_device{}()};
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
    auto space = std::make_unique<SetCoverSpace>(ParseInstance(input));
    Annealer annealer(std::move(space), std::make_unique<ExpDecayScheduler>());
    auto solution = annealer.Run();
    std::cout << solution->Evaluate() << '\n';
  } catch (const std::exception &error) {
    std::cerr << "invalid set-cover instance: " << error.what() << '\n';
    return 1;
  }
}
