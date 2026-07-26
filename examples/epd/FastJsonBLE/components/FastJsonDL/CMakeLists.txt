# FastJsonDL — ESP-IDF component CMakeLists
#
# Component registration.  Consumers must have FastEPD available either as a
# managed component (managed_components/bitbank2__FastEPD) or as a local
# component (components/FastEPD).
#
# cJSON ships with ESP-IDF as the "json" component, so no extra fetch is
# needed for it.

idf_component_register(
    SRCS        "src/FastJsonDL.cpp"
    INCLUDE_DIRS "include"
    REQUIRES     esp_timer
    PRIV_REQUIRES miniz
)
