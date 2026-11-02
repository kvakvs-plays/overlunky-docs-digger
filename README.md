# Description

This project strives to provide better typespec files and HTML documentation output for Overlunky than 
what currently exists in Playlunky under `submodules/overlunky/docs/` by utilizing Clang parsing facilities.

## Development Machine

There are multiple tools like MS Visual C++, CMake, Git, Python and Clang available globally on the development machine.
If they are not, read below `Dependencies Install`...

## Dependencies Install

### Windows (Development)

```powershell
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id Git.Git
winget install -e --id Python.Python.3.12
# MSVC compiler + Windows SDK (skip if Visual Studio already has "Desktop development with C++")
winget install -e --id Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Clang/LLVM development libraries are not available from winget. The first CMake configure downloads
the pinned `clang+llvm-<version>-x86_64-pc-windows-msvc.tar.xz` release (~860 MB, SHA256-checked)
and unpacks it into `thirdparty/` (git-ignored). Later configures reuse it. To use another
LLVM install, pass `-DClang_DIR=<llvm root>/lib/cmake/clang`.

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
# or -G Ninja, from a "Developer PowerShell for VS" so cl.exe is found
```

### Linux

Not set up yet. Clang is expected to come from distro packages (`llvm-<ver>-dev`, `libclang-<ver>-dev`)
or a Docker image; pass `-DClang_DIR=...` if CMake does not find it on its own.