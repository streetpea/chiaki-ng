
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt5 REQUIRED COMPONENTS Core Widgets Gui Concurrent Multimedia OpenGL Svg)
if(APPLE)
	find_package(Qt5 REQUIRED COMPONENTS MacExtras)
endif()
if(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
	find_package(SDL2 MODULE REQUIRED)
endif()

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

set(RESOURCE_FILES "")

if(APPLE)
	list(APPEND RESOURCE_FILES "chiaki.icns")
endif()

add_executable(chiaki WIN32
		${RESOURCE_FILES}
		include/exception.h
		src/main.cpp
		include/streamwindow.h
		src/streamwindow.cpp
		include/mainwindow.h
		src/mainwindow.cpp
		include/dynamicgridwidget.h
		src/dynamicgridwidget.cpp
		include/serveritemwidget.h
		src/serveritemwidget.cpp
		include/discoverymanager.h
		src/discoverymanager.cpp
		include/streamsession.h
		src/streamsession.cpp
		include/sessionlog.h
		src/sessionlog.cpp
		include/avopenglwidget.h
		src/avopenglwidget.cpp
		include/avopenglframeuploader.h
		src/avopenglframeuploader.cpp
		include/servericonwidget.h
		src/servericonwidget.cpp
		include/settings.h
		src/settings.cpp
		include/registdialog.h
		src/registdialog.cpp
		include/host.h
		src/host.cpp
		include/settingskeycapturedialog.h
		src/settingskeycapturedialog.cpp
		include/settingsdialog.h
		src/settingsdialog.cpp
		include/manualhostdialog.h
		src/manualhostdialog.cpp
		res/resources.qrc
		include/controllermanager.h
		src/controllermanager.cpp
		include/loginpindialog.h
		src/loginpindialog.cpp)
target_include_directories(chiaki PRIVATE include)

target_link_libraries(chiaki chiaki-lib)
if(CHIAKI_ENABLE_CLI)
	add_definitions(-DCHIAKI_ENABLE_CLI)
	target_link_libraries(chiaki chiaki-cli-lib)
endif()

target_link_libraries(chiaki Qt5::Core Qt5::Widgets Qt5::Gui Qt5::Concurrent Qt5::Multimedia Qt5::OpenGL Qt5::Svg)
if(APPLE)
	target_link_libraries(chiaki Qt5::MacExtras)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_QT_MACEXTRAS)
endif()
if(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
	target_link_libraries(chiaki SDL2::SDL2)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
endif()
if(CHIAKI_ENABLE_SETSU)
	target_link_libraries(chiaki setsu)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SETSU)
endif()
if(NOT CHIAKI_ENABLE_FFMPEG_DECODER)
	message(FATAL_ERROR "Chiaki GUI requires CHIAKI_ENABLE_FFMPEG_DECODER=ON")
endif()

set_target_properties(chiaki PROPERTIES
		MACOSX_BUNDLE TRUE
		MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/MacOSXBundleInfo.plist.in"
		MACOSX_BUNDLE_BUNDLE_NAME Chiaki
		MACOSX_BUNDLE_BUNDLE_VERSION ${CHIAKI_VERSION}
		MACOSX_BUNDLE_COPYRIGHT "thestr4ng3r (AGPLv3)"
		MACOSX_BUNDLE_GUI_IDENTIFIER "org.chiaki.chiaki"
		MACOSX_BUNDLE_ICON_FILE chiaki.icns
		RESOURCE "${RESOURCE_FILES}")

install(TARGETS chiaki
		RUNTIME DESTINATION bin
		BUNDLE DESTINATION bin)
install(FILES chiaki.desktop DESTINATION share/applications)
install(FILES chiaki.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES re.chiaki.Chiaki.appdata.xml DESTINATION share/metainfo)
