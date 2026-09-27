// #include <windows.h>
// #include <cstdio>
// //#include <SDL.h>
// #include "MinHook.h"

// // ===== Указатели на оригинальные функции SDL =====
// typedef int (SDLCALL *PFN_SDL_PollEvent)(SDL_Event*);
// typedef void (SDLCALL *PFN_SDL_RenderPresent)(SDL_Renderer*);

// PFN_SDL_PollEvent  original_SDL_PollEvent  = nullptr;
// PFN_SDL_RenderPresent original_SDL_RenderPresent = nullptr;

// // ===== Рисование =====
// typedef int (SDLCALL *PFN_SDL_SetRenderDrawColor)(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8);
// typedef int (SDLCALL *PFN_SDL_RenderFillRect)(SDL_Renderer*, const SDL_Rect*);
// typedef int (SDLCALL *PFN_SDL_RenderDrawRect)(SDL_Renderer*, const SDL_Rect*);
// typedef int (SDLCALL *PFN_SDL_SetRenderDrawBlendMode)(SDL_Renderer*, SDL_BlendMode);
// typedef int (SDLCALL *PFN_SDL_GetRenderDrawColor)(SDL_Renderer*, Uint8*, Uint8*, Uint8*, Uint8*);
// typedef int (SDLCALL *PFN_SDL_GetRenderDrawBlendMode)(SDL_Renderer*, SDL_BlendMode*);
// typedef SDL_bool (SDLCALL *PFN_SDL_RenderIsClipEnabled)(SDL_Renderer*);
// typedef void (SDLCALL *PFN_SDL_RenderGetClipRect)(SDL_Renderer*, SDL_Rect*);
// typedef int (SDLCALL *PFN_SDL_RenderSetClipRect)(SDL_Renderer*, const SDL_Rect*);

// PFN_SDL_SetRenderDrawColor    pSDL_SetRenderDrawColor    = nullptr;
// PFN_SDL_RenderFillRect        pSDL_RenderFillRect        = nullptr;
// PFN_SDL_RenderDrawRect        pSDL_RenderDrawRect        = nullptr;
// PFN_SDL_SetRenderDrawBlendMode pSDL_SetRenderDrawBlendMode = nullptr;
// PFN_SDL_GetRenderDrawColor    pSDL_GetRenderDrawColor    = nullptr;
// PFN_SDL_GetRenderDrawBlendMode pSDL_GetRenderDrawBlendMode = nullptr;
// PFN_SDL_RenderIsClipEnabled   pSDL_RenderIsClipEnabled   = nullptr;
// PFN_SDL_RenderGetClipRect     pSDL_RenderGetClipRect     = nullptr;
// PFN_SDL_RenderSetClipRect     pSDL_RenderSetClipRect     = nullptr;

// // ===== Кнопка =====
// bool buttonHovered = false;
// SDL_Rect buttonRect = { 50, 50, 200, 60 };

// // ===== Рисование кнопки (без отсечения) =====
// void DrawButton(SDL_Renderer* renderer) {
//     if (!pSDL_SetRenderDrawBlendMode || !pSDL_SetRenderDrawColor ||
//         !pSDL_RenderFillRect || !pSDL_RenderDrawRect) return;

//     // Сохраняем текущие состояния
//     Uint8 r, g, b, a;
//     SDL_BlendMode oldBlend;
//     SDL_Rect oldClip;
//     SDL_bool clipEnabled = SDL_FALSE;

//     if (pSDL_GetRenderDrawColor && pSDL_GetRenderDrawBlendMode &&
//         pSDL_RenderIsClipEnabled && pSDL_RenderGetClipRect && pSDL_RenderSetClipRect) {

//         pSDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
//         pSDL_GetRenderDrawBlendMode(renderer, &oldBlend);
//         clipEnabled = pSDL_RenderIsClipEnabled(renderer);
//         if (clipEnabled)
//             pSDL_RenderGetClipRect(renderer, &oldClip);

//         // Отключаем отсечение на время рисования кнопки
//         pSDL_RenderSetClipRect(renderer, NULL);
//     }

//     // Рисуем кнопку
//     pSDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
//     pSDL_SetRenderDrawColor(renderer, buttonHovered ? 100 : 60, 60, 60, 180);
//     pSDL_RenderFillRect(renderer, &buttonRect);

