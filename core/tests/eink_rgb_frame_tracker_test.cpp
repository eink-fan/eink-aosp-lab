#include "eink_rgb_frame_tracker.h"
#include "eink_frame_demand_gate.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>

using namespace einklab;
void Require(bool ok) { if (!ok) std::abort(); }

int main() {
  // Same luma, different color: the gray gate would discard this transition.
  std::array<std::uint8_t, 4> red{75,0,0,255}, green{0,38,0,255};
  RgbaSource a{red.data(),1,1,1,RgbaLayout::kRgba8888};
  RgbaSource b{green.data(),1,1,1,RgbaLayout::kRgba8888};
  Require(ConvertToGrayscale(a,Rotation::kNone)->Pixels() ==
          ConvertToGrayscale(b,Rotation::kNone)->Pixels());
  RgbFrameTracker tracker(1);
  FrameDemandGate gate;
  Require(gate.Evaluate({tracker.Update(a,4),3,1,1}) == DemandDecision::kSessionStart);
  Require(gate.Evaluate({tracker.Update(b,4),3,1,1}) == DemandDecision::kPanelContentChanged);
  Require(gate.Evaluate({tracker.Update(b,4),3,1,1}) == DemandDecision::kUnchanged);

  for (int width : {1,7,16,31,64,65}) {
    constexpr int height=3;
    const int stride=width+3;
    std::vector<std::uint8_t> pixels(stride*height*4,0);
    RgbaSource source{pixels.data(),width,height,stride,RgbaLayout::kRgba8888};
    RgbFrameTracker exact(width*height);
    auto signature=exact.Update(source,pixels.size());
    Require(signature != 0);
    for (int y=0;y<height;++y) {
      for (int x=0;x<width;++x) {
        for (int c=0;c<3;++c) {
          pixels[(y*stride+x)*4+c] ^= static_cast<std::uint8_t>(0x81+c*13);
          const auto changed=exact.Update(source,pixels.size());
          Require(changed != signature && exact.Update(source,pixels.size()) == changed);
          signature=changed;
        }
        pixels[(y*stride+x)*4+3]=255;
      }
      std::fill(pixels.begin()+(y*stride+width)*4,
                pixels.begin()+(y+1)*stride*4,0xab);
    }
    Require(exact.Update(source,pixels.size()) == signature);
    for (int y=0;y<height;++y) for (int x=0;x<width;++x)
      std::swap(pixels[(y*stride+x)*4],pixels[(y*stride+x)*4+2]);
    source.layout=RgbaLayout::kBgra8888;
    Require(exact.Update(source,pixels.size()) == signature);
    const auto required=static_cast<std::size_t>((height-1)*stride+width)*4;
    Require(exact.Update(source,required) == signature);
    Require(exact.Update(source,required-1) == 0);
    Require(exact.Update({},0) == 0);
    source.layout=static_cast<RgbaLayout>(99);
    Require(exact.Update(source,pixels.size()) == 0);
    source.layout=RgbaLayout::kBgra8888;
    Require(exact.Update(source,pixels.size()) == signature);
    exact.Reset();
    Require(exact.Update(source,pixels.size()) != signature);
    RgbFrameTracker too_small(width*height-1);
    Require(too_small.Update(source,pixels.size()) == 0);
  }
  std::array<std::uint8_t,24> black{};
  RgbFrameTracker geometry(6);
  const auto signature=geometry.Update({black.data(),2,3,2,RgbaLayout::kRgba8888},24);
  Require(geometry.Update({black.data(),3,2,3,RgbaLayout::kRgba8888},24) != signature);
  Require(geometry.Update({black.data(),1,std::numeric_limits<int>::max(),
                          std::numeric_limits<int>::max(),RgbaLayout::kRgba8888},24) == 0);
  std::cout << "PASS\n";
}
