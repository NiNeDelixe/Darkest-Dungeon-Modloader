#include <windows.h>
#include <string>

extern "C" __declspec(dllexport) void ModInit()
{
    AllocConsole();
    FILE* f;
    freopen_s(&f, "CONOUT$", "w", stdout);
    printf("Hello from Example Mod!\n");
    MessageBoxA(NULL, "Mod Loaded!", "ExampleMod", MB_OK);
}

BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID)
{
    return TRUE;
}