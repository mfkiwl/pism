cmake_minimum_required (VERSION 3.16)

# Get PISM's "short" X.Y.Z version from a file
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION)
  file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/VERSION Pism_VERSION LIMIT_COUNT 1)
else()
  message(FATAL_ERROR "'VERSION' not found: cannot determine PISM's version")
endif()

project (Pism
  VERSION ${Pism_VERSION}
  HOMEPAGE_URL "https://www.pism.io"
  LANGUAGES C CXX)

include ("CMake/PISM_CMake_macros.cmake")
list (APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/CMake")
pism_set_full_version()

include (GNUInstallDirs)
# doc directory: override capitalization ("pism" instead of "Pism" from the "project(...)"
# call above)
set(CMAKE_INSTALL_DOCDIR ${CMAKE_INSTALL_DATAROOTDIR}/doc/pism)

# Deal with build types
mark_as_advanced(CLEAR CMAKE_BUILD_TYPE)
# Set the build type to "Release" so that users get optimized code by default:
#
# This code block comes from the Kokkos project.
if (NOT CMAKE_BUILD_TYPE)
  set(DEFAULT_BUILD_TYPE "Release")
  message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
  set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE STRING
    "Choose the type of build, options are: Debug, Release, RelWithDebInfo and MinSizeRel."
    FORCE)
endif()

# ``find_package()`` uses ``<PackageName>_ROOT`` variables.
cmake_policy(SET CMP0074 NEW)

# Require C++11 compiler support.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Looks like CMAKE_CXX_STANDARD does not support Intel C++ compilers
# yet...
if (CMAKE_CXX_COMPILER_ID MATCHES "Intel" AND NOT CMAKE_CXX_FLAGS MATCHES "-std=c\\+\\+11")
  message (STATUS "Adding -std=c++11 to C++ compiler flags for Intel compilers.")
  set (CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS}" CACHE STRING "C++ compiler flags" FORCE)
endif()

# Add a flag that makes it easier to use LLDB to debug code compiled with Clang:
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  add_compile_options(-fstandalone-debug)
endif()

# Put executables in the build directory:
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_BINARY_DIR})

set(CMAKE_INSTALL_MESSAGE "LAZY")
set(CMAKE_COLOR_MAKEFILE OFF)

# Set Pism_CONFIG_FILE (*after* we set the CMAKE_INSTALL_PREFIX above).
pism_check_build_dir_location()
set (Pism_CONFIG_FILE "${CMAKE_INSTALL_FULL_DATADIR}/pism/pism_config.nc" CACHE STRING "" FORCE)
mark_as_advanced (Pism_CONFIG_FILE)
file (WRITE ${PROJECT_BINARY_DIR}/.petscrc "-config ${PROJECT_BINARY_DIR}/pism_config.nc")

# The default options cache
option (Pism_BUILD_EXTRA_EXECS "Build extra executables (mostly testing/verification)" OFF)
option (BUILD_SHARED_LIBS      "Build shared Pism libraries" ON)
option (Pism_DEBIAN_SYSTEMWIDE "Use settings appropriate when installing in a system-wide location on Debian/Ubuntu or building a package" OFF)
option (Pism_BUILD_PYTHON_BINDINGS "Build python bindings" OFF)
option (Pism_BUILD_ICEBIN "Build PISM portions of IceBin library" OFF)
option (Pism_BUILD_DOCS "Build PISM's documentation with 'make all'." OFF)
option (Pism_USE_PROJ "Use PROJ to compute longitudes and latitudes." OFF)
option (Pism_USE_YAC_INTERPOLATION "Use YAC and PROJ for interpolation" OFF)
option (Pism_USE_PARALLEL_NETCDF4 "Enables parallel NetCDF-4 I/O." OFF)
option (Pism_USE_PNETCDF "Enables parallel NetCDF-3 I/O using PnetCDF." OFF)
option (Pism_ENABLE_DOCUMENTATION "Enable targets building PISM's documentation." ON)

