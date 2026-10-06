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
