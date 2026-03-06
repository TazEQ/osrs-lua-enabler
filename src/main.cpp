#include <Windows.h>
#include <cstdio>
#include <libhat/scanner.hpp>
#include <libhat/signature.hpp>
#include <libhat/process.hpp>

static void report(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len <= 0) return;

    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
    if (h == nullptr || h == INVALID_HANDLE_VALUE) {
        if (AttachConsole(ATTACH_PARENT_PROCESS))
            h = GetStdHandle(STD_OUTPUT_HANDLE);
    }
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(h, buf, static_cast<DWORD>(len), &written, nullptr);
    }
}

// Beta gate — returns whether Lua plugins are enabled
static constexpr auto sig_check_for_lua = hat::compile_signature<
    "83 B9 ? ? ? ? 02 7D ? 48 8B 81 ? ? ? ? 48 C1 E8 23 A8 01 75">();

// Init gate — JNZ that skips Lua VM creation
static constexpr auto sig_lua_init_gate2 = hat::compile_signature<
    "75 ? 48 8D 8F ? ? ? ? E8 ? ? ? ? 48 8B 8F ? ? ? ? 48 8B 89 ? ? ? ?">();

// Timeout enforcer — kills long-running pcall invocations
static constexpr auto sig_timeout_check = hat::compile_signature<
    "40 53 48 81 EC ? ? ? ? 48 8B D9 E8 ? ? ? ? 48 2B 83 ? ? ? ? 48 3B 83">();

static void apply_patches() {
    auto mod = hat::process::get_process_module();
    auto text = mod.get_section_data(".text");
    if (text.empty()) {
        report("[-] lua_enabler: .text section not found\n");
        return;
    }

    static constexpr uint8_t patch_enable[] = { 0xB0, 0x01, 0xC3 }; // mov al,1; ret
    static constexpr uint8_t patch_nop2[]   = { 0x90, 0x90 };
    static constexpr uint8_t patch_ret[]    = { 0xC3 };

    struct ScanEntry {
        const char* name;
        hat::scan_result result;
        const uint8_t* patch;
        size_t patch_size;
    };

    ScanEntry entries[] = {
        { "check_for_lua",   hat::find_pattern(text, sig_check_for_lua),  patch_enable, sizeof(patch_enable) },
        { "lua_init_gate2",  hat::find_pattern(text, sig_lua_init_gate2), patch_nop2,   sizeof(patch_nop2)   },
        { "timeout_check",   hat::find_pattern(text, sig_timeout_check),  patch_ret,    sizeof(patch_ret)    },
    };

    int applied = 0;
    const char* first_failure = nullptr;
    for (auto& e : entries) {
        if (!e.result.has_result()) {
            if (!first_failure) first_failure = e.name;
            continue;
        }

        auto* addr = reinterpret_cast<uint8_t*>(const_cast<std::byte*>(e.result.get()));
        DWORD old_protect;
        if (!VirtualProtect(addr, e.patch_size, PAGE_EXECUTE_READWRITE, &old_protect)) {
            if (!first_failure) first_failure = e.name;
            continue;
        }

        memcpy(addr, e.patch, e.patch_size);
        FlushInstructionCache(GetCurrentProcess(), addr, e.patch_size);
        VirtualProtect(addr, e.patch_size, old_protect, &old_protect);
        applied++;
    }

    if (applied == 3) {
        report("[+] lua_enabler: applied 3/3 patches\n");
    } else {
        report("[-] lua_enabler: applied %d/3 patches (first failure: %s)\n",
               applied, first_failure ? first_failure : "unknown");
    }
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        apply_patches();
    }
    return TRUE;
}
