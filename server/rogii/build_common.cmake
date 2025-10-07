if(
    NOT DEFINED ROOT
    OR NOT DEFINED ARCH
)
    message(
        FATAL_ERROR
        "Assert: ROOT = ${ROOT}; ARCH = ${ARCH}"
    )
endif()

set(
    PROJECT_ROOT_PATH
    "${CMAKE_CURRENT_LIST_DIR}/.."
)

set(
    ROGII_FOLDER_PATH
    "${CMAKE_CURRENT_LIST_DIR}"
)

if(NOT DEFINED GIT_COMMIT)
    set(
        GIT_COMMIT
        000000
    )

    if(DEFINED ENV{GIT_COMMIT})
        set(
            GIT_COMMIT
            $ENV{GIT_COMMIT}
        )
    else()
        find_package(Git)
        if(Git_FOUND)
            execute_process(
                COMMAND
                    ${GIT_EXECUTABLE} rev-parse --short HEAD
                OUTPUT_VARIABLE
                    GIT_COMMIT
                OUTPUT_STRIP_TRAILING_WHITESPACE
                WORKING_DIRECTORY
                    ${PROJECT_ROOT_PATH}
            )
        endif()
    endif()
endif()

set(
    TAG
    ""
)

if(DEFINED ENV{TAG})
    set(
        TAG
        "$ENV{TAG}"
    )
else()
    set(
        TAG
        "_${GIT_COMMIT}"
    )
endif()

# Detect PROJ version from root CMakeLists.txt
file(
    READ
    "${PROJECT_ROOT_PATH}/CMakeLists.txt"
    _ROOT_CMAKELISTS_CONTENT
)

set(
    VERSION_MAJOR
    0
)
set(
    VERSION_MINOR
    0
)
set(
    VERSION_PATCH
    0
)

# Extract the line with proj_version(...)
string(
    REGEX MATCH
    "proj_version\([^)\n]*\)"
    _PROJ_VERSION_LINE
    "${_ROOT_CMAKELISTS_CONTENT}"
)

if(_PROJ_VERSION_LINE)
    # Extract individual numbers
    string(
        REGEX MATCH
        "MAJOR[ \t]+([0-9]+)"
        _PROJ_VERSION_MAJOR_MATCH
        "${_PROJ_VERSION_LINE}"
    )
    if(_PROJ_VERSION_MAJOR_MATCH)
        set(
            VERSION_MAJOR
            ${CMAKE_MATCH_1}
        )
    endif()

    string(
        REGEX MATCH
        "MINOR[ \t]+([0-9]+)"
        _PROJ_VERSION_MINOR_MATCH
        "${_PROJ_VERSION_LINE}"
    )
    if(_PROJ_VERSION_MINOR_MATCH)
        set(
            VERSION_MINOR
            ${CMAKE_MATCH_1}
        )
    endif()

    string(
        REGEX MATCH
        "PATCH[ \t]+([0-9]+)"
        _PROJ_VERSION_PATCH_MATCH
        "${_PROJ_VERSION_LINE}"
    )
    if(_PROJ_VERSION_PATCH_MATCH)
        set(
            VERSION_PATCH
            ${CMAKE_MATCH_1}
        )
    endif()
endif()

if(NOT DEFINED BUILD_NUMBER)
    if(DEFINED ENV{BUILD_NUMBER})
        set(
            BUILD_NUMBER
            $ENV{BUILD_NUMBER}
        )
    else()
        set(
            BUILD_NUMBER
            0
        )
    endif()
endif()

set(
    PACKAGE_NAME
    "funq-${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${ARCH}-${BUILD_NUMBER}${TAG}"
)

set(
    DEBUG_PATH
    "${PROJECT_ROOT_PATH}/build/debug_${ARCH}"
)

file(
    MAKE_DIRECTORY
    "${DEBUG_PATH}"
)

