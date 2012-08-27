SET(MySQL_DIR .)

# locate cmyth
find_library(CMYTH_LIBRARIES NAMES cmyth /usr/local/lib /opt/local/lib)
find_library(REFMEM_LIBRARIES NAMES refmem /usr/local/lib /opt/local/lib)
if (CMYTH_LIBRARIES AND REFMEM_LIBRARIES)
	MESSAGE(STATUS "Found cmyth: ${CMYTH_LIBRARIES}")
ELSE (LIBUSB_LIBRARIES AND REFMEM_LIBRARIES)
	MESSAGE(FATAL_ERROR "Could not find cmyth")
ENDIF (CMYTH_LIBRARIES AND REFMEM_LIBRARIES)
find_package(MySQL REQUIRED)
# locate GST
find_package(PkgConfig)
pkg_check_modules(GST REQUIRED gstreamer-0.10)
pkg_check_modules(GST_BASE REQUIRED gstreamer-base-0.10)

add_definitions(-DVERSION=0 -DPACKAGE=\"MythTV\")

include_directories(${MYSQL_INCLUDE_DIRS} ${GST_INCLUDE_DIRS} ${GST_BASE_INCLUDE_DIRS})
add_library(gstmythtvsrc SHARED gstmythtvsrc.c)
install(TARGETS gstmythtvsrc DESTINATION lib/gstreamer-0.10)
target_link_libraries(gstmythtvsrc ${CMYTH_LIBRARIES} ${REFMEM_LIBRARIES} ${MYSQL_LIBRARIES} ${GST_LIBRARIES} ${GST_BASE_LIBRARIES})
