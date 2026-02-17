cmake_minimum_required(VERSION 3.20)
if(APPLE)
  project(testapp LANGUAGES CXX OBJCXX)
else()
  project(testapp LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

include(FetchContent)
FetchContent_Declare(
  simpleble
  GIT_REPOSITORY https://github.com/OpenBluetoothToolbox/SimpleBLE.git
  GIT_TAG main
)
FetchContent_GetProperties(simpleble)
if(NOT simpleble_POPULATED)
  FetchContent_Populate(simpleble)
  add_subdirectory(${simpleble_SOURCE_DIR}/simpleble ${simpleble_BINARY_DIR})
endif()

# Help CMake find SimpleBLE headers including generated ones
include_directories(${simpleble_SOURCE_DIR}/simpleble/include)
include_directories(${simpleble_BINARY_DIR}/export)

include_directories(
  src
  ${CMAKE_SOURCE_DIR}/include
)

if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0")
endif()

set(SRC_FILES
  src/JoyConDecoder.cpp
  src/MacBluetoothManager.cpp
  src/testapp.cpp
)

if(WIN32)
  # No extra files needed here yet
elseif(APPLE)
  list(APPEND SRC_FILES src/MacVirtualController.mm)
endif()

add_executable(testapp ${SRC_FILES})

if(WIN32)
    target_link_directories(testapp PRIVATE ${CMAKE_SOURCE_DIR}/lib)
    target_link_libraries(testapp
        PRIVATE
            setupapi
            hid
            ViGEmClient
            windowsapp
    )
elseif(APPLE)
    target_link_libraries(testapp
        PRIVATE
            simpleble
            "-framework IOBluetooth"
            "-framework CoreBluetooth"
            "-framework IOKit"
            "-framework Foundation"
            "-framework CoreGraphics"
    )
endif()

if(MSVC)
  target_compile_options(testapp PRIVATE /W3 /permissive-)
else()
  target_compile_options(testapp PRIVATE -Wall -Wextra -pedantic)
endif()
