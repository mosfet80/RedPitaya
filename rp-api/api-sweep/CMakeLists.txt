cmake_minimum_required(VERSION 3.14)
project(rp-sweep)

option(BUILD_SHARED "Builds shared library" ON)
option(BUILD_STATIC "Builds static library" ON)
option(BUILD_PYTHON_MODULE "Builds python module" ON)

option(IS_INSTALL "Install library" ON)
option(BUILD_DOC "Build documentation" ON)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/output)
set(CMAKE_C_COMPILER "gcc")
set(CMAKE_CXX_COMPILER "g++")
set(CMAKE_CXX_STANDARD 17)
set(C_STANDARD 17)


if(NOT DEFINED INSTALL_DIR)
    message(WARNING,"Installation path not set. Installation will be skipped")
    set(IS_INSTALL OFF)
endif()

if(NOT DEFINED DEBUG_REG)
  set(DEBUG_REG OFF)
endif()


if(NOT DEFINED VERSION)
  set(VERSION 0.00-0000)
endif()

if(NOT DEFINED REVISION)
  set(REVISION devbuild)
endif()


message(STATUS "VERSION=${VERSION}")
message(STATUS "REVISION=${REVISION}")
message(STATUS "Install path ${INSTALL_DIR}")

message(STATUS "Compiler C path: ${CMAKE_C_COMPILER}")
message(STATUS "Compiler C ID: ${CMAKE_C_COMPILER_ID}")
message(STATUS "Compiler C version: ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "Compiler C is part: ${CMAKE_COMPILER_IS_GNUC}")

message(STATUS "Compiler C++ path: ${CMAKE_CXX_COMPILER}")
message(STATUS "Compiler C++ ID: ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "Compiler C++version: ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Compiler C++ is part: ${CMAKE_COMPILER_IS_GNUCXX}")

list(APPEND r_paths
    /opt/redpitaya/lib
    /opt/redpitaya/lib/web
)

set(CMAKE_INSTALL_RPATH ${r_paths})

include_directories("include")
include_directories("src")
include_directories(${INSTALL_DIR}/include)
include_directories(${INSTALL_DIR}/include/common)
link_directories(${INSTALL_DIR}/lib)

list(APPEND src
    ${CMAKE_SOURCE_DIR}/src/rp_sweep.cpp
)

list(APPEND header
    ${CMAKE_SOURCE_DIR}/src/rp_sweep.h
)

add_compile_options(-mcpu=cortex-a9 -mfpu=neon-fp16 -fPIC)
add_compile_definitions(ARCH_ARM)
add_compile_options(-Wall -pedantic -Wextra -Wno-unused-parameter -DVERSION=${VERSION} -DREVISION=${REVISION} $<$<CONFIG:Debug>:-g3> $<$<CONFIG:Release>:-Os> -ffunction-sections -fdata-sections)

add_library(${PROJECT_NAME}-obj OBJECT ${src})


if(BUILD_SHARED)

    if (BUILD_PYTHON_MODULE)
        find_package(SWIG REQUIRED)
        INCLUDE(${SWIG_USE_FILE})

        find_package(PythonLibs)
        include_directories(${PYTHON_INCLUDE_PATH})

        set_property(SOURCE src/rp_sweep.i PROPERTY CPLUSPLUS ON)
        # SET_SOURCE_FILES_PROPERTIES(src/rp_sweep.i PROPERTIES SWIG_FLAGS "-includeall")

        SWIG_ADD_LIBRARY(rp_sweep_py LANGUAGE python SOURCES src/rp_sweep.i ${src})
        SWIG_LINK_LIBRARIES(rp_sweep_py ${PYTHON_LIBRARIES} rp rp-hw-calib rp-hw-profiles rp-i2c rp-spi rp-gpio rp-hw i2c -lm -lpthread)
     endif()

    add_library(${PROJECT_NAME}-shared SHARED)
    set_property(TARGET ${PROJECT_NAME}-shared PROPERTY OUTPUT_NAME ${PROJECT_NAME})
    target_sources(${PROJECT_NAME}-shared PRIVATE $<TARGET_OBJECTS:${PROJECT_NAME}-obj>)
    target_link_libraries(${PROJECT_NAME}-shared rp -lm -lpthread)

    if(IS_INSTALL)
        install(TARGETS ${PROJECT_NAME}-shared
            LIBRARY DESTINATION ${INSTALL_DIR}/lib
            ARCHIVE DESTINATION ${INSTALL_DIR}/lib)

        install(FILES ${header}
            DESTINATION ${INSTALL_DIR}/include/common)

        if (BUILD_PYTHON_MODULE)
            install(FILES $<TARGET_PROPERTY:rp_sweep_py,SWIG_SUPPORT_FILES> DESTINATION ${INSTALL_DIR}/lib/python)
            install(TARGETS rp_sweep_py
                LIBRARY DESTINATION ${INSTALL_DIR}/lib/python
                ARCHIVE DESTINATION ${INSTALL_DIR}/lib/python)
            install(FILES tests/rp_sweep_test.py tests/rp_sweep_func_test.py
                DESTINATION ${INSTALL_DIR}/lib/python PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
                GROUP_EXECUTE GROUP_READ WORLD_READ WORLD_WRITE WORLD_EXECUTE)
        endif()
    endif()
endif()


if(BUILD_STATIC)
    add_library(${PROJECT_NAME}-static STATIC)
    set_property(TARGET ${PROJECT_NAME}-static PROPERTY OUTPUT_NAME ${PROJECT_NAME})
    target_sources(${PROJECT_NAME}-static PRIVATE $<TARGET_OBJECTS:${PROJECT_NAME}-obj>)
    target_link_libraries(${PROJECT_NAME}-static -lm -lpthread)

    if(IS_INSTALL)
        install(TARGETS ${PROJECT_NAME}-static
            LIBRARY DESTINATION ${INSTALL_DIR}/lib
            ARCHIVE DESTINATION ${INSTALL_DIR}/lib)

        install(FILES ${header}
            DESTINATION ${INSTALL_DIR}/include/common)
    endif()
endif()

unset(INSTALL_DIR CACHE)