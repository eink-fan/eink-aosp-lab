package org.neo2.controls;

/** Pure, deterministic bounds and panel-plane conversion for sleep-image previews. */
final class SleepImageTransforms {
    static final long MAX_ENCODED_BYTES = 32L * 1024L * 1024L;
    static final int MAX_DIMENSION = 16384;
    static final long MAX_SOURCE_PIXELS = 64L * 1024L * 1024L;
    static final long MAX_DECODE_PIXELS = 8L * 1024L * 1024L;
    // SurfaceFlinger's measured display projection is portrait 1072x1448 in
    // Android space and ROTATION_270 into the panel-native 1448x1072 plane.
    // Keep that hardware transform explicit instead of inferring it from a
    // pair of swapped dimensions at individual call sites.
    static final int PRESENTATION_TO_PANEL_ROTATION = 270;

    enum Framing { FIT, CROP }

    static final class DecodePlan {
        final int sampleSize;
        final int width;
        final int height;

        DecodePlan(int sampleSize, int width, int height) {
            this.sampleSize = sampleSize;
            this.width = width;
            this.height = height;
        }
    }

    static final class Crop {
        final int left;
        final int top;
        final int width;
        final int height;

        Crop(int left, int top, int width, int height) {
            this.left = left;
            this.top = top;
            this.width = width;
            this.height = height;
        }
    }

    static final class Planes {
        final byte[] gray;
        final byte[] alpha;

        Planes(byte[] gray, byte[] alpha) {
            this.gray = gray;
            this.alpha = alpha;
        }
    }

    static Planes builtInFallback(int width, int height) {
        if (width < 16 || height < 16) throw new IllegalArgumentException("invalid geometry");
        byte[] gray = new byte[width * height];
        byte[] alpha = new byte[gray.length];
        java.util.Arrays.fill(gray, (byte) 0xf0);
        java.util.Arrays.fill(alpha, (byte) 0xff);

        int left = width / 8;
        int right = width - left - 1;
        int top = height / 8;
        int bottom = height - top - 1;
        int center = width / 2;
        int stroke = Math.max(1, Math.min(width, height) / 180);
        for (int y = top; y <= bottom; ++y) {
            for (int x = left; x <= right; ++x) {
                boolean outline = x - left < stroke || right - x < stroke ||
                        y - top < stroke || bottom - y < stroke;
                boolean fold = Math.abs(x - center) < stroke;
                if (outline || fold) gray[y * width + x] = 0x30;
            }
        }
        return new Planes(gray, alpha);
    }

    static DecodePlan plan(long encodedBytes, int width, int height, boolean animated) {
        if (encodedBytes < 0 || encodedBytes > MAX_ENCODED_BYTES) {
            throw new IllegalArgumentException("encoded image is too large");
        }
        if (animated) throw new IllegalArgumentException("animated images are not supported");
        if (width <= 0 || height <= 0 || width > MAX_DIMENSION || height > MAX_DIMENSION ||
                (long) width * height > MAX_SOURCE_PIXELS) {
            throw new IllegalArgumentException("image dimensions are too large");
        }
        int sample = 1;
        while ((long) ceilDivide(width, sample) * ceilDivide(height, sample) >
                MAX_DECODE_PIXELS) {
            if (sample > (1 << 29)) throw new IllegalArgumentException("no bounded decode plan");
            sample <<= 1;
        }
        return new DecodePlan(sample, ceilDivide(width, sample), ceilDivide(height, sample));
    }

    static Crop crop(int width, int height, float horizontal, float vertical,
            int aspectWidth, int aspectHeight) {
        if (width <= 0 || height <= 0 || aspectWidth <= 0 || aspectHeight <= 0) {
            throw new IllegalArgumentException("invalid geometry");
        }
        horizontal = clamp(horizontal);
        vertical = clamp(vertical);
        int cropWidth = width;
        int cropHeight = (int) (((long) width * aspectHeight) / aspectWidth);
        if (cropHeight > height) {
            cropHeight = height;
            cropWidth = (int) (((long) height * aspectWidth) / aspectHeight);
        }
        cropWidth = Math.max(1, Math.min(width, cropWidth));
        cropHeight = Math.max(1, Math.min(height, cropHeight));
        int left = Math.round((width - cropWidth) * horizontal);
        int top = Math.round((height - cropHeight) * vertical);
        return new Crop(Math.max(0, Math.min(width - cropWidth, left)),
                Math.max(0, Math.min(height - cropHeight, top)), cropWidth, cropHeight);
    }

