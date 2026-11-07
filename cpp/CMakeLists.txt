cmake_minimum_required(VERSION 3.24)

project(ml_odonto_cpp VERSION 0.1.0 LANGUAGES CXX)

option(ML_ODONTO_BUILD_TESTS "Build the C++ unit tests" ON)
option(ML_ODONTO_ENABLE_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(ML_ODONTO_WITH_ONNX "Build the ONNX Runtime inference adapter" OFF)

add_library(ml_odonto_core
    src/detection.cpp
    src/image.cpp
    src/io.cpp
    src/pipeline.cpp
    src/preprocessing.cpp
    src/stb_impl.cpp
)
add_library(ml_odonto::core ALIAS ml_odonto_core)

include(GNUInstallDirs)

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
# stb (domínio público) vem junto em third_party; os avisos dele não são nossos
target_include_directories(ml_odonto_core SYSTEM PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb")
if(MSVC)
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "/W0")
else()
    set_source_files_properties(src/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")
endif()
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
if(WIN32)
    target_link_libraries(ml-odonto-cli PRIVATE shell32)  # CommandLineToArgvW
endif()
if(ML_ODONTO_WITH_ONNX AND WIN32)
    # o executável precisa das DLLs do ONNX Runtime ao lado dele
    file(GLOB ONNXRUNTIME_DLLS "${ONNXRUNTIME_ROOT}/lib/*.dll")
    add_custom_command(TARGET ml-odonto-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${ONNXRUNTIME_DLLS} $<TARGET_FILE_DIR:ml-odonto-cli>)
    install(FILES ${ONNXRUNTIME_DLLS} DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

install(TARGETS ml_odonto_core ml-odonto-cli
    EXPORT ml_odontoTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Programa de janela própria (Windows): abre o aplicativo numa janela WebView2, sem navegador.
# Precisa da biblioteca webview (WEBVIEW_INCLUDE, com webview/webview.h) e do SDK do WebView2
# (WEBVIEW2_INCLUDE, com WebView2.h); o tools/build.py baixa os dois para cpp/.deps.
option(ML_ODONTO_BUILD_APP "Build the ML ODONTO desktop window (Windows)" OFF)
if(ML_ODONTO_BUILD_APP)
    if(NOT WIN32)
        message(FATAL_ERROR "ML_ODONTO_BUILD_APP is Windows-only")
    endif()
    if(NOT WEBVIEW_INCLUDE OR NOT WEBVIEW2_INCLUDE)
        message(FATAL_ERROR "Set WEBVIEW_INCLUDE and WEBVIEW2_INCLUDE")
    endif()
    enable_language(RC)
    add_executable(ml-odonto-app WIN32 app/ml_odonto_app.cpp app/ml_odonto.rc)
    set_target_properties(ml-odonto-app PROPERTIES OUTPUT_NAME "ML ODONTO")
    target_compile_features(ml-odonto-app PRIVATE cxx_std_20)
    target_include_directories(ml-odonto-app SYSTEM PRIVATE "${WEBVIEW_INCLUDE}" "${WEBVIEW2_INCLUDE}")
    if(NOT MSVC)
        target_include_directories(ml-odonto-app SYSTEM PRIVATE app/compat)  # EventToken.h
    endif()
    target_compile_definitions(ml-odonto-app PRIVATE UNICODE _UNICODE WEBVIEW_STATIC)
    target_link_libraries(ml-odonto-app PRIVATE ws2_32 advapi32 ole32 shell32 shlwapi user32 version)
    install(TARGETS ml-odonto-app RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

if(ML_ODONTO_BUILD_TESTS)
    enable_testing()
    add_executable(ml_odonto_core_tests tests/core_tests.cpp)
    target_link_libraries(ml_odonto_core_tests PRIVATE ml_odonto::core)
    add_test(NAME ml_odonto_core_tests COMMAND ml_odonto_core_tests)
endif()

# ML ODONTO nativo em Qt 6 (migração das telas do app R). Precisa do Qt (CMAKE_PREFIX_PATH).
option(ML_ODONTO_BUILD_DESKTOP "Build the native Qt desktop program" OFF)
if(ML_ODONTO_BUILD_DESKTOP)
    add_subdirectory(desktop)
endif()
