# Cross/multilib build for 32-bit x86 Linux (i686) from an x86_64 host:
#   sudo apt install gcc-multilib g++-multilib libsdl2-dev:i386
#   cmake -B build-i386 -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-linux-i386.cmake
# Not needed when building natively on a 32-bit system (antiX i386).
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR i686)
set(CMAKE_C_FLAGS_INIT "-m32 -march=i686 -mtune=generic")
set(CMAKE_CXX_FLAGS_INIT "-m32 -march=i686 -mtune=generic")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-m32")
set(CMAKE_LIBRARY_PATH /usr/lib/i386-linux-gnu)
