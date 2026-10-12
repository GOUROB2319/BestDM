cmake_minimum_required(VERSION 3.21)
project(BestDM VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick Network Sql)
qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(BestDM
    BestDM/main.cpp
    BestDM/downloadmanager.cpp
    BestDM/downloadmanager.h
    BestDM/domain.h
    BestDM/persistencerepository.cpp
    BestDM/persistencerepository.h
    BestDM/downloadtask.cpp
    BestDM/downloadtask.h
    BestDM/downloadqueue.cpp
    BestDM/downloadqueue.h
)
qt_add_resources(BestDM bestdm_qml
    PREFIX "/qt/qml/bestdm"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/BestDM"
    FILES BestDM/main.qml
)
qt_add_resources(BestDM bestdm_assets
    PREFIX "/assets"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/BestDM/assets"
    FILES "BestDM/assets/Glossy 3D Download App Icon.png"
)
target_link_libraries(BestDM PRIVATE Qt6::Quick Qt6::Network Qt6::Sql)
set_target_properties(BestDM PROPERTIES WIN32_EXECUTABLE TRUE)

if(WIN32)
    get_target_property(BESTDM_QMAKE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(BESTDM_QT_BIN "${BESTDM_QMAKE}" DIRECTORY)
    find_program(BESTDM_WINDEPLOYQT windeployqt HINTS "${BESTDM_QT_BIN}" REQUIRED)
    add_custom_command(TARGET BestDM POST_BUILD
        COMMAND "${BESTDM_WINDEPLOYQT}" --dir "$<TARGET_FILE_DIR:BestDM>"
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/BestDM" --no-translations
                --no-system-d3d-compiler --no-opengl-sw "$<TARGET_FILE:BestDM>"
        COMMENT "Deploying required Qt runtime libraries and QML imports for BestDM")
endif()
