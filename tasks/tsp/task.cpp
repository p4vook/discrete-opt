#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/public.h"
#include "optlib/sched.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct Point {
  double x;
  double y;
};

struct TspInstance {
  std::vector<Point> points;
};

double CalculateTourLength(const TspInstance &instance,
                           const std::vector<int> &point_order) {
  if (point_order.empty()) {
    return 0.0;
  }

  double length = 0.0;
  for (std::size_t position = 0; position < point_order.size(); ++position) {
    const Point &from = instance.points[point_order[position]];
    const Point &to =
        instance.points[point_order[(position + 1) % point_order.size()]];
    length += std::hypot(to.x - from.x, to.y - from.y);
  }
  return length;
}

TspInstance ParseInstance(std::istream &input) {
  std::int64_t point_count;
  if (!(input >> point_count) || point_count < 0) {
    throw std::runtime_error("expected a non-negative point count");
  }

  TspInstance instance;
  instance.points.reserve(static_cast<std::size_t>(point_count));

  for (std::int64_t point_index = 0; point_index < point_count;
       ++point_index) {
    Point point;
    if (!(input >> point.x >> point.y)) {
      throw std::runtime_error("expected " + std::to_string(point_count) +
                               " point descriptions");
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

struct MstWarmStart {
  std::vector<int> preorder;
  double length = 0.0;
};

MstWarmStart BuildMstWarmStart(const TspInstance &instance) {
  const std::size_t point_count = instance.points.size();
  if (point_count == 0) {
    return MstWarmStart{};
  }

  std::vector<double> minimum_squared_distance(
      point_count, std::numeric_limits<double>::infinity());
  std::vector<int> parent(point_count, -1);
  std::vector<char> in_tree(point_count, false);
  minimum_squared_distance[0] = 0.0;
  double mst_length = 0.0;

  for (std::size_t tree_size = 0; tree_size < point_count; ++tree_size) {
    int next_point = -1;
    for (std::size_t point_index = 0; point_index < point_count;
         ++point_index) {
      if (!in_tree[point_index] &&
          (next_point == -1 ||
           minimum_squared_distance[point_index] <
               minimum_squared_distance[next_point])) {
        next_point = static_cast<int>(point_index);
      }
    }

    in_tree[next_point] = true;
    mst_length += std::sqrt(minimum_squared_distance[next_point]);
    const Point &from = instance.points[next_point];
    for (std::size_t point_index = 0; point_index < point_count;
         ++point_index) {
      if (in_tree[point_index]) {
        continue;
      }
      const Point &to = instance.points[point_index];
      const double dx = to.x - from.x;
      const double dy = to.y - from.y;
      const double squared_distance = dx * dx + dy * dy;
      if (squared_distance < minimum_squared_distance[point_index]) {
        minimum_squared_distance[point_index] = squared_distance;
        parent[point_index] = next_point;
      }
    }
  }

  std::vector<std::vector<int>> children(point_count);
  for (std::size_t point_index = 1; point_index < point_count; ++point_index) {
    children[parent[point_index]].push_back(static_cast<int>(point_index));
  }

  std::vector<int> point_order;
  point_order.reserve(point_count);
  std::vector<int> stack{0};
  while (!stack.empty()) {
    const int point_index = stack.back();
    stack.pop_back();
    point_order.push_back(point_index);
    for (auto child = children[point_index].rbegin();
         child != children[point_index].rend(); ++child) {
      stack.push_back(*child);
    }
  }
  return MstWarmStart{.preorder = std::move(point_order),
                      .length = mst_length};
}

struct TspState : State {
  std::vector<int> point_order;
  const TspInstance *instance;

  TspState(const TspInstance &instance_, const std::vector<int> &point_order_,
           double normalization_factor)
      : point_order(point_order_), instance(&instance_),
        normalization_factor_(normalization_factor > 0.0
                                  ? normalization_factor
                                  : 1.0) {
    cached_length_ = CalculateTourLength(*instance, point_order);
  }

  double Evaluate() const override {
    return cached_length_ / normalization_factor_;
  }

  double RawScore() const { return cached_length_; }

  double EdgeLengthAt(std::size_t position) const {
    const Point &from = instance->points[point_order[position]];
    const Point &to =
        instance->points[point_order[(position + 1) % point_order.size()]];
    return std::hypot(to.x - from.x, to.y - from.y);
  }

  void SetLength(double length) { cached_length_ = length; }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<TspState>(*this);
  }

private:
  double cached_length_ = 0.0;
  double normalization_factor_ = 1.0;
};

bool IsValid(const TspInstance &instance, const TspState &state) {
  if (state.instance != &instance ||
      state.point_order.size() != instance.points.size()) {
    return false;
  }

  std::vector<char> seen(instance.points.size(), false);
  for (std::size_t position = 0; position < state.point_order.size();
       ++position) {
    const int point_index = state.point_order[position];
    if (point_index < 0 ||
        point_index >= static_cast<int>(instance.points.size()) ||
        seen[point_index]) {
      return false;
    }
    seen[point_index] = true;
  }

  const double calculated_length =
      CalculateTourLength(instance, state.point_order);
  const double tolerance =
      1e-9 * std::max(1.0, std::abs(calculated_length));
  return std::abs(state.RawScore() - calculated_length) <= tolerance;
}

class SwapCandidate : public Candidate {
public:
  SwapCandidate(TspState &state, std::size_t first_position,
                std::size_t second_position)
      : state_(state), first_position_(first_position),
        second_position_(second_position), previous_length_(state.RawScore()) {
    const std::size_t point_count = state_.point_order.size();
    std::array<std::size_t, 4> affected_positions{
        (first_position_ + point_count - 1) % point_count, first_position_,
        (second_position_ + point_count - 1) % point_count, second_position_};
    std::sort(affected_positions.begin(), affected_positions.end());
    const auto affected_end =
        std::unique(affected_positions.begin(), affected_positions.end());

    double previous_affected_length = 0.0;
    for (auto position = affected_positions.begin(); position != affected_end;
         ++position) {
      previous_affected_length += state_.EdgeLengthAt(*position);
    }

    std::swap(state_.point_order[first_position_],
              state_.point_order[second_position_]);

    double current_affected_length = 0.0;
    for (auto position = affected_positions.begin(); position != affected_end;
         ++position) {
      current_affected_length += state_.EdgeLengthAt(*position);
    }
    state_.SetLength(previous_length_ - previous_affected_length +
                     current_affected_length);
  }

  void Accept() override {}

  void Reject() override {
    std::swap(state_.point_order[first_position_],
              state_.point_order[second_position_]);
    state_.SetLength(previous_length_);
  }

private:
  TspState &state_;
  std::size_t first_position_;
  std::size_t second_position_;
  double previous_length_;
};

class NoOpCandidate : public Candidate {
public:
  void Accept() override {}
  void Reject() override {}
};

class TspSpace : public StateSpace {
public:
  explicit TspSpace(TspInstance instance, std::uint32_t seed = 5489u)
      : instance_(std::move(instance)),
        mst_(BuildMstWarmStart(instance_)),
        state_(std::make_unique<TspState>(instance_, mst_.preorder,
                                          mst_.length /
                                              (instance_.points.size() > 1
                                                   ? instance_.points.size() - 1
                                                   : 1))),
        random_(seed) {}

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    const std::size_t point_count = state_->point_order.size();
    if (point_count < 2) {
      pending_ = std::make_unique<NoOpCandidate>();
      return pending_.get();
    }

    std::uniform_int_distribution<std::size_t> first_distribution(
        0, point_count - 1);
    std::uniform_int_distribution<std::size_t> second_distribution(
        0, point_count - 2);
    const std::size_t first_position = first_distribution(random_);
    std::size_t second_position = second_distribution(random_);
    if (second_position >= first_position) {
      ++second_position;
    }

    pending_ = std::make_unique<SwapCandidate>(
        *state_, first_position, second_position);
    return pending_.get();
  }

  bool IsValid(const TspState &state) const {
    return ::IsValid(instance_, state);
  }

  void WriteSolution(std::ostream &output, const TspState &state) const {
    output << std::setprecision(17);
    output << "score " << state.RawScore() << '\n';
    output << "point_order";
    for (int point_index : state.point_order) {
      output << ' ' << point_index;
    }
    output << '\n';
  }

private:
  TspInstance instance_;
  MstWarmStart mst_;
  std::unique_ptr<TspState> state_;
  std::unique_ptr<Candidate> pending_;
  std::mt19937 random_;
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "usage: tsp <instance-file> [solution-file] [--seed N]\n";
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
      std::cerr << "usage: tsp <instance-file> [solution-file] [--seed N]\n";
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
    auto space = std::make_unique<TspSpace>(ParseInstance(input), seed);
    TspSpace *space_view = space.get();
    Annealer annealer(
        std::move(space),
        std::make_unique<ExpDecayScheduler>(1.0L, 0.9999999L, 0.0001L),
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

    const auto *tsp_solution = dynamic_cast<const TspState *>(solution.get());
    if (tsp_solution == nullptr || !space_view->IsValid(*tsp_solution)) {
      throw std::runtime_error("annealer produced an invalid solution");
    }
    space_view->WriteSolution(*output, *tsp_solution);
  } catch (const std::exception &error) {
    std::cerr << "invalid TSP instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
