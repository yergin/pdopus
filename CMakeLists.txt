cmake_minimum_required(VERSION 2.8.12)
project(PDOPUS C)

add_subdirectory(opus)

set(OPUS_DIR ${PDOPUS_SOURCE_DIR}/opus/opus)

include_directories(${OPUS_DIR}/include ${OPUS_DIR}/src ${OPUS_DIR}/celt ${OPUS_DIR}/silk)

set(CMAKE_MACOSX_RPATH 1)

add_library(opusenc SHARED opusenc~.c)
add_library(opusdec SHARED opusdec~.c)

target_link_libraries(opusenc PRIVATE ${PDOPUS_SOURCE_DIR}/opus/libopus.a)
target_link_libraries(opusdec PRIVATE ${PDOPUS_SOURCE_DIR}/opus/libopus.a)

set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS} -undefined dynamic_lookup")

set_target_properties(opusenc PROPERTIES OUTPUT_NAME "opusenc~" PREFIX "" SUFFIX ".pd_darwin")
set_target_properties(opusdec PROPERTIES OUTPUT_NAME "opusdec~" PREFIX "" SUFFIX ".pd_darwin")
