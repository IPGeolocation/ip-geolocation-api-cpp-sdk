cmake_minimum_required(VERSION 3.16)

project(ipgeolocation_cpp_sdk VERSION 1.0.0 LANGUAGES CXX)

option(IPGEOLOCATION_BUILD_TESTS "Build the C++ SDK unit tests" ON)
option(IPGEOLOCATION_ENABLE_COVERAGE "Enable LLVM source-based coverage instrumentation" OFF)
set(IPGEOLOCATION_COVERAGE_MIN_LINE "90.0")
set(IPGEOLOCATION_COVERAGE_MIN_BRANCH "90.0")
set(IPGEOLOCATION_COVERAGE_MIN_FUNCTION "90.0")

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

set(IPGEOLOCATION_INSTALL_CMAKEDIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/ipgeolocation"
    CACHE STRING "Install path for the IPGeolocation CMake package files")

if (IPGEOLOCATION_ENABLE_COVERAGE)
    if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "IPGEOLOCATION_ENABLE_COVERAGE currently requires Clang")
    endif ()

    add_compile_options(-O0 -g -fprofile-instr-generate -fcoverage-mapping)
    add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif ()

find_package(CURL REQUIRED)

set(IPGEOLOCATION_LIBRARY_SOURCES
    src/client.cpp
    src/curl_http_transport.cpp
    src/internal.cpp
    src/json.cpp
)

