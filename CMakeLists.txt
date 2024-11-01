cmake_minimum_required(VERSION 3.5)
project(hid_test C CXX)

set(CMAKE_CXX_FLAGS "${CMAKE_C_FLAGS} -w -pthread -lrt --std=c++11")

include_directories(${CMAKE_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/lib)

add_executable(hid_host_test hid_host_test.cpp report_item.c report_usage.c)
target_link_libraries(hid_host_test hidapi-hidraw usb-1.0 usbgx)

add_executable(hid_device_test hid_device_test.cpp)
target_link_libraries(hid_device_test hidapi-hidraw usb-1.0 usbgx)

add_executable(storage_device_test storage_device_test.cpp)
target_link_libraries(storage_device_test usbgx)

add_executable(uac_device_test uac_device_test.cpp)
target_link_libraries(uac_device_test usbgx)