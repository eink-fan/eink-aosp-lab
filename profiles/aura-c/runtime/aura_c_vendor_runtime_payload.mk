# Aura C Android-14 private runtime selected only by the Aura product. The
# Kaleido LUT files remain on the stock vendor partition and are verified as
# owner-device prerequisites rather than duplicated into the GSI system image.
PRODUCT_ARTIFACT_PATH_REQUIREMENT_ALLOWED_LIST += \
    system/lib64/libEink_Kaleido_render_64.so \
    system/lib64/libeink.so \
    system/lib64/libeink_color.so \
    system/lib64/libeinkutils.so \
    system/lib64/libidisplayengine.so \
    system/etc/default_wbf.bin \
    system/etc/regal.wbf \
    system/etc/aura_c_frontlight_calibration.conf

PRODUCT_PACKAGES += \
    libaura_c_stock_idisplayengine \
    libaura_c_stock_eink \
    libaura_c_stock_eink_color \
    libaura_c_stock_kaleido_render \
    libaura_c_stock_einkutils \
    aura_c_stock_default_wbf \
    aura_c_stock_regal_wbf \
    aura_c_stock_frontlight_levels

PRODUCT_PRODUCT_PROPERTIES += \
    ro.eink.fpga.bridge.support=0 \
    ro.sys.eink.idisplay.cfa=1 \
    ro.sys.eink.idisplay.cfa.regal=0
