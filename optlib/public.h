#pragma once

#include <memory>

struct Scheduler {
  virtual long double Temperature() const = 0;

  virtual void CoolDown() = 0;

  virtual bool IsFrozen() = 0;

  virtual ~Scheduler() = default;
};

struct AcceptPolicy {
  virtual bool ShouldShift(double from_score, double to_score,
                           long double temperature) = 0;

  virtual ~AcceptPolicy() = default;
};

struct State {
  // lower is better
  virtual double Evaluate() const = 0;

  virtual std::unique_ptr<State> Snapshot() const = 0;

  virtual ~State() = default;
};

struct Candidate {
  virtual void Accept() = 0;

  virtual void Reject() = 0;

  virtual ~Candidate() = default;
};

struct StateSpace {
  virtual State *Current() const = 0;

  virtual Candidate *Next() = 0;

  virtual ~StateSpace() = default;
};
