// Cube World Alpha - Pet Food Names Mod
//
// Adds the name of the pet that a pet food tames to its name in parentheses.
// The name is shown wherever the game shows the item name: tooltips, vendor
// lists, pickup widgets and pickup messages.

// ---- build requirements -----------------------------------------------------
#include <stdint.h>

#if !defined(_WIN32)
#  error "Target must be Windows: this is a DLL injected into Cube.exe. Cross-compile, e.g. i686-w64-mingw32-g++."
#endif
#if UINTPTR_MAX != 0xFFFFFFFFu
#  error "Target must be 32-bit: Cube.exe is PE32."
#endif
#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ < 8
#  error "GCC 8 or newer: older GCC silently IGNORES __attribute__((naked)) on x86 and builds a broken DLL."
#endif

#include <windows.h>
#include <string.h>

// ---- game layout ------------------------------------------------------------

// cube::Item
struct Item
{
    uint8_t type;
    uint8_t subType;
    // additional fields not needed, thus no padding
};

// MSVC std::wstring, the text is inline while capacity < 8, see Text()
struct WString
{
    union {
        wchar_t  buffer[8];
        wchar_t* pointer;
    };
    uint32_t size;
    uint32_t capacity;
};
static_assert(sizeof(WString) == 0x18, "WString layout");

static const uint8_t TYPE_PET_FOOD = 20;

typedef WString* (__attribute__((thiscall)) *CreatureNameFn)(void* names, WString* out, int32_t creature);
typedef WString* (__attribute__((thiscall)) *ConstructFn)(WString* string, const wchar_t* text);
typedef void (__cdecl *DeleteFn)(void* p);

static const uint32_t RVA_CREATURE_NAME = 0x0019AA60;   // creature name by id, empty if unknown
static const uint32_t RVA_CONSTRUCT     = 0x0000EB60;   // std::wstring(const wchar_t*)
static const uint32_t RVA_DELETE        = 0x0028D66E;   // the game's operator delete

static uint32_t base;

// ---- hook bodies ------------------------------------------------------------

// returns the pointer to where the actual content is, either to the internal
// buffer or to the location on the heap
static const wchar_t* Text(const WString* s)
{
    return s->capacity >= 8 ? s->pointer : s->buffer;
}

// releases the string content if it was stored on the heap due to size
static void Release(WString* s)
{
    if (s->capacity >= 8) {
        DeleteFn Delete = (DeleteFn)(base + RVA_DELETE);
        Delete(s->pointer);
    }
}

// copies text to out, returns the new end, never writes past last
static wchar_t* Append(wchar_t* out, wchar_t* last, const wchar_t* text)
{
    // stop at null byte termination or if last address was reached
    while (*text && out < last) {
        *out++ = *text++;
    }
    return out;
}

// called when the item name function returns, the name is already in out
extern "C" void OnItemName(void* names, WString* out, const Item* item)
{
    if (item->type != TYPE_PET_FOOD) {
        return;
    }
    CreatureNameFn CreatureName = (CreatureNameFn)(base + RVA_CREATURE_NAME);
    ConstructFn Construct = (ConstructFn)(base + RVA_CONSTRUCT);

    WString creatureName;
    // subType holds the id of the pet it tames
    CreatureName(names, &creatureName, item->subType);

    if (creatureName.size > 0) {
        // "<food> (<creature>)"
        wchar_t name[256];
        wchar_t* last = name + 255;
        wchar_t* end = Append(name, last, Text(out));
        end = Append(end, last, L" (");
        end = Append(end, last, Text(&creatureName));
        end = Append(end, last, L")");
        *end = 0;

        Release(out);           // release current pet food name content
        Construct(out, name);   // and write our modified version into it
    }

    Release(&creatureName);
}

// ---- hooks ------------------------------------------------------------------

struct HookSite {
    uint32_t rva;
    int length;
    uint8_t expected[10];
};

static const HookSite SITE_NAME = {
    0x00199FDD,
    10,
    { 0x8B, 0x4D, 0xF4, 0x64, 0x89, 0x0D, 0x00, 0x00, 0x00, 0x00 }
};

extern "C" {
    uint32_t jmp_back_name = 0;
}

extern "C" __attribute__((naked)) void HookStubName()
{
    asm("pushad");                              // push registers to stack
    asm("push dword ptr [ebp+0xc]");            // item, 3rd argument for next function call
    asm("push eax");                            // name, 2nd argument for next function call
    asm("push dword ptr [ebp-0x6a4]");          // name tables, 1st argument for next function call
    asm("call _OnItemName");                    // call function to add the creature to pet food
    asm("add esp, 12");                         // remove the arguments from the stack
    asm("popad");                               // pops stack back to registers
    asm("mov ecx, dword ptr [ebp-0xc]");        // original code
    asm("mov dword ptr fs:0, ecx");             // original code
    asm("jmp dword ptr [_jmp_back_name]");      // jump back
}

// ---- patching ---------------------------------------------------------------
static bool WriteHook(const HookSite& site, void* target)
{
    BYTE* location = (BYTE*)(base + site.rva);
    DWORD oldProtection;
    if (!VirtualProtect(location, site.length, PAGE_EXECUTE_READWRITE, &oldProtection)) {
        return false;
    }
    location[0] = 0xE9; // jmp rel32
    *(uint32_t*)(location + 1) = (uint32_t)target - (uint32_t)location - 5;
    for (int i = 5; i < site.length; i++) {
        location[i] = 0x90; // no-op the rest
    }
    VirtualProtect(location, site.length, oldProtection, &oldProtection);
    FlushInstructionCache(GetCurrentProcess(), location, site.length);
    return true;
}

extern "C" __declspec(dllexport) BOOL APIENTRY DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    if (fdwReason != DLL_PROCESS_ATTACH) {
        return TRUE;
    }

    base = (uint32_t)GetModuleHandle(NULL);

    // only patch if the hook site is valid
    if (memcmp((BYTE*)(base + SITE_NAME.rva), SITE_NAME.expected, SITE_NAME.length) != 0) {
        return TRUE;
    }

    jmp_back_name = base + SITE_NAME.rva + SITE_NAME.length;
    WriteHook(SITE_NAME, (void*)&HookStubName);
    return TRUE;
}
