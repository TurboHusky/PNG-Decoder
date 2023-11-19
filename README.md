# PNG Decoder

Basic decoder for PNG images, tested with http://www.schaik.com/pngsuite/

 Built in Alpine Linux using:
 * GCC 12.2.1 / Mingw GCC 12.2.0
 * GNU Make 4.4.1
 * CMake 3.26.5

## Build Instructions - Linux Host

The following are summary notes on how to build the project using different build tools. All commands are run from the project root folder.

### Static Linking

To build a static binary using the GCC compiler:

```
cc -c src/filter.c -Iinclude -o build/obj/filter.o
cc -c src/png.c-Iinclude -o build/obj/png.o 
cc -c src/zlib.c-Iinclude -o build/obj/zlib.o 

ar -rcs build/lib/libpng.a build/obj/filter.o build/obj/png.o build/obj/zlib.o

cc -c src/main.c -Iinclude -o build/obj/main.o 
cc build/obj/main.o -static -Lbuild/lib -lpng -o build/bin/png_static
```

Symbols in an object file can be listed using the **nm** command:

```
nm build/obj/png.o
```

### Dynamic Linking

To build a shared library and linked binary using the GCC compiler:

```
cc -fPIC -c src/filter.c -Iinclude -o build/obj/filter.s.o
cc -fPIC -c src/png.c -Iinclude -o build/obj/png.s.o
cc -fPIC -c src/zlib.c -Iinclude -o build/obj/zlib.s.o

cc -shared build/obj/png.s.o build/obj/filter.s.o build/obj/zlib.s.o -o build/lib/libpng.so

cc -c src/main.c -Iinclude -o build/obj/main.o
cc build/obj/main.o -Lbuild/lib -lpng -Wl,--enable-new-dtags,-rpath,build/lib -o build/bin/png_dynamic
```

#### Notes on Shared Libraries

See https://tldp.org/HOWTO/Program-Library-HOWTO/shared-libraries.html.

If the shared library has not been installed as part of the filesystem, the linked executable will not be able to find it. In this case the location of the shared library location can be specified either with the *`LD_LIBRARY_PATH`* environment variable or by hardcoding the path into the executable using *`RPATH`* or *`RUNPATH`*.

Shared library paths are searched in the following order:

1. `RPATH` (deprecated)
1. `LD_LIBRARY_PATH`
1. `RUNPATH`
1. Standard directories

To check the shared library dependencies for an executable, use **ldd**:
```
ldd png2ppm
```

Build using RPATH:
```    
cc build/obj/main.o -Lbuild/lib -lpng -Wl,-rpath,build/lib -o build/bin/png_dynamic
```

Build using RUNPATH:
```
cc build/obj/main.o -Lbuild/lib -lpng -Wl,--enable-new-dtags,-rpath,build/lib -o build/bin/png_dynamic
```

To check the executable header and inspect `RPATH`:
```
readelf -d build/bin/png_dynamic | grep PATH
```

### Runtime Loading

The path to the shared library may also be handled by the executable.

```
cc main.c -o build/bin/png_runtime
```

In the example provided the path is hardcoded as a relative path to the runtime folder; i.e. the binary must be run from the same folder as the shared library.

### Cross Compiling for Windows

To cross compile for Windows using Mingw64, replace the c compiler and archiver with the equivalent tools:

```
CC=x86_64-w64-mingw32-cc
AR=x86_64-w64-mingw32-ar
```

## Make

A Makefile is included with build options for static, shared and runtime loaded libary builds. The build instructions are as follows:

```
make static
```
Outputs to build/bin.
```
make dynamic
```
Outputs to build/bin and build/lib.
```
make runtime
```
Outputs to build/bin and build/lib.
```
make clean
```
Deletes build folder.

### Cross Compiling for Windows

To cross-compile for Windows include the CC and AR variables when running make:

```
make CC=x86_64-w64-mingw32-cc AR=x86_64-w64-mingw32-ar static
```

## CMake

The following CMake presets are available:

* windows_static
* windows_shared
* windows_runtime
* linux_static
* linux_shared
* linux_runtime

These handle the output paths and configuration for Linux/Windows builds, assuming the appropriate GCC/Mingw tools are installed.

To build with CMake:

```
cmake --preset linux_static
cmake --build --preset linux_static
```