set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv32)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

if(NOT DEFINED TOOLCHAIN_PREFIX)
  set(TOOLCHAIN_PREFIX riscv-none-elf)
endif()

find_program(CMAKE_C_COMPILER NAMES ${TOOLCHAIN_PREFIX}-gcc riscv-none-embed-gcc REQUIRED)
find_program(CMAKE_ASM_COMPILER NAMES ${TOOLCHAIN_PREFIX}-gcc riscv-none-embed-gcc REQUIRED)
find_program(CMAKE_OBJCOPY NAMES ${TOOLCHAIN_PREFIX}-objcopy riscv-none-embed-objcopy REQUIRED)
find_program(CMAKE_OBJDUMP NAMES ${TOOLCHAIN_PREFIX}-objdump riscv-none-embed-objdump REQUIRED)
find_program(CMAKE_SIZE NAMES ${TOOLCHAIN_PREFIX}-size riscv-none-embed-size REQUIRED)

set(CMAKE_C_STANDARD 11)
