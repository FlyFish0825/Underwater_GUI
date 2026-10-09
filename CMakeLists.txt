cmake_minimum_required(VERSION 3.16)

# build/gui is a deployable runtime directory, never a CMake binary directory.
get_filename_component(ROV_PACKAGE_DIR "${CMAKE_SOURCE_DIR}/build/gui" ABSOLUTE)
get_filename_component(ROV_BINARY_DIR "${CMAKE_BINARY_DIR}" ABSOLUTE)
string(TOLOWER "${ROV_PACKAGE_DIR}/" ROV_PACKAGE_COMPARE)
string(TOLOWER "${ROV_BINARY_DIR}/" ROV_BINARY_COMPARE)
string(FIND "${ROV_BINARY_COMPARE}" "${ROV_PACKAGE_COMPARE}" ROV_BINARY_IN_PACKAGE)
if(ROV_BINARY_IN_PACKAGE EQUAL 0)
    message(FATAL_ERROR "build/gui is runtime-only. Configure under build/test/gui instead.")
endif()
project(rov_ui VERSION 0.1.0 LANGUAGES CXX)
set(ROV_UI_RUNTIME_DIR "${CMAKE_BINARY_DIR}" CACHE PATH "Directory for the main application only")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Concurrent PrintSupport Network)

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
    src/communication/bootloader/BootloaderBatchCommands.cpp
    src/communication/bootloader/BootloaderBatchCommands.h
    src/communication/bootloader/BootloaderDownloadController.cpp
    src/communication/bootloader/BootloaderDownloadController.h
    src/communication/bootloader/BootloaderAutonomousUpgrade.cpp
    src/communication/bootloader/BootloaderAutonomousUpgrade.h
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
    src/communication/protocol/SensorProtocol.cpp
    src/communication/protocol/SensorProtocol.h
    src/communication/protocol/SystemHeartbeatProtocol.cpp
    src/communication/protocol/SystemHeartbeatProtocol.h
    src/communication/service/BootloaderCommunicationService.cpp
    src/communication/service/BootloaderCommunicationService.h
    src/communication/transport/SerialTransport.cpp
    src/communication/transport/SerialTransport.h
    src/communication/transport/ByteTransport.h
    src/communication/transport/TcpTransport.cpp
    src/communication/transport/TcpTransport.h
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

add_library(rov_sensor_data STATIC
    src/data/services/SensorDataService.cpp
    src/data/services/SensorDataService.h
    src/contracts/sensors/SensorContract.h
)

add_library(rov_camera STATIC
    src/communication/protocol/StereoCameraProtocol.cpp
    src/communication/protocol/StereoCameraProtocol.h
    src/data/services/StereoCameraService.cpp
    src/data/services/StereoCameraService.h
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
    src/pages/dashboard/SensorPanel.cpp
    src/pages/dashboard/SensorPanel.h
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
set_target_properties(rov_ui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${ROV_UI_RUNTIME_DIR}")

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
    rov_sensor_data
)

foreach(target IN LISTS ROV_UI_TARGETS)
    target_include_directories(${target} PUBLIC src)
    target_link_libraries(${target} PUBLIC Qt5::Core Qt5::Gui Qt5::Widgets)
endforeach()

target_link_libraries(rov_page_firmware PUBLIC rov_communication)
target_link_libraries(rov_communication PUBLIC Qt5::Network)
target_link_libraries(rov_page_firmware PUBLIC rov_firmware_history)
target_link_libraries(rov_firmware_history_ui PUBLIC rov_firmware_history rov_ui_common)
target_link_libraries(rov_page_firmware PUBLIC rov_firmware_history_ui)
target_link_libraries(rov_motor_data PUBLIC rov_communication)
target_link_libraries(rov_sensor_data PUBLIC rov_communication)
target_link_libraries(rov_camera PUBLIC Qt5::Network Qt5::Concurrent)
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
target_link_libraries(rov_page_settings PUBLIC rov_firmware_history_ui)

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
    rov_sensor_data
)

if (WIN32)
    target_link_libraries(rov_communication PRIVATE setupapi)
endif()


