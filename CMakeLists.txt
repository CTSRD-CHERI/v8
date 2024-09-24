# install devel/glib20
# pkg64 install devel/py-Jinja2
# pkg64c install devel/icu
cmake_minimum_required(VERSION 3.24)
include(ExternalProject)

project(v8)

find_package(
  Python3
  REQUIRED
  COMPONENTS Interpreter
)

if (NO_CHERI MATCHES "yes")
  message(STATUS "Configuring for non-CHERI")
endif()

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/base/trace_event/common)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/buildtools)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/googletest/src)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/icu)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/zlib)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/clang)

ExternalProject_Add(
  build
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/build
  GIT_REPOSITORY https://github.com/chromium-cheri/build.git
  GIT_TAG        9a86e31a61f262071edd37ac24e05515c2764fd3
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  buildtools 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/buildtools
  GIT_REPOSITORY https://github.com/chromium-cheri/buildtools.git
  GIT_TAG        8b16338d17cd71b04a6ba28da7322ab6739892c2
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  clang 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/tools/clang
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/tools/clang.git
  GIT_TAG        4ee099ac1c0d6e86e53cedfdcfd7cd2d45e126ca
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  common 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/base/trace_event/common
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/base/trace_event/common.git
  GIT_TAG        147f65333c38ddd1ebf554e89965c243c8ce50b3
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  googletest
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/third_party/googletest/src
  GIT_REPOSITORY https://chromium.googlesource.com/external/github.com/google/googletest.git
  GIT_TAG        af29db7ec28d6df1c7f0f745186884091e602e07
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  icu 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/third_party/icu
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/deps/icu.git
  GIT_TAG        a2961dc659b4ae847a9c6120718cc2517ee57d9e
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  zlib 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/third_party/zlib
  GIT_REPOSITORY https://github.com/chromium-cheri/zlib.git
  GIT_TAG        05ca39ba1b282fc3ed9e5e3f0ebfadafaa25ef21
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

add_custom_target(gcc-toolchain ALL
  COMMAND echo 'checkout_google_benchmark = false' > ${CMAKE_SOURCE_DIR}/build/config/gclient_args.gni)

add_custom_target(checkout-google-benchmark ALL
  COMMAND sed -i '' 's|%%LOCALBASE%%|/usr/local|' ${CMAKE_SOURCE_DIR}/build/toolchain/gcc_toolchain.gni ${CMAKE_SOURCE_DIR}/buildtools/third_party/libc++/BUILD.gn)

if (NO_CHERI MATCHES "yes")
  if (UNCOMPRESSED MATCHES "yes")
    set(V8_TARGET_DIRECTORY out/flat.uncompressed.release)
    set(V8_GN_FILE cheri/flat-uncompressed.gn)
  else()
    set(V8_TARGET_DIRECTORY out/flat.compressed.release)
    set(V8_GN_FILE cheri/flat-compressed.gn)
  endif()
else()
  if (UNCOMPRESSED MATCHES "yes")
    set(V8_TARGET_DIRECTORY out/cheri.uncompressed.release)
    set(V8_GN_FILE cheri/cheri-uncompressed.gn)
  else()
    set(V8_TARGET_DIRECTORY out/cheri.compressed.release)
    set(V8_GN_FILE cheri/cheri-compressed.gn)
  endif()
endif()


add_custom_target(mkdir-target ALL
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMAND mkdir -p ${V8_TARGET_DIRECTORY})

add_custom_target(cp-gn-file ALL
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMAND cp ${V8_GN_FILE} ${V8_TARGET_DIRECTORY}/args.gn)

add_custom_target(generate-ninja ALL
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
  COMMAND gn gen ${V8_TARGET_DIRECTORY})
