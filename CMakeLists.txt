cmake_minimum_required(VERSION 3.16)
project(PGM_Gantry_Frontend VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Network
    Charts
)

set(SOURCES
    main.cpp
    gantry_client.cpp
    main_window.cpp
    login_dialog.cpp
    credential_store.cpp
)

set(HEADERS
    gantry_data.h
    gantry_client.h
    gauge_widget.h
    main_window.h
    login_dialog.h
    credential_store.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
    Qt6::Charts
)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# Windows 下隐藏控制台窗口（发布时）
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE $<CONFIG:Release>
    )
    target_link_libraries(${PROJECT_NAME} PRIVATE crypt32)
endif()
