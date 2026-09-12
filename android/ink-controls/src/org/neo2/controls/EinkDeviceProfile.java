package org.neo2.controls;

/** Immutable build-selected geometry/capability data; product behavior remains common. */
final class EinkDeviceProfile {
    static final EinkDeviceProfile NEO2 = new EinkDeviceProfile(
            "neo2", 1448, 1072, 1072, 1448, 270,
            "default-open-book-pattern.png", "built-in-fallback", true, true, true, false);
    static final EinkDeviceProfile OCEAN = new EinkDeviceProfile(
            "ocean", 1264, 1680, 1264, 1680, 0,
            "default-open-book-pattern-ocean.png", "built-in-fallback-ocean-v1", true, true,
            false, false);
    static final EinkDeviceProfile MUSNAP_X = new EinkDeviceProfile(
            "musnap_x", 1920, 2560, 1920, 2560, 0,
            "default-open-book-pattern-musnap-x.png", "built-in-fallback-musnap-x-v1", false,
            true, false, false);
    static final EinkDeviceProfile AURA_C = new EinkDeviceProfile(
            "aura_c", 2480, 1860, 1860, 2480, 270,
            "default-open-book-pattern-aura-c.png", "built-in-fallback-aura-c-v2", true,
            false, false, true);

    final String name;
    final int panelWidth;
    final int panelHeight;
    final int presentationWidth;
    final int presentationHeight;
    final int presentationToPanelRotation;
    final String defaultSleepImageAsset;
    final String builtInSleepImageId;
    final boolean hasFrontLight;
    final boolean hasPogoButtons;
    final boolean hasDefaultNmResource;
    final boolean hasColorCfa;

    private EinkDeviceProfile(String name, int panelWidth, int panelHeight,
            int presentationWidth, int presentationHeight,
            int presentationToPanelRotation, String defaultSleepImageAsset,
            String builtInSleepImageId, boolean hasFrontLight, boolean hasPogoButtons,
            boolean hasDefaultNmResource, boolean hasColorCfa) {
        this.name = name;
        this.panelWidth = panelWidth;
        this.panelHeight = panelHeight;
        this.presentationWidth = presentationWidth;
        this.presentationHeight = presentationHeight;
        this.presentationToPanelRotation = presentationToPanelRotation;
        this.defaultSleepImageAsset = defaultSleepImageAsset;
        this.builtInSleepImageId = builtInSleepImageId;
        this.hasFrontLight = hasFrontLight;
        this.hasPogoButtons = hasPogoButtons;
        this.hasDefaultNmResource = hasDefaultNmResource;
        this.hasColorCfa = hasColorCfa;
    }

    static EinkDeviceProfile require(String name) {
        if (NEO2.name.equals(name)) return NEO2;
        if (OCEAN.name.equals(name)) return OCEAN;
        if (MUSNAP_X.name.equals(name)) return MUSNAP_X;
        if (AURA_C.name.equals(name)) return AURA_C;
        throw new IllegalArgumentException("unsupported e-ink device variant");
    }

    SleepImageTransforms.Planes renderFallback(int[] pixels, int width, int height) {
        // Aura's bundled artwork is portrait presentation space. Earlier
        // products' assets are already panel-oriented; preserve their bytes.
        if (this == AURA_C) {
            return SleepImageTransforms.renderForPanel(pixels, width, height, 0,
                    SleepImageTransforms.Framing.FIT, 0.5f, 0.5f,
                    panelWidth, panelHeight, presentationToPanelRotation, hasColorCfa);
        }
        return SleepImageTransforms.render(pixels, width, height, 0,
                SleepImageTransforms.Framing.FIT, 0.5f, 0.5f, panelWidth, panelHeight);
    }
}
