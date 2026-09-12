#include "eink_engine_marker_guard.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using namespace einklab;
void Require(bool ok) { if (!ok) std::abort(); }

int main() {
  using P=EngineMarkerProgress;
  constexpr EngineMarkerContract contract{100,1000};
  // Another frame is displaying while accepted white remains mergeable.
  Require(CheckEngineMarkerStart(10,11,10,0,contract) == P::kWaiting);
  Require(CheckEngineMarkerStart(10,11,10,500,contract) == P::kWaiting);
  Require(CheckEngineMarkerStart(10,11,11,700,contract) == P::kStarted);
  // White starting releases target, but not the following ordinary input.
  Require(CheckEngineMarkerStart(11,12,11,0,contract) == P::kWaiting);
  Require(CheckEngineMarkerStart(11,12,12,500,contract) == P::kStarted);
  Require(CheckEngineMarkerStart(10,12,12,0,contract) == P::kDrift);
  Require(CheckEngineMarkerStart(10,10,10,0,contract) == P::kDrift);
  Require(CheckEngineMarkerStart(10,11,12,0,contract) == P::kDrift);
  Require(CheckEngineMarkerStart(10,11,10,1000,contract) == P::kTimeout);
  Require(CheckEngineMarkerStart(10,11,11,1000,contract) == P::kTimeout);
  Require(CheckEngineMarkerStart(10,0,0,0,contract) == P::kDrift);
  Require(CheckEngineMarkerStart(99,100,100,0,contract) == P::kStarted);
  Require(CheckEngineMarkerStart(100,101,101,0,contract) == P::kDrift);
  Require(CheckEngineMarkerStart(0,1,1,0,{100,0}) == P::kTimeout);
  constexpr auto max=std::numeric_limits<std::uint32_t>::max();
  Require(!CanTrackNextEngineMarker(max,{max,1000}));
  Require(CheckEngineMarkerStart(max,0,0,0,{max,1000}) == P::kDrift);
  std::cout << "PASS\n";
}
