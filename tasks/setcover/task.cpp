#include "optlib/anneal.h"
#include "optlib/accept.h"
#include "optlib/random.h"
#include "optlib/sched.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

struct SetCoverInstance {
  int element_count;
  std::vector<long double> original_costs;
  std::vector<long double> costs;
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
    long double cost;
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

  long double total_cost = 0;
  for (long double cost : instance.costs) {
    total_cost += cost;
  }
  if (total_cost <= 0) {
    throw std::runtime_error("set costs must have a positive total");
  }
  instance.original_costs = instance.costs;
  long double scale = static_cast<long double>(set_count) / total_cost;
  for (long double &cost : instance.costs) {
    cost *= scale;
  }

  return instance;
}

struct SetCover : State {
  std::vector<int> covered_by;
  std::vector<int> set_usage;
  std::vector<int> active_sets;
  std::vector<int> active_positions;
  std::vector<long double> set_costs;
  long double total_cost = 0;
  std::vector<std::vector<int>> assigned_elements;
  std::vector<int> assignment_positions;

  SetCover(std::vector<int> covered_by_, std::vector<long double> set_costs_)
      : covered_by(std::move(covered_by_)), set_usage(set_costs_.size()),
        active_positions(set_costs_.size(), -1),
        set_costs(std::move(set_costs_)),
        assigned_elements(set_usage.size()),
        assignment_positions(covered_by.size()) {
    for (int element = 0; element < static_cast<int>(covered_by.size());
         ++element) {
      int set = covered_by[element];
      ++set_usage[set];
      assignment_positions[element] = assigned_elements[set].size();
      assigned_elements[set].push_back(element);
    }
    for (int set = 0; set < static_cast<int>(set_usage.size()); ++set) {
      if (set_usage[set] > 0) {
        Activate(set);
      }
    }
  }

  double Evaluate() const override { return total_cost; }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<SetCover>(*this);
  }

  bool IsActive(int set) const { return active_positions[set] != -1; }

  void Reassign(int element, int new_set) {
    int old_set = covered_by[element];
    if (old_set == new_set) {
      return;
    }

    int old_position = assignment_positions[element];
    int last_element = assigned_elements[old_set].back();
    assigned_elements[old_set][old_position] = last_element;
    assignment_positions[last_element] = old_position;
    assigned_elements[old_set].pop_back();

    if (--set_usage[old_set] == 0) {
      Deactivate(old_set);
    }
    if (set_usage[new_set]++ == 0) {
      Activate(new_set);
    }
    covered_by[element] = new_set;
    assignment_positions[element] = assigned_elements[new_set].size();
    assigned_elements[new_set].push_back(element);
  }

