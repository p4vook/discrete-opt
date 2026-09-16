#pragma once

#include "optlib/public.h"

class Annealer {
public:
  Annealer(std::unique_ptr<StateSpace> space, std::unique_ptr<Scheduler> sched);

  std::unique_ptr<State> Run();

private:
  std::unique_ptr<StateSpace> space_;
  std::unique_ptr<Scheduler> sched_;
  std::unique_ptr<State> optimum_;
};
