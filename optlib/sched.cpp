#include "sched.h"

#include <cmath>

ExpDecayScheduler::ExpDecayScheduler(long double start, long double step,
                                     long double min)
    : temp_(start), step_(step), min_(min) {}

bool ExpDecayScheduler::ShouldShift(double from_score, double to_score) {
    if (to_score < from_score) {
        return true;
    }
    long double delta =
        static_cast<long double>(to_score) - static_cast<long double>(from_score);
    long double shift_probability = std::exp(-delta / temp_);
    return distr_(rnd_) < shift_probability;
}

void ExpDecayScheduler::CoolDown() {
    temp_ *= step_;
}

bool ExpDecayScheduler::IsFrozen() {
    return temp_ <= min_;
}
