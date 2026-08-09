#include "eink_snapshot_pool.h"

#include <cassert>
#include <memory>
#include <vector>

namespace {

class Buffer final : public einklab::ComposeBuffer {};

std::shared_ptr<Buffer> MakeBuffer() {
  return std::make_shared<Buffer>();
}

}  // namespace

int main() {
  einklab::SnapshotPool empty_pool({});
  assert(empty_pool.Capacity() == 0);
  assert(!empty_pool.TryAcquire().has_value());
  assert(empty_pool.DropCount() == 1);

  einklab::SnapshotPool null_pool({std::shared_ptr<einklab::ComposeBuffer>{}});
  assert(null_pool.Capacity() == 0);
  assert(!null_pool.TryAcquire().has_value());
  assert(null_pool.DropCount() == 1);

  auto first_buffer = MakeBuffer();
  auto second_buffer = MakeBuffer();
  einklab::SnapshotPool pool({first_buffer, second_buffer});
  assert(pool.Capacity() == 2);
  assert(pool.BusyCount() == 0);
  assert(pool.DropCount() == 0);

  const auto first = pool.TryAcquire();
  const auto second = pool.TryAcquire();
  assert(first.has_value() && second.has_value());
  assert(first->slot != second->slot);
  assert(pool.BusyCount() == 2);
  assert(!pool.TryAcquire().has_value());
  assert(pool.DropCount() == 1);

  // Copying cannot be released as though conversion had completed.
  assert(!pool.ReleaseAfterConversion(*first));
  assert(pool.HandOffToConverter(*first));
  assert(!pool.HandOffToConverter(*first));
  assert(pool.ReleaseAfterConversion(*first));
  assert(pool.BusyCount() == 1);

  const auto replacement = pool.TryAcquire();
  assert(replacement.has_value());
  assert(replacement->slot == first->slot);
  assert(replacement->generation != first->generation);
  // A delayed first worker cannot release a newer copy in the same slot.
  assert(!pool.ReleaseAfterConversion(*first));
  assert(!pool.AbortCopy(*first));
  assert(pool.AbortCopy(*replacement));

  assert(pool.AbortCopy(*second));
  assert(pool.BusyCount() == 0);
}
