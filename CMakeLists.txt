cmake_minimum_required(VERSION 3.22)

project(MatroskaTagsEditor
  VERSION 1.0.0
  DESCRIPTION "Matroska tags editor"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(EBML CONFIG QUIET)
find_package(Matroska CONFIG QUIET)

if(NOT TARGET EBML::ebml OR NOT TARGET Matroska::matroska)
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(EBML REQUIRED IMPORTED_TARGET libebml)
  pkg_check_modules(MATROSKA REQUIRED IMPORTED_TARGET libmatroska)
  add_library(EBML::ebml ALIAS PkgConfig::EBML)
  add_library(Matroska::matroska ALIAS PkgConfig::MATROSKA)
endif()

find_package(wxWidgets REQUIRED COMPONENTS core base)
include(${wxWidgets_USE_FILE})

add_library(matroska_tags_core
  src/core/tag_document.cpp
  src/core/matroska_reader.cpp
  src/core/matroska_writer.cpp
)

target_include_directories(matroska_tags_core
  PUBLIC
    ${PROJECT_SOURCE_DIR}/src
)

target_link_libraries(matroska_tags_core
  PUBLIC
    EBML::ebml
    Matroska::matroska
)

if(WIN32)
  target_compile_definitions(matroska_tags_core
    PUBLIC
      NOMINMAX
      WIN32_LEAN_AND_MEAN
  )
endif()

add_executable(matroska-tags-editor
  src/app/main.cpp
)

target_link_libraries(matroska-tags-editor
  PRIVATE
    matroska_tags_core
    ${wxWidgets_LIBRARIES}
)

if(WIN32)
  set_target_properties(matroska-tags-editor PROPERTIES
    WIN32_EXECUTABLE TRUE
  )

  add_custom_command(TARGET matroska-tags-editor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy -t $<TARGET_FILE_DIR:matroska-tags-editor> $<TARGET_RUNTIME_DLLS:matroska-tags-editor>
    COMMAND_EXPAND_LISTS
  )
endif()

if(APPLE)
  set_target_properties(matroska-tags-editor PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "Matroska Tags Editor"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.miralia.matroska-tags-editor"
  )
endif()

if(EXISTS "${PROJECT_SOURCE_DIR}/tests-local/CMakeLists.txt")
  add_subdirectory(tests-local)
endif()
