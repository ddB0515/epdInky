set(srcs 
    "src/miniz_tdef.c"
    "src/miniz_tinfl.c"
    "src/miniz_zip.c"
    "src/miniz.c"
)

idf_component_register(
    SRCS ${srcs}      
    INCLUDE_DIRS "."
)