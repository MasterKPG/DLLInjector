
#include <iostream>
#include <Windows.h>
#include <string>

bool InjectDLL(const char* dllPath, DWORD processId) {
	HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, processId);
	if (hProcess == NULL) {
		std::cerr << "Error: Unable to open process. Error Code: " << GetLastError() << std::endl;
		return false;
	}

	// Allocate memory for the dll path in the target process
	size_t dllPathSize = strlen(dllPath) + 1;
	LPVOID remoteDLLpath = VirtualAllocEx(hProcess, NULL, dllPathSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (remoteDLLpath == NULL) {
		std::cerr << "Error : Unable to allocate memory in target process. Error Code: " << GetLastError() << std::endl;
		CloseHandle(hProcess);
		return false;
	}

	// Write the dll path to the allocated memory
	if (!WriteProcessMemory(hProcess, remoteDLLpath, dllPath, dllPathSize, NULL)) {
		std::cerr << "Error: Unable to write to process memory. Error code: " << GetLastError() << std::endl;
		VirtualFreeEx(hProcess, remoteDLLpath, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	// Get the adress of LoadlibraryA in kernel32.dll
	HMODULE hKernel32 = GetModuleHandleA("Kernel32.dll");
	if (hKernel32 == NULL) {
		std::cerr << "Error: Unable to get handle to kernel32.dll. Error Code: " << GetLastError() << std::endl;
		VirtualFreeEx(hProcess, remoteDLLpath, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	FARPROC loadLibraryAddr = GetProcAddress(hKernel32, "LoadLibraryA");
	if (loadLibraryAddr == NULL) {
		std::cerr << "Error: Unable to get address of LoadLibraryA. Error Code: " << GetLastError() << std::endl;
		VirtualFreeEx(hProcess, remoteDLLpath, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	// Create a remote thread that calls LoadLibraryA with the dll path as argument
	HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, (LPTHREAD_START_ROUTINE)loadLibraryAddr, remoteDLLpath, 0, NULL);
	if (hThread == NULL) {
		std::cerr << "Error: Unable to create remote thread. Error Code: " << GetLastError() << std::endl;
		VirtualFreeEx(hProcess, remoteDLLpath, 0, MEM_RELEASE);
		CloseHandle(hProcess);
		return false;
	}

	// Wait for thread to finish
	WaitForSingleObject(hThread, INFINITE);

	// Check if the dll was successfully injected
	DWORD exitCode;
	GetExitCodeThread(hThread, &exitCode);
	bool Success = (exitCode != 0);

	// Clean up
	CloseHandle(hThread);
	VirtualFreeEx(hProcess, remoteDLLpath, 0, MEM_RELEASE);
	CloseHandle(hProcess);

	return Success;
}

void setConsoleToUTF8() {
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
}

int main()
{
	setConsoleToUTF8();
	std::cout << R"(
		██████╗ ██╗     ██╗      ██████╗███████╗███╗   ██╗████████╗██████╗  █████╗ ██╗
		██╔══██╗██║     ██║     ██╔════╝██╔════╝████╗  ██║╚══██╔══╝██╔══██╗██╔══██╗██║
		██║  ██║██║     ██║     ██║     █████╗  ██╔██╗ ██║   ██║   ██████╔╝███████║██║
		██║  ██║██║     ██║     ██║     ██╔══╝  ██║╚██╗██║   ██║   ██╔══██╗██╔══██║██║
		██████╔╝███████╗███████╗╚██████╗███████╗██║ ╚████║   ██║   ██║  ██║██║  ██║███████╗
		╚═════╝ ╚══════╝╚══════╝ ╚═════╝╚══════╝╚═╝  ╚═══╝   ╚═╝   ╚═╝  ╚═╝╚═╝  ╚═╝╚══════╝

		───────────────────────────┬───────────────────────────
		[✓] Core components loaded[✓] Memory mapper active
		───────────────────────────┴───────────────────────────
		███╗   ███╗ █████╗ ███████╗████████╗███████╗██████╗ ██╗  ██╗██████╗ ███████╗
		████╗ ████║██╔══██╗██╔════╝╚══██╔══╝██╔════╝██╔══██╗██║ ██╔╝██╔══██╗██╔════╝
		██╔████╔██║███████║███████╗   ██║   █████╗  ██████╔╝█████╔╝ ██████╔╝██║  ███╗
		██║╚██╔╝██║██╔══██║╚════██║   ██║   ██╔══╝  ██╔══██╗██╔═██╗ ██╔═══╝ ██║   ██║
		██║ ╚═╝ ██║██║  ██║███████║   ██║   ███████╗██║  ██║██║  ██╗██║     ╚██████╔╝
		╚═╝     ╚═╝╚═╝  ╚═╝╚══════╝   ╚═╝   ╚══════╝╚═╝  ╚═╝╚═╝  ╚═╝╚═╝      ╚═════╝)" << "\n";
	std::cout << "Enter the path to your DLL file: (Example: C:\\path\\to\\your\\dll.dll)\n";
	std::string dllPath;
	std::cin >> dllPath;

	DWORD processId;
	std::cout << "Enter the process ID: ";
	std::cin >> processId;

	if (InjectDLL(dllPath.c_str(), processId)) {
		std::cout << "DLL injected successfully!\n" << std::endl;
	}
	else {
		std::cerr << "Error: Unable to inject DLL\n" << std::endl;
	}

	return 0;
}


