#include "eink_submission_observer.h"

#include <cstdlib>
#include <iostream>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  einklab::SubmissionObserver observer;
  Require(!observer.ObserveTimeout(100, 10), "timeout existed before a submission");

  observer.RecordAccepted(7, 1'000);
  Require(observer.ObserveTimeout(1'500, 500), "timeout was not observed at the boundary");
  Require(!observer.ObserveTimeout(2'000, 500), "same submission timed out twice");

  observer.RecordAccepted(8, 2'000);
  const auto diagnostics = observer.diagnostics();
  Require(diagnostics.accepted_submissions == 2 && diagnostics.completion_unknown_submissions == 2 &&
                  diagnostics.timeout_observations == 1 && diagnostics.latest_submission_id == 8 &&
                  diagnostics.pending_completion_unknown && diagnostics.latest_timeout_elapsed_ns == 0,
          "submission observation state is wrong");
  std::cout << "PASS\n";
  return 0;
}
