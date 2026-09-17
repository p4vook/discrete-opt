#include "optlib/public.h"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
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
  instance.points.reserve(static_cast<std::size_t>(point_count) + 1);
  instance.points.push_back(
      DeliveryPoint{.package_weight = 0, .x = 0.0, .y = 0.0});

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

struct VrpState : State {
  std::vector<int> permutation;
  const VrpInstance *instance;

  VrpState(const VrpInstance &instance_, std::vector<int> permutation_)
      : permutation(std::move(permutation_)), instance(&instance_) {}

  double Evaluate() const override {
    return CalculateRouteLength(*instance, permutation);
  }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<VrpState>(*this);
  }
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
    return instance.CustomerCount() == 0;
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
  return true;
}

std::optional<VrpState> BuildWarmStart(const VrpInstance &instance) {
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
  VrpState state(instance, std::move(permutation));
  if (!IsValid(instance, state)) {
    throw std::runtime_error("constructed an invalid warm start");
  }
  return state;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: vrp <instance-file>\n";
    return 1;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  try {
    VrpInstance instance = ParseInstance(input);
    const std::optional<VrpState> state = BuildWarmStart(instance);
    if (!state.has_value()) {
      std::cout << "warm start failed\n";
      return 2;
    }
    std::cout << "warm start succeeded\n";
  } catch (const std::exception &error) {
    std::cerr << "invalid VRP instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
