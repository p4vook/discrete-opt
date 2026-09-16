#include "anneal.h"

Annealer::Annealer(std::unique_ptr<StateSpace> space,
                   std::unique_ptr<Scheduler> sched)
    : space_(std::move(space)), sched_(std::move(sched)),
      optimum_(space_->Current()->Snapshot()) {}

std::unique_ptr<State> Annealer::Run() {
  stats_ = {};
  stats_.initial_score = space_->Current()->Evaluate();
  stats_.best_score = optimum_->Evaluate();
  for (; !sched_->IsFrozen(); sched_->CoolDown()) {
    ++stats_.iterations;
    auto previous_score = space_->Current()->Evaluate();
    auto candidate = space_->Next();
    if (sched_->ShouldShift(previous_score, space_->Current()->Evaluate())) {
      ++stats_.accepted;
      candidate->Accept();
      if (space_->Current()->Evaluate() < optimum_->Evaluate()) {
        optimum_ = space_->Current()->Snapshot();
        stats_.best_score = optimum_->Evaluate();
        ++stats_.best_updates;
      }
    } else {
      ++stats_.rejected;
      candidate->Reject();
    }
  }
  stats_.final_score = space_->Current()->Evaluate();
  return std::move(optimum_);
}

const AnnealStats &Annealer::Stats() const {
  return stats_;
}
