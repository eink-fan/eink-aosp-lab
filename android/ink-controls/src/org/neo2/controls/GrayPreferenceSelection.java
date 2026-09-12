package org.neo2.controls;

/** Persistence decisions, kept independent of Android for failure-path tests. */
final class GrayPreferenceSelection {
    interface Store { int read(); boolean save(int mode); }
    interface Backend { int[] configure(int mode); }
    static final class Result {
        final int mode;
        final boolean saved, redrawQueued;
        Result(int mode, boolean saved, boolean redrawQueued) {
            this.mode = mode; this.saved = saved; this.redrawQueued = redrawQueued;
        }
    }
    static Result apply(int requested, Store store, Backend backend) {
        if (requested < -1 || requested > 2) return new Result(-1, false, false);
        int target = requested == -1 ? store.read() : requested;
        if (target < 0 || target > 2) target = 1;
        int[] reply = backend.configure(target);
        if (reply == null || reply.length != 2 || reply[0] < 0 || reply[0] > 2 ||
                (reply[1] != 0 && reply[1] != 1)) return new Result(-1, false, false);
        boolean saved = reply[0] == target;
        if (saved && requested >= 0) saved = store.save(target);
        return new Result(reply[0], saved, reply[1] == 1);
    }
    private GrayPreferenceSelection() {}
}
