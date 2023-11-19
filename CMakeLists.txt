cmake_minimum_required(VERSION 3.14 FATAL_ERROR)

project(bn_workflow_acc VERSION 0.1.0 LANGUAGES CXX)

option(BN_API_PATH "Path to the Binary Ninja API")
if(NOT BN_API_PATH)
	message(FATAL_ERROR "Project option `BN_API_PATH` must be set.")
endif()

set(HEADLESS ON)
add_subdirectory(${BN_API_PATH} ${PROJECT_BINARY_DIR}/api)

add_library(workflow_acc SHARED plugin.cpp)
target_compile_features(workflow_acc PRIVATE cxx_std_17)
target_link_libraries(workflow_acc PRIVATE binaryninjaapi)

if(${CMAKE_SYSTEM_NAME} STREQUAL "LINUX")
	target_compile_options(workflow_acc PRIVATE "-fPIC")
endif()

bn_install_plugin(workflow_acc)