add_library(ipgeolocation_release_obj OBJECT ${IPGEOLOCATION_LIBRARY_SOURCES})
target_compile_features(ipgeolocation_release_obj PRIVATE cxx_std_17)
target_compile_definitions(ipgeolocation_release_obj PRIVATE IPGEOLOCATION_BUILDING_LIBRARY=1)
target_include_directories(ipgeolocation_release_obj
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ipgeolocation_release_obj PRIVATE CURL::libcurl)
set_target_properties(ipgeolocation_release_obj PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

set(IPGEOLOCATION_COMBINED_OBJECT
    ${CMAKE_CURRENT_BINARY_DIR}/ipgeolocation_combined.o)

add_custom_command(
    OUTPUT ${IPGEOLOCATION_COMBINED_OBJECT}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${IPGEOLOCATION_COMBINED_OBJECT}
    COMMAND ${CMAKE_CXX_COMPILER} -nostdlib -r
            $<TARGET_OBJECTS:ipgeolocation_release_obj>
            -o ${IPGEOLOCATION_COMBINED_OBJECT}
    DEPENDS ipgeolocation_release_obj
    COMMAND_EXPAND_LISTS
    VERBATIM
    COMMENT "Combining the IPGeolocation C++ SDK objects into a single release archive object"
)

set_source_files_properties(${IPGEOLOCATION_COMBINED_OBJECT}
    PROPERTIES
        GENERATED TRUE
        EXTERNAL_OBJECT TRUE
)

add_library(ipgeolocation STATIC ${IPGEOLOCATION_COMBINED_OBJECT})
target_compile_features(ipgeolocation PUBLIC cxx_std_17)
add_library(ipgeolocation::ipgeolocation ALIAS ipgeolocation)
target_include_directories(ipgeolocation
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(ipgeolocation PUBLIC CURL::libcurl)
set_target_properties(ipgeolocation PROPERTIES
    EXPORT_NAME ipgeolocation
    LINKER_LANGUAGE CXX
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/ipgeolocationConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/ipgeolocationConfig.cmake
    INSTALL_DESTINATION ${IPGEOLOCATION_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/ipgeolocationConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS ipgeolocation
    EXPORT ipgeolocationTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/ipgeolocation
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT ipgeolocationTargets
    FILE ipgeolocationTargets.cmake
    NAMESPACE ipgeolocation::
    DESTINATION ${IPGEOLOCATION_INSTALL_CMAKEDIR}
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ipgeolocationConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/ipgeolocationConfigVersion.cmake
    DESTINATION ${IPGEOLOCATION_INSTALL_CMAKEDIR}
)

if (IPGEOLOCATION_BUILD_TESTS)
    add_library(ipgeolocation_test_support STATIC
        ${IPGEOLOCATION_LIBRARY_SOURCES}
        src/client_test_access.cpp
    )
    target_compile_features(ipgeolocation_test_support PRIVATE cxx_std_17)
    target_compile_definitions(ipgeolocation_test_support
        PUBLIC
            IPGEOLOCATION_ENABLE_TESTING_ACCESS=1
        PRIVATE
            IPGEOLOCATION_BUILDING_LIBRARY=1
    )
    target_include_directories(ipgeolocation_test_support
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
    )
    target_link_libraries(ipgeolocation_test_support PUBLIC CURL::libcurl)

    add_executable(ipgeolocation_tests
        tests/cpp_sdk_tests.cpp
    )
    target_compile_features(ipgeolocation_tests PRIVATE cxx_std_17)
    target_include_directories(ipgeolocation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ipgeolocation_tests PRIVATE ipgeolocation_test_support)
    add_test(NAME ipgeolocation_tests COMMAND ipgeolocation_tests)

    add_executable(ipgeolocation_live_integration_tests
        tests/live_integration_tests.cpp
    )
    target_compile_features(ipgeolocation_live_integration_tests PRIVATE cxx_std_17)
    target_include_directories(ipgeolocation_live_integration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ipgeolocation_live_integration_tests PRIVATE ipgeolocation)
    add_test(NAME ipgeolocation_live_integration_tests COMMAND ipgeolocation_live_integration_tests)
    set_tests_properties(ipgeolocation_live_integration_tests PROPERTIES
        LABELS "live"
        SKIP_RETURN_CODE 77
    )

    add_executable(ipgeolocation_live_field_parity_tests
        tests/live_field_parity_tests.cpp
    )
    target_compile_features(ipgeolocation_live_field_parity_tests PRIVATE cxx_std_17)
    target_include_directories(ipgeolocation_live_field_parity_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ipgeolocation_live_field_parity_tests PRIVATE ipgeolocation_test_support)
    add_test(NAME ipgeolocation_live_field_parity_tests COMMAND ipgeolocation_live_field_parity_tests)
    set_tests_properties(ipgeolocation_live_field_parity_tests PROPERTIES
        LABELS "live"
        SKIP_RETURN_CODE 77
    )

    if (IPGEOLOCATION_ENABLE_COVERAGE)
        if (APPLE)
            find_program(IPGEOLOCATION_XCRUN_EXECUTABLE xcrun REQUIRED)
        else ()
            find_program(IPGEOLOCATION_LLVM_PROFDATA_EXECUTABLE llvm-profdata REQUIRED)
            find_program(IPGEOLOCATION_LLVM_COV_EXECUTABLE llvm-cov REQUIRED)
        endif ()

        set(IPGEOLOCATION_COVERAGE_GATE_SCRIPT
            ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_coverage.cmake)
        set(IPGEOLOCATION_COVERAGE_PROFILE_RAW
            ${CMAKE_CURRENT_BINARY_DIR}/coverage/ipgeolocation-tests.profraw)
        set(IPGEOLOCATION_COVERAGE_PROFILE_DATA
            ${CMAKE_CURRENT_BINARY_DIR}/coverage/ipgeolocation-tests.profdata)
        set(IPGEOLOCATION_COVERAGE_REPORT_FILE
            ${CMAKE_CURRENT_BINARY_DIR}/coverage/ipgeolocation-tests-report.txt)
        set(IPGEOLOCATION_COVERAGE_IGNORE_REGEX
            "(tests/|/Library/Developer/|/usr/include|/opt/homebrew|/Applications/Xcode)")

        set(IPGEOLOCATION_COVERAGE_CHECK_COMMAND
            ${CMAKE_COMMAND}
            -DIPGEOLOCATION_TEST_BINARY=$<TARGET_FILE:ipgeolocation_tests>
            -DIPGEOLOCATION_PROFILE_RAW=${IPGEOLOCATION_COVERAGE_PROFILE_RAW}
            -DIPGEOLOCATION_PROFILE_DATA=${IPGEOLOCATION_COVERAGE_PROFILE_DATA}
            -DIPGEOLOCATION_REPORT_FILE=${IPGEOLOCATION_COVERAGE_REPORT_FILE}
            -DIPGEOLOCATION_IGNORE_FILENAME_REGEX=${IPGEOLOCATION_COVERAGE_IGNORE_REGEX}
            -DIPGEOLOCATION_MIN_LINE=${IPGEOLOCATION_COVERAGE_MIN_LINE}
            -DIPGEOLOCATION_MIN_BRANCH=${IPGEOLOCATION_COVERAGE_MIN_BRANCH}
            -DIPGEOLOCATION_MIN_FUNCTION=${IPGEOLOCATION_COVERAGE_MIN_FUNCTION}
            -DIPGEOLOCATION_XCRUN_EXECUTABLE=${IPGEOLOCATION_XCRUN_EXECUTABLE}
            -DIPGEOLOCATION_LLVM_PROFDATA_EXECUTABLE=${IPGEOLOCATION_LLVM_PROFDATA_EXECUTABLE}
            -DIPGEOLOCATION_LLVM_COV_EXECUTABLE=${IPGEOLOCATION_LLVM_COV_EXECUTABLE}
            -P ${IPGEOLOCATION_COVERAGE_GATE_SCRIPT}
        )

        add_custom_target(ipgeolocation_coverage_check
            COMMAND ${IPGEOLOCATION_COVERAGE_CHECK_COMMAND}
            DEPENDS ipgeolocation_tests
            USES_TERMINAL
            VERBATIM
            COMMENT "Running the IPGeolocation C++ SDK coverage gate"
        )

        add_test(NAME ipgeolocation_coverage_check COMMAND ${IPGEOLOCATION_COVERAGE_CHECK_COMMAND})
        set_tests_properties(ipgeolocation_coverage_check PROPERTIES LABELS "coverage")
    endif ()
endif ()