# PISM will eventually use Jansson to read configuration files.
# set (Pism_USE_JANSSON OFF)
option (Pism_USE_JANSSON "Use Jansson to read configuration files." OFF)

option (Pism_TEST_USING_VALGRIND "Add extra regression tests using valgrind" OFF)
mark_as_advanced (Pism_TEST_USING_VALGRIND)

option (Pism_PKG_CONFIG_STATIC "Use the --static pkg-config flag to get linker flags needed by prerequisites" OFF)
option (Pism_ADD_FPIC "Add -fPIC to C & C++ compiler flags. Try turning it off if it does not work." ON)
option (Pism_CODE_COVERAGE "Add compiler options for code coverage testing." OFF)
option (Pism_LINK_STATICALLY "Set CMake flags to try to ensure that everything is linked statically")
option (Pism_LOOK_FOR_LIBRARIES "Specifies whether PISM should look for libraries. (Disable this on Crays.)" ON)
option (Pism_USE_EVERYTRACE "Use the Everytrace library to provide stacktraces on crashes." OFF)
option (Pism_DEBUG "Enables extra checks in the code." OFF)
option (Pism_PEDANTIC_WARNINGS  "Compile with pedantic warnings." ON)
option (Pism_GPROF_FLAGS        "Add flags necessary to profile with gprof." OFF)

# Use rpath by default; this has to go first, because rpath settings may be overridden later.
if (Pism_DEBIAN_SYSTEMWIDE)
  pism_dont_use_rpath()
else()
  pism_use_rpath()
endif()

if (Pism_LINK_STATICALLY)
  pism_strictly_static()
endif ()

# Add -fPIC to C and CXX flags.
if (Pism_ADD_FPIC)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif ()

if (Pism_CODE_COVERAGE)
  add_compile_options(-fprofile-arcs -ftest-coverage -g -O0)
  add_link_options(--coverage)

  add_custom_target (coverage_report
    # remove coverage data from src/pythonbindings
    COMMAND lcov --directory ${Pism_BINARY_DIR}/src/pythonbindings -z
    COMMAND lcov --directory ${Pism_BINARY_DIR}/src/external -z
    COMMAND lcov --base-directory ${Pism_SOURCE_DIR} --directory src --quiet --no-external --capture -o pism-coverage.info
    COMMAND genhtml -t "PISM Coverage report" -o cover --demangle-cpp --legend pism-coverage.info
    WORKING_DIRECTORY ${Pism_BINARY_DIR}
    VERBATIM
    )
  add_custom_target (coverage_reset
    COMMAND lcov -d ${Pism_BINARY_DIR} -z
    WORKING_DIRECTORY ${Pism_BINARY_DIR}
    VERBATIM
    )
endif ()

if (Pism_PEDANTIC_WARNINGS)
  pism_set_pedantic_flags()
endif (Pism_PEDANTIC_WARNINGS)

