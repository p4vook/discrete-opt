#pragma once

#include "optlib/public.h"

#include <random>

class MetropolisAcceptPolicy : public AcceptPolicy {
public:
  bool ShouldShift(double from_score, double to_score,
                   long double temperature) override;

private:
  std::uniform_real_distribution<long double> distr_{0.0L, 1.0L};
  std::mt19937 rnd_;
};
