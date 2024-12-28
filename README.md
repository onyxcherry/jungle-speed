# Jungle Speed

Jungle Speed game implementation on BSD sockets

## Build

0. Requirements:

   - cmake installed (version 3.11+)
   - [vcpkg](https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started?pivots=shell-bash) installed

1. Copy [CMakeUserPresets.sample.json](CMakeUserPresets.sample.json) file as `CMakeUserPresets.json` and fill the `VCPKG_ROOT` value
2. Install dependency

    ```shell
    vcpkg install
    ```

3. Create building scripts

    ```shell
    cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
    ```

4. Build executables

    ```shell
    cmake --build build
    ```

    The binary can be also built using e.g. VSCode (`CMake: Build` --> `default` preset) [as described](https://learn.microsoft.com/en-gb/vcpkg/get_started/get-started-vscode?pivots=shell-bash#6---build-and-run-the-project)

## Running

### Server

Run with default interface and port

```shell
./build/JungleSpeedServer
```

or pass the options

```shell
./build/JungleSpeedServer 0.0.0.0 9000
```

### Client

TODO
