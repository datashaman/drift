cmake_minimum_required(VERSION 3.25)

project(Drift VERSION 0.1.0 LANGUAGES C CXX)

if(NOT APPLE)
    message(FATAL_ERROR "The Drift POC currently supports macOS only")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)
include(FetchContent)

FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 9.0.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

find_program(NPM_EXECUTABLE npm REQUIRED)

set(DRIFT_UI_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/ui")
set(DRIFT_UI_DIST_DIR "${DRIFT_UI_SOURCE_DIR}/dist")

file(GLOB_RECURSE DRIFT_UI_SOURCES CONFIGURE_DEPENDS
    "${DRIFT_UI_SOURCE_DIR}/src/*"
)

set(DRIFT_UI_OUTPUTS
    "${DRIFT_UI_DIST_DIR}/index.html"
    "${DRIFT_UI_DIST_DIR}/assets/app.css"
    "${DRIFT_UI_DIST_DIR}/assets/app.js"
)

add_custom_command(
    OUTPUT ${DRIFT_UI_OUTPUTS}
    COMMAND "${NPM_EXECUTABLE}" ci
    COMMAND "${NPM_EXECUTABLE}" run build
    WORKING_DIRECTORY "${DRIFT_UI_SOURCE_DIR}"
    DEPENDS
        ${DRIFT_UI_SOURCES}
        "${DRIFT_UI_SOURCE_DIR}/index.html"
        "${DRIFT_UI_SOURCE_DIR}/package.json"
        "${DRIFT_UI_SOURCE_DIR}/package-lock.json"
        "${DRIFT_UI_SOURCE_DIR}/tsconfig.json"
        "${DRIFT_UI_SOURCE_DIR}/vite.config.ts"
    COMMENT "Building the packaged Drift React interface"
    VERBATIM
)
add_custom_target(DriftUIBundle DEPENDS ${DRIFT_UI_OUTPUTS})

juce_add_gui_app(Drift
    PRODUCT_NAME "Drift"
    COMPANY_NAME "datashaman"
    BUNDLE_ID "com.datashaman.drift"
    VERSION "${PROJECT_VERSION}"
)
target_sources(Drift PRIVATE
    Source/App/Main.cpp
    Source/App/MainComponent.cpp
    Source/App/MainComponent.h
    Source/UI/UiResourceProvider.cpp
    Source/UI/UiResourceProvider.h
)
target_include_directories(Drift PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
target_compile_definitions(Drift PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=1
)
target_link_libraries(Drift
    PRIVATE
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
add_dependencies(Drift DriftUIBundle)

add_custom_command(TARGET Drift POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:Drift>/../Resources/ui"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:Drift>/../Resources/ui"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory
        "${DRIFT_UI_DIST_DIR}"
        "$<TARGET_FILE_DIR:Drift>/../Resources/ui"
    COMMENT "Packaging the Drift interface in the application bundle"
    VERBATIM
)

if(BUILD_TESTING)
    add_executable(DriftTests
        Tests/UiResourceProviderTests.cpp
        Source/UI/UiResourceProvider.cpp
        Source/UI/UiResourceProvider.h
    )
    target_include_directories(DriftTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
    target_compile_definitions(DriftTests PRIVATE
        DRIFT_TEST_UI_DIR="${DRIFT_UI_DIST_DIR}"
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=1
    )
    target_link_libraries(DriftTests
        PRIVATE
            juce::juce_gui_extra
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )
    add_dependencies(DriftTests DriftUIBundle)
    add_test(NAME DriftTests COMMAND DriftTests)
endif()
