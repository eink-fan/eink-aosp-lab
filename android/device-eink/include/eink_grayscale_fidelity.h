#pragma once

#include "eink_software_grayscale.h"

#include <cstdint>

namespace neo2::eink {

// Aggregate-only comparison of two independently owned grayscale snapshots.
// It retains no pixel storage and is suitable for the R1 source-fidelity
// diagnostic path. `comparable=false` means dimensions or storage are not
// compatible; it is deliberately distinct from a byte mismatch.
struct GrayscaleFidelityResult {
  bool comparable = false;
  bool exact_match = false;
  std::uint64_t differing_pixels = 0;
  int left = 0;
  int top = 0;
  int right = 0;
  int bottom = 0;
};

[[nodiscard]] GrayscaleFidelityResult CompareGrayscaleFidelity(
        const OwnedGrayscaleBuffer& source, const OwnedGrayscaleBuffer& snapshot);

}  // namespace neo2::eink
