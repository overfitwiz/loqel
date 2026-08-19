cmake_minimum_required(VERSION 3.26)

project(loqel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/utf-8)
endif()

set(
    NEMO_SDK_DIR
    "${CMAKE_SOURCE_DIR}/install/nemo-cpu-min"
    CACHE PATH
    "Installed NeMo Speech SDK prefix for the current platform"
)

set(
    LLAMA_SDK_DIR
    "${CMAKE_SOURCE_DIR}/install/llama-cpu-min"
    CACHE PATH
    "Installed llama.cpp SDK prefix for the current platform"
)

option(
    LOQEL_BUILD_APP
    "Build the native desktop application (currently Windows only)"
    ${WIN32}
)

option(
    LOQEL_BUILD_CORE
    "Build the portable dictation core (requires an installed NeMo Speech SDK)"
    ${LOQEL_BUILD_APP}
)

if(LOQEL_BUILD_APP AND NOT LOQEL_BUILD_CORE)
    message(FATAL_ERROR "LOQEL_BUILD_APP requires LOQEL_BUILD_CORE=ON")
endif()

add_library(
    loqel_formatter
    STATIC
    src/MarkdownFormatter.cpp
)

target_include_directories(
    loqel_formatter
    PUBLIC
    src
)

if(LOQEL_BUILD_CORE)
    list(PREPEND CMAKE_PREFIX_PATH "${NEMO_SDK_DIR}" "${LLAMA_SDK_DIR}")
    find_package(NeMoSpeech REQUIRED COMPONENTS ASR)
    find_package(llama CONFIG REQUIRED)

    add_library(
        loqel_core
        STATIC
        src/core/ApplicationCore.cpp
        src/AudioQueue.cpp
        src/AsrEngine.cpp
        src/LlmCorrectionPolicy.cpp
        src/LlmPostprocessor.cpp
    )

    target_include_directories(
        loqel_core
        PUBLIC
        src
        src/core
    )

    target_link_libraries(
        loqel_core
        PUBLIC
        loqel_formatter
        NeMoSpeech::ASR
        llama
    )
endif()

if(LOQEL_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR
            "The desktop platform adapter is currently implemented only for Windows. "
            "Build with -DLOQEL_BUILD_APP=OFF -DLOQEL_BUILD_CORE=ON and "
            "follow docs/build-macos.md or docs/build-ubuntu.md."
        )
    endif()

    enable_language(RC)

    add_executable(
        loqel
        WIN32
        src/main.cpp
        src/App.cpp
        src/TrayIcon.cpp
        src/Hotkey.cpp
        src/Overlay.cpp
        src/TextInjector.cpp
        src/Paths.cpp
        src/FormattingSettings.cpp
        src/SettingsWindow.cpp
        src/AudioCapture.cpp
        src/windows/WindowsText.cpp
        src/resources.rc
    )

    target_include_directories(
        loqel
        PRIVATE
        src
        src/core
    )

    target_link_libraries(
        loqel
        PRIVATE
        loqel_core
        user32
        shell32
        gdi32
        ole32
        ksuser
    )

    target_compile_definitions(
        loqel
        PRIVATE
        UNICODE
        _UNICODE
    )

    # The checked-in development SDK is a Windows installation prefix. Copy
    # its runtime DLLs beside the executable; release packaging can replace
    # this with an install(CODE) dependency collection step later.
    if(EXISTS "${NEMO_SDK_DIR}/bin")
        add_custom_command(
            TARGET loqel
            POST_BUILD
            COMMAND
                ${CMAKE_COMMAND} -E copy_directory
                "${NEMO_SDK_DIR}/bin"
                "$<TARGET_FILE_DIR:loqel>"
        )
    endif()

    # A release is a portable directory: the application, the NeMo/ggml
    # runtime DLLs, the MSVC runtime, and the optional model downloader.
    install(TARGETS loqel RUNTIME DESTINATION .)

    install(
        DIRECTORY "${NEMO_SDK_DIR}/bin/"
        DESTINATION .
        FILES_MATCHING
        PATTERN "*.dll"
    )

    install(FILES download-model.bat download-llm.bat DESTINATION .)

    if(EXISTS "${NEMO_SDK_DIR}/share/licenses/nemo-speech")
        install(
            DIRECTORY "${NEMO_SDK_DIR}/share/licenses/nemo-speech"
            DESTINATION licenses
        )
    endif()

    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
    include(InstallRequiredSystemLibraries)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(
        markdown_formatter_tests
        tests/MarkdownFormatterTests.cpp
    )

    target_link_libraries(
        markdown_formatter_tests
        PRIVATE
        loqel_formatter
    )

    add_test(NAME markdown_formatter COMMAND markdown_formatter_tests)

    add_executable(
        application_settings_tests
        tests/ApplicationSettingsTests.cpp
    )

    target_include_directories(
        application_settings_tests
        PRIVATE
        src
    )

    add_test(NAME application_settings COMMAND application_settings_tests)

    add_executable(
        llm_correction_policy_tests
        tests/LlmCorrectionPolicyTests.cpp
        src/LlmCorrectionPolicy.cpp
    )

    target_include_directories(
        llm_correction_policy_tests
        PRIVATE
        src
    )

    add_test(NAME llm_correction_policy COMMAND llm_correction_policy_tests)

    if(LOQEL_BUILD_CORE)
        add_executable(
            llm_postprocessor_integration_tests
            tests/LlmPostprocessorIntegrationTests.cpp
        )

        target_link_libraries(
            llm_postprocessor_integration_tests
            PRIVATE
            loqel_core
        )

        add_test(
            NAME llm_postprocessor_integration
            COMMAND llm_postprocessor_integration_tests
        )
        set_tests_properties(
            llm_postprocessor_integration
            PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        )
    endif()
endif()
