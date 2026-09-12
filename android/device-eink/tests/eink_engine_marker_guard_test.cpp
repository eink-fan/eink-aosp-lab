#include "eink_engine_marker_guard.h"

#include <cstdlib>
#include <iostream>

using neo2::eink::CheckEngineMarkerStart;
using neo2::eink::EngineMarkerProgress;

void Require(bool value, const char* description) {
  if (!value) { std::cerr << description << '\n'; std::exit(1); }
}

int main() {
  using P = EngineMarkerProgress;
  // Reproduce the failing queue phase: the previous square is still running
  // after white acceptance. Neither acceptance nor an arbitrary gap releases
  // the target. Only the exact white reaching display releases it.
  Require(CheckEngineMarkerStart(3241, 3242, 3241, 0, 5000) == P::kWaiting,
          "acceptance released target");
  Require(CheckEngineMarkerStart(3241, 3242, 3241, 250, 5000) == P::kWaiting,
          "elapsed gap released target");
  Require(CheckEngineMarkerStart(3241, 3242, 3242, 450, 5000) == P::kStarted,
          "exact white start did not release target");
  Require(CheckEngineMarkerStart(3242, 3243, 3242, 250, 5000) == P::kWaiting,
          "white start released following square before target start");
  Require(CheckEngineMarkerStart(3242, 3243, 3243, 450, 5000) == P::kStarted,
          "exact target start did not release following square");
  Require(CheckEngineMarkerStart(3241, 3243, 3243, 450, 5000) == P::kDrift,
          "skipped white treated as success");
  Require(CheckEngineMarkerStart(3241, 3241, 3241, 0, 5000) == P::kDrift,
          "unchanged acceptance marker accepted");
  Require(CheckEngineMarkerStart(3241, 3242, 3243, 450, 5000) == P::kDrift,
          "display marker beyond expected accepted");
  Require(CheckEngineMarkerStart(3241, 3242, 3241, 5000, 5000) == P::kTimeout,
          "stalled engine waited without bound");
  Require(CheckEngineMarkerStart(3241, 3242, 3242, 5000, 5000) == P::kTimeout,
          "late observation accepted after deadline");
  Require(CheckEngineMarkerStart(0xfffffffeU, 0, 0, 0, 5000) == P::kDrift,
          "engine special reset guessed across");
  Require(CheckEngineMarkerStart(0xffffffffU, 0, 0, 0, 5000) == P::kDrift,
          "marker wrap guessed across");
  std::cout << "PASS\n";
}
