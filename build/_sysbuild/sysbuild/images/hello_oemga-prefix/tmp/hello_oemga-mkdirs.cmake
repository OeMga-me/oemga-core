# Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
# file LICENSE.rst or https://cmake.org/licensing for details.

cmake_minimum_required(VERSION ${CMAKE_VERSION}) # this file comes with cmake

# If CMAKE_DISABLE_SOURCE_CHANGES is set to true and the source directory is an
# existing directory in our source tree, calling file(MAKE_DIRECTORY) on it
# would cause a fatal error, even though it would be a no-op.
if(NOT EXISTS "/workdir/oemga-core/apps/hello_oemga")
  file(MAKE_DIRECTORY "/workdir/oemga-core/apps/hello_oemga")
endif()
file(MAKE_DIRECTORY
  "/workdir/oemga-core/build/hello_oemga"
  "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix"
  "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/tmp"
  "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/src/hello_oemga-stamp"
  "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/src"
  "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/src/hello_oemga-stamp"
)

set(configSubDirs )
foreach(subDir IN LISTS configSubDirs)
    file(MAKE_DIRECTORY "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/src/hello_oemga-stamp/${subDir}")
endforeach()
if(cfgdir)
  file(MAKE_DIRECTORY "/workdir/oemga-core/build/_sysbuild/sysbuild/images/hello_oemga-prefix/src/hello_oemga-stamp${cfgdir}") # cfgdir has leading slash
endif()
