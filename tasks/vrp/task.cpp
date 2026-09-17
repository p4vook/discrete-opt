#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/public.h"
#include "optlib/sched.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct DeliveryPoint {
  std::int64_t package_weight;
  double x;
  double y;
};

struct VrpInstance {
  std::int64_t truck_count;
  std::int64_t truck_capacity;
  std::vector<DeliveryPoint> points;

  std::size_t CustomerCount() const { return points.size() - 1; }
  int VehicleMarker() const { return 0; }
};

VrpInstance ParseInstance(std::istream &input) {
  std::int64_t point_count;
  std::int64_t truck_count;
  std::int64_t truck_capacity;
  if (!(input >> point_count >> truck_count >> truck_capacity) ||
      point_count < 0 || truck_count < 0 || truck_capacity < 0) {
    throw std::runtime_error(
        "expected non-negative point and truck counts and truck capacity");
  }

  VrpInstance instance{.truck_count = truck_count,
                       .truck_capacity = truck_capacity,
                       .points = {}};
  instance.points.reserve(static_cast<std::size_t>(point_count));

  for (std::int64_t point_index = 0; point_index < point_count;
       ++point_index) {
    DeliveryPoint point;
    if (!(input >> point.package_weight >> point.x >> point.y)) {
      throw std::runtime_error("expected " + std::to_string(point_count) +
                               " point descriptions");
    }
    if (point.package_weight < 0) {
      throw std::runtime_error("negative package weight for point " +
                               std::to_string(point_index));
    }
    if (!std::isfinite(point.x) || !std::isfinite(point.y)) {
      throw std::runtime_error("non-finite coordinate for point " +
                               std::to_string(point_index));
    }
    instance.points.push_back(point);
  }

  if (instance.points.empty()) {
    throw std::runtime_error("expected a depot point");
  }
  if (instance.points.front().package_weight != 0) {
    throw std::runtime_error("expected the depot to have zero package weight");
  }

  std::string trailing_token;
  if (input >> trailing_token) {
    throw std::runtime_error("unexpected data after point descriptions");
  }

  return instance;
}

double Distance(const DeliveryPoint &first, const DeliveryPoint &second) {
  return std::hypot(second.x - first.x, second.y - first.y);
}

double CalculateRouteLength(const VrpInstance &instance,
                            const std::vector<int> &permutation) {
  double length = 0.0;
  for (std::size_t position = 0; position < permutation.size(); ++position) {
    const int first = permutation[position];
    const int second = permutation[(position + 1) % permutation.size()];
    length += Distance(instance.points[first], instance.points[second]);
  }
  return length;
}

std::int64_t CalculateCapacityOverload(
    const VrpInstance &instance, const std::vector<int> &permutation) {
  if (permutation.empty()) {
    return 0;
  }

  const auto first_marker =
      std::find(permutation.begin(), permutation.end(),
                instance.VehicleMarker());
  if (first_marker == permutation.end()) {
    return std::numeric_limits<std::int64_t>::max();
  }

  const std::size_t marker_position =
      static_cast<std::size_t>(first_marker - permutation.begin());
  std::int64_t current_weight = 0;
  std::int64_t total_overload = 0;
  for (std::size_t offset = 1; offset <= permutation.size(); ++offset) {
    const int item =
        permutation[(marker_position + offset) % permutation.size()];
    if (item == instance.VehicleMarker()) {
      if (current_weight > instance.truck_capacity) {
        total_overload += current_weight - instance.truck_capacity;
      }
      current_weight = 0;
    } else {
      current_weight += instance.points[item].package_weight;
    }
  }
  return total_overload;
}

