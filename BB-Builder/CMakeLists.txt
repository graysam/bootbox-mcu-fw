cmake_minimum_required(VERSION 3.24)
project(BBBuilder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 6.4 COMPONENTS Widgets Xml WebEngineWidgets REQUIRED)

set(BBBuilder_HEADERS
    include/BBBuilder/Project.h
    include/BBBuilder/SigmaParser.h
    include/BBBuilder/BundleWriter.h
    include/BBBuilder/ModuleCatalog.h
    include/BBBuilder/Controls/ControlTile.h
    include/BBBuilder/Controls/ModuleCard.h
    include/BBBuilder/Controls/InspectorPanel.h
    include/BBBuilder/UI/DraggableTreeWidget.h
    include/BBBuilder/UI/PreviewPane.h
    src/ui/MainWindow.h
)

set(BBBuilder_SOURCES
    src/main.cpp
    src/core/Project.cpp
    src/core/SigmaParser.cpp
    src/core/BundleWriter.cpp
    src/core/ModuleCatalog.cpp
    src/ui/controls/ControlTile.cpp
    src/ui/controls/ModuleCard.cpp
    src/ui/controls/InspectorPanel.cpp
    src/ui/DraggableTreeWidget.cpp
    src/ui/PreviewPane.cpp
    src/ui/MainWindow.cpp
)

qt_add_executable(BBBuilder
    MANUAL_FINALIZATION
    ${BBBuilder_HEADERS}
    ${BBBuilder_SOURCES}
)

qt_add_resources(BBBuilder preview_resources
    PREFIX "/preview"
    FILES
        resources/preview/preview.html
        resources/preview/preview.css
        resources/preview/preview.js
)

target_include_directories(BBBuilder PRIVATE include)

target_link_libraries(BBBuilder PRIVATE Qt6::Widgets Qt6::Xml Qt6::WebEngineWidgets)

install(TARGETS BBBuilder RUNTIME DESTINATION bin)

qt_finalize_executable(BBBuilder)
