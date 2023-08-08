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

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/base/trace_event/common)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/build)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/buildtools)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/googletest/src)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/icu)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/third_party/zlib)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/tools/clang)

ExternalProject_Add(
  build
  SOURCE_DIR	 ${CMAKE_SOURCE_DIR}/build	
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/build.git
  GIT_TAG        a56852668f892151fc97343b57af15e82db2e798
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  buildtools 
  SOURCE_DIR	 ${CMAKE_SOURCE_DIR}/buildtools
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/buildtools.git
  GIT_TAG        8b16338d17cd71b04a6ba28da7322ab6739892c2
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  clang 
  SOURCE_DIR 	 ${CMAKE_SOURCE_DIR}/tools/clang
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/tools/clang.git
  GIT_TAG        4e79fda976d9c3f12c8bfbec297bd27dd70a1343
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  common 
  SOURCE_DIR	 ${CMAKE_SOURCE_DIR}/base/trace_event/common
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/base/trace_event/common.git
  GIT_TAG        d115b033c4e53666b535cbd1985ffe60badad082
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  googletest
  SOURCE_DIR	 ${CMAKE_SOURCE_DIR}/third_party/googletest/src
  GIT_REPOSITORY https://chromium.googlesource.com/external/github.com/google/googletest.git
  GIT_TAG        af29db7ec28d6df1c7f0f745186884091e602e07
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  icu 
  SOURCE_DIR	  ${CMAKE_SOURCE_DIR}/third_party/icu
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/deps/icu.git
  GIT_TAG        bf58678f110d1d1fb8765627e5a35fdc2fbe1ff6 
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  zlib 
  SOURCE_DIR     ${CMAKE_SOURCE_DIR}/third_party/zlib
  GIT_REPOSITORY https://chromium.googlesource.com/chromium/src/third_party/zlib.git
  GIT_TAG        64bbf988543996eb8df9a86877b32917187eba8f
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

ExternalProject_Add(
  cheribsd-ports 
  SOURCE_DIR	 cheribsd-ports
  GIT_REPOSITORY https://github.com/gcjenkinson/cheribsd-ports.git
  GIT_TAG        v8_dependencies 
  GIT_SHALLOW	 true
  CONFIGURE_COMMAND ""
  BUILD_COMMAND ""
  INSTALL_COMMAND ""
)

add_custom_target(apply-patches ALL
	COMMAND find ${CMAKE_BINARY_DIR}/cheribsd-ports/lang/v8/files -type f -name 'patch-*' -print0 | xargs -t -0 -n 1 patch -p0 -u -N -d ${CMAKE_SOURCE_DIR} -i || true)

add_custom_target(apply-cheribsd-patche ALL
	COMMAND patch -p0 -u -N -d ${CMAKE_SOURCE_DIR} < ${CMAKE_BINARY_DIR}/cheribsd-ports/lang/v8/files/cheribsd.patch || true)

add_custom_target(gcc-toolchain ALL
	COMMAND echo 'checkout_google_benchmark = false' > ${CMAKE_SOURCE_DIR}/build/config/gclient_args.gni)

add_custom_target(checkout-google-benchmark ALL
	COMMAND sed -i '' 's|%%LOCALBASE%%|/usr/local|' ${CMAKE_SOURCE_DIR}/build/toolchain/gcc_toolchain.gni ${CMAKE_SOURCE_DIR}/buildtools/third_party/libc++/BUILD.gn)

add_custom_target(replace-icu ALL
	COMMAND python3 ${CMAKE_SOURCE_DIR}/build/linux/unbundle/replace_gn_files.py --system-libraries icu)

file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/out/Release/gen/shim_headers/icui18n_shim/third_party/icu/source/i18n/unicode)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/out/Release/gen/shim_headers/icuuc_shim/third_party/icu/source/common/unicode)
file(MAKE_DIRECTORY ${CMAKE_SOURCE_DIR}/out/Release/gen/include)

add_custom_target(cp-icu ALL
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMAND cp -R /usr/local64/include/unicode ${CMAKE_SOURCE_DIR}/out/Release/gen/include/unicode)

add_custom_target(generate-ninja ALL
	WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
	COMMAND gn gen out/Release)
