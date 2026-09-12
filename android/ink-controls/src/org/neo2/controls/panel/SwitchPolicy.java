package org.neo2.controls.panel;

/** Single-worker policy. Times are elapsedRealtime milliseconds, never wall clock. */
final class SwitchPolicy {
    private final String ownPackage;
    private final long cooldown;
    private long delay, lastQueued = -1;
    private String observed, pending;
    private long due;
    SwitchPolicy(String ownPackage, long delay, long cooldown) {
        this.ownPackage = ownPackage; this.delay = delay; this.cooldown = cooldown;
    }
    void reset() { observed = null; pending = null; }
    void configure(long value, long now) {
        delay = value;
        if (pending != null) due = deadline(now);
    }
    private long deadline(long now) {
        return Math.max(now + delay, lastQueued < 0 ? 0 : lastQueued + cooldown);
    }
    boolean observe(String next, long now) {
        if (next == null || next.isEmpty()) { reset(); return false; }
        if (next.equals(observed)) return false;
        boolean first = observed == null;
        observed = next; pending = null;
        if (first || next.equals(ownPackage) || next.equals("com.android.systemui") ||
                next.equals("android")) return false;
        pending = next; due = deadline(now); return true;
    }
    long dueAt() { return pending == null ? -1 : due; }
    String take(long now) {
        if (pending == null || now < due) return null;
        String result = pending; pending = null; return result;
    }
    void queued(long now) { lastQueued = now; }
}
