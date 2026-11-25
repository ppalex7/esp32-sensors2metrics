# The following lines of boilerplate have to be in your project's CMakeLists
# in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32-sensors2metrics)

# sdkconfig.defaults applies to new configurations; older local sdkconfig files
# may still select an OTA table or a different flash size.
string(TOLOWER "${CONFIG_ESPTOOLPY_FLASHSIZE}" flash_size)
set(expected_partition_table "partitions/${flash_size}.csv")
if(NOT CONFIG_PARTITION_TABLE_CUSTOM OR
   NOT "${CONFIG_PARTITION_TABLE_CUSTOM_FILENAME}" STREQUAL "${expected_partition_table}")
    message(WARNING "Select the matching single-app partition table (${expected_partition_table}) in menuconfig. See README.md.")
endif()

# Cap the lifetime of positive DNS answers cached by the resolver itself.
idf_component_get_property(lwip_lib lwip COMPONENT_LIB)
target_compile_definitions(${lwip_lib} PRIVATE DNS_MAX_TTL=600)