//     pSDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
//     pSDL_RenderDrawRect(renderer, &buttonRect);

//     // Восстанавливаем исходные состояния
//     if (pSDL_SetRenderDrawColor && pSDL_SetRenderDrawBlendMode && pSDL_RenderSetClipRect) {
//         pSDL_SetRenderDrawColor(renderer, r, g, b, a);
//         pSDL_SetRenderDrawBlendMode(renderer, oldBlend);
//         if (clipEnabled)
//             pSDL_RenderSetClipRect(renderer, &oldClip);
//         else
//             pSDL_RenderSetClipRect(renderer, NULL);
//     }
// }

// // ===== Хуки =====
// int SDLCALL Hooked_SDL_PollEvent(SDL_Event* event) {
//     int result = original_SDL_PollEvent(event);
//     if (result && event) {
//         if (event->type == SDL_MOUSEMOTION) {
//             int mx = event->motion.x;
//             int my = event->motion.y;
//             buttonHovered = (mx >= buttonRect.x && mx <= buttonRect.x + buttonRect.w &&
//                              my >= buttonRect.y && my <= buttonRect.y + buttonRect.h);
//         } else if (event->type == SDL_MOUSEBUTTONDOWN) {
//             if (event->button.button == SDL_BUTTON_LEFT && buttonHovered) {
//                 MessageBoxA(NULL, "MODS button clicked!", "ExampleMod", MB_OK);
//             }
//         }
//     }
//     return result;
// }

// void SDLCALL Hooked_SDL_RenderPresent(SDL_Renderer* renderer) {
//     DrawButton(renderer);
//     original_SDL_RenderPresent(renderer);
// }

// // ===== Загрузка указателей на функции SDL =====
// bool GetSDLFunctionPointers() {
//     HMODULE sdl = GetModuleHandleA("SDL2.dll");
//     if (!sdl) return false;

//     #define LOAD(name) p##name = (PFN_##name)GetProcAddress(sdl, #name)
//     LOAD(SDL_SetRenderDrawColor);
//     LOAD(SDL_RenderFillRect);
//     LOAD(SDL_RenderDrawRect);
//     LOAD(SDL_SetRenderDrawBlendMode);
//     LOAD(SDL_GetRenderDrawColor);
//     LOAD(SDL_GetRenderDrawBlendMode);
//     LOAD(SDL_RenderIsClipEnabled);
//     LOAD(SDL_RenderGetClipRect);
//     LOAD(SDL_RenderSetClipRect);
//     #undef LOAD

//     // Для рисования обязательны только первые 4, остальные желательны
//     return pSDL_SetRenderDrawColor && pSDL_RenderFillRect &&
//            pSDL_RenderDrawRect && pSDL_SetRenderDrawBlendMode;
// }

// // ===== Установка хуков =====
// void InstallHooks() {
//     if (MH_Initialize() != MH_OK) {
//         MessageBoxA(NULL, "MinHook init failed", "Error", MB_OK);
//         return;
//     }

//     HMODULE sdl = GetModuleHandleA("SDL2.dll");
//     if (!sdl) {
//         MessageBoxA(NULL, "SDL2.dll not found", "Error", MB_OK);
//         return;
//     }

//     void* pollAddr    = GetProcAddress(sdl, "SDL_PollEvent");
//     void* presentAddr = GetProcAddress(sdl, "SDL_RenderPresent");

//     MH_CreateHook(pollAddr,    Hooked_SDL_PollEvent,    (void**)&original_SDL_PollEvent);
//     MH_CreateHook(presentAddr, Hooked_SDL_RenderPresent, (void**)&original_SDL_RenderPresent);
//     MH_EnableHook(MH_ALL_HOOKS);
// }

// // ===== Точка входа мода =====
// extern "C" __declspec(dllexport) void ModInit() {
//     AllocConsole();
//     freopen_s((FILE**)stdout, "CONOUT$", "w", stdout);
//     printf("[ExampleMod] Starting...\n");

//     if (!GetSDLFunctionPointers()) {
//         printf("[ExampleMod] Failed to get SDL function pointers\n");
//         return;
//     }
//     InstallHooks();
//     printf("[ExampleMod] Ready. Button should be visible now.\n");
// }

// BOOL APIENTRY DllMain(HMODULE, DWORD, LPVOID) {
//     return TRUE;
// }