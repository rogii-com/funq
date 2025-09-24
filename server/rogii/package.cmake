include(CMakeFindDependencyMacro)

# Expose PROJ library and headers from this package location

if(NOT TARGET PROJ::proj)
    add_library(
        PROJ::proj
        SHARED
        IMPORTED
    )

    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        file(GLOB _PROJ_DLL_RELEASE "${CMAKE_CURRENT_LIST_DIR}/bin/proj_*.dll")
        list(FILTER _PROJ_DLL_RELEASE EXCLUDE REGEX ".*_d\\.dll$")
        list(SORT _PROJ_DLL_RELEASE)
        list(GET _PROJ_DLL_RELEASE 0 _PROJ_DLL_PATH)
        if(NOT _PROJ_DLL_PATH)
            message(FATAL_ERROR "PROJ runtime DLL not found in ${CMAKE_CURRENT_LIST_DIR}/bin")
        endif()

        file(GLOB _PROJ_DLL_DEBUG "${CMAKE_CURRENT_LIST_DIR}/bin/*_d.dll")
        list(SORT _PROJ_DLL_DEBUG)
        list(GET _PROJ_DLL_DEBUG 0 _PROJ_DLL_DEBUG_PATH)

        set(_IMPLIB_RELEASE "${CMAKE_CURRENT_LIST_DIR}/lib/proj.lib")
        set(_IMPLIB_DEBUG "${CMAKE_CURRENT_LIST_DIR}/lib/proj_d.lib")
        if(NOT EXISTS "${_IMPLIB_DEBUG}")
            set(_IMPLIB_DEBUG "${_IMPLIB_RELEASE}")
        endif()

        set_target_properties(
            PROJ::proj
            PROPERTIES
                IMPORTED_LOCATION
                    ${_PROJ_DLL_PATH}
                IMPORTED_IMPLIB
                    ${_IMPLIB_RELEASE}
                IMPORTED_LOCATION_DEBUG
                    ${_PROJ_DLL_DEBUG_PATH}
                IMPORTED_IMPLIB_DEBUG
                    ${_IMPLIB_DEBUG}
                INTERFACE_INCLUDE_DIRECTORIES
                    ${CMAKE_CURRENT_LIST_DIR}/include
        )
    else()
        file(GLOB _PROJ_SO "${CMAKE_CURRENT_LIST_DIR}/lib/libproj*.so*")
        list(SORT _PROJ_SO)
        list(GET _PROJ_SO 0 _PROJ_SO_PATH)
        if(NOT _PROJ_SO_PATH)
            set(_PROJ_SO_PATH ${CMAKE_CURRENT_LIST_DIR}/lib/libproj.so)
        endif()
        set_target_properties(
            PROJ::proj
            PROPERTIES
                IMPORTED_LOCATION
                    ${_PROJ_SO_PATH}
                IMPORTED_LOCATION_DEBUG
                    ${_PROJ_SO_PATH}
                INTERFACE_INCLUDE_DIRECTORIES
                    ${CMAKE_CURRENT_LIST_DIR}/include
        )
    endif()
endif()

# Install convenience: drop runtime and library into root of package when this
# package.cmake is consumed by CNPM installer. Keep component names generic.
set(
    COMPONENT_NAMES

    CNPM_RUNTIME_proj
    CNPM_RUNTIME
)

foreach(COMPONENT_NAME ${COMPONENT_NAMES})
    install(
        FILES
            $<TARGET_FILE:PROJ::proj>
            $<$<CONFIG:Debug>:$<TARGET_FILE:PROJ::proj>>
        DESTINATION
            .
        COMPONENT
            ${COMPONENT_NAME}
        EXCLUDE_FROM_ALL
    )
endforeach()

