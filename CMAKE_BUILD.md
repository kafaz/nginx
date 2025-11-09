# Building NGINX with CMake

This CMakeLists.txt provides an alternative build system for NGINX using CMake. This is not the official build system (NGINX uses custom shell scripts in `auto/`), but can be useful for development, IDE integration, and cross-platform builds.

## Prerequisites

### Linux/Unix
```bash
sudo apt install cmake build-essential libpcre2-dev zlib1g-dev libssl-dev
```

### Windows
- CMake 3.15+
- Visual Studio 2019+ or MinGW-w64
- PCRE2 (optional)
- zlib (optional)
- OpenSSL (optional for SSL support)

### macOS
```bash
brew install cmake pcre2 zlib openssl
```

## Build Options

| Option | Default | Description |
|--------|---------|-------------|
| `HTTP_SSL` | ON | Enable HTTP SSL module |
| `HTTP_V2` | ON | Enable HTTP/2 module |
| `HTTP_V3` | OFF | Enable HTTP/3 (QUIC) module |
| `HTTP_GZIP` | ON | Enable HTTP gzip module |
| `WITH_PCRE` | ON | Enable PCRE support |
| `WITH_ZLIB` | ON | Enable zlib support |
| `WITH_THREADS` | ON | Enable thread pool support |
| `WITH_DEBUG` | OFF | Enable debug logging |

## Building on Linux/Unix

### Basic build
```bash
mkdir build
cd build
cmake ..
make
```

### Build with all features
```bash
mkdir build
cd build
cmake .. \
  -DHTTP_SSL=ON \
  -DHTTP_V2=ON \
  -DHTTP_GZIP=ON \
  -DWITH_PCRE=ON \
  -DWITH_ZLIB=ON \
  -DWITH_THREADS=ON
make
```

### Build with debug
```bash
cmake .. -DCMAKE_BUILD_TYPE=Debug -DWITH_DEBUG=ON
make
```

### Install
```bash
sudo make install
```

## Building on Windows

### Using Visual Studio
```cmd
mkdir build
cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

### Using MinGW
```cmd
mkdir build
cd build
cmake .. -G "MinGW Makefiles"
cmake --build .
```

### With vcpkg dependencies
```cmd
vcpkg install pcre2 zlib openssl
mkdir build
cd build
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

## Building on macOS

```bash
mkdir build
cd build
cmake .. \
  -DOPENSSL_ROOT_DIR=/usr/local/opt/openssl \
  -DPCRE2_ROOT=/usr/local/opt/pcre2
make
```

## Running NGINX

After building, the binary will be at:
- Linux/macOS: `build/nginx`
- Windows: `build/Release/nginx.exe` or `build/Debug/nginx.exe`

Run with:
```bash
./build/nginx -c conf/nginx.conf
```

## IDE Integration

### CLion
Open the project directory directly - CLion will detect CMakeLists.txt automatically.

### Visual Studio Code
1. Install the CMake Tools extension
2. Open the project directory
3. Select a kit when prompted
4. Build using the CMake sidebar

### Visual Studio
```cmd
cmake .. -G "Visual Studio 17 2022"
start nginx.sln
```

## Differences from Official Build

This CMake build is a simplified version and may not include:
- All platform-specific optimizations
- All optional third-party modules
- Some advanced configuration detection
- Dynamic module loading

For production builds, use the official build system:
```bash
auto/configure --with-http_ssl_module --with-http_v2_module
make
sudo make install
```

## Troubleshooting

### PCRE not found
```bash
# Ubuntu/Debian
sudo apt install libpcre2-dev

# Or specify PCRE path manually
cmake .. -DPCRE2_ROOT=/path/to/pcre2
```

### OpenSSL not found
```bash
# Ubuntu/Debian
sudo apt install libssl-dev

# Or specify OpenSSL path
cmake .. -DOPENSSL_ROOT_DIR=/path/to/openssl
```

### zlib not found
```bash
# Ubuntu/Debian
sudo apt install zlib1g-dev

# Or specify zlib path
cmake .. -DZLIB_ROOT=/path/to/zlib
```

## Contributing

This CMake build system is a convenience tool. For official NGINX development, please use the standard build system documented in CLAUDE.md and at https://nginx.org/en/docs/dev/development_guide.html

## License

NGINX is licensed under the 2-clause BSD license. See the LICENSE file for details.
