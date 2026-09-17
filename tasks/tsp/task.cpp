#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/public.h"
#include "optlib/sched.h"

#include <algorithm>
#include <array>
#include <chrono>
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

double PointDistance(const TspInstance &instance, int first, int second) {
  const Point &first_point = instance.points[first];
  const Point &second_point = instance.points[second];
  return std::hypot(second_point.x - first_point.x,
                    second_point.y - first_point.y);
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

class ReverseSegmentCandidate : public Candidate {
public:
  ReverseSegmentCandidate(TspState &state, std::size_t start,
                          std::size_t length)
      : state_(state), start_(start), previous_length_(state.RawScore()) {
    const std::size_t point_count = state_.point_order.size();
    previous_points_.assign(state_.point_order.begin() + start_,
                            state_.point_order.begin() + start_ + length);

    std::vector<std::size_t> affected_positions;
    affected_positions.reserve(length + 1);
    affected_positions.push_back((start_ + point_count - 1) % point_count);
    for (std::size_t offset = 0; offset < length; ++offset) {
      affected_positions.push_back(start_ + offset);
    }
    std::sort(affected_positions.begin(), affected_positions.end());
    const auto affected_end =
        std::unique(affected_positions.begin(), affected_positions.end());

    double previous_affected_length = 0.0;
    for (auto position = affected_positions.begin(); position != affected_end;
         ++position) {
      previous_affected_length += state_.EdgeLengthAt(*position);
    }

    if (length > 1) {
      std::reverse(state_.point_order.begin() + start_,
                   state_.point_order.begin() + start_ + length);
    }

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
    std::copy(previous_points_.begin(), previous_points_.end(),
              state_.point_order.begin() + start_);
    state_.SetLength(previous_length_);
  }

private:
  TspState &state_;
  std::size_t start_;
  double previous_length_;
  std::vector<int> previous_points_;
};

class RegularizeSegmentCandidate : public Candidate {
public:
  RegularizeSegmentCandidate(TspState &state, std::size_t start,
                             std::size_t length)
      : state_(state), start_(start), previous_length_(state.RawScore()),
        previous_points_(state_.point_order.begin() + start_,
                         state_.point_order.begin() + start_ + length) {
    const std::size_t point_count = state_.point_order.size();
    const int left = state_.point_order[(start_ + point_count - 1) % point_count];
    const int right = state_.point_order[(start_ + length) % point_count];
    const std::size_t state_count = std::size_t{1} << length;
    const double infinity = std::numeric_limits<double>::infinity();

    std::vector<double> best(state_count * length, infinity);
    std::vector<std::int8_t> parent(state_count * length, -1);
    for (std::size_t last = 0; last < length; ++last) {
      best[(std::size_t{1} << last) * length + last] =
          PointDistance(*state_.instance, left, previous_points_[last]);
    }

    for (std::size_t mask = 1; mask < state_count; ++mask) {
      for (std::size_t last = 0; last < length; ++last) {
        if ((mask & (std::size_t{1} << last)) == 0) {
          continue;
        }
        const double current = best[mask * length + last];
        if (!std::isfinite(current)) {
          continue;
        }
        for (std::size_t next = 0; next < length; ++next) {
          const std::size_t next_bit = std::size_t{1} << next;
          if ((mask & next_bit) != 0) {
            continue;
          }
          const std::size_t next_mask = mask | next_bit;
          const double candidate =
              current + PointDistance(*state_.instance,
                                      previous_points_[last],
                                      previous_points_[next]);
          double &next_best = best[next_mask * length + next];
          if (candidate < next_best) {
            next_best = candidate;
            parent[next_mask * length + next] =
                static_cast<std::int8_t>(last);
          }
        }
      }
    }

    const std::size_t full_mask = state_count - 1;
    std::size_t last = 0;
    double optimized_length = infinity;
    for (std::size_t candidate_last = 0; candidate_last < length;
         ++candidate_last) {
      const double candidate =
          best[full_mask * length + candidate_last] +
          PointDistance(*state_.instance, previous_points_[candidate_last],
                        right);
      if (candidate < optimized_length) {
        optimized_length = candidate;
        last = candidate_last;
      }
    }

    std::size_t mask = full_mask;
    for (std::size_t position = length; position-- > 0;) {
      state_.point_order[start_ + position] = previous_points_[last];
      const std::int8_t previous = parent[mask * length + last];
      mask ^= std::size_t{1} << last;
      if (position != 0) {
        last = static_cast<std::size_t>(previous);
      }
    }

    double previous_affected_length =
        PointDistance(*state_.instance, left, previous_points_.front()) +
        PointDistance(*state_.instance, previous_points_.back(), right);
    double current_affected_length =
        PointDistance(*state_.instance, left,
                      state_.point_order[start_]) +
        PointDistance(*state_.instance,
                      state_.point_order[start_ + length - 1], right);
    for (std::size_t offset = 1; offset < length; ++offset) {
      previous_affected_length +=
          PointDistance(*state_.instance, previous_points_[offset - 1],
                        previous_points_[offset]);
      current_affected_length +=
          PointDistance(*state_.instance,
                        state_.point_order[start_ + offset - 1],
                        state_.point_order[start_ + offset]);
    }
    state_.SetLength(previous_length_ - previous_affected_length +
                     current_affected_length);
  }

  void Accept() override {}

  void Reject() override {
    std::copy(previous_points_.begin(), previous_points_.end(),
              state_.point_order.begin() + start_);
    state_.SetLength(previous_length_);
  }

private:
  TspState &state_;
  std::size_t start_;
  double previous_length_;
  std::vector<int> previous_points_;
};

class GreedyReconstructCandidate : public Candidate {
public:
  GreedyReconstructCandidate(TspState &state, std::size_t start,
                             std::size_t length)
      : state_(state), previous_length_(state.RawScore()),
        previous_order_(std::move(state_.point_order)) {
    const std::size_t point_count = previous_order_.size();
    std::vector<int> removed_points(previous_order_.begin() + start,
                                    previous_order_.begin() + start + length);

    state_.point_order.reserve(point_count);
    state_.point_order.insert(state_.point_order.end(),
                              previous_order_.begin(),
                              previous_order_.begin() + start);
    state_.point_order.insert(state_.point_order.end(),
                              previous_order_.begin() + start + length,
                              previous_order_.end());

    double current_length =
        CalculateTourLength(*state_.instance, state_.point_order);
    for (int point_index : removed_points) {
      std::size_t best_edge = 0;
      double best_delta = std::numeric_limits<double>::infinity();
      for (std::size_t edge = 0; edge < state_.point_order.size(); ++edge) {
        const int first = state_.point_order[edge];
        const int second =
            state_.point_order[(edge + 1) % state_.point_order.size()];
        const double delta =
            PointDistance(*state_.instance, first, point_index) +
            PointDistance(*state_.instance, point_index, second) -
            PointDistance(*state_.instance, first, second);
        if (delta < best_delta) {
          best_delta = delta;
          best_edge = edge;
        }
      }
      state_.point_order.insert(state_.point_order.begin() + best_edge + 1,
                                point_index);
      current_length += best_delta;
    }
    state_.SetLength(current_length);
  }

  void Accept() override {}

  void Reject() override {
    state_.point_order = std::move(previous_order_);
    state_.SetLength(previous_length_);
  }

private:
  TspState &state_;
  double previous_length_;
  std::vector<int> previous_order_;
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

    if (point_count > regularization_window_size_ &&
        ShouldRegularize()) {
      std::uniform_int_distribution<std::size_t> start_distribution(
          0, point_count - regularization_window_size_);
      const std::size_t start = start_distribution(random_);
      const auto begin = Clock::now();
      pending_ = std::make_unique<RegularizeSegmentCandidate>(
          *state_, start, regularization_window_size_);
      const auto duration =
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                               begin);
      regularization_time_ += duration;
      estimated_regularization_time_ = duration;
      ++regularization_count_;
      return pending_.get();
    }

    const double expected_length =
        std::max(1.0, std::log2(static_cast<double>(point_count)));
    std::geometric_distribution<std::size_t> length_distribution(
        1.0 / expected_length);
    const std::size_t length =
        std::min(point_count, 1 + length_distribution(random_));
    std::uniform_int_distribution<std::size_t> start_distribution(
        0, point_count - length);
    const std::size_t start = start_distribution(random_);

    if (length <= point_count - 2 && ShouldGreedyReconstruct()) {
      const auto begin = Clock::now();
      pending_ = std::make_unique<GreedyReconstructCandidate>(
          *state_, start, length);
      const auto duration =
          std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() -
                                                               begin);
      greedy_reconstruction_time_ += duration;
      estimated_greedy_reconstruction_time_ = duration;
      ++greedy_reconstruction_count_;
      return pending_.get();
    }

    pending_ =
        std::make_unique<ReverseSegmentCandidate>(*state_, start, length);
    return pending_.get();
  }

  bool IsValid(const TspState &state) const {
    return ::IsValid(instance_, state);
  }

  std::size_t RegularizationCount() const { return regularization_count_; }

  double RegularizationSeconds() const {
    return std::chrono::duration<double>(regularization_time_).count();
  }

  double RegularizationShare() const {
    const auto elapsed = Clock::now() - annealing_start_;
    if (elapsed <= Clock::duration::zero()) {
      return 0.0;
    }
    return std::chrono::duration<double>(regularization_time_).count() /
           std::chrono::duration<double>(elapsed).count();
  }

  std::size_t GreedyReconstructionCount() const {
    return greedy_reconstruction_count_;
  }

  double GreedyReconstructionSeconds() const {
    return std::chrono::duration<double>(greedy_reconstruction_time_).count();
  }

  double GreedyReconstructionShare() const {
    const auto elapsed = Clock::now() - annealing_start_;
    if (elapsed <= Clock::duration::zero()) {
      return 0.0;
    }
    return std::chrono::duration<double>(greedy_reconstruction_time_).count() /
           std::chrono::duration<double>(elapsed).count();
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
  bool ShouldRegularize() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - annealing_start_);
    if (regularization_time_ + estimated_regularization_time_ > elapsed / 5) {
      return false;
    }
    return regularization_distribution_(random_);
  }

  bool ShouldGreedyReconstruct() {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        Clock::now() - annealing_start_);
    if (greedy_reconstruction_time_ +
            estimated_greedy_reconstruction_time_ >
        elapsed / 2) {
      return false;
    }
    return greedy_reconstruction_distribution_(random_);
  }

  using Clock = std::chrono::steady_clock;
  static constexpr std::size_t regularization_window_size_ = 10;
  TspInstance instance_;
  MstWarmStart mst_;
  std::unique_ptr<TspState> state_;
  std::unique_ptr<Candidate> pending_;
  std::mt19937 random_;
  Clock::time_point annealing_start_ = Clock::now();
  std::chrono::nanoseconds regularization_time_{0};
  std::chrono::nanoseconds estimated_regularization_time_ =
      std::chrono::milliseconds(20);
  std::size_t regularization_count_ = 0;
  std::bernoulli_distribution regularization_distribution_{1.0 / 1024.0};
  std::chrono::nanoseconds greedy_reconstruction_time_{0};
  std::chrono::nanoseconds estimated_greedy_reconstruction_time_ =
      std::chrono::milliseconds(20);
  std::size_t greedy_reconstruction_count_ = 0;
  std::bernoulli_distribution greedy_reconstruction_distribution_{0.5};
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
    std::cerr << "regularization stats: count="
              << space_view->RegularizationCount()
              << " seconds=" << space_view->RegularizationSeconds()
              << " share=" << space_view->RegularizationShare() << '\n';
    std::cerr << "greedy reconstruction stats: count="
              << space_view->GreedyReconstructionCount()
              << " seconds=" << space_view->GreedyReconstructionSeconds()
              << " share=" << space_view->GreedyReconstructionShare() << '\n';

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
