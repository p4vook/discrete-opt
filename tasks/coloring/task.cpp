#include "optlib/accept.h"
#include "optlib/anneal.h"
#include "optlib/public.h"
#include "optlib/random.h"
#include "optlib/sched.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ColoringInstance {
  int vertex_count;
  std::vector<std::pair<int, int>> edges;
  std::vector<std::vector<int>> neighbors;
};

struct ColoringState : State {
  std::vector<int> vertex_order;
  const ColoringInstance *instance;

  explicit ColoringState(const ColoringInstance &instance_)
      : vertex_order(instance_.vertex_count), instance(&instance_),
        cached_colors_(instance_.vertex_count, -1),
        color_marks_(instance_.vertex_count, -1) {
    std::iota(vertex_order.begin(), vertex_order.end(), 0);
    RecomputeEvaluation();
    normalization_factor_ = std::max(1, cached_color_count_);
  }

  const std::vector<int> &GreedyColoring() const {
    if (!colors_valid_) {
      RecomputeEvaluation();
    }
    return cached_colors_;
  }

  double Evaluate() const override {
    return static_cast<double>(RawScore()) / normalization_factor_;
  }

  int RawScore() const {
    if (!score_valid_) {
      RecomputeEvaluation();
    }
    return cached_color_count_;
  }

  std::unique_ptr<State> Snapshot() const override {
    return std::make_unique<ColoringState>(*this);
  }

  void InvalidateEvaluation() {
    score_valid_ = false;
    colors_valid_ = false;
  }

  void RestoreScore(int score) {
    cached_color_count_ = score;
    score_valid_ = true;
    colors_valid_ = false;
  }

private:
  void RecomputeEvaluation() const {
    std::fill(cached_colors_.begin(), cached_colors_.end(), -1);
    std::fill(color_marks_.begin(), color_marks_.end(), -1);
    int color_count = 0;

    for (int vertex : vertex_order) {
      for (int neighbor : instance->neighbors[vertex]) {
        int color = cached_colors_[neighbor];
        if (color != -1) {
          color_marks_[color] = vertex;
        }
      }

      int color = 0;
      while (color < color_count && color_marks_[color] == vertex) {
        ++color;
      }
      if (color == color_count) {
        ++color_count;
      }
      cached_colors_[vertex] = color;
    }
    cached_color_count_ = color_count;
    score_valid_ = true;
    colors_valid_ = true;
  }

  mutable std::vector<int> cached_colors_;
  mutable std::vector<int> color_marks_;
  mutable int cached_color_count_ = 0;
  mutable bool score_valid_ = false;
  mutable bool colors_valid_ = false;
  int normalization_factor_ = 1;
};

ColoringInstance ParseInstance(std::istream &input) {
  int vertex_count;
  int edge_count;
  if (!(input >> vertex_count >> edge_count) || vertex_count < 0 ||
      edge_count < 0) {
    throw std::runtime_error(
        "expected non-negative vertex and edge counts");
  }

  ColoringInstance instance{.vertex_count = vertex_count,
                            .edges = {},
                            .neighbors =
                                std::vector<std::vector<int>>(vertex_count)};
  instance.edges.reserve(static_cast<std::size_t>(edge_count));

  for (int edge_index = 0; edge_index < edge_count; ++edge_index) {
    int first_vertex;
    int second_vertex;
    if (!(input >> first_vertex >> second_vertex)) {
      throw std::runtime_error("expected " + std::to_string(edge_count) +
                               " edge descriptions");
    }
    if (first_vertex < 0 || first_vertex >= vertex_count ||
        second_vertex < 0 || second_vertex >= vertex_count) {
      throw std::runtime_error("vertex index out of range in edge " +
                               std::to_string(edge_index));
    }
    instance.edges.emplace_back(first_vertex, second_vertex);
    instance.neighbors[first_vertex].push_back(second_vertex);
    instance.neighbors[second_vertex].push_back(first_vertex);
  }

  std::string trailing_token;
  if (input >> trailing_token) {
    throw std::runtime_error("unexpected data after edge descriptions");
  }

  return instance;
}

bool IsValid(const ColoringInstance &instance, const ColoringState &state) {
  if (state.instance != &instance || state.vertex_order.size() !=
      static_cast<std::size_t>(instance.vertex_count)) {
    return false;
  }

  std::vector<char> seen(instance.vertex_count, false);
  for (int vertex : state.vertex_order) {
    if (vertex < 0 || vertex >= instance.vertex_count || seen[vertex]) {
      return false;
    }
    seen[vertex] = true;
  }
  return true;
}

class PathCycleCandidate : public Candidate {
public:
  PathCycleCandidate(ColoringState &state, const std::vector<int> &path)
      : state_(state), previous_score_(state.RawScore()) {
    positions_.reserve(path.size());
    previous_vertices_.reserve(path.size());

    std::vector<int> positions_by_vertex(state.vertex_order.size());
    for (std::size_t position = 0; position < state.vertex_order.size();
         ++position) {
      positions_by_vertex[state.vertex_order[position]] =
          static_cast<int>(position);
    }
    for (int vertex : path) {
      positions_.push_back(positions_by_vertex[vertex]);
    }
    for (int position : positions_) {
      previous_vertices_.push_back(state_.vertex_order[position]);
    }
    for (std::size_t index = 0; index < positions_.size(); ++index) {
      const std::size_t next_index = (index + 1) % positions_.size();
      state_.vertex_order[positions_[next_index]] = previous_vertices_[index];
    }
    state_.InvalidateEvaluation();
  }

