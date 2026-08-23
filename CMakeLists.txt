cmake_minimum_required(VERSION 3.24)
project(obs-airplay VERSION 0.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "" FORCE)
set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)

set(HOMEBREW_PREFIX "/opt/homebrew")
set(ENV{PKG_CONFIG_PATH}
    "${HOMEBREW_PREFIX}/lib/pkgconfig:${HOMEBREW_PREFIX}/opt/openssl@3/lib/pkgconfig:$ENV{PKG_CONFIG_PATH}")
set(ENV{PATH} "${HOMEBREW_PREFIX}/bin:$ENV{PATH}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED libavcodec libavutil libswscale)
pkg_check_modules(FDK REQUIRED fdk-aac)
pkg_check_modules(OPENSSL REQUIRED openssl)

set(OBS_SRC "${CMAKE_SOURCE_DIR}/vendor/obs-studio-32.2.2")
set(OBS_APP "/Applications/OBS.app")
set(OBSCONFIG_DIR "${CMAKE_SOURCE_DIR}/vendor/obs-airplay-upstream/resources/macos/lib")

add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/UxPlay/lib/llhttp" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/UxPlay/lib/playfair" EXCLUDE_FROM_ALL)
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/UxPlay/lib" EXCLUDE_FROM_ALL)

add_executable(AirPlayReceiverHelper MACOSX_BUNDLE
  src/helper/main.cpp
  src/helper/video_decoder.cpp
  src/helper/audio_decoder.cpp
)
target_include_directories(AirPlayReceiverHelper PRIVATE
  src
  third_party/UxPlay/lib
  ${FFMPEG_INCLUDE_DIRS}
  ${FDK_INCLUDE_DIRS}
  ${OPENSSL_INCLUDE_DIRS}
)
target_link_libraries(AirPlayReceiverHelper PRIVATE
  airplay
  ${FFMPEG_LIBRARIES}
  ${FDK_LIBRARIES}
  "-framework VideoToolbox"
  "-framework CoreMedia"
  "-framework CoreVideo"
  "-framework CoreFoundation"
  "-framework AudioToolbox"
)
target_link_directories(AirPlayReceiverHelper PRIVATE ${FFMPEG_LIBRARY_DIRS} ${FDK_LIBRARY_DIRS})
set_target_properties(AirPlayReceiverHelper PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/HelperInfo.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "AirPlayReceiverHelper"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.local.obs-airplay.helper"
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
  BUILD_RPATH "@executable_path/../Frameworks"
  INSTALL_RPATH "@executable_path/../Frameworks"
)

add_library(obs-airplay MODULE src/plugin/plugin.cpp)
target_include_directories(obs-airplay PRIVATE
  src
  ${OBS_SRC}/libobs
  ${OBSCONFIG_DIR}
  ${HOMEBREW_PREFIX}/include
)
target_compile_definitions(obs-airplay PRIVATE HAVE_OBSCONFIG_H=1)
target_link_libraries(obs-airplay PRIVATE
  "-framework libobs"
)
target_link_options(obs-airplay PRIVATE
  "LINKER:-F${OBS_APP}/Contents/Frameworks"
  "LINKER:-rpath,@executable_path/../Frameworks"
)
set_target_properties(obs-airplay PROPERTIES
  BUNDLE TRUE
  BUNDLE_EXTENSION plugin
  PREFIX ""
  OUTPUT_NAME obs-airplay
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/cmake/PluginInfo.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "obs-airplay"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.local.obs-airplay"
  MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
  MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
)

add_custom_command(TARGET obs-airplay POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory
          "$<TARGET_BUNDLE_DIR:obs-airplay>/Contents/Resources"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "$<TARGET_BUNDLE_DIR:AirPlayReceiverHelper>"
          "$<TARGET_BUNDLE_DIR:obs-airplay>/Contents/Resources/AirPlayReceiverHelper.app"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/data"
          "$<TARGET_BUNDLE_DIR:obs-airplay>/Contents/Resources"
  COMMAND /usr/bin/python3 "${CMAKE_SOURCE_DIR}/scripts/bundle_dylibs.py"
          "$<TARGET_BUNDLE_DIR:obs-airplay>/Contents/Resources/AirPlayReceiverHelper.app/Contents/MacOS/AirPlayReceiverHelper"
  COMMAND codesign --force --deep -s - "$<TARGET_BUNDLE_DIR:obs-airplay>"
  COMMENT "Embed helper, bundle dylibs, ad-hoc sign"
)

install(DIRECTORY "$<TARGET_BUNDLE_DIR:obs-airplay>"
        DESTINATION "${CMAKE_SOURCE_DIR}/dist")
