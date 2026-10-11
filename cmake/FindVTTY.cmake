# - try to find the libvtty library
#
# Cache Variables: (probably not for direct use in your scripts)
#  VTTY_INCLUDE_DIR
#  VTTY_LIBRARY
#
# Non-cache variables you might use in your CMakeLists.txt:
#  VTTY_FOUND
#  VTTY_INCLUDE_DIRS
#  VTTY_LIBRARIES
#
# Requires these CMake modules:
#  FindPackageHandleStandardArgs (known included with CMake >=2.6.2)
#
# Original Author:
# 2009-2010 Ryan Pavlik <rpavlik@iastate.edu> <abiryan@ryand.net>
# http://academic.cleardefinition.com
# Iowa State University HCI Graduate Program/VRAC
#
# Copyright Iowa State University 2009-2010.
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

find_library(VTTY_LIBRARY
    NAMES vtty)

find_path(VTTY_INCLUDE_DIR
    NAMES vtty.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VTTY
    DEFAULT_MSG
    VTTY_LIBRARY
    VTTY_INCLUDE_DIR)

if(VTTY_FOUND)
    set(VTTY_LIBRARIES "${VTTY_LIBRARY}")

    set(VTTY_INCLUDE_DIRS "${VTTY_INCLUDE_DIR}")
endif()

