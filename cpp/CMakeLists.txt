cmake_minimum_required(VERSION 3.24)

project(ml_odonto_cpp VERSION 0.1.0 LANGUAGES CXX)

option(ML_ODONTO_BUILD_TESTS "Build the C++ unit tests" ON)
option(ML_ODONTO_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ML_ODONTO_WITH_ONNX "Build the ONNX Runtime inference adapter" OFF)

add_library(ml_odonto_core
    src/detection.cpp
    src/image.cpp
    src/io.cpp
    src/preprocessing.cpp
)
add_library(ml_odonto::core ALIAS ml_odonto_core)

if(ML_ODONTO_WITH_ONNX)
    if(NOT ONNXRUNTIME_ROOT)
        message(FATAL_ERROR "Set ONNXRUNTIME_ROOT to an extracted ONNX Runtime package")
    endif()
    find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h
        PATHS "${ONNXRUNTIME_ROOT}/include" NO_DEFAULT_PATH REQUIRED)
    find_library(ONNXRUNTIME_LIBRARY onnxruntime
        PATHS "${ONNXRUNTIME_ROOT}/lib" NO_DEFAULT_PATH REQUIRED)
    target_sources(ml_odonto_core PRIVATE src/onnx_engine.cpp)
    target_include_directories(ml_odonto_core SYSTEM PRIVATE "${ONNXRUNTIME_INCLUDE_DIR}")
    target_link_libraries(ml_odonto_core PUBLIC "${ONNXRUNTIME_LIBRARY}")
    target_compile_definitions(ml_odonto_core PUBLIC ML_ODONTO_WITH_ONNX=1)
endif()

target_compile_features(ml_odonto_core PUBLIC cxx_std_20)
target_include_directories(ml_odonto_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(MSVC)
    target_compile_options(ml_odonto_core PRIVATE /W4 /permissive- /utf-8)
    if(ML_ODONTO_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(ml_odonto_core PRIVATE /WX)
    endif()
else()
    target_compile_options(ml_odonto_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    if(ML_ODONTO_ENABLE_WARNINGS_AS_ERRORS)
        target_compile_options(ml_odonto_core PRIVATE -Werror)
    endif()
endif()

add_executable(ml-odonto-cli src/main.cpp)
target_link_libraries(ml-odonto-cli PRIVATE ml_odonto::core)

include(GNUInstallDirs)
install(TARGETS ml_odonto_core ml-odonto-cli
    EXPORT ml_odontoTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

if(ML_ODONTO_BUILD_TESTS)
    enable_testing()
    add_executable(ml_odonto_core_tests tests/core_tests.cpp)
    target_link_libraries(ml_odonto_core_tests PRIVATE ml_odonto::core)
    add_test(NAME ml_odonto_core_tests COMMAND ml_odonto_core_tests)
endif()
