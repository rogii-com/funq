include(
    ${CMAKE_CURRENT_LIST_DIR}/msvs_versions.cmake
)

CNPM_ADD_PACKAGE(
    NAME
        msvs
    VERSION
        ${MSVS_YEAR_VERSION}.${MSVS_VCTOOLS_VERSION}
    BUILD_NUMBER
        0
)