double CalculateMstAverageEdgeLength(const VrpInstance &instance) {
  if (instance.CustomerCount() == 0) {
    return 1.0;
  }

  const std::size_t point_count = instance.points.size();
  std::vector<double> minimum_squared_distance(
      point_count, std::numeric_limits<double>::infinity());
  std::vector<char> in_tree(point_count, false);
  minimum_squared_distance[instance.VehicleMarker()] = 0.0;
  double mst_length = 0.0;

  for (std::size_t tree_size = 0; tree_size < point_count; ++tree_size) {
    std::size_t next_point = point_count;
    for (std::size_t point = 0; point < point_count; ++point) {
      if (!in_tree[point] &&
          (next_point == point_count ||
           minimum_squared_distance[point] <
               minimum_squared_distance[next_point])) {
        next_point = point;
      }
    }

    in_tree[next_point] = true;
    mst_length += std::sqrt(minimum_squared_distance[next_point]);
    const DeliveryPoint &from = instance.points[next_point];
    for (std::size_t point = 0; point < point_count; ++point) {
      if (in_tree[point]) {
        continue;
      }
      const DeliveryPoint &to = instance.points[point];
      const double dx = to.x - from.x;
      const double dy = to.y - from.y;
      const double squared_distance = dx * dx + dy * dy;
      if (squared_distance < minimum_squared_distance[point]) {
        minimum_squared_distance[point] = squared_distance;
      }
    }
  }

  const double average = mst_length / instance.CustomerCount();
  return average > 0.0 ? average : 1.0;
}

struct VrpState : State {
  std::vector<int> permutation;
  const VrpInstance *instance;

  VrpState(const VrpInstance &instance_, std::vector<int> permutation_,
           double normalization_factor)
      : permutation(std::move(permutation_)), instance(&instance_),
        normalization_factor_(normalization_factor) {
    Recalculate();
  }

  double Evaluate() const override {
    return cached_length_ / normalization_factor_;
  }

  double RawScore() const { return cached_length_; }

  std::int64_t CapacityOverload() const { return capacity_overload_; }

  void Recalculate() {
    cached_length_ = CalculateRouteLength(*instance, permutation);
    capacity_overload_ = CalculateCapacityOverload(*instance, permutation);
  }

  void SetMetrics(double length, std::int64_t capacity_overload) {
    cached_length_ = length;
    capacity_overload_ = capacity_overload;
  }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<VrpState>(*this);
  }

private:
  double cached_length_ = 0.0;
  std::int64_t capacity_overload_ = 0;
  double normalization_factor_ = 1.0;
};

bool IsValid(const VrpInstance &instance, const VrpState &state) {
  if (state.instance != &instance ||
      state.permutation.size() !=
          instance.CustomerCount() +
              static_cast<std::size_t>(instance.truck_count)) {
    return false;
  }

  const int vehicle_marker = instance.VehicleMarker();
  std::vector<char> seen(instance.CustomerCount(), false);
  std::size_t marker_count = 0;
  std::size_t first_marker = state.permutation.size();
  for (std::size_t position = 0; position < state.permutation.size();
       ++position) {
    const int item = state.permutation[position];
    if (item == vehicle_marker) {
      ++marker_count;
      if (first_marker == state.permutation.size()) {
        first_marker = position;
      }
      continue;
    }
    if (item <= 0 || item >= static_cast<int>(instance.points.size()) ||
        seen[item - 1]) {
      return false;
    }
    seen[item - 1] = true;
  }

  if (marker_count != static_cast<std::size_t>(instance.truck_count)) {
    return false;
  }
  if (marker_count == 0) {
    return instance.CustomerCount() == 0 && state.RawScore() == 0.0;
  }

  std::int64_t current_weight = 0;
  for (std::size_t offset = 1; offset <= state.permutation.size(); ++offset) {
    const int item = state.permutation[
        (first_marker + offset) % state.permutation.size()];
    if (item == vehicle_marker) {
      current_weight = 0;
      continue;
    }
    current_weight += instance.points[item].package_weight;
    if (current_weight > instance.truck_capacity) {
      return false;
    }
  }
  const double calculated_length =
      CalculateRouteLength(instance, state.permutation);
  const double tolerance =
      1e-9 * std::max(1.0, std::abs(calculated_length));
  return state.CapacityOverload() == 0 &&
         std::abs(state.RawScore() - calculated_length) <= tolerance;
}

