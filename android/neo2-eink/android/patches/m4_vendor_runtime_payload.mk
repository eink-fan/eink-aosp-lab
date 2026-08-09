# Private RM06L Android-14 M4 runtime payload. This file is inert until the
# explicit NEO2_M4_RUNTIME_PAYLOAD=true product gate selects it. It adds data
# files only: no SurfaceFlinger source references this payload and no vendor
# wrapper is constructed, initialized, or called by this include.

# This RM06L has no FPGA/H7 bridge. The retained direct engine executor checks
# this exact property before initialization so a generic image cannot silently
# take an unimplemented controller branch. Keep it within the same explicit
# M4 product gate as the private runtime files.
PRODUCT_PRODUCT_PROPERTIES += ro.eink.fpga.bridge.support=0

# aosp_arm64 inherits generic_system.mk, whose artifact guard otherwise rejects
# parent-product additions under system/lib64. Keep this admission exact and
# scoped to the thirteen private system files plus the two reviewed generic
# provider modules (`libusb` and Health).  Those generic modules are dual-ABI,
# so their unavoidable 32-bit sibling outputs are admitted too; system_ext
# payload modules remain in their normal system_ext image path.
PRODUCT_ARTIFACT_PATH_REQUIREMENT_ALLOWED_LIST += \
    system/lib/android.hardware.health-V1-ndk.so \
    system/lib/libusb.so \
    system/lib64/android.hardware.health-V1-ndk.so \
    system/lib64/libEink_Kaleido_render.so \
    system/lib64/libeink.so \
    system/lib64/libeink_ED070KC4.so \
    system/lib64/libeink_ED070KC5.so \
    system/lib64/libeink_color.so \
    system/lib64/libeinkinput.so \
    system/lib64/libeinkpen.so \
    system/lib64/libeinksfpatch.so \
    system/lib64/libeinkskia.so \
    system/lib64/libeinkutils.so \
    system/lib64/libfbhandsurface.so \
    system/lib64/libidisplayV3.so \
    system/lib64/libidisplayengine.so \
    system/lib64/libusb.so \
    system/etc/default_wbf.bin \
    system/etc/default_nm.bin \
    system/etc/neo2_frontlight_calibration.conf \
    system/etc/regal.wbf

PRODUCT_PACKAGES += \
    android.hardware.health-V1-ndk \
    libusb \
    libneo2_stock_einksfpatch \
    libneo2_stock_idisplayengine \
    libneo2_stock_idisplayv3 \
    libneo2_stock_einkutils \
    libneo2_stock_fbhandsurface \
    libneo2_stock_einkpen \
    libneo2_stock_einkskia \
    libneo2_stock_eink \
    libneo2_stock_eink_color \
    libneo2_stock_eink_kaleido_render \
    libneo2_stock_eink_ed070kc4 \
    libneo2_stock_eink_ed070kc5 \
    libneo2_stock_einkinput \
    neo2_stock_default_wbf \
    neo2_stock_default_nm \
    neo2_stock_regal_wbf \
    neo2_stock_frontlight_calibration \
    libneo2_stock_jpeg_oal \
    libneo2_stock_jpeg_alpha \
    libneo2_stock_jpeg_alpha_oal \
    libneo2_stock_mms_1_0 \
    libneo2_stock_mms_1_1 \
    libneo2_stock_mms_1_2 \
    libneo2_stock_mms_1_3
