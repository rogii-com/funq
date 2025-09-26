if not exist "%BUILD_ENV_SCRIPTS_PATH%" (
    echo "Environment scripts directory does not exist. BUILD_ENV_SCRIPTS_PATH = %BUILD_ENV_SCRIPTS_PATH%"
    exit /B 1
)

call "%BUILD_ENV_SCRIPTS_PATH%\vs_sdk.bat" %1 ${MSVS_YEAR_VERSION} ${MSVS_SDK_VERSION} ${MSVS_VCTOOLS_VERSION}
