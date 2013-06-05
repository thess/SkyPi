# Include path
include_directories(../Include)

set (HEADERS ../Include/SkyPi.h)

add_library(AstroFuncs Astro.c Vsop87.c ${HEADERS})

add_library(PicasoSerial Picaso_Serial_4DLibrary.c)
