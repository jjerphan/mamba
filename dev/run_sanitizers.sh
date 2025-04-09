#!/bin/bash
set -euo pipefail

# Check if a sanitizer type is provided
if [ $# -ne 1 ]; then
    echo "Usage: $0 <sanitizer_type>"
    echo "Available sanitizers: asan, ubsan, tsan, msan"
    exit 1
fi

SANITIZER=$1

# Validate sanitizer type
case $SANITIZER in
    asan|ubsan|tsan|msan)
        echo "Running $SANITIZER sanitizer..."
        ;;
    *)
        echo "Invalid sanitizer type. Available options: asan, ubsan, tsan, msan"
        exit 1
        ;;
esac

# Set up environment variables based on sanitizer type
case $SANITIZER in
    asan)
        export CXXFLAGS="-fsanitize=address -fno-omit-frame-pointer"
        export CFLAGS="-fsanitize=address -fno-omit-frame-pointer"
        export LDFLAGS="-fsanitize=address"
        ;;
    ubsan)
        export CXXFLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
        export CFLAGS="-fsanitize=undefined -fno-omit-frame-pointer"
        export LDFLAGS="-fsanitize=undefined"
        ;;
    tsan)
        export CXXFLAGS="-fsanitize=thread -fno-omit-frame-pointer"
        export CFLAGS="-fsanitize=thread -fno-omit-frame-pointer"
        export LDFLAGS="-fsanitize=thread"
        ;;
    msan)
        export CXXFLAGS="-fsanitize=memory -fno-omit-frame-pointer"
        export CFLAGS="-fsanitize=memory -fno-omit-frame-pointer"
        export LDFLAGS="-fsanitize=memory"
        ;;
esac

# Install dependencies with vcpkg, passing sanitizer flags
echo "Installing dependencies with vcpkg..."
VCPKG_OVERLAY_TRIPLETS=vcpkg/triplets
cat > "$VCPKG_OVERLAY_TRIPLETS/x64-linux-$SANITIZER.cmake" << EOF
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CXX_FLAGS "$CXXFLAGS")
set(VCPKG_C_FLAGS "$CFLAGS")
set(VCPKG_LINKER_FLAGS "$LDFLAGS")
EOF

./vcpkg/vcpkg install --triplet "x64-linux-$SANITIZER"

# Clean previous build directory
echo "Cleaning previous build directory..."
rm -rf build-$SANITIZER
mkdir -p build-$SANITIZER
cd build-$SANITIZER

# Configure and build
echo "Configuring and building..."
../vcpkg/installed/x64-linux-$SANITIZER/tools/cmake/bin/cmake .. \
    -DCMAKE_BUILD_TYPE=Debug \
    -DBUILD_LIBMAMBA=ON \
    -DBUILD_LIBMAMBAPY=OFF \
    -DBUILD_SHARED=OFF \
    -DBUILD_STATIC=ON \
    -DBUILD_LIBMAMBA_TESTS=ON \
    -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
    -DCMAKE_C_FLAGS="$CFLAGS" \
    -DCMAKE_EXE_LINKER_FLAGS="$LDFLAGS" \
    -DCMAKE_MODULE_LINKER_FLAGS="$LDFLAGS" \
    -DCMAKE_SHARED_LINKER_FLAGS="$LDFLAGS" \
    -DCMAKE_TOOLCHAIN_FILE=../vcpkg/scripts/buildsystems/vcpkg.cmake \
    -DVCPKG_TARGET_TRIPLET="x64-linux-$SANITIZER"

../vcpkg/installed/x64-linux-$SANITIZER/tools/cmake/bin/cmake --build . --config Debug

# Run tests
echo "Running tests..."
../vcpkg/installed/x64-linux-$SANITIZER/tools/cmake/bin/ctest --output-on-failure

# Run libmamba tests
echo "Running libmamba tests..."
./libmamba/tests/test_libmamba

# Clean up
echo "Cleaning up..."
cd ..
rm -rf build-$SANITIZER 