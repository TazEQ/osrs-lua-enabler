#include <windows.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstring>

static const char* DEFAULT_OSCLIENT =
    "C:\\Program Files (x86)\\Jagex Launcher\\Games\\Old School RuneScape\\Client\\osclient.exe";

static DWORD find_pid(const char* process_name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);

    DWORD pid = 0;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, process_name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32Next(snap, &pe));
    }

    CloseHandle(snap);
    return pid;
}

static DWORD launch_and_get_pid(const char* exe_path) {
    if (GetFileAttributesA(exe_path) == INVALID_FILE_ATTRIBUTES) {
        printf("[-] executable not found: %s\n", exe_path);
        return 0;
    }

    // Extract parent directory so the game can find its resources
    char working_dir[MAX_PATH];
    strncpy_s(working_dir, exe_path, MAX_PATH - 1);
    char* last_sep = strrchr(working_dir, '\\');
    if (!last_sep)
        last_sep = strrchr(working_dir, '/');
    if (last_sep)
        *last_sep = '\0';

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};

    if (!CreateProcessA(exe_path, nullptr, nullptr, nullptr, FALSE, 0, nullptr, working_dir, &si, &pi)) {
        printf("[-] CreateProcess failed (error %lu)\n", GetLastError());
        return 0;
    }

    printf("[+] launched %s (PID %lu)\n", exe_path, pi.dwProcessId);

    // Wait for the GUI message loop to become idle
    DWORD wait_result = WaitForInputIdle(pi.hProcess, 10000);
    if (wait_result != 0) {
        printf("[*] WaitForInputIdle returned %lu, falling back to Sleep\n", wait_result);
        Sleep(2000);
    }

    DWORD pid = pi.dwProcessId;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return pid;
}

static int inject_dll(DWORD pid, const char* full_path) {
    HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!proc) {
        printf("[-] OpenProcess failed (error %lu) -- try running as admin\n", GetLastError());
        return 1;
    }

    size_t path_len = strlen(full_path) + 1;
    LPVOID remote_buf = VirtualAllocEx(proc, nullptr, path_len, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote_buf) {
        printf("[-] VirtualAllocEx failed\n");
        CloseHandle(proc);
        return 1;
    }

    if (!WriteProcessMemory(proc, remote_buf, full_path, path_len, nullptr)) {
        printf("[-] WriteProcessMemory failed\n");
        VirtualFreeEx(proc, remote_buf, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }

    HMODULE k32 = GetModuleHandleA("kernel32.dll");
    void* load_lib_addr = reinterpret_cast<void*>(GetProcAddress(k32, "LoadLibraryA"));
    auto load_lib = reinterpret_cast<LPTHREAD_START_ROUTINE>(load_lib_addr);

    HANDLE thread = CreateRemoteThread(proc, nullptr, 0, load_lib, remote_buf, 0, nullptr);
    if (!thread) {
        printf("[-] CreateRemoteThread failed (error %lu)\n", GetLastError());
        VirtualFreeEx(proc, remote_buf, 0, MEM_RELEASE);
        CloseHandle(proc);
        return 1;
    }

    printf("[+] injecting %s ...\n", full_path);
    WaitForSingleObject(thread, 5000);

    DWORD exit_code = 0;
    GetExitCodeThread(thread, &exit_code);
    if (exit_code) {
        printf("[+] success -- DLL loaded at 0x%lX\n", exit_code);
    } else {
        printf("[-] LoadLibraryA returned NULL -- DLL failed to load\n");
    }

    VirtualFreeEx(proc, remote_buf, 0, MEM_RELEASE);
    CloseHandle(thread);
    CloseHandle(proc);
    return exit_code ? 0 : 1;
}

int main(int argc, char* argv[]) {
    const char* dll_path = nullptr;
    const char* proc_name = "osclient.exe";
    const char* launch_path = nullptr;
    bool launch_mode = false;

    // Parse arguments: inject.exe <dll_path> [process_name]
    //                   inject.exe <dll_path> --launch [exe_path]
    if (argc >= 2)
        dll_path = argv[1];

    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--launch") == 0) {
            launch_mode = true;
            if (i + 1 < argc)
                launch_path = argv[++i];
            else
                launch_path = DEFAULT_OSCLIENT;
        } else {
            proc_name = argv[i];
        }
    }

    if (!dll_path) {
        printf("usage: inject.exe <dll_path> [process_name]\n");
        printf("       inject.exe <dll_path> --launch [exe_path]\n");
        printf("\n");
        printf("  attach mode: inject into a running process (default: osclient.exe)\n");
        printf("  launch mode: start the exe, wait for it to initialize, then inject\n");
        return 1;
    }

    // Resolve DLL to absolute path
    char full_path[MAX_PATH];
    if (!GetFullPathNameA(dll_path, MAX_PATH, full_path, nullptr)) {
        printf("[-] bad path: %s\n", dll_path);
        return 1;
    }

    if (GetFileAttributesA(full_path) == INVALID_FILE_ATTRIBUTES) {
        printf("[-] file not found: %s\n", full_path);
        return 1;
    }

    // Get target PID
    DWORD pid = 0;
    if (launch_mode) {
        pid = launch_and_get_pid(launch_path);
    } else {
        pid = find_pid(proc_name);
        if (pid)
            printf("[+] found %s (PID %lu)\n", proc_name, pid);
    }

    if (!pid) {
        if (launch_mode)
            printf("[-] failed to launch: %s\n", launch_path);
        else
            printf("[-] process not found: %s\n", proc_name);
        return 1;
    }

    return inject_dll(pid, full_path);
}