set(
    CMAKE_INSTALL_PREFIX
    ${ROOT}/${PACKAGE_NAME}
)

file(
    MAKE_DIRECTORY
    "${ROOT}"
)

set(
    GENERATOR
    -G "Ninja"
)

# Attention: TIFF is intentionally disabled because we do not need to download data from external sources.
set(
    ENABLE_TIFF_VALUE
    OFF
)

set(
    ENABLE_CURL_VALUE
    OFF
)

set(FOLDERS_TO_ARCHIVE 
    ../server/bin
    ../server/funq_server
)


execute_process(
    COMMAND
        "${CMAKE_COMMAND}" ${GENERATOR} -DGIT_COMMIT=${GIT_COMMIT} -DCMAKE_BUILD_TYPE=Debug -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DENABLE_TIFF=${ENABLE_TIFF_VALUE} -DENABLE_CURL=${ENABLE_CURL_VALUE} -DBUILD_PROJSYNC=OFF ${PROJECT_ROOT_PATH}
    WORKING_DIRECTORY
        "${DEBUG_PATH}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build .
    WORKING_DIRECTORY
        "${DEBUG_PATH}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build . --target install
    WORKING_DIRECTORY
        "${DEBUG_PATH}"
)

file(
    REMOVE_RECURSE 
    "${PROJECT_ROOT_PATH}/build/"
)

file(
    COPY
        "${PROJECT_ROOT_PATH}/funq_server/"
    DESTINATION 
        "${CMAKE_INSTALL_PREFIX}/funq_server"
)

file(
    COPY
        "${PROJECT_ROOT_PATH}/bin/"
        "${PROJECT_ROOT_PATH}/funq_server/runner.py"
    DESTINATION 
        "${CMAKE_INSTALL_PREFIX}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E tar cf "../funq-Debug.7z" --format=7zip -- .
    WORKING_DIRECTORY
        "${CMAKE_INSTALL_PREFIX}"
)

file(
    REMOVE_RECURSE 
    "${PROJECT_ROOT_PATH}/bin/"
    "${CMAKE_INSTALL_PREFIX}"
)

set(
    RELEASE_PATH
    "${PROJECT_ROOT_PATH}/build/release_${ARCH}"
)

file(
    MAKE_DIRECTORY
    "${RELEASE_PATH}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" ${GENERATOR} -DGIT_COMMIT=${GIT_COMMIT} -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DBUILD_TESTING=OFF -DBUILD_EXAMPLES=OFF -DENABLE_TIFF=${ENABLE_TIFF_VALUE} -DENABLE_CURL=${ENABLE_CURL_VALUE} -DBUILD_PROJSYNC=OFF ${PROJECT_ROOT_PATH}
    WORKING_DIRECTORY
        "${RELEASE_PATH}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build .
    WORKING_DIRECTORY
        "${RELEASE_PATH}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" --build . --target install
    WORKING_DIRECTORY
        "${RELEASE_PATH}"
)

file(
    REMOVE_RECURSE 
    "${PROJECT_ROOT_PATH}/build/"
)

file(
    COPY
        "${PROJECT_ROOT_PATH}/funq_server/"
    DESTINATION 
        "${CMAKE_INSTALL_PREFIX}/funq_server"
)

file(
    COPY
        "${PROJECT_ROOT_PATH}/bin/"
        "${PROJECT_ROOT_PATH}/funq_server/runner.py"
    DESTINATION 
        "${CMAKE_INSTALL_PREFIX}"
)

execute_process(
    COMMAND
        "${CMAKE_COMMAND}" -E tar cf "../funq-RelWithDebInfo.7z" --format=7zip -- .
    WORKING_DIRECTORY
        "${CMAKE_INSTALL_PREFIX}"
)

file(
    REMOVE_RECURSE 
    "${PROJECT_ROOT_PATH}/bin/"
    "${CMAKE_INSTALL_PREFIX}"
)
