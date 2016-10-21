cmake_minimum_required(VERSION 2.8.11)
project(PDOPUS C)

add_subdirectory(opus)

set(OPUS_DIR ${PDOPUS_SOURCE_DIR}/opus/opus)

include_directories(${OPUS_DIR}/include ${OPUS_DIR}/src ${OPUS_DIR}/celt ${OPUS_DIR}/silk)

add_library(opusenc SHARED opusenc~.c)

target_link_libraries(opusenc PRIVATE ${PDOPUS_SOURCE_DIR}/opus/libopus.a)

set(CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS "${CMAKE_SHARED_LIBRARY_CREATE_C_FLAGS} -undefined dynamic_lookup")

set_target_properties(opusenc PROPERTIES OUTPUT_NAME "opusenc~" PREFIX "" SUFFIX ".pd_darwin")
