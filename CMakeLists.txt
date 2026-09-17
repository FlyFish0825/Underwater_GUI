cmake_minimum_required(VERSION 3.16)

project(rov_ui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt5 5.9 REQUIRED COMPONENTS Core Gui Widgets)

add_library(rov_ui_common STATIC
    src/ui/common/UiPrimitives.cpp
    src/ui/common/UiPrimitives.h
)

add_library(rov_preview STATIC
    src/preview/PreviewData.cpp
    src/preview/PreviewData.h
)

add_library(rov_communication STATIC
    src/communication/protocol/CanGatewayProtocol.cpp
    src/communication/protocol/CanGatewayProtocol.h
    src/communication/service/BootloaderCommunicationService.cpp
    src/communication/service/BootloaderCommunicationService.h
    src/communication/transport/SerialTransport.cpp
    src/communication/transport/SerialTransport.h
)

add_library(rov_page_dashboard STATIC
    src/pages/dashboard/DashboardPage.cpp
    src/pages/dashboard/DashboardPage.h
)

add_library(rov_page_motor_debug STATIC
    src/pages/motor_debug/MotorDebugPage.cpp
    src/pages/motor_debug/MotorDebugPage.h
)

add_library(rov_page_firmware STATIC
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
    rov_preview
    rov_communication
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
    rov_preview
    rov_ui_common
    rov_communication
)

if (WIN32)
    target_link_libraries(rov_communication PRIVATE setupapi)
endif()

if (WIN32)
    set_target_properties(rov_ui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
