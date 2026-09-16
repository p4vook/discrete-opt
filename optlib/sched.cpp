#include "sched.h"

ExpDecayScheduler::ExpDecayScheduler(long double start, long double step,
                                     long double min)
    : temp_(start), step_(step), min_(min) {}

long double ExpDecayScheduler::Temperature() const {
    return temp_;
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

long double LinearScheduler::Temperature() const {
    return temp_;
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

long double ReciprocalScheduler::Temperature() const {
    return temp_;
}

void ReciprocalScheduler::CoolDown() {
    ++iteration_;
    temp_ = start_ / (1.0L + rate_ * iteration_);
}

bool ReciprocalScheduler::IsFrozen() {
    return temp_ <= min_;
}
