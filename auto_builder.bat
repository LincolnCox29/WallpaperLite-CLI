@echo off
setlocal

if not exist "build\" (
    mkdir build
    if errorlevel 1 (
        echo Failed to create build directory
        pause
        exit /b 1
    )
)

cd build
if errorlevel 1 (
    echo Failed to enter build directory
    pause
    exit /b 1
)

echo Configuring project with CMake...
cmake -A Win32 ..
if errorlevel 1 (
    echo CMake configuration failed
    pause
    exit /b 1
)

echo Building project...
cmake --build . --config Release
if errorlevel 1 (
    echo Build failed
    pause
    exit /b 1
)

echo Build completed successfully
pause