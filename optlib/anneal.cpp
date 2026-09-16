#include "anneal.h"

Annealer::Annealer(std::unique_ptr<StateSpace> space,
                   std::unique_ptr<Scheduler> sched)
    : space_(std::move(space)), sched_(std::move(sched)),
      optimum_(space_->Current()->Snapshot()) {}

std::unique_ptr<State> Annealer::Run() {
  for (; !sched_->IsFrozen(); sched_->CoolDown()) {
    auto previous_score = space_->Current()->Evaluate();
    auto candidate = space_->Next();
    if (sched_->ShouldShift(previous_score, space_->Current()->Evaluate())) {
      candidate->Accept();
      if (space_->Current()->Evaluate() < optimum_->Evaluate()) {
        optimum_ = space_->Current()->Snapshot();
      }
    } else {
      candidate->Reject();
    }
  }
  return std::move(optimum_);
}
