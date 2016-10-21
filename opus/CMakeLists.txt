# CMakeLists files in this project can
# refer to the root source directory of the project as ${OPUS_SOURCE_DIR} and
# to the root binary directory of the project as ${OPUS_BINARY_DIR}.
cmake_minimum_required (VERSION 2.8.11)
project (OPUS C)

set(SRC_DIRS opus/src opus/celt opus/silk opus/silk/float)

add_definitions(-DOPUS_BUILD -DHAVE_LRINT -DHAVE_LRINTF -DVAR_ARRAYS -DOPUS_EXPORT=)

foreach(_dir ${SRC_DIRS})
  file(GLOB FILES ${_dir}/*.c ${_dir}/*.h)
  set(SRC_FILES ${SRC_FILES} ${FILES})
endforeach()

include_directories(opus/include ${OPUS_SOURCE_DIR}/opus)

add_library(opus ${SRC_FILES})
target_include_directories(opus PUBLIC ${SRC_DIRS})