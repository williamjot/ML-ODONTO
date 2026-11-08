# ML ODONTO nativo (Qt 6): o programa completo em C++, sem R. Usa o mesmo banco SQLite do app R,
# para os dois conviverem durante a migração. Só módulos LGPL do Qt (Core, Gui, Widgets, Sql).
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Sql)
qt_standard_project_setup()

set(APP_R "${CMAKE_CURRENT_SOURCE_DIR}/../../software/odontolesao/inst")

# Lógica sem interface (banco, métricas, contornos): usada pelo programa e pelos testes
qt_add_library(ml_odonto_logic STATIC
    src/Database.cpp
    src/Metrics.cpp
    src/Contours.cpp
)
target_include_directories(ml_odonto_logic PUBLIC src)
target_link_libraries(ml_odonto_logic PUBLIC ml_odonto::core Qt6::Core Qt6::Gui Qt6::Sql)
# Esquema do banco e registro dos modelos: os mesmos arquivos do pacote R (fonte única)
file(GLOB SQL_FILES "${APP_R}/sql/*.sql")
qt_add_resources(ml_odonto_logic "esquema"
    PREFIX "/"
    BASE "${APP_R}"
    FILES ${SQL_FILES} "${APP_R}/modelos/modelos.yml")

qt_add_executable(ml-odonto-desktop WIN32
    src/main.cpp
    src/Context.h
    src/Icons.h
    src/pages/Pages.h
    src/Charts.cpp
    src/Pane.cpp
    src/ImageViewer.cpp
    src/MainWindow.cpp
    src/pages/InicioPage.cpp
    src/pages/RevisaoPage.cpp
    src/pages/AuditoriaPage.cpp
    src/pages/ResultadosPage.cpp
    src/pages/AplicarPage.cpp
    src/pages/AjudaPage.cpp
    src/pages/ConfigPage.cpp
)
set_target_properties(ml-odonto-desktop PROPERTIES OUTPUT_NAME "ML ODONTO")
target_include_directories(ml-odonto-desktop PRIVATE src)

# Ícone, manifesto e versão. No MinGW o windres não aceita espaços nos caminhos de include, e o .rc
# não precisa de nenhum: compila dentro de app/ só com caminhos relativos.
if(MINGW)
    set(APP_RES "${CMAKE_CURRENT_BINARY_DIR}/ml_odonto_res.obj")
    add_custom_command(OUTPUT "${APP_RES}"
        COMMAND "${CMAKE_RC_COMPILER}" -O coff -i ml_odonto.rc -o "${APP_RES}"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../app"
        DEPENDS ../app/ml_odonto.rc ../app/ml_odonto.ico ../app/ml_odonto.manifest
        VERBATIM)
    target_sources(ml-odonto-desktop PRIVATE "${APP_RES}")
else()
    target_sources(ml-odonto-desktop PRIVATE ../app/ml_odonto.rc)
endif()
target_link_libraries(ml-odonto-desktop PRIVATE ml_odonto_logic Qt6::Widgets)

# Manual interativo num WebView2 embutido (biblioteca webview + SDK do WebView2; o build_desktop.py baixa)
if(WIN32 AND WEBVIEW_INCLUDE AND WEBVIEW2_INCLUDE)
    target_include_directories(ml-odonto-desktop SYSTEM PRIVATE "${WEBVIEW_INCLUDE}" "${WEBVIEW2_INCLUDE}")
    if(MINGW)
        target_include_directories(ml-odonto-desktop SYSTEM PRIVATE ../app/compat)  # EventToken.h
    endif()
    target_compile_definitions(ml-odonto-desktop PRIVATE ML_ODONTO_HAS_WEBVIEW WEBVIEW_STATIC)
    target_link_libraries(ml-odonto-desktop PRIVATE advapi32 ole32 shell32 shlwapi user32 version)
endif()

# Manual: o mesmo do pacote R
qt_add_resources(ml-odonto-desktop "manual"
    PREFIX "/"
    BASE "${APP_R}"
    FILES "${APP_R}/manuais/manual_dentista.md" "${APP_R}/manuais/manual_dentista.html")
qt_add_resources(ml-odonto-desktop "icone"
    PREFIX "/"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/../app"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/../app/ml_odonto.ico")

if(ML_ODONTO_BUILD_TESTS)
    qt_add_executable(ml_odonto_desktop_tests tests/desktop_tests.cpp)
    target_link_libraries(ml_odonto_desktop_tests PRIVATE ml_odonto_logic)
    add_test(NAME ml_odonto_desktop_tests COMMAND ml_odonto_desktop_tests)
    # o Qt (DLLs e o driver SQLite) vem da instalação usada na compilação
    set_tests_properties(ml_odonto_desktop_tests PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()
