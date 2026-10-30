// Injector.cpp — implementation of the DLL injection core.
#include "Injector.h"

#include <TlHelp32.h>

#include <algorithm>
#include <cwchar>

#pragma comment(lib, "Advapi32.lib")

namespace inj {
namespace {

// Format a GetLastError() code into readable text, e.g. "(5) Access is denied."
std::wstring LastErrorText(DWORD err) {
    LPWSTR buf = nullptr;
    const DWORD len = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buf), 0, nullptr);

    std::wstring text = L"(" + std::to_wstring(err) + L") ";
    if (len && buf) {
        std::wstring msg(buf, len);
        while (!msg.empty() && (msg.back() == L'\n' || msg.back() == L'\r' ||
                                msg.back() == L' ' || msg.back() == L'.')) {
            msg.pop_back();
        }
        text += msg + L".";
    } else {
        text += L"Unknown error.";
    }
    if (buf) LocalFree(buf);
    return text;
}

// Is the host OS 64-bit?
bool Is64BitOS() {
#if defined(_WIN64)
    return true;
#else
    // 32-bit binary: if we are running under WOW64 the OS is 64-bit.
    using PFN = BOOL(WINAPI*)(HANDLE, PBOOL);
    auto fn = reinterpret_cast<PFN>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process"));
    BOOL wow = FALSE;
    if (fn) fn(GetCurrentProcess(), &wow);
    return wow == TRUE;
#endif
}

// Architecture of a process given an open handle, using IsWow64Process2 when
// available (accurate on ARM64) and falling back to IsWow64Process.
Arch ArchFromHandle(HANDLE hProcess) {
    if (!hProcess) return Arch::Unknown;

    using PFN2 = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    static auto isWow64_2 = reinterpret_cast<PFN2>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));

    if (isWow64_2) {
        USHORT processMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        USHORT nativeMachine = IMAGE_FILE_MACHINE_UNKNOWN;
        if (isWow64_2(hProcess, &processMachine, &nativeMachine)) {
            if (processMachine == IMAGE_FILE_MACHINE_UNKNOWN) {
                // Not running under emulation: it is a native-arch process.
                if (nativeMachine == IMAGE_FILE_MACHINE_AMD64 ||
                    nativeMachine == IMAGE_FILE_MACHINE_ARM64) {
                    return Arch::X64;
                }
                return Arch::X86;
            }
            return processMachine == IMAGE_FILE_MACHINE_I386 ? Arch::X86
                                                             : Arch::X64;
        }
    }

    BOOL wow = FALSE;
    if (IsWow64Process(hProcess, &wow)) {
        if (!Is64BitOS()) return Arch::X86;
        return wow ? Arch::X86 : Arch::X64;
    }
    return Arch::Unknown;
}

}  // namespace

const wchar_t* ArchName(Arch a) {
    switch (a) {
        case Arch::X86: return L"x86";
        case Arch::X64: return L"x64";
        default: return L"—";
    }
}

bool EnableDebugPrivilege() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
                          TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token)) {
        return false;
    }

    LUID luid{};
    bool ok = false;
    if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
        TOKEN_PRIVILEGES tp{};
        tp.PrivilegeCount = 1;
        tp.Privileges[0].Luid = luid;
        tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        AdjustTokenPrivileges(token, FALSE, &tp, sizeof(tp), nullptr, nullptr);
        ok = (GetLastError() == ERROR_SUCCESS);
    }
    CloseHandle(token);
    return ok;
}

Arch CurrentProcessArch() {
#if defined(_WIN64)
    return Arch::X64;
#else
    return Arch::X86;
#endif
}

Arch GetProcessArch(DWORD pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) h = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (!h) return Arch::Unknown;
    Arch a = ArchFromHandle(h);
    CloseHandle(h);
    return a;
}

Arch GetDllArch(const std::wstring& dllPath) {
    HANDLE file = CreateFileW(dllPath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                              nullptr);
    if (file == INVALID_HANDLE_VALUE) return Arch::Unknown;

    Arch result = Arch::Unknown;
    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0,
                                        nullptr);
    if (mapping) {
        auto base = static_cast<BYTE*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
        if (base) {
            auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
            if (dos->e_magic == IMAGE_DOS_SIGNATURE) {
                auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(
                    base + dos->e_lfanew);
                if (nt->Signature == IMAGE_NT_SIGNATURE) {
                    const WORD machine = nt->FileHeader.Machine;
                    if (machine == IMAGE_FILE_MACHINE_I386) {
                        result = Arch::X86;
                    } else if (machine == IMAGE_FILE_MACHINE_AMD64 ||
                               machine == IMAGE_FILE_MACHINE_ARM64) {
                        result = Arch::X64;
                    }
                }
            }
            UnmapViewOfFile(base);
        }
        CloseHandle(mapping);
    }
    CloseHandle(file);
    return result;
}

