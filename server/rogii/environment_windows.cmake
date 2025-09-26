include(${CMAKE_CURRENT_LIST_DIR}/msvs_package.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/windowssdk_package.cmake)

CNPM_ADD_PACKAGE(
    NAME
        Qt
    VERSION
        6.8.3
    BUILD_NUMBER
        46
    TAG
        "sdk22621_vsbt22"
)
