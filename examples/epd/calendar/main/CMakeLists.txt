idf_component_register(
   SRCS
      "TPS65185.c" "STC3115.c" "main.c" "tca6408.c" "kxtj3_1057.c" "rv3028.c" "sd_card_functions.c"
      "calendar.c"
   INCLUDE_DIRS "."
)

# calendar.c uses extern "C" / C++ syntax; compile it as C++ so FastEPD.h
# takes the C++ branch and does not re-inline the .inl implementation files.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/calendar.c"
    PROPERTIES LANGUAGE CXX
)