  void Accept() override {}

  void Reject() override {
    for (std::size_t index = 0; index < positions_.size(); ++index) {
      state_.vertex_order[positions_[index]] = previous_vertices_[index];
    }
    state_.RestoreScore(previous_score_);
  }

private:
  ColoringState &state_;
  int previous_score_;
  std::vector<int> positions_;
  std::vector<int> previous_vertices_;
};

class NoOpCandidate : public Candidate {
public:
  void Accept() override {}
  void Reject() override {}
};

class ColoringSpace : public StateSpace {
public:
  explicit ColoringSpace(ColoringInstance instance,
                         std::uint32_t seed = 5489u)
      : instance_(std::move(instance)),
        state_(std::make_unique<ColoringState>(instance_)),
        random_(seed) {}

  State *Current() const override { return state_.get(); }

  Candidate *Next() override {
    std::vector<int> path = RandomPath();
    if (path.size() < 3) {
      pending_ = std::make_unique<NoOpCandidate>();
    } else {
      pending_ = std::make_unique<PathCycleCandidate>(*state_, path);
    }
    return pending_.get();
  }

  bool IsValid(const ColoringState &state) const {
    if (!::IsValid(instance_, state)) {
      return false;
    }
    const std::vector<int> &colors = state.GreedyColoring();
    for (const auto &[first_vertex, second_vertex] : instance_.edges) {
      if (colors[first_vertex] == colors[second_vertex]) {
        return false;
      }
    }
    return true;
  }

  void WriteSolution(std::ostream &output, const ColoringState &state) const {
    const std::vector<int> &colors = state.GreedyColoring();
    output << "score " << state.RawScore() << '\n';
    output << "colors";
    for (int color : colors) {
      output << ' ' << color;
    }
    output << '\n';
    output << "vertex_order";
    for (int vertex : state.vertex_order) {
      output << ' ' << vertex;
    }
    output << '\n';
  }

private:
  std::vector<int> RandomPath() {
    if (instance_.vertex_count == 0) {
      return {};
    }

    const double expected_length =
        std::max(1.0, std::log2(static_cast<double>(instance_.vertex_count)));
    std::geometric_distribution<int> length_distribution(
        1.0 / expected_length);
    std::uniform_int_distribution<int> start_distribution(
        0, instance_.vertex_count - 1);
    std::vector<char> visited(instance_.vertex_count, false);
    std::vector<int> path;

    int vertex = start_distribution(random_);
    int previous_vertex = -1;
    path.push_back(vertex);
    visited[vertex] = true;
    const int target_length =
        std::min(instance_.vertex_count, 1 + length_distribution(random_));
    while (static_cast<int>(path.size()) < target_length &&
           !instance_.neighbors[vertex].empty()) {
      const std::vector<int> &neighbors = instance_.neighbors[vertex];
      int eligible_count = 0;
      for (int neighbor : neighbors) {
        eligible_count += neighbor != previous_vertex;
      }
      if (eligible_count == 0) {
        break;
      }

      std::uniform_int_distribution<int> neighbor_distribution(
          0, eligible_count - 1);
      int selected = neighbor_distribution(random_);
      int next_vertex = -1;
      for (int neighbor : neighbors) {
        if (neighbor != previous_vertex && selected-- == 0) {
          next_vertex = neighbor;
          break;
        }
      }
      if (visited[next_vertex]) {
        break;
      }
      path.push_back(next_vertex);
      visited[next_vertex] = true;
      previous_vertex = vertex;
      vertex = next_vertex;
    }
    return path;
  }

  ColoringInstance instance_;
  std::unique_ptr<ColoringState> state_;
  std::unique_ptr<Candidate> pending_;
  std::mt19937 random_;
};

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr <<
        "usage: coloring <instance-file> [solution-file] [--seed N]\n";
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
      std::cerr <<
          "usage: coloring <instance-file> [solution-file] [--seed N]\n";
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
    auto space = std::make_unique<ColoringSpace>(ParseInstance(input), seed);
    ColoringSpace *space_view = space.get();
    Annealer annealer(
        std::move(space),
        std::make_unique<ExpDecayScheduler>(1.0L, 0.99987500183603828L,
                                            0.00001L),
        std::make_unique<MetropolisAcceptPolicy>(seed));
    std::unique_ptr<State> solution = annealer.Run([](const AnnealStats &stats) {
      std::cerr << "anneal progress: elapsed=" << stats.elapsed_seconds
                << "s iterations=" << stats.iterations
                << " temperature=" << stats.temperature
                << " accepted=" << stats.accepted
                << " rejected=" << stats.rejected
                << " best_updates=" << stats.best_updates
                << " current_score=" << stats.current_score
                << " best_score=" << stats.best_score << '\n';
    });
    const AnnealStats &stats = annealer.Stats();
    std::cerr << "anneal stats: iterations=" << stats.iterations
              << " accepted=" << stats.accepted
              << " rejected=" << stats.rejected
              << " best_updates=" << stats.best_updates
              << " initial_score=" << stats.initial_score
              << " final_score=" << stats.final_score
              << " best_score=" << stats.best_score << '\n';

    const auto *coloring_solution =
        dynamic_cast<const ColoringState *>(solution.get());
    if (coloring_solution == nullptr ||
        !space_view->IsValid(*coloring_solution)) {
      throw std::runtime_error("annealer produced an invalid solution");
    }
    space_view->WriteSolution(*output, *coloring_solution);
  } catch (const std::exception &error) {
    std::cerr << "invalid coloring instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
