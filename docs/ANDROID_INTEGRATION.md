# Android framework integration targets

Select three independent inputs: the framework source revision, a model/runtime
binding, and the presentation policies supported by that binding. A target
Android version is not the version of the extracted vendor runtime. Runtime
compatibility must be established for the exact pair; API level alone is not
an ABI guarantee.

| Framework target | Model/runtime example | Public coverage |
| --- | --- | --- |
| Android 14 / API 34 | Neo 2 monochrome runtime | Profile, authored capture/engine bridge and historical patches |
| Android 14 / API 34 | Aura C color runtime | Pinned manifest, ordered patches, native graft, Controls and fresh-workspace tooling; public assembly not built/tested |
| Android 17 / API 37 | Neo 2 Android-14 runtime | Pinned local extraction, setup and systemimage build recipe |

These rows record available integration work, not a compatibility matrix for
unlisted combinations. A future color device or another Android release can
reuse portable policies while providing its own binding.

## Android 14 integration boundary

The existing [Android bridge](../android/neo2-eink/android/README.md) and
`android/neo2-eink/android/patches/android14-*.patch` record staged metadata,
capture, loader, and lower-engine work. They are historical stages, not a
single patch series to apply indiscriminately. Select and review a coherent
stage against an exact pinned source revision. The public Android-17 setup
helper is not an Android-14 builder.

An Android-14 port must provide its own immutable manifest, ordered patch set,
payload identity checks, and build contract before claiming reproducibility.
The [Aura recipe](../profiles/aura-c/BUILD.md) supplies these source inputs and
tools; its new public assembly still needs separately authorized build/device
qualification. It does not depend on a private build server or checkout.
Compile SurfaceFlinger capture and synchronization against that target's
RenderEngine, GraphicBuffer, fence, and Binder interfaces. Rebase framework
services, Settings and SystemUI at their actual Android-14 owners; copying
Android-17 patches or assuming newer Soong properties exist is insufficient.
Check platform span types and size arithmetic with that target's toolchain.

Keep lifecycle delivery and presentation ownership explicit: preserve RGB for
color sleep images, invalidate stale catalogs at generation changes, and order
the final sleep frame before backend power-off according to its verified
contract. A delay alone cannot prove completion. App packaging and provisioning
are separate framework concerns and must not become color-policy dependencies.

Normalize a broken composer mode only in the model binding, for the exact
observed tuple. Leave unknown tuples to the geometry gate. A successful
Android-14 build does not validate an older vendor composer, runtime loading,
userdata compatibility, or physical output.

Validate in layers: host policy tests, exact-target compilation, payload and
ABI checks, then separately authorized boot and device observations. Preserve
the [safety boundary](SAFETY.md) and [DSU decisions](../tools/dsu/README.md).
