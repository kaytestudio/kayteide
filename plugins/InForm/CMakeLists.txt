# ─── KayteInForm static library ──────────────────────────────────────────────
# Qt6 C++ port of InForm (GUI engine + form designer for QB64,
# https://github.com/FellippeHeitor/InForm). The original QB64 sources stay in
# InForm/ for reference; this library replaces them inside KayteIDE:
#   - InFormDocument - reads / writes InForm .frm files (SUB __UI_LoadForm)
#   - InFormRuntime  - builds live Qt widgets from a form, raises InForm events
#   - InFormPreview  - runs a form next to an event log (UiEditorPreview)
#   - InFormConvert  - VB-DOS form -> InForm (vbdos2inform) + .bas event skeleton
#   - InFormKayte    - Kayte lang: .kfm forms + .kayte / .kjs / .bas programs
#   - InFormCobol    - RAD for COBOL: a GnuCOBOL SCREEN SECTION program
#   - InFormWeb      - web apps: a .kjs program + its form as an HTML page,
#                      run by kayte-web.js (web/) - the browser's qt("...")
# The IDE embeds it in its widget palette, Components dock and UI designer.

add_library(KayteInForm STATIC
        src/InFormTypes.cpp
        src/InFormDocument.cpp
        src/InFormRuntime.cpp
        src/InFormPreview.cpp
        src/InFormConvert.cpp
        src/InFormKayte.cpp
        src/InFormCobol.cpp
        src/InFormWeb.cpp
        include/InFormTypes.hpp
        include/InFormDocument.hpp
        include/InFormRuntime.hpp
        include/InFormPreview.hpp
        include/InFormConvert.hpp
        include/InFormKayte.hpp
        include/InFormCobol.hpp
        include/InFormWeb.hpp
)

set_target_properties(KayteInForm PROPERTIES AUTOMOC ON)

# The browser runtime copied into every exported web app.
qt_add_resources(KayteInForm "kayte_web"
        PREFIX "/kayte-web"
        BASE   web
        FILES  web/kayte-web.js web/kayte-web.css
)

target_include_directories(KayteInForm
        PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(KayteInForm
        PUBLIC  Qt6::Core Qt6::Gui Qt6::Widgets
)
