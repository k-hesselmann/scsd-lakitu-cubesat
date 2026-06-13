cmake_minimum_required(VERSION 3.13)

# coralmicro ships the ARM toolchain; select it before project() unless the
# caller already passed one. (Matches the coralmicro out-of-tree sample.)
if (NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE
        ${CMAKE_CURRENT_LIST_DIR}/coralmicro/cmake/toolchain-arm-none-eabi-gcc.cmake)
endif()

project(cloud-payload)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# So firmware includes like "libs/base/..." and "third_party/nxp/..." resolve
# against the coralmicro submodule, and pull in its build.
include_directories(coralmicro)
add_subdirectory(coralmicro)

# The DATA file lands on the device filesystem at its path relative to this
# project root — so models/<f> -> /models/<f>, which is kModelPath in the .cc.
add_executable_m7(cloud_payload
    src/cloud_regressor.cc
    DATA
    ${CMAKE_CURRENT_LIST_DIR}/models/cloud_regressor_qat_int8_edgetpu.tflite
)

target_link_libraries(cloud_payload
    libs_base-m7_freertos
    libs_rpc_http_server
)
