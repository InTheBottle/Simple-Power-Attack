# Simple Power Attack

Simple Power Attack is an SKSE plugin that maps power attacks to a configurable alternate input key.

## Features

- Configurable alternate key for power attack
- Mouse and gamepad key options through SKSE Menu Framework
- INI-backed key persistence
- Supports SE, AE, and VR via CommonLibSSE-NG

## Requirements

- Windows
- Visual Studio 2022 with Desktop development with C++
- CMake 3.21+
- vcpkg

## Clone

Clone with submodules so CommonLibSSE-NG and nested dependencies are present:

```powershell
git clone --recursive https://github.com/InTheBottle/Simple-Power-Attack.git
cd "Simple-Power-Attack"
```

If you already cloned without submodules:

```powershell
git submodule update --init --recursive
```

## Build

Use CMake presets defined in this repository.

Configure:

```powershell
cmake --preset build-release-msvc
```

Build:

```powershell
cmake --build build/release-msvc --config Release
```

Output DLL:

- build/release-msvc/Release/SimplePowerAttack.dll

## Notes

- This project uses CommonLibSSE-NG as a git submodule at extern/CommonLibSSE-NG.
- If dependencies fail to resolve, make sure VCPKG_ROOT is set and points to your local vcpkg installation.
