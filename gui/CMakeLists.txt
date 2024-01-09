
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
find_package(Qt6 REQUIRED COMPONENTS Core Gui Concurrent Svg)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()
if(CHIAKI_ENABLE_QMLGUI)
	find_package(Qt6 REQUIRED COMPONENTS Qml Quick)
else()
	find_package(Qt6 REQUIRED COMPONENTS OpenGL Widgets OpenGLWidgets WebEngineWidgets)
endif()

if(WIN32)
	add_definitions(-DWIN32_LEAN_AND_MEAN)
endif()

set(SOURCE_FILES
		include/exception.h
		src/main.cpp
		include/discoverymanager.h
		src/discoverymanager.cpp
		include/streamsession.h
		src/streamsession.cpp
		include/sessionlog.h
		src/sessionlog.cpp
		include/settings.h
		src/settings.cpp
		include/host.h
		src/host.cpp
		res/resources.qrc
		include/controllermanager.h
		src/controllermanager.cpp
		include/loginpindialog.h
		src/loginpindialog.cpp
		include/shortcutdialog.h
		src/shortcutdialog.cpp
		include/crc.h
		src/crc.c
		include/steamshortcutentry.h
		src/steamshortcutentry.cpp
		include/steamtools.h
		src/steamtools.cpp
		include/vdf_parser.hpp
		include/vdfstatemachine.h
		src/vdfstatemachine.cpp
		include/sgdbenums.h
		include/steamgriddbapi.h
		src/steamgriddbapi.cpp
		include/sgdbartworkwidget.h
		src/sgdbartworkwidget.cpp
		include/imageloader.h
		src/imageloader.cpp
		include/psnaccountid.h
		src/psnaccountid.cpp
		include/jsonrequester.h
		src/jsonrequester.cpp)
set(RESOURCE_FILES "")

set(UI_FILES ui/shortcutdialog.ui)

if (CHIAKI_ENABLE_ARTWORK)
	add_definitions(-DCHIAKI_ENABLE_ARTWORK)

	list(APPEND SOURCE_FILES
			include/sgdbenums.h
			include/steamgriddbapi.h
			src/steamgriddbapi.cpp
			include/sgdbartworkwidget.h
			src/sgdbartworkwidget.cpp
			include/imageloader.h
			src/imageloader.cpp
			include/artworkdialog.h
			src/artworkdialog.cpp)

	list(APPEND UI_FILES
			ui/artworkdialog.ui)
endif()

qt6_wrap_ui(UI_HEADERS ${UI_FILES})

if(APPLE)
	list(APPEND RESOURCE_FILES "chiaki.icns")
endif()

if(CHIAKI_ENABLE_QMLGUI)
	list(APPEND SOURCE_FILES
		src/qml/qml.qrc
		include/qmlmainwindow.h
		src/qmlmainwindow.cpp
		include/qmlbackend.h
		src/qmlbackend.cpp
		include/qmlcontroller.h
		src/qmlcontroller.cpp
		include/qmlsettings.h
		src/qmlsettings.cpp
		include/qmlsvgprovider.h
		src/qmlsvgprovider.cpp
		include/systemdinhibit.h
		src/systemdinhibit.cpp
	)
else()
	list(APPEND SOURCE_FILES
			include/streamwindow.h
			src/streamwindow.cpp
			include/mainwindow.h
			src/mainwindow.cpp
			include/dynamicgridwidget.h
			src/dynamicgridwidget.cpp
			include/serveritemwidget.h
			src/serveritemwidget.cpp
			include/avopenglwidget.h
			src/avopenglwidget.cpp
			include/avopenglframeuploader.h
			src/avopenglframeuploader.cpp
			include/servericonwidget.h
			src/servericonwidget.cpp
			include/registdialog.h
			src/registdialog.cpp
			include/settingskeycapturedialog.h
			src/settingskeycapturedialog.cpp
			include/settingsdialog.h
			src/settingsdialog.cpp
			include/manualhostdialog.h
			src/manualhostdialog.cpp
			include/loginpindialog.h
			src/loginpindialog.cpp
			include/psnlogindialog.h
			src/psnlogindialog.cpp
	)
	if (CHIAKI_ENABLE_PLACEBO)
		list(APPEND SOURCE_FILES
				include/avplacebowidget.h
				src/avplacebowidget.cpp
				include/avplaceboframeuploader.h
				src/avplaceboframeuploader.cpp
		)
	endif()
endif()

if(CHIAKI_ENABLE_STEAMDECK_NATIVE)
	list(APPEND SOURCE_FILES
			include/sdinputcontext.h
			src/sdinputcontext.cpp
	)
endif()

add_executable(chiaki WIN32
		${RESOURCE_FILES}
		${SOURCE_FILES}
		${UI_HEADERS}
)
target_include_directories(chiaki PRIVATE include)


if(CHIAKI_ENABLE_QMLGUI)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_QML)
	target_link_libraries(chiaki Qt::Qml Qt::Quick)
else()
	target_link_libraries(chiaki Qt::OpenGL Qt::Widgets Qt::OpenGLWidgets Qt::WebEngineWidgets)
endif()

if(CHIAKI_ENABLE_PLACEBO OR CHIAKI_ENABLE_STEAMDECK_NATIVE)
	if (NOT TARGET Qt::GuiPrivate)
		message(FATAL_ERROR "Placebo renderer and Steam native require Qt::GuiPrivate, please install qt6-base-private-dev if you're on Ubuntu/Debian.")
		return()
	endif()
	target_link_libraries(chiaki Qt::GuiPrivate)
endif()

if(CHIAKI_ENABLE_PLACEBO)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_PLACEBO)
	add_library(placebo-libav-impl src/libav_impl.c)
	target_link_libraries(placebo-libav-impl PkgConfig::LIBPLACEBO avcodec avutil avformat)
	target_link_libraries(chiaki placebo-libav-impl)
	target_link_libraries(chiaki PkgConfig::LIBPLACEBO)
endif()

target_link_libraries(chiaki chiaki-lib)
if(CHIAKI_ENABLE_CLI)
	add_definitions(-DCHIAKI_ENABLE_CLI)
	target_link_libraries(chiaki chiaki-cli-lib)
endif()

target_link_libraries(chiaki Qt::Core Qt::Gui Qt::Concurrent Qt::Svg)
target_link_libraries(chiaki SDL2::SDL2)
if(UNIX AND NOT APPLE)
	target_link_libraries(chiaki Qt::DBus)
endif()
if(CHIAKI_ENABLE_SPEEX)
	target_link_libraries(chiaki speexdsp)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SPEEX)
endif()
if(CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SDL_GAMECONTROLLER)
endif()
if(CHIAKI_ENABLE_SETSU)
	target_link_libraries(chiaki setsu)
	target_compile_definitions(chiaki PRIVATE CHIAKI_GUI_ENABLE_SETSU)
endif()
if(CHIAKI_ENABLE_STEAMDECK_NATIVE)
	target_link_libraries(chiaki sdeck)
	target_compile_definitions(chiaki PRIVATE QT_STATICPLUGIN CHIAKI_GUI_ENABLE_STEAMDECK_NATIVE)
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
install(FILES chiaki4deck.desktop DESTINATION share/applications)
install(FILES chiaki4deck.png DESTINATION share/icons/hicolor/512x512/apps)
install(FILES io.github.streetpea.Chiaki4deck.appdata.xml DESTINATION share/metainfo)
