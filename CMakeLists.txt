#此文件从模板自动生成! 请勿更改!
# author: Jetbrains & lan
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_VERSION 1)
cmake_minimum_required(VERSION 3.15)

# specify cross-compilers and tools
# set(STM32_TOOLCHAIN_PATH /opt/gcc-arm-none-eabi)
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_SOURCE_DIR}/cmake/stm32_gcc.cmake CACHE STRING "stm32 toolchain file")

# project settings
project(lora C CXX ASM)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD 17)
set(CMAKE_INCLUDE_CURRENT_DIR TRUE)

# set library config
set(HAL_COMP_LIST RCC GPIO CORTEX UART DMA SPI TIM CRC STM32F1)
set(CMSIS_COMP_LIST "STM32F103C8")
set(FREERTOS_COMP_LIST STM32F1 ARM_CM3)

# clang-format
if (NOT DEFINED CLANG_FORMAT_BIN)
    # attempt to find the binary if user did not specify
    find_program(CLANG_FORMAT_BIN
            NAMES clang-format clang-format-12
            HINTS ${CLANG_SEARCH_PATH})
endif()
if ("${CLANG_FORMAT_BIN}" STREQUAL "CLANG_FORMAT_BIN-NOTFOUND")
    message(WARNING "couldn't find clang-format.")
else()
    message(STATUS "found clang-format at ${CLANG_FORMAT_BIN}")
endif()

