cmake_minimum_required(VERSION 3.10)
project(DistributedKeyValueStore LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find the Protobuf package
find_package(Protobuf REQUIRED)

# Specify the output directory for the generated files
set(PROTOBUF_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/protobufs/generated")
file(MAKE_DIRECTORY ${PROTOBUF_GEN_DIR})

# Add a custom command to generate the C++ files from the .proto file
add_custom_command(
    OUTPUT "${PROTOBUF_GEN_DIR}/dkvs.pb.cc" "${PROTOBUF_GEN_DIR}/dkvs.pb.h"
    COMMAND protoc --cpp_out=${PROTOBUF_GEN_DIR} --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/protobufs ${CMAKE_CURRENT_SOURCE_DIR}/protobufs/dkvs.proto
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/protobufs/dkvs.proto
)

# Add the generated source file to a variable
set(PROTO_SOURCES "${PROTOBUF_GEN_DIR}/dkvs.pb.cc")

# Include the directory with the generated header file
include_directories(${PROTOBUF_GEN_DIR})

# Add executables and link against the Protobuf libraries
add_executable(Server server.cpp threadpool.cpp hashring.cpp ${PROTO_SOURCES})
target_link_libraries(Server PRIVATE ${Protobuf_LIBRARIES})

add_executable(Client client.cpp threadpool.cpp hashring.cpp ${PROTO_SOURCES})
target_link_libraries(Client PRIVATE ${Protobuf_LIBRARIES})