    static Planes render(int[] pixels, int width, int height, int userRotation,
            Framing framing, float horizontal, float vertical, int outputWidth, int outputHeight) {
        if (pixels == null || pixels.length != (long) width * height || outputWidth <= 0 ||
                outputHeight <= 0) throw new IllegalArgumentException("invalid pixels");
        int rotation = normalizeRotation(userRotation);
        int rotatedWidth = rotation == 90 || rotation == 270 ? height : width;
        int rotatedHeight = rotation == 90 || rotation == 270 ? width : height;
        Crop source = framing == Framing.CROP
                ? crop(rotatedWidth, rotatedHeight, horizontal, vertical,
                        outputWidth, outputHeight)
                : new Crop(0, 0, rotatedWidth, rotatedHeight);
        byte[] gray = new byte[outputWidth * outputHeight];
        byte[] alpha = new byte[gray.length];
        java.util.Arrays.fill(gray, (byte) 0xf0);

        int targetLeft = 0;
        int targetTop = 0;
        int targetWidth = outputWidth;
        int targetHeight = outputHeight;
        if (framing == Framing.FIT) {
            double scale = Math.min((double) outputWidth / source.width,
                    (double) outputHeight / source.height);
            targetWidth = Math.max(1, (int) Math.round(source.width * scale));
            targetHeight = Math.max(1, (int) Math.round(source.height * scale));
            targetLeft = (outputWidth - targetWidth) / 2;
            targetTop = (outputHeight - targetHeight) / 2;
        }
        for (int y = 0; y < targetHeight; ++y) {
            int rotatedY = source.top + Math.min(source.height - 1,
                    (int) (((long) y * source.height) / targetHeight));
            for (int x = 0; x < targetWidth; ++x) {
                int rotatedX = source.left + Math.min(source.width - 1,
                        (int) (((long) x * source.width) / targetWidth));
                int sourceIndex = sourceIndex(rotatedX, rotatedY, width, height, rotation);
                int argb = pixels[sourceIndex];
                int red = (argb >>> 16) & 0xff;
                int green = (argb >>> 8) & 0xff;
                int blue = argb & 0xff;
                int destination = (targetTop + y) * outputWidth + targetLeft + x;
                gray[destination] = (byte) (((77 * red + 150 * green + 29 * blue) >> 8) & 0xf0);
                alpha[destination] = (byte) (argb >>> 24);
            }
        }
        return new Planes(gray, alpha);
    }

    /** Frames in portrait Android space, then applies the measured panel ROTATION_270 once. */
    static Planes renderForPanel(int[] pixels, int width, int height, int userRotation,
            Framing framing, float horizontal, float vertical,
            int panelWidth, int panelHeight) {
        Planes presentation = render(pixels, width, height, userRotation, framing,
                horizontal, vertical, panelHeight, panelWidth);
        return presentationToPanel(presentation, panelHeight, panelWidth);
    }

    /** Converts portrait presentation planes to the fixed landscape panel transport. */
    static Planes presentationToPanel(Planes presentation, int width, int height) {
        validatePlanes(presentation, width, height);
        byte[] gray = new byte[presentation.gray.length];
        byte[] alpha = new byte[presentation.alpha.length];
        int panelWidth = height;
        for (int panelY = 0; panelY < width; ++panelY) {
            for (int panelX = 0; panelX < height; ++panelX) {
                int source = sourceIndex(panelX, panelY, width, height,
                        PRESENTATION_TO_PANEL_ROTATION);
                int destination = panelY * panelWidth + panelX;
                gray[destination] = presentation.gray[source];
                alpha[destination] = presentation.alpha[source];
            }
        }
        return new Planes(gray, alpha);
    }

