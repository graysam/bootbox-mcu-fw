cmake_minimum_required(VERSION 3.24)
project(BBBuilder VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenGL REQUIRED)
find_package(PkgConfig)
pkg_check_modules(GLFW REQUIRED glfw3)
find_package(Qt6 6.4 COMPONENTS Core Xml REQUIRED)

set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/imgui)
set(TINYFD_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tinyfiledialogs)
file(GLOB IMGUI_SOURCES
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
)

set(BB_CORE_HEADERS
    include/BBBuilder/Project.h
    include/BBBuilder/SigmaParser.h
    include/BBBuilder/BundleWriter.h
    include/BBBuilder/ModuleCatalog.h
)

set(BB_CORE_SOURCES
    src/core/Project.cpp
    src/core/SigmaParser.cpp
    src/core/BundleWriter.cpp
    src/core/ModuleCatalog.cpp
)

set(BB_APP_SOURCES
    src/imgui_main.cpp
    src/imgui_model.cpp
    src/imgui_canvas.cpp
)

add_executable(BBBuilder
    ${BB_CORE_HEADERS}
    ${BB_CORE_SOURCES}
    ${BB_APP_SOURCES}
    ${IMGUI_SOURCES}
    ${TINYFD_DIR}/tinyfiledialogs.c
)

target_include_directories(BBBuilder PRIVATE include)
target_include_directories(BBBuilder PRIVATE ${IMGUI_DIR} ${IMGUI_DIR}/backends ${GLFW_INCLUDE_DIRS} ${TINYFD_DIR})

target_link_libraries(BBBuilder PRIVATE OpenGL::GL ${GLFW_LIBRARIES} Qt6::Core Qt6::Xml)

install(TARGETS BBBuilder RUNTIME DESTINATION bin)
