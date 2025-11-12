cmake_minimum_required(VERSION 3.24)
project(BBBuilder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 COMPONENTS Widgets Xml REQUIRED)

set(BBBuilder_HEADERS
    include/BBBuilder/Project.h
    include/BBBuilder/SigmaParser.h
    include/BBBuilder/BundleWriter.h
    include/BBBuilder/ModuleCatalog.h
    src/ui/MainWindow.h
)

set(BBBuilder_SOURCES
    src/main.cpp
    src/core/Project.cpp
    src/core/SigmaParser.cpp
    src/core/BundleWriter.cpp
    src/core/ModuleCatalog.cpp
    src/ui/MainWindow.cpp
)

qt_add_executable(BBBuilder
    MANUAL_FINALIZATION
    ${BBBuilder_HEADERS}
    ${BBBuilder_SOURCES}
)

target_include_directories(BBBuilder PRIVATE include)

target_link_libraries(BBBuilder PRIVATE Qt6::Widgets Qt6::Xml)

install(TARGETS BBBuilder RUNTIME DESTINATION bin)

qt_finalize_executable(BBBuilder)
