include(${CMAKE_CURRENT_LIST_DIR}/msvs_package.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/windowssdk_package.cmake)

CNPM_ADD_PACKAGE(
    NAME
        Qt
    VERSION
        6.11.1
    BUILD_NUMBER
        57
    TAG
        "sdk22621_vsbt22"
)

