# PNG Decoder

Basic decoder for PNG images, tested with http://www.schaik.com/pngsuite/

## Build Instructions - Linux Host

### Static Linking

```
cc -c src/filter.c -Iinclude -o build/obj/filter.o
cc -c src/png.c-Iinclude -o build/obj/png.o 
cc -c src/zlib.c-Iinclude -o build/obj/zlib.o 

ar -rcs build/lib/libpng.a build/obj/filter.o build/obj/png.o build/obj/zlib.o

cc -c src/main.c -Iinclude -o build/obj/main.o 
cc build/obj/main.o -static -Lbuild/lib -lpng -o build/bin/png_static
```

To list symbols in object files:

```
nm build/obj/png.o
```

### Dynamic Linking

```
cc -fPIC -c src/filter.c -Iinclude -o build/obj/filter.s.o
cc -fPIC -c src/png.c -Iinclude -o build/obj/png.s.o
cc -fPIC -c src/zlib.c -Iinclude -o build/obj/zlib.s.o

cc -shared build/obj/png.s.o build/obj/filter.s.o build/obj/zlib.s.o -o build/lib/libpng.so

cc -c src/main.c -Iinclude -o build/obj/main.o
cc build/obj/main.o -Lbuild/lib -lpng -o build/bin/png_dynamic
```

Search paths for dynamically linked libraries are searched in the following order:
1. RPATH (deprecated)
1. LD_LIBRARY_PATH
1. RUNPATH


Build using RPATH with cc 12.2.1:

```    
cc build/obj/main.o -Lbuild/lib -lpng -Wl,-rpath,build/lib -o build/bin/png_dynamic
```

Build using RUNPATH with cc 12.2.1:

```
cc build/obj/main.o -Lbuild/lib -lpng -Wl,--enable-new-dtags,-rpath,build/lib -o build/bin/png_dynamic
```

Check application header for path:

```
readelf -d build/bin/png_dynamic | grep PATH
```

### Runtime Loading
```
cc main.c -o build/bin/png_runtime
```

The path to the lib is hardcoded as a relative path to folder the application is called form, so this binary must be run from the same folder as libpng.so.

### Make

#### Cross Compiling

```
make CC=x86_64-w64-mingw32-cc AR=x86_64-w64-mingw32-ar
```

### CMake

To build for Linux:

1. Ensure cc and cmake are installed
1. Run the following commands from the src folder:

    ```
    mkdir build
    cd build
    cmake ..
    cmake --build .
    ```

#### Cross Compiling for Windows

To cross-compile for Windows:

1. Ensure a cross compiler is installed. The toolchain provided (mingw.cmake) assumes mingw is installed in /usr/x86_64-w64-mingw32. If you are using a different cross-compiler you will need to update the toolchain.

1. Run the following commands from the src folder:

    ```
    mkdir build_win
    cd build_win
    cmake -DCMAKE_TOOLCHAIN_FILE=../mingw.cmake ..
    cmake --build .
    ```

### Windows Host

Suggest CMake GUI or similar depending on setup.