if (WIN32)
    set_target_properties(rov_ui PROPERTIES WIN32_EXECUTABLE TRUE)
    # Image reception needs these SDK runtimes even after a plain incremental build.
    add_custom_command(TARGET rov_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Network> $<TARGET_FILE_DIR:rov_ui>
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::Concurrent> $<TARGET_FILE_DIR:rov_ui>
        COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:rov_ui>/imageformats
        COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:Qt5::QJpegPlugin> $<TARGET_FILE_DIR:rov_ui>/imageformats
        VERBATIM)
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(rov_stereo_camera_service_test EXCLUDE_FROM_ALL tests/StereoCameraServiceTest.cpp)
    target_link_libraries(rov_stereo_camera_service_test PRIVATE rov_camera rov_communication)
    add_test(NAME rov_stereo_camera_service_test COMMAND rov_stereo_camera_service_test -platform offscreen)
    get_target_property(ROV_APP_LIBRARIES rov_ui LINK_LIBRARIES)
    add_executable(rov_stereo_camera_page_test EXCLUDE_FROM_ALL tests/StereoCameraPageTest.cpp
        src/app/MainWindow.cpp src/app/MainWindow.h resources/resources.qrc)
    target_link_libraries(rov_stereo_camera_page_test PRIVATE ${ROV_APP_LIBRARIES})
    # Explicit receive-only LAN probe; never run as part of CTest.
    add_executable(rov_stereo_camera_probe EXCLUDE_FROM_ALL tests/StereoCameraProbe.cpp
        src/app/MainWindow.cpp src/app/MainWindow.h resources/resources.qrc)
    target_link_libraries(rov_stereo_camera_probe PRIVATE ${ROV_APP_LIBRARIES})
    add_test(NAME rov_stereo_camera_page_test COMMAND rov_stereo_camera_page_test -platform offscreen)
    add_executable(rov_tcp_transport_test EXCLUDE_FROM_ALL tests/TcpTransportTest.cpp)
    target_link_libraries(rov_tcp_transport_test PRIVATE rov_communication Qt5::Network)
    add_test(NAME rov_tcp_transport_test COMMAND rov_tcp_transport_test)
    add_executable(rov_nano_connection_page_test EXCLUDE_FROM_ALL tests/NanoConnectionPageTest.cpp resources/resources.qrc)
    target_link_libraries(rov_nano_connection_page_test PRIVATE rov_page_firmware)
    add_test(NAME rov_nano_connection_page_test COMMAND rov_nano_connection_page_test -platform offscreen)
    add_executable(rov_motor_control_session_test EXCLUDE_FROM_ALL tests/MotorControlSessionTest.cpp resources/resources.qrc)
    target_link_libraries(rov_motor_control_session_test PRIVATE rov_page_motor_debug)
    add_test(NAME rov_motor_control_session_test COMMAND rov_motor_control_session_test -platform offscreen)
    # Explicit invocation only: may connect to a real LAN endpoint in receive-only mode.
    add_executable(rov_nano_tcp_probe EXCLUDE_FROM_ALL tests/NanoTcpProbe.cpp)
    target_link_libraries(rov_nano_tcp_probe PRIVATE rov_communication)
    add_executable(rov_bootloader_batch_commands_test EXCLUDE_FROM_ALL tests/BootloaderBatchCommandsTest.cpp)
    target_include_directories(rov_bootloader_batch_commands_test PRIVATE src)
    target_link_libraries(rov_bootloader_batch_commands_test PRIVATE Qt5::Core rov_communication)
    add_test(NAME rov_bootloader_batch_commands_test COMMAND rov_bootloader_batch_commands_test)
    add_executable(rov_firmware_batch_page_test EXCLUDE_FROM_ALL
        tests/FirmwareBatchPageTest.cpp resources/resources.qrc)
    target_link_libraries(rov_firmware_batch_page_test PRIVATE rov_page_firmware)
    add_test(NAME rov_firmware_batch_page_test COMMAND rov_firmware_batch_page_test -platform offscreen)
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

    add_executable(rov_motor_temperature_average_test EXCLUDE_FROM_ALL
        tests/MotorTemperatureAverageTest.cpp
    )
    target_link_libraries(rov_motor_temperature_average_test PRIVATE Qt5::Core rov_motor_data)
    add_test(NAME rov_motor_temperature_average_test COMMAND rov_motor_temperature_average_test)

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

# AA5B sensor tests use synthetic frames only; they never open a physical COM port.
if (BUILD_TESTING)
    add_executable(rov_sensor_protocol_test EXCLUDE_FROM_ALL tests/SensorProtocolTest.cpp)
    target_link_libraries(rov_sensor_protocol_test PRIVATE rov_sensor_data)
    add_test(NAME rov_sensor_protocol_test COMMAND rov_sensor_protocol_test)

    add_executable(rov_sensor_page_test EXCLUDE_FROM_ALL tests/SensorPageTest.cpp)
    target_link_libraries(rov_sensor_page_test PRIVATE rov_page_dashboard)
    add_test(NAME rov_sensor_page_test COMMAND rov_sensor_page_test -platform offscreen)
endif()

# Hardware probing is explicitly opt-in; never a CTest/default-build side effect.
if(BUILD_TESTING)
    add_executable(rov_sensor_hardware_probe EXCLUDE_FROM_ALL tests/SensorHardwareProbe.cpp)
    target_link_libraries(rov_sensor_hardware_probe PRIVATE Qt5::Core rov_sensor_data)
endif()

# AA55 old/new uplink compatibility; test injection never opens a physical port.
if(BUILD_TESTING)
    add_executable(rov_can_gateway_timestamp_test EXCLUDE_FROM_ALL tests/CanGatewayTimestampTest.cpp)
    target_link_libraries(rov_can_gateway_timestamp_test PRIVATE rov_communication)
    add_test(NAME rov_can_gateway_timestamp_test COMMAND rov_can_gateway_timestamp_test)
endif()
