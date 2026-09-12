#include "eink_lower_engine_layout.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

}  // namespace

int main() {
  // Logical rows [0..3], [10..13], [20..23], [30..33] become two controller
  // rows with adjacent logical rows interleaved byte-for-byte.
  const std::vector<std::uint8_t> logical{
          0, 1, 2, 3,
          10, 11, 12, 13,
          20, 21, 22, 23,
          30, 31, 32, 33,
  };
  std::vector<std::uint8_t> arranged;
  Require(neo2::eink::InterleaveAdjacentGrayscaleRows(
                  logical.data(), logical.size(), 4, 4, &arranged),
          "valid dual-gate layout was rejected");
  Require(arranged == std::vector<std::uint8_t>({
                              0, 10, 1, 11, 2, 12, 3, 13,
                              20, 30, 21, 31, 22, 32, 23, 33,
                      }),
          "adjacent rows were not interleaved into controller order");

  Require(!neo2::eink::InterleaveAdjacentGrayscaleRows(
                  logical.data(), logical.size(), 4, 3, &arranged),
          "odd panel height was accepted");
  Require(!neo2::eink::InterleaveAdjacentGrayscaleRows(
                  logical.data(), logical.size() - 1, 4, 4, &arranged),
          "wrong byte count was accepted");
  Require(!neo2::eink::InterleaveAdjacentGrayscaleRows(
                  nullptr, logical.size(), 4, 4, &arranged),
          "null source was accepted");
  Require(!neo2::eink::InterleaveAdjacentGrayscaleRows(
                  logical.data(), logical.size(), 4, 4, nullptr),
          "null destination was accepted");

  std::cout << "PASS\n";
  return 0;
}
