# Build & Test Instructions

## Environment & Toolchain
- **Operating System**: Windows 11 / Windows Server (64-bit)
- **Compiler**: Microsoft Visual C++ 19.50.35730.0 (Visual Studio 2026 Community)
- **Assembler**: Microsoft MASM 14.50.35717 (`ml64.exe`)
- **CMake**: version 4.3.2
- **AngelScript Version**: 2.39.0 WIP
- **Engine Flags**: `AS_64BIT_PTR`, `AS_WIN`, `AS_X64_MSVC`

---

## 1. Configure Build Files
From the repository root (`e:\Github\src\angelscript`):
```powershell
cmake -B build/test_feature -S sdk/tests/test_feature/projects/cmake
```

## 2. Compile Test Suite
```powershell
cmake --build build/test_feature --config Release
```
*(Optionally `--config Debug` for debug symbol inspection)*

## 3. Run the Full Test Suite
**IMPORTANT**: The test runner expects its working directory to be `sdk/tests/test_feature/bin` because tests load relative script assets from `scripts/`:
```powershell
cd sdk/tests/test_feature/bin
.\Release\test_feature.exe
```
Or from repo root:
```powershell
powershell -Command "cd sdk/tests/test_feature/bin; .\Release\test_feature.exe"
```

## 4. Expected Baseline Output
```text
AngelScript version: 2.39.0 WIP
AngelScript options: AS_64BIT_PTR AS_WIN AS_X64_MSVC
...
All of the tests passed with success.
```
