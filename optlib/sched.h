#pragma once

#include "optlib/public.h"

#include <random>

class ExpDecayScheduler : public Scheduler {
public:
  ExpDecayScheduler(double start = 1, double step = 0.9999, double min = 0.0001);

  bool ShouldShift(double from_score, double to_score) override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  double temp_;
  const double step_;
  const double min_;
  std::uniform_real_distribution<double> distr_{0, 1};
  std::mt19937 rnd_;
};
