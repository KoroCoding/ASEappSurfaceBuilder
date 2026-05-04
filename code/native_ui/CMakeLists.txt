cmake_minimum_required(VERSION 3.21)
project(aseapp_native_ui VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

include(GNUInstallDirs)

if(NOT DEFINED CMAKE_PREFIX_PATH)
  if(DEFINED ENV{CONDA_PREFIX})
    list(APPEND CMAKE_PREFIX_PATH "$ENV{CONDA_PREFIX}/Library")
  endif()
endif()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core)

set(ASEAPP_ICON_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/assets/aseapp_surface_builder_icon.svg")
set(ASEAPP_GENERATED_ICON_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/icons")
set(ASEAPP_ICON_PNG "${ASEAPP_GENERATED_ICON_DIR}/aseapp_surface_builder_icon.png")
set(ASEAPP_ICON_PATH "${ASEAPP_GENERATED_ICON_DIR}/aseapp_surface_builder_icon.ico")
set(ASEAPP_MACOS_ICON_PATH "${ASEAPP_GENERATED_ICON_DIR}/aseapp_surface_builder_icon.icns")

execute_process(
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_icon_assets.py
    --source ${ASEAPP_ICON_SOURCE}
    --out-dir ${ASEAPP_GENERATED_ICON_DIR}
  RESULT_VARIABLE ASEAPP_ICON_GENERATE_RESULT
  OUTPUT_VARIABLE ASEAPP_ICON_GENERATE_OUTPUT
  ERROR_VARIABLE ASEAPP_ICON_GENERATE_ERROR
)
if(NOT ASEAPP_ICON_GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR "Failed to generate app icons:\n${ASEAPP_ICON_GENERATE_OUTPUT}\n${ASEAPP_ICON_GENERATE_ERROR}")
endif()

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/app_icons.qrc.in
  ${CMAKE_CURRENT_BINARY_DIR}/app_icons.qrc
  @ONLY
)

add_executable(aseapp_native_ui
  src/main.cpp
  src/MainWindow.cpp
  src/MainWindow.h
  src/SurfaceCustomizationRegistry.cpp
  src/SurfaceCustomizationRegistry.h
  src/SurfaceOperations.cpp
  src/SurfaceOperations.h
  src/StructureCanvas.cpp
  src/StructureCanvas.h
  src/ElementStyle.cpp
  src/ElementStyle.h
  src/StructureFileLoader.cpp
  src/StructureFileLoader.h
  src/StructureData.h
  ${CMAKE_CURRENT_BINARY_DIR}/app_icons.qrc
)

target_include_directories(aseapp_native_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(aseapp_native_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/src/aseapp_native_ui.rc.in
  ${CMAKE_CURRENT_BINARY_DIR}/aseapp_native_ui.rc
  @ONLY
)

if(WIN32)
  target_sources(aseapp_native_ui PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/aseapp_native_ui.rc)
endif()

if(APPLE)
  target_sources(aseapp_native_ui PRIVATE ${ASEAPP_MACOS_ICON_PATH})
  set_source_files_properties(${ASEAPP_MACOS_ICON_PATH} PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )
  set_target_properties(aseapp_native_ui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ASEapp Surface Builder"
    MACOSX_BUNDLE_GUI_IDENTIFIER "jp.takae.aseapp.surfacebuilder"
    MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
    MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
    MACOSX_BUNDLE_ICON_FILE "aseapp_surface_builder_icon.icns"
  )
endif()

if(WIN32)
  set_target_properties(aseapp_native_ui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(aseapp_native_ui PROPERTIES
  OUTPUT_NAME "ASEappNativeUI"
)

install(TARGETS aseapp_native_ui
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES README.txt QUICKSTART.txt CHANGELOG.txt DESTINATION .)

qt_generate_deploy_app_script(
  TARGET aseapp_native_ui
  OUTPUT_SCRIPT deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR
)
install(SCRIPT ${deploy_script})

if(WIN32)
  include(InstallRequiredSystemLibraries)
  install(FILES ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}
    DESTINATION ${CMAKE_INSTALL_BINDIR}
  )

  set(ASEAPP_QT_RUNTIME_DLLS
    double-conversion.dll
    freetype.dll
    jpeg8.dll
    libcrypto-3-x64.dll
    libpng16.dll
    libssl-3-x64.dll
    pcre2-16.dll
    zlib.dll
    zstd.dll
    deflate.dll
    Lerc.dll
    liblzma.dll
    libsharpyuv.dll
    libwebp.dll
    libwebpdemux.dll
    libwebpmux.dll
    tiff.dll
  )
  set(ASEAPP_QT_RUNTIME_SEARCH_DIRS)
  if(DEFINED ENV{CONDA_PREFIX})
    file(TO_CMAKE_PATH "$ENV{CONDA_PREFIX}/Library/bin" ASEAPP_CONDA_RUNTIME_BIN)
    list(APPEND ASEAPP_QT_RUNTIME_SEARCH_DIRS "${ASEAPP_CONDA_RUNTIME_BIN}")
  endif()
  foreach(ASEAPP_PREFIX IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND ASEAPP_QT_RUNTIME_SEARCH_DIRS
      "${ASEAPP_PREFIX}/bin"
      "${ASEAPP_PREFIX}/Library/bin"
    )
  endforeach()
  list(REMOVE_DUPLICATES ASEAPP_QT_RUNTIME_SEARCH_DIRS)
  foreach(ASEAPP_QT_RUNTIME_DLL IN LISTS ASEAPP_QT_RUNTIME_DLLS)
    unset(ASEAPP_QT_RUNTIME_PATH)
    foreach(ASEAPP_QT_RUNTIME_DIR IN LISTS ASEAPP_QT_RUNTIME_SEARCH_DIRS)
      if(EXISTS "${ASEAPP_QT_RUNTIME_DIR}/${ASEAPP_QT_RUNTIME_DLL}")
        set(ASEAPP_QT_RUNTIME_PATH "${ASEAPP_QT_RUNTIME_DIR}/${ASEAPP_QT_RUNTIME_DLL}")
        break()
      endif()
    endforeach()
    if(NOT ASEAPP_QT_RUNTIME_PATH)
      message(FATAL_ERROR "Required Qt runtime DLL was not found: ${ASEAPP_QT_RUNTIME_DLL}")
    endif()
    install(FILES "${ASEAPP_QT_RUNTIME_PATH}"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endforeach()
endif()

if(WIN32)
  set(CPACK_GENERATOR "ZIP")
  set(ASEAPP_PACKAGE_PLATFORM "Windows")
elseif(APPLE)
  set(CPACK_GENERATOR "DragNDrop")
  set(ASEAPP_PACKAGE_PLATFORM "macOS")
else()
  set(CPACK_GENERATOR "TGZ")
  set(ASEAPP_PACKAGE_PLATFORM "Linux")
endif()

set(CPACK_PACKAGE_NAME "ASEappSurfaceBuilder")
set(CPACK_PACKAGE_VENDOR "ASEapp")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}-${ASEAPP_PACKAGE_PLATFORM}")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/dist")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
include(CPack)
