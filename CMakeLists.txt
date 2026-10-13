cmake_minimum_required(VERSION 3.21)
project(BestDM VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.12 REQUIRED COMPONENTS Quick Qml Network Sql Widgets QuickControls2 QuickDialogs2)
qt_standard_project_setup(REQUIRES 6.12)

qt_add_executable(BestDM
    BestDM/main.cpp
    BestDM/applicationprotocol.cpp
    BestDM/applicationprotocol.h
    BestDM/singleinstancecoordinator.cpp
    BestDM/singleinstancecoordinator.h
    BestDM/downloadmanager.cpp
    BestDM/downloadmanager.h
    BestDM/categoryrules.cpp
    BestDM/categoryrules.h
    BestDM/securecredentialstore.cpp
    BestDM/securecredentialstore.h
    BestDM/traycontroller.cpp
    BestDM/traycontroller.h
    BestDM/schedulewindow.cpp
    BestDM/schedulewindow.h
    BestDM/downloadlistproxy.cpp
    BestDM/downloadlistproxy.h
    BestDM/diagnosticlogger.cpp
    BestDM/diagnosticlogger.h
    BestDM/bandwidthlimiter.cpp
    BestDM/bandwidthlimiter.h
    BestDM/domain.h
    BestDM/persistencerepository.cpp
    BestDM/persistencerepository.h
    BestDM/downloadtask.cpp
    BestDM/downloadtask.h
    BestDM/safefile.h
    BestDM/segmenteddownloader.cpp
    BestDM/segmenteddownloader.h
    BestDM/downloadqueue.cpp
    BestDM/downloadqueue.h
)
set_source_files_properties(BestDM/main.qml PROPERTIES QT_RESOURCE_ALIAS "Main.qml")
qt_add_qml_module(BestDM
    URI BestDM.App
    VERSION 1.0
    QML_FILES BestDM/main.qml
)
set_source_files_properties("BestDM/assets/Glossy 3D Download App Icon.png"
    PROPERTIES QT_RESOURCE_ALIAS "Glossy 3D Download App Icon.png")
qt_add_resources(BestDM bestdm_assets
    PREFIX "/assets"
    FILES "BestDM/assets/Glossy 3D Download App Icon.png"
)
target_link_libraries(BestDM PRIVATE Qt6::Quick Qt6::Qml Qt6::Network Qt6::Sql Qt6::Widgets
    Qt6::QuickControls2 Qt6::QuickDialogs2)
if(WIN32)
    target_link_libraries(BestDM PRIVATE advapi32)
endif()
set_target_properties(BestDM PROPERTIES WIN32_EXECUTABLE TRUE)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Test)
    qt_add_executable(BestDMTests
        tests/test_domain_persistence.cpp
        BestDM/persistencerepository.cpp
        BestDM/persistencerepository.h
        BestDM/domain.h
    )
    target_link_libraries(BestDMTests PRIVATE Qt6::Core Qt6::Sql Qt6::Test)
    add_test(NAME BestDM.DomainAndPersistence COMMAND BestDMTests)

    qt_add_executable(BestDMProxyTests
        tests/test_download_list_proxy.cpp
        BestDM/downloadlistproxy.cpp
        BestDM/downloadlistproxy.h
    )
    target_link_libraries(BestDMProxyTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BestDM.DownloadListProxy COMMAND BestDMProxyTests)

    qt_add_executable(BestDMNetworkTests
        tests/test_download_task.cpp
        BestDM/downloadmanager.cpp
        BestDM/downloadmanager.h
        BestDM/applicationprotocol.cpp
        BestDM/applicationprotocol.h
        BestDM/categoryrules.cpp
        BestDM/categoryrules.h
        BestDM/persistencerepository.cpp
        BestDM/persistencerepository.h
        BestDM/downloadtask.cpp
        BestDM/downloadtask.h
        BestDM/safefile.h
        BestDM/segmenteddownloader.cpp
        BestDM/segmenteddownloader.h
        BestDM/bandwidthlimiter.cpp
        BestDM/bandwidthlimiter.h
        BestDM/downloadqueue.cpp
        BestDM/downloadqueue.h
        BestDM/securecredentialstore.cpp
        BestDM/securecredentialstore.h
        BestDM/traycontroller.cpp
        BestDM/traycontroller.h
        BestDM/schedulewindow.cpp
        BestDM/schedulewindow.h
        BestDM/domain.h
    )
    target_link_libraries(BestDMNetworkTests PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Sql Qt6::Widgets Qt6::Test)
    if(WIN32)
        target_link_libraries(BestDMNetworkTests PRIVATE advapi32)
    endif()
    add_test(NAME BestDM.NetworkAndQueue COMMAND BestDMNetworkTests)

    qt_add_executable(BestDMBandwidthTests
        tests/test_bandwidth_limiter.cpp
        BestDM/bandwidthlimiter.cpp
        BestDM/bandwidthlimiter.h
    )
    target_link_libraries(BestDMBandwidthTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BestDM.BandwidthLimiter COMMAND BestDMBandwidthTests)

    qt_add_executable(BestDMCategoryTests
        tests/test_category_rules.cpp
        BestDM/categoryrules.cpp
        BestDM/categoryrules.h
    )
    target_link_libraries(BestDMCategoryTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BestDM.CategoryRules COMMAND BestDMCategoryTests)

    qt_add_executable(BestDMScheduleTests
        tests/test_schedule_window.cpp
        BestDM/schedulewindow.cpp
        BestDM/schedulewindow.h
    )
    target_link_libraries(BestDMScheduleTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BestDM.ScheduleWindow COMMAND BestDMScheduleTests)

    qt_add_executable(BestDMDiagnosticTests
        tests/test_diagnostic_logger.cpp
        BestDM/diagnosticlogger.cpp
        BestDM/diagnosticlogger.h
        BestDM/safefile.h
    )
    target_link_libraries(BestDMDiagnosticTests PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME BestDM.DiagnosticLogger COMMAND BestDMDiagnosticTests)
endif()

foreach(BESTDM_TARGET BestDM BestDMTests BestDMProxyTests BestDMNetworkTests BestDMBandwidthTests BestDMCategoryTests BestDMScheduleTests BestDMDiagnosticTests)
    if(TARGET ${BESTDM_TARGET})
        if(MSVC)
            target_compile_options(${BESTDM_TARGET} PRIVATE /W4 /permissive-)
        else()
            target_compile_options(${BESTDM_TARGET} PRIVATE -Wall -Wextra -Wpedantic)
        endif()
    endif()
endforeach()

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
