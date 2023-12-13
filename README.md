# thermal-desktop-results-reader
Thermal Desktop Results Reader

This application demonstrates how to connect Thermal Desktop OpenTD APIs to System Coupling Participant library APIs.

It uses C++/CLI language (a kind of a hybrid between C# and C++) to tie the .NET DLL and regular DLL with C++ interfaces
in a single application.

You will need two sets of DLLs:
  1. OpenTD DLLs
    - `OpenTDv232.dll`
	- `OpenTDv232.Results.dll`
	- `SinapsXNet.dll`
	- `CRlog4Net.dll`
  2. SCP Library DLLs
    - `SysC.SystemCouplingParticipant.lib` (for build-time)
    - `SysC.SystemCouplingParticipant.dll` (for run-time)
	- `mpi_wrapper.dll` (for run-time)

# Compilation command

A simple way to compile this using MSVC compiler:

```
cl /clr /std:c++17 /O2 /EHa /nologo /I"C:\ANSYSDev\ANSYS Inc\v242\SystemCoupling\runTime\winx64\include" /FU"CRlog4Net.dll" /FU"OpenTDv232.dll" /FU"OpenTDv232.Results.dll" /FU"SinapsXNet.dll" TDResultsReader.cpp /link /subsystem:console "SysC.SystemCouplingParticipant.lib"
```

Alternativly, use Visual Studio to create "CLR Console App" project. A pre-configured project is included in this repo.
Main steps:
- "CLR Console App" as project type.
- Use .NET Framework 4.8
- Use C++17
- Add this includes directory: <ansys install>\SystemCoupling\runTime\winx64\include
- Add this lib: SysC.SystemCouplingParticipant.lib
- Add above-mentioned .NET references
- Turn off pre-compiled headers options
