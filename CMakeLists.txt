# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(blackmagic)

include(components/espfs/cmake/include.cmake)
target_add_espfs(blackmagic.elf espfs ${PROJECT_SOURCE_DIR}/html)

# Fill in variables inside `version.h.in` and generate `version.h`
cmake_minimum_required(VERSION 3.0.0)
message(STATUS "Resolving GIT Version")
set(_build_version "unknown")
find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} describe --always --dirty --tags
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    OUTPUT_VARIABLE _build_version
    ERROR_QUIET
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
  message( STATUS "GIT hash: ${_build_version}")
else()
  message(STATUS "GIT not found")
endif()
string(TIMESTAMP _time_stamp)
configure_file(${CMAKE_SOURCE_DIR}/main/include/version.h.in ${CMAKE_SOURCE_DIR}/main/include/version.h @ONLY)
