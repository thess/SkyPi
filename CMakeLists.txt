cmake_minimum_required (VERSION 2.6)

project (SkyPi)

# The version number.
set (SkyPi_VERSION_MAJOR 0)
set (SkyPi_VERSION_MINOR 95)

# configure a header file to pass some of the CMake settings
# to the source code
configure_file (
  "${PROJECT_SOURCE_DIR}/Include/config.h.in"
  "${PROJECT_SOURCE_DIR}/Include/config.h"
 )

set (CMAKE_C_FLAGS "-g -O2 -Wall -s")

# Add include path
include_directories(./Include)

# Header files
set (HEADERS ./Include/SkyPi.h)

add_subdirectory(Lib)

add_executable(SkyPi SkyPi.c ${HEADERS})

target_link_libraries(SkyPi AstroFuncs PicasoSerial -lrt -lm)

# Installation rules
install(PROGRAMS ${CMAKE_BINARY_DIR}/SkyPi DESTINATION /usr/local/bin)
install(FILES ${CMAKE_SOURCE_DIR}/data/hyg11.csv DESTINATION /usr/local/lib/SkyPi
	RENAME starmap.csv)
