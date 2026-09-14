#pragma once

#include "sched.h"

#include <cmath>

ExpDecayScheduler::ExpDecayScheduler(double start, double step, double min)
    : temp_(start), step_(step), min_(min) {}

bool ExpDecayScheduler::ShouldShift(double from_score, double to_score) {
    if (to_score > from_score) {
        return true;
    }
    double shift_probability = exp(-(from_score/to_score)/temp_);
    return distr_(rnd_) < shift_probability;
}

void ExpDecayScheduler::CoolDown() {
    temp_ *= step_;
}

bool ExpDecayScheduler::IsFrozen() {
    return temp_ <= min_;
}

