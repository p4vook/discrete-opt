#include "sched.h"

#include <cmath>

namespace {

bool ShouldAccept(double from_score, double to_score, long double temp,
                  std::uniform_real_distribution<long double> &distr,
                  std::mt19937 &rnd) {
    if (to_score < from_score) {
        return true;
    }
    long double delta =
        static_cast<long double>(to_score) - static_cast<long double>(from_score);
    return distr(rnd) < std::exp(-delta / temp);
}

}  // namespace

ExpDecayScheduler::ExpDecayScheduler(long double start, long double step,
                                     long double min)
    : temp_(start), step_(step), min_(min) {}

bool ExpDecayScheduler::ShouldShift(double from_score, double to_score) {
    return ShouldAccept(from_score, to_score, temp_, distr_, rnd_);
}

void ExpDecayScheduler::CoolDown() {
    temp_ *= step_;
}

bool ExpDecayScheduler::IsFrozen() {
    return temp_ <= min_;
}

LinearScheduler::LinearScheduler(long double start, long double decrement,
                                 long double min)
    : temp_(start), decrement_(decrement), min_(min) {}

bool LinearScheduler::ShouldShift(double from_score, double to_score) {
    return ShouldAccept(from_score, to_score, temp_, distr_, rnd_);
}

void LinearScheduler::CoolDown() {
    temp_ -= decrement_;
}

bool LinearScheduler::IsFrozen() {
    return temp_ <= min_;
}

ReciprocalScheduler::ReciprocalScheduler(long double start, long double rate,
                                         long double min)
    : temp_(start), start_(start), rate_(rate), min_(min) {}

bool ReciprocalScheduler::ShouldShift(double from_score, double to_score) {
    return ShouldAccept(from_score, to_score, temp_, distr_, rnd_);
}

void ReciprocalScheduler::CoolDown() {
    ++iteration_;
    temp_ = start_ / (1.0L + rate_ * iteration_);
}

bool ReciprocalScheduler::IsFrozen() {
    return temp_ <= min_;
}
