cmake_minimum_required(VERSION 3.21)
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
project(aseapp_native_ui VERSION 1.3.0 LANGUAGES CXX)

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
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Core Network)

if(MSVC AND WIN32 AND DEFINED ENV{CONDA_PREFIX})
  # Conda's Qt packages are release CRT builds. Keep Debug targets on /MD as
  # well to avoid MSVCRTD/msvcrt mixing and Debug-only test crashes.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

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

set(ASEAPP_NATIVE_UI_COMMON_SOURCES
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

add_executable(aseapp_native_ui
  src/AppStartup.cpp
  src/AppStartup.h
  src/SingleInstance.cpp
  src/SingleInstance.h
  src/main.cpp
  ${ASEAPP_NATIVE_UI_COMMON_SOURCES}
)

target_include_directories(aseapp_native_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(aseapp_native_ui PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Network)

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
    INSTALL_RPATH "@executable_path/../Frameworks"
  )
endif()

if(WIN32)
  set_target_properties(aseapp_native_ui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set_target_properties(aseapp_native_ui PROPERTIES
  OUTPUT_NAME "ASEappNativeUI"
)

set(ASEAPP_QT_PLUGIN_SEARCH_DIRS)
if(WIN32)
  if(DEFINED ENV{CONDA_PREFIX})
    file(TO_CMAKE_PATH "$ENV{CONDA_PREFIX}/Library/lib/qt6/plugins" ASEAPP_CONDA_QT_PLUGIN_DIR)
    list(APPEND ASEAPP_QT_PLUGIN_SEARCH_DIRS "${ASEAPP_CONDA_QT_PLUGIN_DIR}")
  endif()
  foreach(ASEAPP_PREFIX IN LISTS CMAKE_PREFIX_PATH)
    list(APPEND ASEAPP_QT_PLUGIN_SEARCH_DIRS
      "${ASEAPP_PREFIX}/lib/qt6/plugins"
      "${ASEAPP_PREFIX}/Library/lib/qt6/plugins"
      "${ASEAPP_PREFIX}/plugins"
    )
  endforeach()
  list(REMOVE_DUPLICATES ASEAPP_QT_PLUGIN_SEARCH_DIRS)
  foreach(ASEAPP_PLUGIN_DIR IN LISTS ASEAPP_QT_PLUGIN_SEARCH_DIRS)
    if(EXISTS "${ASEAPP_PLUGIN_DIR}/platforms/qwindows.dll")
      set(ASEAPP_QT_PLUGIN_DIR "${ASEAPP_PLUGIN_DIR}")
      break()
    endif()
  endforeach()
endif()

function(aseapp_prepare_windows_build_runtime target_name)
  if(NOT WIN32 OR NOT ASEAPP_QT_PLUGIN_DIR)
    return()
  endif()
  set(ASEAPP_BUILD_QT_CONF "${CMAKE_CURRENT_BINARY_DIR}/${target_name}_qt.conf")
  file(WRITE "${ASEAPP_BUILD_QT_CONF}" "[Paths]\nPlugins = plugins\n")
  add_custom_command(TARGET ${target_name} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      "${ASEAPP_QT_PLUGIN_DIR}"
      "$<TARGET_FILE_DIR:${target_name}>/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${ASEAPP_BUILD_QT_CONF}"
      "$<TARGET_FILE_DIR:${target_name}>/qt.conf"
    COMMENT "Copying Qt plugins for direct Windows build-tree launches"
  )
endfunction()

aseapp_prepare_windows_build_runtime(aseapp_native_ui)

install(TARGETS aseapp_native_ui
  BUNDLE DESTINATION .
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

include(CTest)
if(BUILD_TESTING)
  set(ASEAPP_TEST_ENV)
  set(ASEAPP_GUI_QT_TEST_ENV)
  if(WIN32 AND DEFINED ENV{CONDA_PREFIX})
    file(TO_NATIVE_PATH "$ENV{CONDA_PREFIX}/Library/bin" ASEAPP_TEST_CONDA_LIBRARY_BIN)
    file(TO_NATIVE_PATH "$ENV{CONDA_PREFIX}" ASEAPP_TEST_CONDA_PREFIX)
    file(TO_NATIVE_PATH "$ENV{CONDA_PREFIX}/Scripts" ASEAPP_TEST_CONDA_SCRIPTS)
    file(TO_NATIVE_PATH "$ENV{CONDA_PREFIX}/Library/lib/qt6/plugins" ASEAPP_TEST_QT_PLUGIN_PATH)
    set(ASEAPP_TEST_PATH "${ASEAPP_TEST_CONDA_LIBRARY_BIN};${ASEAPP_TEST_CONDA_PREFIX};${ASEAPP_TEST_CONDA_SCRIPTS};$ENV{PATH}")
    string(REPLACE ";" "\\;" ASEAPP_TEST_PATH "${ASEAPP_TEST_PATH}")
    set(ASEAPP_TEST_ENV "PATH=${ASEAPP_TEST_PATH}")
    set(ASEAPP_GUI_QT_TEST_ENV
      "QT_PLUGIN_PATH=${ASEAPP_TEST_QT_PLUGIN_PATH}"
      "QT_QPA_PLATFORM=offscreen"
    )
  endif()

  add_executable(aseapp_adsorbate_pose_self_test
    src/AdsorbatePoseSelfTestMain.cpp
    ${ASEAPP_NATIVE_UI_COMMON_SOURCES}
  )
  target_include_directories(aseapp_adsorbate_pose_self_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
  target_link_libraries(aseapp_adsorbate_pose_self_test PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
  set_target_properties(aseapp_adsorbate_pose_self_test PROPERTIES
    OUTPUT_NAME "ASEappAdsorbatePoseSelfTest"
  )

  add_test(
    NAME adsorbate_pose_gui_self_test
    COMMAND $<TARGET_FILE:aseapp_adsorbate_pose_self_test>
      --test-output "${CMAKE_CURRENT_BINARY_DIR}/adsorbate_pose_self_test"
  )
  set(ASEAPP_GUI_SELF_TEST_ENV)
  if(ASEAPP_TEST_ENV)
    list(APPEND ASEAPP_GUI_SELF_TEST_ENV "${ASEAPP_TEST_ENV}")
  endif()
  if(ASEAPP_GUI_QT_TEST_ENV)
    list(APPEND ASEAPP_GUI_SELF_TEST_ENV ${ASEAPP_GUI_QT_TEST_ENV})
  endif()
  set_tests_properties(adsorbate_pose_gui_self_test PROPERTIES TIMEOUT 30)
  if(ASEAPP_GUI_SELF_TEST_ENV)
    set_tests_properties(adsorbate_pose_gui_self_test PROPERTIES ENVIRONMENT "${ASEAPP_GUI_SELF_TEST_ENV}")
  endif()

  add_test(
    NAME ase_extxyz_roundtrip
    COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/test_adsorbate_pose_roundtrip.py"
      --work-dir "${CMAKE_CURRENT_BINARY_DIR}/adsorbate_pose_ase_roundtrip"
  )
  set_tests_properties(ase_extxyz_roundtrip PROPERTIES TIMEOUT 30)
  if(ASEAPP_TEST_ENV)
    set_tests_properties(ase_extxyz_roundtrip PROPERTIES ENVIRONMENT "${ASEAPP_TEST_ENV}")
  endif()
endif()

install(FILES README.txt QUICKSTART.txt CHANGELOG.txt DESTINATION .)

if(APPLE)
  set(ASEAPP_MACOS_PUBLIC_CERT "${CMAKE_CURRENT_SOURCE_DIR}/certs/ASEappSurfaceBuilderLocalCodeSigning.cer")
  if(EXISTS "${ASEAPP_MACOS_PUBLIC_CERT}")
    install(FILES "${ASEAPP_MACOS_PUBLIC_CERT}" DESTINATION .)
  endif()
  install(PROGRAMS
    tools/macos/ASEapp-macOS-Allow-This-App.command
    DESTINATION .
  )
  install(FILES
    tools/macos/README-macOS.txt
    DESTINATION .
  )
endif()

set(ASEAPP_DEPLOY_TOOL_OPTIONS)
if(APPLE)
  list(APPEND ASEAPP_DEPLOY_TOOL_OPTIONS -no-codesign)
  if(DEFINED ENV{CONDA_PREFIX} AND EXISTS "$ENV{CONDA_PREFIX}/lib")
    list(APPEND ASEAPP_DEPLOY_TOOL_OPTIONS "-libpath=$ENV{CONDA_PREFIX}/lib")
  else()
    foreach(ASEAPP_PREFIX IN LISTS CMAKE_PREFIX_PATH)
      if(EXISTS "${ASEAPP_PREFIX}/lib")
        list(APPEND ASEAPP_DEPLOY_TOOL_OPTIONS "-libpath=${ASEAPP_PREFIX}/lib")
        break()
      endif()
    endforeach()
  endif()
endif()

if(ASEAPP_DEPLOY_TOOL_OPTIONS)
  qt_generate_deploy_app_script(
    TARGET aseapp_native_ui
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
    DEPLOY_TOOL_OPTIONS ${ASEAPP_DEPLOY_TOOL_OPTIONS}
  )
else()
  qt_generate_deploy_app_script(
    TARGET aseapp_native_ui
    OUTPUT_SCRIPT deploy_script
    NO_UNSUPPORTED_PLATFORM_ERROR
  )
endif()
install(SCRIPT ${deploy_script})

if(APPLE)
  install(CODE [=[
    set(ASEAPP_APP_BUNDLE "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/ASEappNativeUI.app")
    if(EXISTS "${ASEAPP_APP_BUNDLE}")
      find_program(ASEAPP_INSTALL_NAME_TOOL install_name_tool)
      find_program(ASEAPP_XATTR xattr)
      find_program(ASEAPP_CODESIGN codesign)

      function(aseapp_add_rpath binary rpath)
        if(EXISTS "${binary}" AND ASEAPP_INSTALL_NAME_TOOL)
          execute_process(
            COMMAND "${ASEAPP_INSTALL_NAME_TOOL}" -add_rpath "${rpath}" "${binary}"
            RESULT_VARIABLE ASEAPP_RPATH_RESULT
            ERROR_VARIABLE ASEAPP_RPATH_ERROR
            OUTPUT_QUIET
          )
          if(ASEAPP_RPATH_RESULT AND NOT ASEAPP_RPATH_ERROR MATCHES "would duplicate")
            message(WARNING "Failed to add rpath '${rpath}' to ${binary}: ${ASEAPP_RPATH_ERROR}")
          endif()
        endif()
      endfunction()

      if(ASEAPP_XATTR)
        execute_process(COMMAND "${ASEAPP_XATTR}" -cr "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
        foreach(ASEAPP_ATTR IN ITEMS
          com.apple.FinderInfo
          com.apple.ResourceFork
          com.apple.fileprovider.fpfs#P
        )
          execute_process(COMMAND "${ASEAPP_XATTR}" -d "${ASEAPP_ATTR}" "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
          execute_process(COMMAND "${ASEAPP_XATTR}" -rd "${ASEAPP_ATTR}" "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
        endforeach()
      endif()

      aseapp_add_rpath("${ASEAPP_APP_BUNDLE}/Contents/MacOS/ASEappNativeUI" "@executable_path/../Frameworks")

      file(GLOB_RECURSE ASEAPP_FRAMEWORK_DYLIBS
        "${ASEAPP_APP_BUNDLE}/Contents/Frameworks/*.dylib"
      )
      foreach(ASEAPP_DYLIB IN LISTS ASEAPP_FRAMEWORK_DYLIBS)
        aseapp_add_rpath("${ASEAPP_DYLIB}" "@loader_path")
      endforeach()

      file(GLOB_RECURSE ASEAPP_PLUGIN_DYLIBS
        "${ASEAPP_APP_BUNDLE}/Contents/PlugIns/*.dylib"
      )
      foreach(ASEAPP_PLUGIN IN LISTS ASEAPP_PLUGIN_DYLIBS)
        aseapp_add_rpath("${ASEAPP_PLUGIN}" "@loader_path/../../Frameworks")
      endforeach()

      if(ASEAPP_XATTR)
        execute_process(COMMAND "${ASEAPP_XATTR}" -cr "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
        foreach(ASEAPP_ATTR IN ITEMS
          com.apple.FinderInfo
          com.apple.ResourceFork
          com.apple.fileprovider.fpfs#P
        )
          execute_process(COMMAND "${ASEAPP_XATTR}" -d "${ASEAPP_ATTR}" "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
          execute_process(COMMAND "${ASEAPP_XATTR}" -rd "${ASEAPP_ATTR}" "${ASEAPP_APP_BUNDLE}" OUTPUT_QUIET ERROR_QUIET)
        endforeach()
      endif()

      if(ASEAPP_CODESIGN)
        if("$ENV{ASEAPP_CODESIGN_IDENTITY}" STREQUAL "")
          set(ASEAPP_CODESIGN_IDENTITY "-")
        else()
          set(ASEAPP_CODESIGN_IDENTITY "$ENV{ASEAPP_CODESIGN_IDENTITY}")
        endif()

        set(ASEAPP_CODESIGN_ARGS
          --force
          --deep
          --sign "${ASEAPP_CODESIGN_IDENTITY}"
        )
        if(NOT ASEAPP_CODESIGN_IDENTITY STREQUAL "-" AND "$ENV{ASEAPP_ENABLE_HARDENED_RUNTIME}" STREQUAL "1")
          list(APPEND ASEAPP_CODESIGN_ARGS --options runtime --timestamp)
        endif()

        execute_process(
          COMMAND "${ASEAPP_CODESIGN}" ${ASEAPP_CODESIGN_ARGS} "${ASEAPP_APP_BUNDLE}"
          RESULT_VARIABLE ASEAPP_CODESIGN_RESULT
          ERROR_VARIABLE ASEAPP_CODESIGN_ERROR
          OUTPUT_QUIET
        )
        if(ASEAPP_CODESIGN_RESULT)
          message(WARNING "Ad-hoc codesign failed for ${ASEAPP_APP_BUNDLE}: ${ASEAPP_CODESIGN_ERROR}")
        endif()
      endif()
    endif()
  ]=])
endif()

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
