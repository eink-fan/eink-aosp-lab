#include "eink_color_quality.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>
using namespace neo2::eink;
void Require(bool ok) { if (!ok) std::abort(); }
int main() {
  // black->red, white->green, blue->red, unchanged red, gray->gray.
  const std::vector<std::uint8_t> old={0,0,0,255, 255,255,255,255,
      0,0,255,255, 255,0,0,255, 50,50,50,255};
  const std::vector<std::uint8_t> target={255,0,0,255, 0,255,0,255,
      255,0,0,255, 255,0,0,255, 100,100,100,255};
  ColorQualityHistory history;
  std::vector<std::uint8_t> intermediate;
  history.Accept(old);
  Require(history.Prepare(target,intermediate)==2);
  for (int i=0;i<20;++i) Require(intermediate[i]==((i<4 || (i>=8 && i<12))?255:target[i]));
  // Preparing a dropped candidate must not change accepted history.
  std::vector<std::uint8_t> discarded(target.size(),255);
  Require(history.Prepare(discarded,intermediate)==0 && intermediate.empty());
  Require(history.Prepare(target,intermediate)==2);
  history.Accept(target);
  Require(history.Prepare(target,intermediate)==0 && intermediate.empty());
  Require(history.Prepare(target,intermediate,{},true)==4);
  auto alpha=target;for(std::size_t i=3;i<alpha.size();i+=4)alpha[i]=0;
  Require(history.Prepare(alpha,intermediate)==0);
  auto subtle=target;subtle[1]=5;
  Require(history.Prepare(subtle,intermediate)==0);
  Require(history.Prepare(subtle,intermediate,{24,1,232})==1);
  history.Reset();
  Require(history.Prepare(target,intermediate)==0);
  Require(history.Prepare(target,intermediate,{},true)==4);
  Require(history.Prepare({},intermediate)==0 && intermediate.empty());
  Require(history.Prepare(std::array<std::uint8_t,3>{0,0,0},intermediate)==0);
  // Subtle near-neutral and near-white source transitions obey the thresholds.
  history.Accept(std::array<std::uint8_t,4>{240,240,240,255});
  Require(history.Prepare(std::array<std::uint8_t,4>{200,100,100,255},intermediate)==0);
  Require(history.Prepare(std::array<std::uint8_t,4>{200,100,100,255},intermediate,{24,12,250})==1);
  history.Reset();
  Require(history.Prepare(std::array<std::uint8_t,4>{100,110,120,255},intermediate)==0);
  std::cout << "PASS\n";
}