if (Pism_GPROF_FLAGS)
  set (CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -pg -fno-omit-frame-pointer -fno-inline-functions -fno-inline-functions-called-once -fno-optimize-sibling-calls")
  set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pg -fno-omit-frame-pointer -fno-inline-functions -fno-inline-functions-called-once -fno-optimize-sibling-calls")
endif ()

# Look for libraries using find_package(...), etc. Run CMake with -DPism_LOOK_FOR_LIBRARIES=OFF
# to build on systems that rely on the module system to set all compiler and linker flags.
if (Pism_LOOK_FOR_LIBRARIES)
  pism_find_prerequisites()
endif()

# Set include directories.
pism_set_dependencies()

if (Pism_USE_EVERYTRACE)
  find_package(Everytrace REQUIRED)
endif()

if (Pism_BUILD_PYTHON_BINDINGS)
  find_package(Python3 COMPONENTS Interpreter Development)
  find_package(PETSc4Py REQUIRED)
  find_package(SWIG REQUIRED)

  if (DEFINED PETSC4PY_VERSION)
    # FindPETSc4Py.cmake does not put PETSC4PY_VERSION into the CMake cache,
    # so we save it here.
    set(Pism_PETSC4PY_VERSION ${PETSC4PY_VERSION} CACHE STRING "PETSc4Py version")
    mark_as_advanced(Pism_PETSC4PY_VERSION)
  endif()

  mark_as_advanced (SWIG_DIR SWIG_EXECUTABLE SWIG_VERSION)

  set (pism_python_path_py "${CMAKE_CURRENT_BINARY_DIR}/pism_python_path.py")
  if (Pism_DEBIAN_SYSTEMWIDE)
    set (pism_sysconfig_scheme "deb_system")
  else()
    set (pism_sysconfig_scheme "posix_prefix")
  endif()
  file (WRITE "${pism_python_path_py}" "
# Get module installation path from Python:
import sysconfig
print(sysconfig.get_path('platlib', '${pism_sysconfig_scheme}', vars={'platbase': '${CMAKE_INSTALL_PREFIX}'}))
")
  unset(pism_sysconfig_scheme)

  execute_process(
    COMMAND ${Python3_EXECUTABLE} ${pism_python_path_py}
    OUTPUT_VARIABLE PISM_FULL_PYTHON_MODULE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  # convert to a path relative to the install prefix
  file(RELATIVE_PATH PISM_PYTHON_MODULE_DIR
    ${CMAKE_INSTALL_PREFIX} ${PISM_FULL_PYTHON_MODULE_DIR})
  unset(pism_python_path_py)
  set(PISM_PYTHON_MODULE_DIR ${PISM_PYTHON_MODULE_DIR}
    CACHE PATH "Python extension module installation directory." )
  set(PISM_FULL_PYTHON_MODULE_DIR ${PISM_FULL_PYTHON_MODULE_DIR}
    CACHE PATH "Python extension module installation directory (full path)." )
  mark_as_advanced(PISM_PYTHON_MODULE_DIR PISM_FULL_PYTHON_MODULE_DIR)
endif ()

# re-run tests that failed
add_custom_target (retest
  COMMAND ${CMAKE_CTEST_COMMAND} --rerun-failed --output-on-failure
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  VERBATIM
)

# run Python tests
add_custom_target (test-python
  COMMAND ${CMAKE_CTEST_COMMAND} -R "Python:"
  WORKING_DIRECTORY ${PROJECT_BINARY_DIR}
  VERBATIM
)

# Install helper scripts residing in util/
install(PROGRAMS
  util/pism_adjust_timeline
  util/pism_check_stationarity
  util/pism_create_timeline
  util/pism_fill_missing_petsc
  util/pism_fill_missing
  util/pism_flowline
  util/pism_nc2cdo
  util/pism_nccmp
  util/pism_plot_profiling
  DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY ${PROJECT_SOURCE_DIR}/examples
  DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/pism
  DIRECTORY_PERMISSIONS
  OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE GROUP_READ WORLD_EXECUTE WORLD_READ
  PATTERN .gitignore EXCLUDE
  PATTERN __pycache__ EXCLUDE
  PATTERN "*.pyc" EXCLUDE
  PATTERN "*.nc" EXCLUDE
  PATTERN "*.npz" EXCLUDE
  PATTERN "*~" EXCLUDE)

add_subdirectory (src)
add_subdirectory (site-packages)

if (Pism_ENABLE_DOCUMENTATION)
  if (Pism_BUILD_DOCS)
    add_subdirectory (doc)
  else()
    add_subdirectory (doc EXCLUDE_FROM_ALL)
  endif()
endif()

# PISM regression testing
ENABLE_TESTING()
include(CTest)

if (CMAKE_VERSION VERSION_GREATER 3.16)
  set (CMAKE_CTEST_ARGUMENTS "--output-on-failure")
endif()
add_subdirectory (test)
add_subdirectory (test/regression)

add_subdirectory (docker)

add_subdirectory (package)
