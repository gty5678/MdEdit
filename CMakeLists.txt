cmake_minimum_required(VERSION 3.24)
project(MdEdit VERSION 1.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.12.0 CONFIG REQUIRED COMPONENTS Widgets Svg)
find_package(MicroTeX CONFIG REQUIRED)
qt_standard_project_setup()
qt_add_executable(MdEdit WIN32 MACOSX_BUNDLE src/main.cpp)
if(APPLE)
  set_target_properties(MdEdit PROPERTIES
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.mdedit.app"
    MACOSX_BUNDLE_BUNDLE_NAME "MdEdit"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
  set(MDEDIT_RESOURCE_DIR "$<TARGET_BUNDLE_CONTENT_DIR:MdEdit>/Resources")
else()
  set(MDEDIT_RESOURCE_DIR "$<TARGET_FILE_DIR:MdEdit>")
endif()
target_link_libraries(MdEdit PRIVATE Qt6::Widgets Qt6::Svg MicroTeX::MicroTeX)
if(MSVC)
  target_compile_options(MdEdit PRIVATE /utf-8 /W4)
endif()
if(WIN32)
  # vcpkg keeps Qt plugins outside the executable directory in the build tree.
  file(GENERATE OUTPUT "$<TARGET_FILE_DIR:MdEdit>/qt.conf"
    CONTENT "[Paths]\nPlugins=$<TARGET_FILE_DIR:Qt6::QWindowsIntegrationPlugin>/..\n")
endif()
add_custom_command(TARGET MdEdit POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${MicroTeX_RESOURCE_DIR}" "${MDEDIT_RESOURCE_DIR}/res"
 COMMAND ${CMAKE_COMMAND} -DSHARE_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share -DDEST_DIR=${MDEDIT_RESOURCE_DIR}/licenses -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy-licenses.cmake"
 VERBATIM)

option(MDEDIT_DEPLOY_WINDOWS_RUNTIME "Deploy Qt beside the build-tree executable" OFF)
if(WIN32 AND MDEDIT_DEPLOY_WINDOWS_RUNTIME)
  add_custom_command(TARGET MdEdit POST_BUILD
    COMMAND Qt6::windeployqt --no-translations --no-compiler-runtime "$<TARGET_FILE:MdEdit>"
    VERBATIM)
endif()

include(GNUInstallDirs)
install(TARGETS MdEdit BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(APPLE)
  set(MDEDIT_INSTALL_RESOURCES "MdEdit.app/Contents/Resources")
else()
  set(MDEDIT_INSTALL_RESOURCES "${CMAKE_INSTALL_BINDIR}")
endif()
install(DIRECTORY "${MicroTeX_RESOURCE_DIR}" DESTINATION "${MDEDIT_INSTALL_RESOURCES}")
# Include licenses for transitive dependencies, including Qt's bundled libraries.
install(DIRECTORY "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/"
  DESTINATION "${MDEDIT_INSTALL_RESOURCES}/licenses" FILES_MATCHING PATTERN "copyright")
qt_generate_deploy_app_script(TARGET MdEdit OUTPUT_SCRIPT deploy_script
  NO_UNSUPPORTED_PLATFORM_ERROR)
if(MINGW)
  # The deployment tools also use Qt DLLs from the vcpkg bin directory.
  install(CODE "set(QT_DEPLOY_PLUGINS_DIR plugins)")
  install(CODE "set(ENV{PATH} \"$<TARGET_FILE_DIR:Qt6::Core>;\$ENV{PATH}\")")
endif()
install(SCRIPT ${deploy_script})
if(MINGW)
  get_filename_component(MDEDIT_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
  install(CODE "
    set(bin \"\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\")
    file(GLOB_RECURSE plugins \"\${CMAKE_INSTALL_PREFIX}/plugins/*.dll\")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_COMMAND \"${CMAKE_OBJDUMP}\")
    set(CMAKE_GET_RUNTIME_DEPENDENCIES_TOOL objdump)
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES \"\${bin}/$<TARGET_FILE_NAME:MdEdit>\"
      MODULES \${plugins}
      DIRECTORIES \"\${bin}\" \"$<TARGET_FILE_DIR:Qt6::Core>\" \"${MDEDIT_COMPILER_BIN}\"
      RESOLVED_DEPENDENCIES_VAR dependencies
      PRE_EXCLUDE_REGEXES \"^(api-ms-|ext-ms-)\"
      POST_EXCLUDE_REGEXES \"/([Ss][Yy][Ss][Tt][Ee][Mm]32|[Ss][Yy][Ss][Ww][Oo][Ww]64)/\")
    foreach(dependency IN LISTS dependencies)
      get_filename_component(name \"\${dependency}\" NAME)
      if(NOT EXISTS \"\${bin}/\${name}\")
        file(INSTALL DESTINATION \"\${bin}\" TYPE SHARED_LIBRARY FILES \"\${dependency}\")
      endif()
    endforeach()
  ")
endif()

include(CTest)
if(BUILD_TESTING)
  add_test(NAME markdown-math-smoke COMMAND MdEdit --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/综合测试.md")
  set_tests_properties(markdown-math-smoke PROPERTIES TIMEOUT 60)
  if(WIN32)
    set_tests_properties(markdown-math-smoke PROPERTIES
      ENVIRONMENT "QT_QPA_PLATFORM=windows"
      ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
  elseif(APPLE)
    set_tests_properties(markdown-math-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
  else()
    set_tests_properties(markdown-math-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
