include(
    ${CMAKE_CURRENT_LIST_DIR}/msvs_versions.cmake
)

CNPM_ADD_PACKAGE(
    NAME
        windowssdk
    VERSION
        ${MSVS_SDK_VERSION}
    BUILD_NUMBER
        0
)