std::vector<ProcessInfo> EnumerateProcesses() {
    std::vector<ProcessInfo> list;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return list;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snap, &entry)) {
        do {
            if (entry.th32ProcessID == 0) continue;  // Idle pseudo-process.
            ProcessInfo info;
            info.pid = entry.th32ProcessID;
            info.name = entry.szExeFile;
            info.arch = GetProcessArch(entry.th32ProcessID);
            list.push_back(std::move(info));
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);

    std::sort(list.begin(), list.end(),
              [](const ProcessInfo& a, const ProcessInfo& b) {
                  int c = _wcsicmp(a.name.c_str(), b.name.c_str());
                  if (c != 0) return c < 0;
                  return a.pid < b.pid;
              });
    return list;
}

InjectResult InjectDll(DWORD pid, const std::wstring& dllPath) {
    InjectResult r;

    // 1. The DLL must exist on disk as a readable file.
    const DWORD attrs = GetFileAttributesW(dllPath.c_str());
    if (attrs == INVALID_FILE_ATTRIBUTES ||
        (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
        r.message = L"DLL not found: " + dllPath;
        return r;
    }

    // 2. Architecture of the DLL must match the architecture of the target.
    const Arch dllArch = GetDllArch(dllPath);
    const Arch procArch = GetProcessArch(pid);
    if (dllArch == Arch::Unknown) {
        r.message = L"File is not a valid PE / DLL image.";
        return r;
    }
    if (procArch != Arch::Unknown && dllArch != procArch) {
        r.message = std::wstring(L"Architecture mismatch: ") +
                    ArchName(dllArch) + L" DLL cannot load into a " +
                    ArchName(procArch) + L" process.";
        return r;
    }

    // 3. Open the target with only the rights injection requires.
    const DWORD access = PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION |
                         PROCESS_VM_OPERATION | PROCESS_VM_WRITE |
                         PROCESS_VM_READ;
    HANDLE hProcess = OpenProcess(access, FALSE, pid);
    if (!hProcess) {
        r.message = L"Could not open process. " + LastErrorText(GetLastError()) +
                    L" Try running as Administrator.";
        return r;
    }

    // 4. Allocate room for the path (wide string) inside the target.
    const SIZE_T bytes = (dllPath.size() + 1) * sizeof(wchar_t);
    LPVOID remote = VirtualAllocEx(hProcess, nullptr, bytes,
                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) {
        r.message = L"Memory allocation failed. " +
                    LastErrorText(GetLastError());
        CloseHandle(hProcess);
        return r;
    }

    auto cleanup = [&] {
        VirtualFreeEx(hProcess, remote, 0, MEM_RELEASE);
        CloseHandle(hProcess);
    };

    // 5. Write the path, then run LoadLibraryW on it in a remote thread.
    if (!WriteProcessMemory(hProcess, remote, dllPath.c_str(), bytes,
                            nullptr)) {
        r.message = L"Writing to process memory failed. " +
                    LastErrorText(GetLastError());
        cleanup();
        return r;
    }

    auto loadLibrary = reinterpret_cast<LPTHREAD_START_ROUTINE>(GetProcAddress(
        GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    if (!loadLibrary) {
        r.message = L"Could not resolve LoadLibraryW. " +
                    LastErrorText(GetLastError());
        cleanup();
        return r;
    }

    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0, loadLibrary,
                                        remote, 0, nullptr);
    if (!hThread) {
        r.message = L"CreateRemoteThread failed. " +
                    LastErrorText(GetLastError());
        cleanup();
        return r;
    }

    WaitForSingleObject(hThread, INFINITE);

    // LoadLibraryW returns the module handle; 0 means the load failed inside
    // the target (missing dependency, DllMain returned FALSE, etc.).
    DWORD exitCode = 0;
    GetExitCodeThread(hThread, &exitCode);
    CloseHandle(hThread);
    cleanup();

    if (exitCode == 0) {
        r.message = L"Remote LoadLibraryW returned NULL — the DLL loaded but "
                    L"its entry point failed, or a dependency is missing.";
        return r;
    }

    r.success = true;
    r.message = L"DLL injected successfully.";
    return r;
}

}  // namespace inj
