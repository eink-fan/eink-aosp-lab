// Deliberately inert. The Android.bp link-probe target brings the recovered
// Sync backend in through whole_static_libs and resolves it against the exact
// stock libeinksfpatch prebuilt. It is non-installable and has no runtime use.
extern "C" int neo2_vendor_sync_link_probe() {
  return 0;
}