private:
  void Activate(int set) {
    active_positions[set] = active_sets.size();
    active_sets.push_back(set);
    total_cost += set_costs[set];
  }

  void Deactivate(int set) {
    int position = active_positions[set];
    int last_set = active_sets.back();
    active_sets[position] = last_set;
    active_positions[last_set] = position;
    active_sets.pop_back();
    active_positions[set] = -1;
    total_cost -= set_costs[set];
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
    set_costs_.reserve(order.size());
    original_set_costs_.reserve(order.size());
    original_set_indices_.reserve(order.size());
    for (int set : order) {
      sets_.push_back(std::move(instance.sets[set]));
      set_costs_.push_back(instance.costs[set]);
      original_set_costs_.push_back(instance.original_costs[set]);
      original_set_indices_.push_back(set);
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
    state_ = std::make_unique<SetCover>(std::move(covered_by), set_costs_);
  }

  State *Current() const override { return state_.get(); }

  void WriteSolution(std::ostream &output, const SetCover &solution) const {
    std::vector<int> selected_sets;
    selected_sets.reserve(solution.active_sets.size());
    long double original_score = 0;
    for (int internal_set : solution.active_sets) {
      selected_sets.push_back(original_set_indices_[internal_set]);
      original_score += original_set_costs_[internal_set];
    }
    std::sort(selected_sets.begin(), selected_sets.end());

    output << "score " << original_score << '\n';
    output << "set_count " << selected_sets.size() << '\n';
    output << "sets";
    for (int set : selected_sets) {
      output << ' ' << set;
    }
    output << '\n';
  }

  bool IsValid(const SetCover &solution) const {
    if (solution.covered_by.size() != sets_for_element_.size() ||
        solution.set_usage.size() != sets_.size() ||
        solution.active_positions.size() != sets_.size() ||
        solution.set_costs != set_costs_ ||
        solution.assigned_elements.size() != sets_.size() ||
        solution.assignment_positions.size() != sets_for_element_.size()) {
      return false;
    }

    std::vector<int> usage(sets_.size());
    for (int element = 0; element < static_cast<int>(solution.covered_by.size());
         ++element) {
      int set = solution.covered_by[element];
      if (set < 0 || set >= static_cast<int>(sets_.size()) ||
          std::find(sets_for_element_[element].begin(),
                    sets_for_element_[element].end(),
                    set) == sets_for_element_[element].end()) {
        return false;
      }
      ++usage[set];
    }
    if (usage != solution.set_usage) {
      return false;
    }
    for (int set = 0; set < static_cast<int>(sets_.size()); ++set) {
      if (solution.assigned_elements[set].size() !=
          static_cast<size_t>(usage[set])) {
        return false;
      }
      for (int position = 0;
           position < static_cast<int>(solution.assigned_elements[set].size());
           ++position) {
        int element = solution.assigned_elements[set][position];
        if (element < 0 ||
            element >= static_cast<int>(solution.covered_by.size()) ||
            solution.covered_by[element] != set ||
            solution.assignment_positions[element] != position) {
          return false;
        }
      }
    }

    std::vector<bool> is_active(sets_.size());
    for (int position = 0;
         position < static_cast<int>(solution.active_sets.size()); ++position) {
      int set = solution.active_sets[position];
      if (set < 0 || set >= static_cast<int>(sets_.size()) || is_active[set] ||
          solution.active_positions[set] != position || usage[set] == 0) {
        return false;
      }
      is_active[set] = true;
    }
    for (int set = 0; set < static_cast<int>(sets_.size()); ++set) {
      if (is_active[set] != (usage[set] > 0) ||
          (is_active[set] && solution.active_positions[set] < 0) ||
          (!is_active[set] && solution.active_positions[set] != -1)) {
        return false;
      }
    }
    long double total_cost = 0;
    for (int set = 0; set < static_cast<int>(sets_.size()); ++set) {
      if (usage[set] > 0) {
        total_cost += set_costs_[set];
      }
    }
    long double tolerance =
        1e-8L *
        std::max(1.0L,
                 std::max(std::abs(solution.total_cost), std::abs(total_cost)));
    return std::abs(solution.total_cost - total_cost) <= tolerance;
  }

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
    std::vector<int> removed_elements = state_->assigned_elements[removed_set];
    for (int element : removed_elements) {
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
    return RandomChoice(choices, random_);
  }

  bool CanRemove(int set) const {
    for (int element : state_->assigned_elements[set]) {
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
  std::vector<long double> set_costs_;
  std::vector<long double> original_set_costs_;
  std::vector<int> original_set_indices_;
  std::unique_ptr<SetCover> state_;
  std::unique_ptr<SetRemovalCandidate> pending_;
  std::mt19937 random_;
};

int main(int argc, char *argv[]) {
  if (argc != 2 && argc != 3) {
    std::cerr << "usage: setcover <instance-file> [solution-file]\n";
    return 1;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  std::ofstream output_file;
  std::ostream *output = &std::cout;
  if (argc == 3) {
    output_file.open(argv[2]);
    if (!output_file) {
      std::cerr << "cannot write solution file: " << argv[2] << '\n';
      return 1;
    }
    output = &output_file;
  }

  try {
    auto space = std::make_unique<SetCoverSpace>(ParseInstance(input));
    SetCoverSpace *space_view = space.get();
    Annealer annealer(std::move(space), std::make_unique<ExpDecayScheduler>(),
                      std::make_unique<MetropolisAcceptPolicy>());
    auto solution = annealer.Run();
    const auto *set_cover_solution = dynamic_cast<const SetCover *>(solution.get());
    if (set_cover_solution == nullptr || !space_view->IsValid(*set_cover_solution)) {
      throw std::runtime_error("annealer produced an invalid solution");
    }
    output->precision(17);
    space_view->WriteSolution(*output, *set_cover_solution);
  } catch (const std::exception &error) {
    std::cerr << "invalid set-cover instance: " << error.what() << '\n';
    return 1;
  }
}
#include <iomanip>
