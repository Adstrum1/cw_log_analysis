cmake_minimum_required(VERSION 3.16)

project(logcollector LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

find_package(nlohmann_json REQUIRED)

pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(CAIRO REQUIRED cairo)

pkg_check_modules(GTKMM4 QUIET gtkmm-4.0)

if(GTKMM4_FOUND)

  set(GTKMM_INCLUDE_DIRS ${GTKMM4_INCLUDE_DIRS})
  set(GTKMM_LIBRARIES ${GTKMM4_LIBRARIES})
  set(GTKMM_CFLAGS ${GTKMM4_CFLAGS_OTHER})
  set(GTKMM_LIBRARY_DIRS ${GTKMM4_LIBRARY_DIRS})

else()

  pkg_check_modules(GTKMM3 REQUIRED gtkmm-3.0)

  set(GTKMM_INCLUDE_DIRS ${GTKMM3_INCLUDE_DIRS})
  set(GTKMM_LIBRARIES ${GTKMM3_LIBRARIES})
  set(GTKMM_CFLAGS ${GTKMM3_CFLAGS_OTHER})
  set(GTKMM_LIBRARY_DIRS ${GTKMM3_LIBRARY_DIRS})

endif()

add_executable(logcollector

    src/main.cpp
    src/MainWindow.cpp
    src/LogMonitor.cpp
    src/LogParser.cpp
    src/LogAnalyzer.cpp
    src/ReportGenerator.cpp
)

target_include_directories(logcollector PRIVATE

    ${GTKMM_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
    ${CAIRO_INCLUDE_DIRS}
)

target_compile_options(logcollector PRIVATE

    ${GTKMM_CFLAGS}
    ${CURL_CFLAGS}
    ${CAIRO_CFLAGS}
)

target_link_directories(logcollector PRIVATE

    ${GTKMM_LIBRARY_DIRS}
    ${CURL_LIBRARY_DIRS}
    ${CAIRO_LIBRARY_DIRS}
)

target_link_libraries(logcollector PRIVATE

    ${GTKMM_LIBRARIES}
    ${CURL_LIBRARIES}
    ${CAIRO_LIBRARIES}

    nlohmann_json::nlohmann_json
)