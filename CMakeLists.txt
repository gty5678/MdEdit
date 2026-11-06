cmake_minimum_required(VERSION 3.24)
project(MdEdit VERSION 1.1 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 6.5 CONFIG REQUIRED COMPONENTS Widgets Svg)
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
add_custom_command(TARGET MdEdit POST_BUILD
 COMMAND ${CMAKE_COMMAND} -E copy_directory "${MicroTeX_RESOURCE_DIR}" "${MDEDIT_RESOURCE_DIR}/res"
 COMMAND ${CMAKE_COMMAND} -DSHARE_DIR=${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share -DDEST_DIR=${MDEDIT_RESOURCE_DIR}/licenses -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/copy-licenses.cmake"
 VERBATIM)

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
install(SCRIPT ${deploy_script})

include(CTest)
if(BUILD_TESTING)
  add_test(NAME markdown-math-smoke COMMAND MdEdit --smoke-test "${CMAKE_CURRENT_SOURCE_DIR}/综合测试.md")
  set_tests_properties(markdown-math-smoke PROPERTIES TIMEOUT 60)
  if(WIN32)
    set_tests_properties(markdown-math-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=windows")
  elseif(APPLE)
    set_tests_properties(markdown-math-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=cocoa")
  else()
    set_tests_properties(markdown-math-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  endif()
endif()