file(GLOB_RECURSE ALL_SOURCE_FILES Core/*.c Core/*.h Core/*.cpp tasks/*.c tasks/*.h tasks/*.cpp)
if(NOT DEFINED NO_CLANG_FORMAT)
    add_custom_target(clang_format ALL
            COMMAND ${CLANG_FORMAT_BIN} -i
            ${ALL_SOURCE_FILES}
    )
    message(STATUS "run clang-format on: ${ALL_SOURCE_FILES}")
endif()

# clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if (NOT DEFINED CLANG_TIDY_BIN)
    # attempt to find the binary if user did not specify
    find_program(CLANG_TIDY_BIN
            NAMES clang-tidy clang-tidy-12
            HINTS ${CLANG_SEARCH_PATH})
endif()
if ("${CLANG_TIDY_BIN}" STREQUAL "CLANG_TIDY_BIN-NOTFOUND")
    message(WARNING "couldn't find clang-tidy.")
else()
    message(STATUS "found clang-tidy at ${CLANG_TIDY_BIN}")
endif()

set(CMAKE_C_CLANG_TIDY ${CLANG_TIDY_BIN} --extra-arg=--sysroot=${STM32_TOOLCHAIN_PATH}/arm-none-eabi -checks=-clang-diagnostic-unused-command-line-argument)
set(CMAKE_CXX_CLANG_TIDY ${CLANG_TIDY_BIN} --extra-arg=--sysroot=${STM32_TOOLCHAIN_PATH}/arm-none-eabi -checks=-clang-diagnostic-unused-command-line-argument)

# find library
find_package(CMSIS COMPONENTS "${CMSIS_COMP_LIST}" REQUIRED)
find_package(HAL COMPONENTS "${HAL_COMP_LIST}" REQUIRED)
find_package(FreeRTOS COMPONENTS "${FREERTOS_COMP_LIST}" REQUIRED)

#Uncomment for hardware floating point
#add_compile_definitions(ARM_MATH_CM4;ARM_MATH_MATRIX_CHECK;ARM_MATH_ROUNDING)
#add_compile_options(-mfloat-abi=hard -mfpu=fpv4-sp-d16)
#add_link_options(-mfloat-abi=hard -mfpu=fpv4-sp-d16)

#Uncomment for software floating point
add_compile_options(-mfloat-abi=soft)

#add_compile_options(-mcpu=cortex-m3 -mthumb -mthumb-interwork)
add_compile_options(-mcpu=cortex-m3 -mthumb)
add_compile_options(-ffunction-sections -fdata-sections -fno-common -fmessage-length=0)

# disable some c++ features
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions -fno-rtti -fno-use-cxa-atexit")

# shrink newlib
add_definitions(-D_REENT_GLOBAL_ATEXIT -D_REENT_SMALL)

# uncomment to mitigate c++17 absolute addresses warnings
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-register")

# Enable assembler files preprocessing
add_compile_options($<$<COMPILE_LANGUAGE:ASM>:-x$<SEMICOLON>assembler-with-cpp>)

# warning
add_compile_options(-Wall -Wextra)
add_compile_options(-Wno-format)

# analysis
add_compile_options(-fstack-usage)

# enable flto
#add_compile_options(-flto)
#add_link_options(-flto)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "Release")
    message(STATUS "Maximum optimization for speed")
    add_compile_options(-Ofast)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RelWithDebInfo")
    message(STATUS "Maximum optimization for speed, debug info included")
    add_compile_options(-Ofast -g3)
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "MinSizeRel")
    message(STATUS "Maximum optimization for size")
    add_compile_options(-Os)
else ()
    message(STATUS "Minimal optimization, debug info included")
    add_compile_options(-Og -g3)
endif ()

set(includes ${includes} Core/Inc SX1278/include tasks/include)
list(FILTER includes EXCLUDE REGEX "Drivers/.*")
list(FILTER includes EXCLUDE REGEX "Middlewares/.*")
include_directories(${includes})

add_definitions(-DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB -DDEBUG -DUSE_HAL_DRIVER -DSTM32F103xB)

file(GLOB_RECURSE STM32_SOURCE "tasks/*.*" "SX1278/*.*" "Core/*.*" "Middlewares/*.*" "Drivers/*.*" "Core/*.*")
list(FILTER STM32_SOURCE EXCLUDE REGEX "Drivers/.*")
list(FILTER STM32_SOURCE EXCLUDE REGEX "Middlewares/.*")

set(LINKER_SCRIPT ${CMAKE_SOURCE_DIR}/STM32F103C8TX_FLASH.ld)

add_link_options(-Wl,-gc-sections,--print-memory-usage,-Map=${PROJECT_BINARY_DIR}/${PROJECT_NAME}.map)
add_link_options(-mcpu=cortex-m3 -mthumb -mthumb-interwork)
#add_link_options(-T ${LINKER_SCRIPT})

#add_executable(${PROJECT_NAME}.elf ${SOURCES} ${LINKER_SCRIPT})
add_executable(${PROJECT_NAME} ${STM32_SOURCE})
target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/Core/Inc)
target_link_libraries(${PROJECT_NAME} PRIVATE
    HAL::STM32::F1::RCC
    HAL::STM32::F1::GPIO
    HAL::STM32::F1::CORTEX
    HAL::STM32::F1::UART
    HAL::STM32::F1::DMA
    HAL::STM32::F1::SPI
    HAL::STM32::F1::TIMEx
    HAL::STM32::F1::CRC
    CMSIS::STM32::F103xB
    CMSIS::STM32::F1::RTOS_V2
    FreeRTOS::STM32::F1::Timers
    FreeRTOS::STM32::F1::Heap::4
    FreeRTOS::STM32::F1::ARM_CM3
    STM32::Nano
    STM32::NoSys
#    STM32::Nano::FloatPrint
#    STM32::Nano::FloatScan
)
add_dependencies(${PROJECT_NAME} clang_format)

stm32_add_linker_script(${PROJECT_NAME} PRIVATE ${LINKER_SCRIPT})

set(HEX_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.hex)
set(BIN_FILE ${PROJECT_BINARY_DIR}/${PROJECT_NAME}.bin)

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_OBJCOPY} -Oihex $<TARGET_FILE:${PROJECT_NAME}> ${HEX_FILE}
        COMMAND ${CMAKE_OBJCOPY} -Obinary $<TARGET_FILE:${PROJECT_NAME}> ${BIN_FILE}
        COMMENT "Building ${HEX_FILE}\nBuilding ${BIN_FILE}")

if(DEFINED OPENOCD_BIN)
    add_custom_target(flash
            COMMAND ${OPENOCD_BIN} -f "${CMAKE_SOURCE_DIR}/utility/stlink.cfg" -c "program ${HEX_FILE}" -c reset -c shutdown
            )
    add_custom_target(debug
            COMMAND ${OPENOCD_BIN} -f "${CMAKE_SOURCE_DIR}/utility/stlink.cfg" -c "program ${HEX_FILE}" -c reset
            )
    add_dependencies(flash ${PROJECT_NAME})
    add_dependencies(debug ${PROJECT_NAME})
endif()
