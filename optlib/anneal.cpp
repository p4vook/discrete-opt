#include "anneal.h"

Annealer::Annealer(std::unique_ptr<StateSpace> space,
                   std::unique_ptr<Scheduler> sched,
                   std::unique_ptr<AcceptPolicy> policy)
    : space_(std::move(space)), sched_(std::move(sched)),
      policy_(std::move(policy)),
      optimum_(space_->Current()->Snapshot()) {}

std::unique_ptr<State>
Annealer::Run(ProgressCallback progress_callback,
              std::chrono::steady_clock::duration progress_interval) {
  stats_ = {};
  stats_.initial_score = space_->Current()->Evaluate();
  stats_.current_score = stats_.initial_score;
  stats_.best_score = optimum_->Evaluate();
  stats_.temperature = sched_->Temperature();
  const auto started_at = std::chrono::steady_clock::now();
  auto next_progress_at = started_at + progress_interval;
  for (; !sched_->IsFrozen(); sched_->CoolDown()) {
    ++stats_.iterations;
    auto previous_score = space_->Current()->Evaluate();
    auto candidate = space_->Next();
    if (policy_->ShouldShift(previous_score, space_->Current()->Evaluate(),
                             sched_->Temperature())) {
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

    if (progress_callback) {
      const auto now = std::chrono::steady_clock::now();
      if (now >= next_progress_at) {
        stats_.current_score = space_->Current()->Evaluate();
        stats_.temperature = sched_->Temperature();
        stats_.elapsed_seconds =
            std::chrono::duration<double>(now - started_at).count();
        progress_callback(stats_);
        do {
          next_progress_at += progress_interval;
        } while (next_progress_at <= now);
      }
    }
  }
  stats_.final_score = space_->Current()->Evaluate();
  stats_.current_score = stats_.final_score;
  stats_.temperature = sched_->Temperature();
  stats_.elapsed_seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - started_at)
                               .count();
  return std::move(optimum_);
}

const AnnealStats &Annealer::Stats() const {
  return stats_;
}
