#pragma once

#include "optlib/public.h"

#include <cstddef>

struct AnnealStats {
  std::size_t iterations = 0;
  std::size_t accepted = 0;
  std::size_t rejected = 0;
  std::size_t best_updates = 0;
  double initial_score = 0;
  double final_score = 0;
  double best_score = 0;
};

class Annealer {
public:
  Annealer(std::unique_ptr<StateSpace> space, std::unique_ptr<Scheduler> sched,
           std::unique_ptr<AcceptPolicy> policy);

  std::unique_ptr<State> Run();

  const AnnealStats &Stats() const;

private:
  std::unique_ptr<StateSpace> space_;
  std::unique_ptr<Scheduler> sched_;
  std::unique_ptr<AcceptPolicy> policy_;
  std::unique_ptr<State> optimum_;
  AnnealStats stats_;
};