std::optional<VrpState> BuildWarmStart(const VrpInstance &instance,
                                       double normalization_factor) {
  if (instance.truck_count == 0 && instance.CustomerCount() != 0) {
    return std::nullopt;
  }
  long double total_weight = 0;
  for (std::size_t customer = 1; customer < instance.points.size();
       ++customer) {
    if (instance.points[customer].package_weight > instance.truck_capacity) {
      return std::nullopt;
    }
    total_weight += instance.points[customer].package_weight;
  }
  if (total_weight > static_cast<long double>(instance.truck_count) *
                         instance.truck_capacity) {
    return std::nullopt;
  }

  const long double threshold =
      instance.CustomerCount() == 0 || instance.truck_count == 0
          ? 0.0L
          : total_weight /
                (static_cast<long double>(instance.CustomerCount()) *
                 instance.truck_count);
  const std::size_t truck_count =
      static_cast<std::size_t>(instance.truck_count);
  const std::size_t capacity =
      static_cast<std::size_t>(instance.truck_capacity);
  std::vector<std::vector<int>> assignments(truck_count);
  std::vector<std::int64_t> loads(truck_count, 0);
  std::vector<char> assigned(instance.points.size(), false);

  for (std::size_t truck = 0; truck < truck_count; ++truck) {
    std::vector<char> reachable(capacity + 1, false);
    std::vector<int> parent_customer(capacity + 1, -1);
    std::vector<std::size_t> parent_load(capacity + 1, 0);
    reachable[0] = true;

    for (std::size_t customer = 1; customer < instance.points.size();
         ++customer) {
      const std::int64_t package_weight =
          instance.points[customer].package_weight;
      if (assigned[customer] || package_weight <= threshold) {
        continue;
      }
      const std::size_t weight = static_cast<std::size_t>(package_weight);
      for (std::size_t load = capacity - weight + 1; load-- > 0;) {
        if (reachable[load] && !reachable[load + weight]) {
          reachable[load + weight] = true;
          parent_customer[load + weight] = static_cast<int>(customer);
          parent_load[load + weight] = load;
        }
      }
    }

    std::size_t best_load = capacity;
    while (!reachable[best_load]) {
      --best_load;
    }
    loads[truck] = static_cast<std::int64_t>(best_load);
    while (best_load != 0) {
      const int customer = parent_customer[best_load];
      if (customer < 0) {
        throw std::runtime_error("failed to reconstruct knapsack warm start");
      }
      assignments[truck].push_back(customer);
      assigned[customer] = true;
      best_load = parent_load[best_load];
    }
  }

  for (std::size_t customer = 1; customer < instance.points.size();
       ++customer) {
    if (!assigned[customer] &&
        instance.points[customer].package_weight > threshold) {
      return std::nullopt;
    }
  }

  for (std::size_t customer = 1; customer < instance.points.size();
       ++customer) {
    if (assigned[customer]) {
      continue;
    }
    const std::int64_t package_weight =
        instance.points[customer].package_weight;
    std::size_t selected_truck = truck_count;
    for (std::size_t truck = 0; truck < truck_count; ++truck) {
      if (package_weight <= instance.truck_capacity - loads[truck]) {
        selected_truck = truck;
        break;
      }
    }
    if (selected_truck == truck_count) {
      return std::nullopt;
    }
    assignments[selected_truck].push_back(static_cast<int>(customer));
    loads[selected_truck] += package_weight;
    assigned[customer] = true;
  }

  std::vector<int> permutation;
  permutation.reserve(instance.CustomerCount() + truck_count);
  for (const std::vector<int> &assignment : assignments) {
    permutation.push_back(instance.VehicleMarker());
    permutation.insert(permutation.end(), assignment.begin(), assignment.end());
  }
  VrpState state(instance, std::move(permutation), normalization_factor);
  if (!IsValid(instance, state)) {
    throw std::runtime_error("constructed an invalid warm start");
  }
  return state;
}

class SwapCandidate : public Candidate {
public:
  SwapCandidate(VrpState &state, std::size_t first, std::size_t second)
      : state_(state), first_(first), second_(second),
        previous_length_(state.RawScore()),
        previous_overload_(state.CapacityOverload()) {
    std::swap(state_.permutation[first_], state_.permutation[second_]);
    state_.Recalculate();
  }

  void Accept() override {}

  void Reject() override {
    std::swap(state_.permutation[first_], state_.permutation[second_]);
    state_.SetMetrics(previous_length_, previous_overload_);
  }

private:
  VrpState &state_;
  std::size_t first_;
  std::size_t second_;
  double previous_length_;
  std::int64_t previous_overload_;
};

