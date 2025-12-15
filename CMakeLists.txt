cmake_minimum_required(VERSION 3.19)
project(NeteaseWebPlayer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find Qt modules
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets WebEngineWidgets)

# Optional X11/XTest for fallback key injection
find_package(X11)

qt_standard_project_setup()

qt_add_executable(NeteaseWebPlayer
        main.cpp
)

set_target_properties(NeteaseWebPlayer PROPERTIES
        OUTPUT_NAME "netease-webplayer"
        AUTOMOC ON
)

target_link_libraries(NeteaseWebPlayer PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::WebEngineWidgets
)

# If X11 found, link X11 and Xtst for XTestFakeKeyEvent fallback
if (X11_FOUND)
    find_library(XTEST_LIB Xtst)
    if (XTEST_LIB)
        target_link_libraries(NeteaseWebPlayer PRIVATE ${X11_LIBRARIES} ${XTEST_LIB})
    else()
        target_link_libraries(NeteaseWebPlayer PRIVATE ${X11_LIBRARIES})
    endif()
endif()

# On some systems Qt WebEngine needs additional flags; keep default behavior otherwise
