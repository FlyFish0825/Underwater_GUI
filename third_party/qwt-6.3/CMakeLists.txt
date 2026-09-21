# Qwt 6.3.x is an upstream source distribution. Keep COPYING and the upstream
# source layout unchanged; this wrapper only selects the QWidget plot subset
# needed by the host application.
set(QWT_SRC_DIR "${CMAKE_CURRENT_LIST_DIR}/src")
file(GLOB QWT_SOURCES CONFIGURE_DEPENDS "${QWT_SRC_DIR}/*.cpp")

# The locked Qt 5.15 SDK used by this project has no QtSvg/QtOpenGL package.
# Plot, picker, zoomer, direct painter and the regular QWidget canvas remain
# available; optional SVG, OpenGL canvas and polar sources are not required.
list(FILTER QWT_SOURCES EXCLUDE REGEX "/qwt_plot_opengl_canvas\\.cpp$")
list(FILTER QWT_SOURCES EXCLUDE REGEX "/qwt_plot_glcanvas\\.cpp$")
list(FILTER QWT_SOURCES EXCLUDE REGEX "/qwt_plot_svgitem\\.cpp$")
list(FILTER QWT_SOURCES EXCLUDE REGEX "/qwt_polar_.*\\.cpp$")

add_library(rov_third_party_qwt STATIC ${QWT_SOURCES})
target_include_directories(rov_third_party_qwt
    PUBLIC
        "${QWT_SRC_DIR}"
)
target_compile_definitions(rov_third_party_qwt
    PUBLIC
        QWT_NO_SVG
)
target_link_libraries(rov_third_party_qwt
    PUBLIC
        Qt5::Core
        Qt5::Gui
        Qt5::Widgets
        Qt5::Concurrent
        Qt5::PrintSupport
)
set_target_properties(rov_third_party_qwt PROPERTIES
    AUTOMOC ON
    AUTOUIC OFF
    AUTORCC OFF
)
