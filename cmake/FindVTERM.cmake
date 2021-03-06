# - try to find VIPER library
#
# Cache Variables: (probably not for direct use in your scripts)
#  VIPER_INCLUDE_DIR
#  VIPER_LIBRARY
#
# Non-cache variables you might use in your CMakeLists.txt:
#  VIPER_FOUND
#  VIPER_INCLUDE_DIRS
#  VIPER_LIBRARIES
#
# Requires these CMake modules:
#  FindPackageHandleStandardArgs (known included with CMake >=2.6.2)
#
# Derived from FindGPM.cmake
# Original Author:
# 2009-2010 Ryan Pavlik <rpavlik@iastate.edu> <abiryan@ryand.net>
# http://academic.cleardefinition.com
# Iowa State University HCI Graduate Program/VRAC
#
# Copyright Iowa State University 2009-2010.
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)

find_library(VTERM_LIBRARY
    NAMES vterm)

find_path(VTERM_INCLUDE_DIR
    NAMES vterm.h)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(VTERM
    DEFAULT_MSG
    VTERM_LIBRARY
    VTERM_INCLUDE_DIR)

if(VTERM_FOUND)
    set(VTERM_LIBRARIES "${VTERM_LIBRARY}")

    set(VTERM_INCLUDE_DIRS "${VTERM_INCLUDE_DIR}")
endif()

