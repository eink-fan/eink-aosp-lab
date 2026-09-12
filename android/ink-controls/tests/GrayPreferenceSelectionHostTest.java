package org.neo2.controls;

public final class GrayPreferenceSelectionHostTest {
    private static final class Store implements GrayPreferenceSelection.Store {
        int mode = 1, writes;
        boolean fail;
        public int read() { return mode; }
        public boolean save(int value) {
            ++writes;
            if (fail) return false;
            mode = value; return true;
        }
    }
    private static void require(boolean ok) { if (!ok) throw new AssertionError(); }
    public static void main(String[] args) {
        Store store = new Store();
        for (int selected = 0; selected <= 2; ++selected) {
            var result = GrayPreferenceSelection.apply(selected, store, m -> new int[]{m, 1});
            require(result.mode == selected && result.saved && result.redrawQueued);
            require(store.mode == selected);
            int writes = store.writes;
            // A restarted renderer must receive the durable choice, not have
            // its new default copied over that choice by a UI read.
            result = GrayPreferenceSelection.apply(-1, store, m -> new int[]{m, 0});
            require(result.mode == selected && result.saved && !result.redrawQueued);
            require(store.writes == writes);
        }
        int writes = store.writes;
        for (int[] reply : new int[][] {null, {}, {7,0}, {1,3}, {1,1,1}}) {
            var result = GrayPreferenceSelection.apply(0, store, m -> reply);
            require(result.mode == -1 && !result.saved && store.writes == writes);
        }
        var mismatch = GrayPreferenceSelection.apply(0, store, m -> new int[]{2,0});
        require(!mismatch.saved && store.mode == 2 && store.writes == writes);
        store.fail = true;
        var failure = GrayPreferenceSelection.apply(0, store, m -> new int[]{m,1});
        require(failure.mode == 0 && !failure.saved && store.mode == 2);
        store.mode = 99;
        var fallback = GrayPreferenceSelection.apply(-1, store, m -> new int[]{m,0});
        require(fallback.mode == 1);
        var invalid = GrayPreferenceSelection.apply(3, store, m -> { throw new AssertionError(); });
        require(invalid.mode == -1);
        System.out.println("Gray preference selection PASS");
    }
}
