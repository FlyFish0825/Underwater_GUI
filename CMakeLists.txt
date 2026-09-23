cmake_minimum_required(VERSION 3.16)

project(rov_ui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport)

# Qwt 6.3.x supplies the reusable QWidget plotting primitives. Its wrapper
# disables optional modules unavailable in the locked Qt SDK (SVG/OpenGL).
add_subdirectory(third_party/qwt-6.3 EXCLUDE_FROM_ALL)

# Fluent-Qt 作为可选的第三方控件库接入：只构建库本身，不构建它的示例、画廊、
# 测试和 Python 绑定，避免把与上位机无关的目标带入现有工程。
set(FLUENT_QT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_GALLERY OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_PYSIDE6_BINDINGS OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_BUILD_WEBASSEMBLY_SUPPORT OFF CACHE BOOL "" FORCE)
set(FLUENT_QT_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(third_party/Fluent-Qt EXCLUDE_FROM_ALL)

add_library(rov_ui_common STATIC
    src/ui/common/AnimatedNavButton.cpp
    src/ui/common/AnimatedNavButton.h
    src/ui/common/AppCheckBox.cpp
    src/ui/common/AppCheckBox.h
    src/ui/common/AppComboBox.cpp
    src/ui/common/AppComboBox.h
    src/ui/common/AppFluentButton.cpp
    src/ui/common/AppFluentButton.h
    src/ui/common/AppInfoBar.cpp
    src/ui/common/AppInfoBar.h
    src/ui/common/AppLineEdit.cpp
    src/ui/common/AppLineEdit.h
    src/ui/common/AppProgressBar.cpp
    src/ui/common/AppProgressBar.h
    src/ui/common/AppProgressRing.cpp
    src/ui/common/AppProgressRing.h
    src/ui/common/AppSlider.cpp
    src/ui/common/AppSlider.h
    src/ui/common/AppToggleSwitch.cpp
    src/ui/common/AppToggleSwitch.h
    src/ui/common/UiPrimitives.cpp
    src/ui/common/UiPrimitives.h
)

add_library(rov_communication STATIC
    src/communication/bootloader/BootloaderDownloadController.cpp
    src/communication/bootloader/BootloaderDownloadController.h
    src/communication/bootloader/BootloaderFirmwareReader.cpp
    src/communication/bootloader/BootloaderFirmwareReader.h
    src/communication/bootloader/BootloaderUpgradeSequence.cpp
    src/communication/bootloader/BootloaderUpgradeSequence.h
    src/communication/bootloader/BootloaderProtocol.cpp
    src/communication/bootloader/BootloaderProtocol.h
    src/communication/bootloader/BootloaderService.cpp
    src/communication/bootloader/BootloaderService.h
    src/communication/bootloader/BootloaderTypes.h
    src/communication/protocol/CanGatewayConfigProtocol.cpp
    src/communication/protocol/CanGatewayConfigProtocol.h
    src/communication/protocol/CanGatewayProtocol.cpp
    src/communication/protocol/CanGatewayProtocol.h
    src/communication/protocol/CanFlowControlProtocol.cpp
    src/communication/protocol/CanFlowControlProtocol.h
    src/communication/protocol/ObserverMotorProtocol.cpp
    src/communication/protocol/ObserverMotorProtocol.h
    src/communication/protocol/SystemHeartbeatProtocol.cpp
    src/communication/protocol/SystemHeartbeatProtocol.h
    src/communication/service/BootloaderCommunicationService.cpp
    src/communication/service/BootloaderCommunicationService.h
    src/communication/transport/SerialTransport.cpp
    src/communication/transport/SerialTransport.h
)

add_library(rov_firmware_history STATIC
    src/data/store/FirmwareHistoryStore.cpp
    src/data/store/FirmwareHistoryStore.h
)

add_library(rov_firmware_history_ui STATIC
    src/ui/firmware_history/FirmwareHistoryDialog.cpp
    src/ui/firmware_history/FirmwareHistoryDialog.h
    src/ui/firmware_history/FirmwareLogFormatter.cpp
    src/ui/firmware_history/FirmwareLogFormatter.h
)

add_library(rov_motor_data STATIC
    src/contracts/motor/ObserverMotorContract.h
    src/data/services/ObserverMotorDataService.cpp
    src/data/services/ObserverMotorDataService.h
)

add_library(rov_camera STATIC
    src/data/services/CameraCaptureService.cpp
    src/data/services/CameraCaptureService.h
)

add_library(rov_research_recording STATIC
    src/data/recording/ResearchDataRecorder.cpp
    src/data/recording/ResearchDataRecorder.h
)

add_library(rov_page_dashboard STATIC
    src/pages/dashboard/DashboardPage.cpp
    src/pages/dashboard/DashboardPage.h
)

add_library(rov_page_motor_debug STATIC
    src/pages/motor_debug/MotorDebugPage.cpp
    src/pages/motor_debug/MotorDebugPage.h
    src/widgets/plot/QwtCurvePlotWidget.cpp
    src/widgets/plot/QwtCurvePlotWidget.h
)

add_library(rov_page_firmware STATIC
    src/pages/firmware/BootloaderCommandDialog.cpp
    src/pages/firmware/BootloaderCommandDialog.h
    src/pages/firmware/FirmwareLogRecordingDialog.cpp
    src/pages/firmware/FirmwareLogRecordingDialog.h
    src/pages/firmware/FirmwarePage.cpp
    src/pages/firmware/FirmwarePage.h
)

add_library(rov_page_manipulator STATIC
    src/pages/manipulator/ManipulatorPage.cpp
    src/pages/manipulator/ManipulatorPage.h
)

add_library(rov_page_vision STATIC
    src/pages/vision/VisionPage.cpp
    src/pages/vision/VisionPage.h
)

add_library(rov_page_settings STATIC
    src/pages/settings/SettingsPlaceholder.cpp
    src/pages/settings/SettingsPlaceholder.h
)

add_executable(rov_ui
    resources/resources.qrc
    resources/rov_ui.rc
    src/app/main.cpp
    src/app/MainWindow.cpp
    src/app/MainWindow.h

    src/contracts/common/ContractTypes.h
    src/contracts/dashboard/DashboardContract.h
    src/contracts/motor_debug/MotorDebugContract.h
    src/contracts/firmware/FirmwareContract.h
    src/contracts/manipulator/ManipulatorContract.h
    src/contracts/vision/VisionContract.h

)

set(ROV_UI_TARGETS
    rov_ui_common
    rov_communication
    rov_motor_data
    rov_camera
    rov_research_recording
    rov_firmware_history
    rov_firmware_history_ui
    rov_page_dashboard
    rov_page_motor_debug
    rov_page_firmware
    rov_page_manipulator
    rov_page_vision
    rov_page_settings
)

foreach(target IN LISTS ROV_UI_TARGETS)
    target_include_directories(${target} PUBLIC src)
    target_link_libraries(${target} PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
endforeach()

target_link_libraries(rov_page_firmware PUBLIC rov_communication)
target_link_libraries(rov_page_firmware PUBLIC rov_firmware_history)
target_link_libraries(rov_firmware_history_ui PUBLIC rov_firmware_history rov_ui_common)
target_link_libraries(rov_page_firmware PUBLIC rov_firmware_history_ui)
target_link_libraries(rov_motor_data PUBLIC rov_communication)
if (WIN32)
    target_link_libraries(rov_camera PRIVATE ole32 oleaut32 strmiids uuid)
endif()
target_link_libraries(rov_research_recording PUBLIC rov_communication)
target_link_libraries(rov_ui_common PUBLIC FluentQt::FluentQt)
target_link_libraries(rov_page_dashboard PUBLIC rov_ui_common)
target_link_libraries(rov_page_motor_debug PUBLIC rov_ui_common)
target_link_libraries(rov_page_motor_debug PUBLIC rov_third_party_qwt)
target_link_libraries(rov_page_firmware PUBLIC rov_ui_common)
target_link_libraries(rov_page_manipulator PUBLIC rov_ui_common)
target_link_libraries(rov_page_vision PUBLIC rov_ui_common)
target_link_libraries(rov_page_settings PUBLIC rov_ui_common)

target_include_directories(rov_ui PRIVATE src)

target_link_libraries(rov_ui PRIVATE
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    rov_page_dashboard
    rov_page_motor_debug
    rov_page_firmware
    rov_page_manipulator
    rov_page_vision
    rov_page_settings
    rov_ui_common
    rov_communication
    rov_motor_data
    rov_camera
    rov_research_recording
)

if (WIN32)
    target_link_libraries(rov_communication PRIVATE setupapi)
endif()


if (WIN32)
    set_target_properties(rov_ui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(rov_bootloader_firmware_reader_test EXCLUDE_FROM_ALL
        tests/BootloaderFirmwareReaderTest.cpp
    )
    target_include_directories(rov_bootloader_firmware_reader_test PRIVATE src)
    target_link_libraries(rov_bootloader_firmware_reader_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_bootloader_firmware_reader_test COMMAND rov_bootloader_firmware_reader_test)

    add_executable(rov_bootloader_upgrade_sequence_test EXCLUDE_FROM_ALL
        tests/BootloaderUpgradeSequenceTest.cpp
    )
    target_include_directories(rov_bootloader_upgrade_sequence_test PRIVATE src)
    target_link_libraries(rov_bootloader_upgrade_sequence_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_bootloader_upgrade_sequence_test COMMAND rov_bootloader_upgrade_sequence_test)

    add_executable(rov_can_bitrate_settings_test EXCLUDE_FROM_ALL tests/CanBitrateSettingsTest.cpp)
    target_link_libraries(rov_can_bitrate_settings_test PRIVATE rov_page_settings rov_firmware_history)
    add_test(NAME rov_can_bitrate_settings_test COMMAND rov_can_bitrate_settings_test -platform offscreen)
    add_executable(rov_can_flow_protocol_test EXCLUDE_FROM_ALL
        tests/CanFlowControlProtocolTest.cpp
    )
    target_include_directories(rov_can_flow_protocol_test PRIVATE src)
    target_link_libraries(rov_can_flow_protocol_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_can_flow_protocol_test COMMAND rov_can_flow_protocol_test)

    add_executable(rov_observer_motor_protocol_test EXCLUDE_FROM_ALL
        tests/ObserverMotorProtocolTest.cpp
    )
    target_include_directories(rov_observer_motor_protocol_test PRIVATE src)
    target_link_libraries(rov_observer_motor_protocol_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_observer_motor_protocol_test COMMAND rov_observer_motor_protocol_test)

    add_executable(rov_system_heartbeat_protocol_test EXCLUDE_FROM_ALL
        tests/SystemHeartbeatProtocolTest.cpp
    )
    target_include_directories(rov_system_heartbeat_protocol_test PRIVATE src)
    target_link_libraries(rov_system_heartbeat_protocol_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_system_heartbeat_protocol_test COMMAND rov_system_heartbeat_protocol_test)

    add_executable(rov_can_gateway_config_protocol_test EXCLUDE_FROM_ALL
        tests/CanGatewayConfigProtocolTest.cpp
    )
    target_include_directories(rov_can_gateway_config_protocol_test PRIVATE src)
    target_link_libraries(rov_can_gateway_config_protocol_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_can_gateway_config_protocol_test COMMAND rov_can_gateway_config_protocol_test)

    add_executable(rov_research_data_recorder_test EXCLUDE_FROM_ALL
        tests/ResearchDataRecorderTest.cpp
    )
    target_include_directories(rov_research_data_recorder_test PRIVATE src)
    target_link_libraries(rov_research_data_recorder_test PRIVATE Qt5::Core rov_research_recording)
    add_test(NAME rov_research_data_recorder_test COMMAND rov_research_data_recorder_test)
endif()
