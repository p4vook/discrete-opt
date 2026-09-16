#pragma once

#include <concepts>
#include <random>
#include <ranges>
#include <stdexcept>

template <std::ranges::random_access_range Range,
          std::uniform_random_bit_generator Engine>
  requires std::ranges::sized_range<Range> && std::ranges::borrowed_range<Range>
decltype(auto) RandomChoice(Range &&range, Engine &engine) {
  using Difference = std::ranges::range_difference_t<Range>;

  Difference size = static_cast<Difference>(std::ranges::size(range));
  if (size == 0) {
    throw std::invalid_argument("RandomChoice requires a non-empty range");
  }
  std::uniform_int_distribution<Difference> distribution(0, size - 1);
  return *(std::ranges::begin(range) + distribution(engine));
}
