#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

struct ColoringInstance {
  int vertex_count;
  std::vector<std::pair<int, int>> edges;
};

struct ColoringState {
  std::vector<int> colors;

  explicit ColoringState(int vertex_count) : colors(vertex_count) {
    std::iota(colors.begin(), colors.end(), 0);
  }
};

ColoringInstance ParseInstance(std::istream &input) {
  int vertex_count;
  int edge_count;
  if (!(input >> vertex_count >> edge_count) || vertex_count < 0 ||
      edge_count < 0) {
    throw std::runtime_error(
        "expected non-negative vertex and edge counts");
  }

  ColoringInstance instance{.vertex_count = vertex_count, .edges = {}};
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
  }

  std::string trailing_token;
  if (input >> trailing_token) {
    throw std::runtime_error("unexpected data after edge descriptions");
  }

  return instance;
}

bool IsValid(const ColoringInstance &instance, const ColoringState &state) {
  if (state.colors.size() !=
      static_cast<std::size_t>(instance.vertex_count)) {
    return false;
  }

  for (int color : state.colors) {
    if (color < 0 || color >= instance.vertex_count) {
      return false;
    }
  }

  for (const auto &[first_vertex, second_vertex] : instance.edges) {
    if (state.colors[first_vertex] == state.colors[second_vertex]) {
      return false;
    }
  }
  return true;
}

int main(int argc, char *argv[]) {
  if (argc != 2) {
    std::cerr << "usage: coloring <instance-file>\n";
    return 1;
  }

  std::ifstream input(argv[1]);
  if (!input) {
    std::cerr << "cannot open instance file: " << argv[1] << '\n';
    return 1;
  }

  try {
    ColoringInstance instance = ParseInstance(input);
    ColoringState state(instance.vertex_count);
    if (!IsValid(instance, state)) {
      throw std::runtime_error("initial state is invalid");
    }
  } catch (const std::exception &error) {
    std::cerr << "invalid coloring instance: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