class ReverseSegmentCandidate : public Candidate {
public:
  ReverseSegmentCandidate(VrpState &state, std::size_t first,
                          std::size_t second)
      : state_(state), first_(first), second_(second),
        previous_length_(state.RawScore()),
        previous_overload_(state.CapacityOverload()) {
    std::reverse(state_.permutation.begin() + first_,
                 state_.permutation.begin() + second_ + 1);
    state_.Recalculate();
  }

  void Accept() override {}

  void Reject() override {
    std::reverse(state_.permutation.begin() + first_,
                 state_.permutation.begin() + second_ + 1);
    state_.SetMetrics(previous_length_, previous_overload_);
  }

private:
  VrpState &state_;
  std::size_t first_;
  std::size_t second_;
  double previous_length_;
  std::int64_t previous_overload_;
};

class LocalGreedyReconstructCandidate : public Candidate {
public:
  LocalGreedyReconstructCandidate(VrpState &state,
                                  std::size_t marker_position,
                                  std::size_t segment_start,
                                  std::size_t segment_length)
      : state_(state), previous_length_(state.RawScore()),
        previous_overload_(state.CapacityOverload()),
        previous_permutation_(std::move(state_.permutation)) {
    const std::size_t item_count = previous_permutation_.size();
    state_.permutation.reserve(item_count);
    for (std::size_t offset = 0; offset < item_count; ++offset) {
      state_.permutation.push_back(
          previous_permutation_[(marker_position + offset) % item_count]);
    }

    std::size_t route_end = 1;
    while (route_end < state_.permutation.size() &&
           state_.permutation[route_end] != state_.instance->VehicleMarker()) {
      ++route_end;
    }
    const std::size_t first_removed = 1 + segment_start;
    std::vector<int> removed_points(
        state_.permutation.begin() + first_removed,
        state_.permutation.begin() + first_removed + segment_length);
    state_.permutation.erase(
        state_.permutation.begin() + first_removed,
        state_.permutation.begin() + first_removed + segment_length);
    route_end -= segment_length;

    for (int customer : removed_points) {
      std::size_t best_edge = 0;
      double best_delta = std::numeric_limits<double>::infinity();
      for (std::size_t edge = 0; edge < route_end; ++edge) {
        const int first = state_.permutation[edge];
        const int second = edge + 1 < state_.permutation.size()
                               ? state_.permutation[edge + 1]
                               : state_.permutation.front();
        const double delta =
            Distance(state_.instance->points[first],
                     state_.instance->points[customer]) +
            Distance(state_.instance->points[customer],
                     state_.instance->points[second]) -
            Distance(state_.instance->points[first],
                     state_.instance->points[second]);
        if (delta < best_delta) {
          best_delta = delta;
          best_edge = edge;
        }
      }
      state_.permutation.insert(state_.permutation.begin() + best_edge + 1,
                                customer);
      ++route_end;
    }
    state_.Recalculate();
  }

  void Accept() override {}

  void Reject() override {
    state_.permutation = std::move(previous_permutation_);
    state_.SetMetrics(previous_length_, previous_overload_);
  }

private:
  VrpState &state_;
  double previous_length_;
  std::int64_t previous_overload_;
  std::vector<int> previous_permutation_;
};

class NoOpCandidate : public Candidate {
public:
  void Accept() override {}
  void Reject() override {}
};

class VrpSpace : public StateSpace {
public:
  VrpSpace(const VrpInstance &instance, VrpState initial_state,
           std::uint32_t seed)
      : instance_(instance),
        state_(std::make_unique<VrpState>(std::move(initial_state))),
        random_(seed) {}

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    const std::size_t item_count = state_->permutation.size();
    if (item_count < 2) {
      pending_ = std::make_unique<NoOpCandidate>();
      return pending_.get();
    }

