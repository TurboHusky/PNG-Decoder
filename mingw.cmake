set(CMAKE_SYSTEM_NAME Windows)

set(CMAKE_C_COMPILER x86_64-w64-mingw32-cc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)

set(CMAKE_FIND_ROOT_PATH /usr/x86_64-w64-mingw32) # Target environment

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER) # Search for programs in host environment
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY) # Search for Libraries and headers in target environment
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)