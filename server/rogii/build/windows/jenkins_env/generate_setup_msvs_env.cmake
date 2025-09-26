include(
    ${CMAKE_CURRENT_LIST_DIR}/../../../msvs_versions.cmake
)

configure_file(
    ${CMAKE_CURRENT_LIST_DIR}/template__setup_msvs_env.bat
    setup_msvs_env.bat
)
