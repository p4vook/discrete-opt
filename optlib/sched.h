#pragma once

#include "optlib/public.h"

#include <cstddef>
#include <random>

class ExpDecayScheduler : public Scheduler {
public:
  ExpDecayScheduler(long double start = 1.0L, long double step = 0.999999L,
                    long double min = 0.0001L);

  bool ShouldShift(double from_score, double to_score) override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double step_;
  const long double min_;
  std::uniform_real_distribution<long double> distr_{0.0L, 1.0L};
  std::mt19937 rnd_;
};

class LinearScheduler : public Scheduler {
public:
  LinearScheduler(long double start, long double decrement, long double min);

  bool ShouldShift(double from_score, double to_score) override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double decrement_;
  const long double min_;
  std::uniform_real_distribution<long double> distr_{0.0L, 1.0L};
  std::mt19937 rnd_;
};

class ReciprocalScheduler : public Scheduler {
public:
  ReciprocalScheduler(long double start, long double rate, long double min);

  bool ShouldShift(double from_score, double to_score) override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double start_;
  const long double rate_;
  const long double min_;
  std::size_t iteration_ = 0;
  std::uniform_real_distribution<long double> distr_{0.0L, 1.0L};
  std::mt19937 rnd_;
};
