# Readme

## Compiling

### Visual Studio
Remove-Item -Recurse -Force build 
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release

### Ninja or MinGW
cmake -B build -G Ninja
cmake --build build

## Run main

## Tests
ctest --test-dir build --output-on-failure

