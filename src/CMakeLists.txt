FILE(GLOB_RECURSE app_sources "*.cpp" "*.c" "*.S")

idf_component_register(SRCS ${app_sources}
                       INCLUDE_DIRS "."
                       REQUIRES espressif__esp-dsp espressif__esp-nn)