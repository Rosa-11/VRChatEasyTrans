cmake_minimum_required(VERSION 3.16)

project(VRChatEasyTrans-AI VERSION 0.1 LANGUAGES CXX)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets LinguistTools Multimedia Network WebSockets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets LinguistTools Multimedia Network WebSockets)

set(TS_FILES VRChatEasyTrans-AI_zh_CN.ts)

set(PROJECT_SOURCES
    main.cpp
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    ${TS_FILES}
    ConfigManager.h
    ConfigManager.cpp
    audiocapture.h
    audiocapture.cpp
    speechrecogniser.h speechrecogniser.cpp
    translator.h translator.cpp
)

if(${QT_VERSION_MAJOR} GREATER_EQUAL 6)
    qt_add_executable(VRChatEasyTrans-AI
        MANUAL_FINALIZATION
        ${PROJECT_SOURCES}
        solooscbroadcaster.h solooscbroadcaster.cpp
    )

    qt_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
else()
    if(ANDROID)
        add_library(VRChatEasyTrans-AI SHARED
            ${PROJECT_SOURCES}
        )
    else()
        add_executable(VRChatEasyTrans-AI
            ${PROJECT_SOURCES}
        )
    endif()

    qt5_create_translation(QM_FILES ${CMAKE_SOURCE_DIR} ${TS_FILES})
endif()

target_link_libraries(VRChatEasyTrans-AI PRIVATE
    Qt${QT_VERSION_MAJOR}::Widgets
    Qt${QT_VERSION_MAJOR}::Multimedia
    Qt${QT_VERSION_MAJOR}::Network
    Qt${QT_VERSION_MAJOR}::WebSockets
)

# 假设 config.ini 在项目根目录（与 CMakeLists.txt 同级）
set(CONFIG_FILE "${CMAKE_SOURCE_DIR}/config.ini")

# 复制 config.ini 到构建目录（与生成的 .exe 同级）
add_custom_command(
    OUTPUT "${CMAKE_BINARY_DIR}/config.ini"  # 目标路径（构建目录）
    COMMAND ${CMAKE_COMMAND} -E copy "${CONFIG_FILE}" "${CMAKE_BINARY_DIR}/config.ini"
    MAIN_DEPENDENCY "${CONFIG_FILE}"  # 当 config.ini 修改时，自动重新复制
    COMMENT "Copying config.ini to build directory"
)

# 将复制操作关联到目标程序，确保编译时自动执行
add_custom_target(
    copy_config_file ALL
    DEPENDS "${CMAKE_BINARY_DIR}/config.ini"
)
add_dependencies(VRChatEasyTrans-AI copy_config_file)

# Qt for iOS sets MACOSX_BUNDLE_GUI_IDENTIFIER automatically since Qt 6.1.
# If you are developing for iOS or macOS you should consider setting an
# explicit, fixed bundle identifier manually though.
if(${QT_VERSION} VERSION_LESS 6.1.0)
  set(BUNDLE_ID_OPTION MACOSX_BUNDLE_GUI_IDENTIFIER com.example.VRChatEasyTrans-AI)
endif()
set_target_properties(VRChatEasyTrans-AI PROPERTIES
    ${BUNDLE_ID_OPTION}
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}
    MACOSX_BUNDLE TRUE
    WIN32_EXECUTABLE TRUE
    AUTOMOC ON
)

include(GNUInstallDirs)
install(TARGETS VRChatEasyTrans-AI
    BUNDLE DESTINATION .
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(QT_VERSION_MAJOR EQUAL 6)
    qt_finalize_executable(VRChatEasyTrans-AI)
endif()
