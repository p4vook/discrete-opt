#include "accept.h"

#include <cmath>

bool MetropolisAcceptPolicy::ShouldShift(double from_score, double to_score,
                                         long double temperature) {
  if (to_score < from_score) {
    return true;
  }
  long double delta =
      static_cast<long double>(to_score) - static_cast<long double>(from_score);
  return distr_(rnd_) < std::exp(-delta / temperature);
}
