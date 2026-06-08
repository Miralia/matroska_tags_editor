cmake_minimum_required(VERSION 3.22)

project(MatroskaTagsEditor
  VERSION 0.2.0
  DESCRIPTION "Native Matroska tags editor"
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

add_executable(matroska-tags-editor
  src/app/main.cpp
)

target_link_libraries(matroska-tags-editor
  PRIVATE
    matroska_tags_core
    ${wxWidgets_LIBRARIES}
)

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
