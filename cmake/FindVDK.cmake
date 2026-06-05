find_library(VDK_LIBRARY
    NAMES vdk)

find_path(VDK_INCLUDE_DIR
    NAMES vdk.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VDK
    DEFAULT_MSG
    VDK_LIBRARY
    VDK_INCLUDE_DIR)

if(VDK_FOUND)
    set(VDK_LIBRARIES "${VDK_LIBRARY}")
    set(VDK_INCLUDE_DIRS "${VDK_INCLUDE_DIR}")
endif()