    for (;;) {
      std::uniform_int_distribution<int> move_distribution(0, 2);
      const int move = move_distribution(random_);
      if (move == 2) {
        std::vector<std::size_t> nonempty_routes;
        for (std::size_t position = 0; position < item_count; ++position) {
          if (state_->permutation[position] == instance_.VehicleMarker() &&
              state_->permutation[(position + 1) % item_count] !=
                  instance_.VehicleMarker()) {
            nonempty_routes.push_back(position);
          }
        }
        if (!nonempty_routes.empty()) {
          std::uniform_int_distribution<std::size_t> route_distribution(
              0, nonempty_routes.size() - 1);
          const std::size_t marker_position =
              nonempty_routes[route_distribution(random_)];
          std::size_t route_size = 0;
          for (std::size_t position = (marker_position + 1) % item_count;
               state_->permutation[position] != instance_.VehicleMarker();
               position = (position + 1) % item_count) {
            ++route_size;
          }

          const double expected_length =
              std::max(1.0, std::log2(static_cast<double>(route_size)));
          std::geometric_distribution<std::size_t> length_distribution(
              1.0 / expected_length);
          const std::size_t segment_length =
              std::min(route_size, 1 + length_distribution(random_));
          std::uniform_int_distribution<std::size_t> start_distribution(
              0, route_size - segment_length);
          const std::size_t segment_start = start_distribution(random_);

          pending_ = std::make_unique<LocalGreedyReconstructCandidate>(
              *state_, marker_position, segment_start, segment_length);
          return pending_.get();
        }
      }

      std::uniform_int_distribution<std::size_t> first_distribution(
          0, item_count - 1);
      std::uniform_int_distribution<std::size_t> second_distribution(
          0, item_count - 2);
      const std::size_t first_sample = first_distribution(random_);
      std::size_t second_sample = second_distribution(random_);
      if (second_sample >= first_sample) {
        ++second_sample;
      }

      if (move == 0) {
        pending_ = std::make_unique<SwapCandidate>(
            *state_, first_sample, second_sample);
      } else {
        const std::size_t first = std::min(first_sample, second_sample);
        const std::size_t second = std::max(first_sample, second_sample);
        pending_ =
            std::make_unique<ReverseSegmentCandidate>(*state_, first, second);
      }
      if (state_->CapacityOverload() == 0) {
        return pending_.get();
      }
      pending_->Reject();
    }
  }

  bool IsValid(const VrpState &state) const {
    return ::IsValid(instance_, state);
  }

  void WriteSolution(std::ostream &output, const VrpState &state) const {
    output << std::setprecision(17);
    output << "score " << state.RawScore() << '\n';
    output << "permutation";
    for (int item : state.permutation) {
      output << ' ' << item;
    }
    output << '\n';
  }

private:
  const VrpInstance &instance_;
  std::unique_ptr<VrpState> state_;
  std::unique_ptr<Candidate> pending_;
  std::mt19937 random_;
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: vrp <instance-file> [solution-file] [--seed N]\n";
    return 1;
  }

  int argument = 2;
  std::string solution_path;
  if (argument < argc && std::string_view(argv[argument]) != "--seed") {
    solution_path = argv[argument++];
  }
  std::uint32_t seed = 5489u;
  if (argument < argc) {
    if (argument + 2 != argc ||
        std::string_view(argv[argument]) != "--seed") {
      std::cerr << "usage: vrp <instance-file> [solution-file] [--seed N]\n";
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
    VrpInstance instance = ParseInstance(input);
    const double normalization_factor =
        CalculateMstAverageEdgeLength(instance);
    std::optional<VrpState> initial_state =
        BuildWarmStart(instance, normalization_factor);
    if (!initial_state.has_value()) {
      throw std::runtime_error("failed to construct a warm start");
    }

    auto space = std::make_unique<VrpSpace>(
        instance, std::move(*initial_state), seed);
    VrpSpace *space_view = space.get();
    Annealer annealer(
        std::move(space),
        std::make_unique<ExpDecayScheduler>(1.0L, 0.9999998L, 0.0001L),
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

    const auto *vrp_solution =
        dynamic_cast<const VrpState *>(solution.get());
    if (vrp_solution == nullptr || !space_view->IsValid(*vrp_solution)) {
      throw std::runtime_error("annealer produced an invalid solution");
    }
    space_view->WriteSolution(*output, *vrp_solution);
  } catch (const std::exception &error) {
    std::cerr << "invalid VRP instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
