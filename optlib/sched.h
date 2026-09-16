#pragma once

#include "optlib/public.h"

#include <cstddef>
class ExpDecayScheduler : public Scheduler {
public:
  ExpDecayScheduler(long double start = 1.0L, long double step = 0.999999L,
                    long double min = 0.0001L);

  long double Temperature() const override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double step_;
  const long double min_;
};

class LinearScheduler : public Scheduler {
public:
  LinearScheduler(long double start, long double decrement, long double min);

  long double Temperature() const override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double decrement_;
  const long double min_;
};

class ReciprocalScheduler : public Scheduler {
public:
  ReciprocalScheduler(long double start, long double rate, long double min);

  long double Temperature() const override;

  void CoolDown() override;

  bool IsFrozen() override;

private:
  long double temp_;
  const long double start_;
  const long double rate_;
  const long double min_;
  std::size_t iteration_ = 0;
};
