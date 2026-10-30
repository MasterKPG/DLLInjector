// Injector.h — reusable DLL injection core (no UI dependencies).
//
// Classic LoadLibrary injection via CreateRemoteThread, with architecture
// validation and least-privilege handles. Intended for debugging, modding
// and instrumentation of processes you own or are authorized to modify.
#pragma once

#include <Windows.h>
#include <string>
#include <vector>

namespace inj {

enum class Arch { Unknown, X86, X64 };

struct ProcessInfo {
    DWORD        pid = 0;
    std::wstring name;
    Arch         arch = Arch::Unknown;
};

struct InjectResult {
    bool         success = false;
    std::wstring message;  // Human-readable summary (success detail or error).
};

// Human-readable name for an architecture ("x86", "x64", "—").
const wchar_t* ArchName(Arch a);

// Best-effort: enable SeDebugPrivilege on the current process so that
// protected / elevated processes can be opened. Returns false if denied.
bool EnableDebugPrivilege();

// Architecture this injector binary was built for.
Arch CurrentProcessArch();

// Architecture of a running process (PROCESS_QUERY_LIMITED_INFORMATION).
Arch GetProcessArch(DWORD pid);

// Architecture read from a DLL's PE header on disk.
Arch GetDllArch(const std::wstring& dllPath);

// Snapshot of running processes, sorted by name (case-insensitive).
// Each entry's architecture is filled in best-effort.
std::vector<ProcessInfo> EnumerateProcesses();

// Inject dllPath into the process identified by pid.
// Validates the file exists and that its architecture matches the target
// before touching the process, and uses a least-privilege handle.
InjectResult InjectDll(DWORD pid, const std::wstring& dllPath);

}  // namespace inj
