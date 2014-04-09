# - Try to find the HeeksCAD  library
#
# Once done, this will define
#  HeeksCAD_FOUND - true if HeeksCAD has been found
#  HeeksCAD_INCLUDE_DIRS - the HeeksCAD include directory
#  HeeksCAD_LIBRARIES - The libraries needed to use PC/SC
#
# Author: Romuald Conty <neomilium@gmail.com>
# Version: 20140409
#

IF(NOT HeeksCAD_FOUND)
   # Will try to find at standard locations
   FIND_PATH(HeeksCAD_INCLUDE_DIRS src/Geom.h PATH_SUFFIXES heekscad)
ENDIF(NOT HeeksCAD_FOUND)

MESSAGE( STATUS "HeeksCAD_INCLUDE_DIRS:     " ${HeeksCAD_INCLUDE_DIRS} )
MESSAGE( STATUS "HeeksCAD_LIBRARIES:     " ${HeeksCAD_LIBRARIES} )

IF( HeeksCAD_INCLUDE_DIRS STREQUAL HeeksCAD_INCLUDE_DIRS-NOTFOUND )
  # try to find at ./heekscad/includes location
  FIND_PATH( HeeksCAD_INCLUDE_DIRS src/Geom.h PATHS "${CMAKE_SOURCE_DIR}/heekscad/include" "../../" DOC "Path to HeeksCAD includes" )
  IF( HeeksCAD_INCLUDE_DIRS STREQUAL Geom.h-NOTFOUND )
    MESSAGE( FATAL_ERROR "Cannot find HeeksCAD include dir." )
   ENDIF( HeeksCAD_INCLUDE_DIRS STREQUAL Geom.h-NOTFOUND )
ENDIF(HeeksCAD_INCLUDE_DIRS STREQUAL HeeksCAD_INCLUDE_DIRS-NOTFOUND )

MESSAGE( STATUS "HeeksCAD_INCLUDE_DIRS:     " ${HeeksCAD_INCLUDE_DIRS} )
MESSAGE( STATUS "HeeksCAD_LIBRARIES:     " ${HeeksCAD_LIBRARIES} )