    /**
     * Inverse-maps final panel planes into portrait UI space and downsamples them.
     * Stored catalog bytes are never rewritten by this presentation-only operation.
     */
    static int[] panelToPresentationPixels(byte[] gray, byte[] alpha,
            int panelWidth, int panelHeight, int outputWidth, int outputHeight) {
        validatePlanes(new Planes(gray, alpha), panelWidth, panelHeight);
        if (outputWidth <= 0 || outputHeight <= 0) {
            throw new IllegalArgumentException("invalid preview geometry");
        }
        int presentationWidth = panelHeight;
        int presentationHeight = panelWidth;
        int[] pixels = new int[Math.multiplyExact(outputWidth, outputHeight)];
        for (int y = 0; y < outputHeight; ++y) {
            int presentationY = Math.min(presentationHeight - 1,
                    (int) (((long) y * presentationHeight) / outputHeight));
            for (int x = 0; x < outputWidth; ++x) {
                int presentationX = Math.min(presentationWidth - 1,
                        (int) (((long) x * presentationWidth) / outputWidth));
                int panelX = presentationY;
                int panelY = presentationWidth - 1 - presentationX;
                int source = panelY * panelWidth + panelX;
                int value = gray[source] & 0xff;
                int opacity = alpha[source] & 0xff;
                pixels[y * outputWidth + x] =
                        (opacity << 24) | (value << 16) | (value << 8) | value;
            }
        }
        return pixels;
    }

    /** Maps an oriented-image coordinate back to its encoded raster for EXIF orientations 1-8. */
    static int exifSourceIndex(int x, int y, int width, int height, int orientation) {
        int sourceX;
        int sourceY;
        switch (orientation) {
            case 1: sourceX = x; sourceY = y; break;
            case 2: sourceX = width - 1 - x; sourceY = y; break;
            case 3: sourceX = width - 1 - x; sourceY = height - 1 - y; break;
            case 4: sourceX = x; sourceY = height - 1 - y; break;
            case 5: sourceX = y; sourceY = x; break;
            case 6: sourceX = y; sourceY = height - 1 - x; break;
            case 7: sourceX = width - 1 - y; sourceY = height - 1 - x; break;
            case 8: sourceX = width - 1 - y; sourceY = x; break;
            default: throw new IllegalArgumentException("invalid EXIF orientation");
        }
        if (sourceX < 0 || sourceY < 0 || sourceX >= width || sourceY >= height) {
            throw new IllegalArgumentException("oriented coordinate is out of bounds");
        }
        return sourceY * width + sourceX;
    }

    private static int sourceIndex(int x, int y, int width, int height, int rotation) {
        int sourceX;
        int sourceY;
        switch (rotation) {
            case 0: sourceX = x; sourceY = y; break;
            case 90: sourceX = y; sourceY = height - 1 - x; break;
            case 180: sourceX = width - 1 - x; sourceY = height - 1 - y; break;
            case 270: sourceX = width - 1 - y; sourceY = x; break;
            default: throw new AssertionError();
        }
        return sourceY * width + sourceX;
    }

    private static int normalizeRotation(int rotation) {
        int normalized = ((rotation % 360) + 360) % 360;
        if (normalized % 90 != 0) throw new IllegalArgumentException("rotation must be orthogonal");
        return normalized;
    }

    private static void validatePlanes(Planes planes, int width, int height) {
        if (planes == null || planes.gray == null || planes.alpha == null ||
                width <= 0 || height <= 0 ||
                planes.gray.length != (long) width * height ||
                planes.alpha.length != planes.gray.length) {
            throw new IllegalArgumentException("invalid planes");
        }
    }

    private static int ceilDivide(int value, int divisor) {
        return (value + divisor - 1) / divisor;
    }

    private static float clamp(float value) {
        if (Float.isNaN(value)) return 0.5f;
        return Math.max(0f, Math.min(1f, value));
    }

    private SleepImageTransforms() {}
}
