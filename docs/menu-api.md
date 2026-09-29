# Fusion Fix menu API (version 1)

Resolve these C exports from `GetModuleHandleW(L"MaxPayne3.FusionFix.asi")` with `GetProcAddress`. No import library, C++ module, STL objects, or shared CRT is required. The ABI is Win32 `__cdecl`. Check every function pointer and require version 1 before calling. A missing API should leave the calling plugin working normally.

```c
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
struct FFMenuChoice { float value; const char* text; };
typedef float (__cdecl *FFMenuGetter)(void* context);
typedef void (__cdecl *FFMenuSetter)(void* context, float value);

uint32_t __cdecl FusionFix_MenuVersion(void);
int __cdecl FusionFix_MenuReady(void);
int __cdecl FusionFix_MenuQueueCallback(void (__cdecl *callback)(void*), void* context);
int __cdecl FusionFix_MenuSetOption(
    const char* id, uint32_t page, uint32_t kind,
    const char* label, const char* help,
    const struct FFMenuChoice* choices, uint32_t count,
    float minimum, float maximum, float step,
    FFMenuGetter getter, FFMenuSetter setter, void* context);
int __cdecl FusionFix_MenuRemoveOption(const char* id);
int __cdecl FusionFix_MenuSetTitle(const char* title);
int __cdecl FusionFix_MenuSetPage(const char* id, const char* title,
    const char* help, void (__cdecl *reset)(void*), void* context);
int __cdecl FusionFix_MenuSetText(const char* key, uint32_t language, const char* text);
#ifdef __cplusplus
}
#endif
```

`MenuReady` returns 1 after menu hooks initialize, otherwise 0. Use the optional `FusionFix_MenuQueueCallback` export to schedule registration from a plugin's initialization or render hook. It returns 1 when queued and invokes the callback once on the frontend thread after initialization, before settings controls are built. While a settings UI is alive, callbacks wait until it is closed. The callback can call the mutation exports normally. Queueing is thread-safe; direct mutations still require the frontend thread. Do not call from DllMain. Keep the callback provider and context loaded until execution; this API does not cancel queued callbacks.

The queue export was added to version 1; consumers must check its address and fall back gracefully when an older version-1 build lacks it. Queue errors are -3 when full, -4 for a null callback, and -5 on allocation failure.

Mutation results: 1 success; 0 ID not found (remove only); -1 not ready; -2 wrong thread; -3 menu busy; -4 invalid argument, duplicate label, or capacity exceeded; -5 internal failure. No C++ exceptions cross the exports.

Pages: 0 Display, 1 Graphics, 2 Controls, 3 Audio, 4 Fusion Fix Options. `MenuSetPage` adds a dedicated scrolling page after Fusion Fix, in registration order, or updates an existing page ID. It returns a stable handle >= 5 to pass to `MenuSetOption`, or a negative mutation error. Up to seven additional pages are supported. The reserved ID `FusionFix` cannot be replaced through this call. Titles and help have the same limits as option labels/help. A non-null reset callback adds a Restore Defaults row that invokes only that callback; null omits the row. The callback must remain loaded for the lifetime of its page (pages persist until process exit). `MenuSetTitle` changes both the Fusion Fix settings link and page heading.

Kinds: 0 choice list, 1 Off/On toggle, 2 native slider. Choice lists require 2–254 distinct finite values and nonempty choice labels. Toggles ignore choices and range arguments and use values 0 and 1. Sliders ignore choices and use minimum, maximum, and positive step, with at most 254 positions including endpoints. Slider labels use the range's decimal precision. Opening a slider preserves imported off-step values until it is changed.

`MenuSetOption` adds an ID or replaces its entire definition, including page, label, help, control, and callbacks. Use a namespaced ID such as `XboxRainDroplets.Enabled`. Built-in option IDs are their initial English labels from `RegisterOptions` in `source/menu.ixx` (for example `Additional FOV` or `HUD aspect ratio`). Labels must be unique across IDs. IDs and labels are 1–159 bytes; optional help is at most 1023 bytes. Strings and choice arrays are copied before returning; labels, help text, choices, and page titles use the translation lookup described below.

Getters and setters run on the frontend thread. Callback code and context must remain valid until removal or process exit; remove every registered option successfully before unloading its provider. Option getter/setter callbacks must not mutate the registry; queued registration callbacks may do so. Getter results must be finite; a failing getter falls back to zero (or the slider minimum). Setters own persistence and application of their settings. External options are not automatically written to Fusion Fix's CFG.

The Fusion Fix page's Restore Defaults resets only the 15 built-in Fusion Fix preferences and saves their CFG. Third-party settings remain owned by their providers. Removing or replacing a built-in menu row does not remove its underlying setting.

## Consumer example

`XboxRainDroplets/source/MaxPayne3.XboxRainDroplets.cpp` demonstrates delayed registration, a dedicated page, and persistent settings in its own CFG. It exposes Enabled plus every setting read by its INI loader: MinSize, MaxSize, MaxDrops, MaxMovingDrops, RadialMovement, EnableGravity, Refractions, SpeedAdjuster, MoveStep, BloodDrops, EnableSnow (BONUS), and ForceRain. The page follows Fusion Fix and resets only these settings. INI values are imported where CFG values are missing; edits save to `MaxPayne3.XboxRainDroplets.cfg`. Missing page API means normal INI-only behavior. Pool changes are applied by the rendering callback.

The C++ registry implementation lives directly in `source/menu.ixx`; HUD aspect-ratio calculations live in `source/widescreen.ixx`.

## Text and languages

Like IV FusionFix, the native GXT lookup checks custom text first and falls back to the game for other keys. MP3 menu controls receive short generated keys (`FFMENU_...`), so translated UTF-16 descriptions do not get truncated by the native 255-byte text buffer. Existing game GXT entries are untouched.

Translations live in separate UTF-8 files under `text/`, using IV FusionFix's GXT source layout:

```text
[Skip intro]
Skip intro

[Hide skip prompt]
Hide the cutscene skip prompt
```

Files: `americanFF.txt`, `frenchFF.txt`, `germanFF.txt`, `italianFF.txt`, `spanishFF.txt`, `japaneseFF.txt`, `russianFF.txt`, `brazilianFF.txt`, `polishFF.txt`, and `koreanFF.txt`. Bracketed English keys stay identical across languages; translate the text below each key. Blank lines separate entries. UTF-8 BOM and CRLF/LF are accepted. Missing or empty translations fall back to English. The build embeds these files directly in the ASI, so edits require rebuilding, without a separate GXT compiler or additional installation files. Product names and numeric choices remain unchanged.

Language IDs for `MenuSetText`: 0 English, 1 French, 2 German, 3 Italian, 4 Spanish, 5 Japanese, 6 Russian, 7 Brazilian Portuguese, 8 Polish, 9 Korean. The active language comes from the game variable that selects its GXT file. `MenuSetText` adds or replaces one UTF-8 translation, copying it before returning; use it inside a queued registration callback before constructing menus. Keys are 1-1023 bytes and translations at most 4095 bytes, valid UTF-8. Empty translations restore English fallback. Use namespaced keys and register language 0 as English; pass those keys as label/help/title/choice strings. Unregistered text displays literally. Option IDs remain language-independent.

`MenuSetPage` and `MenuSetText` are optional additive exports in ABI version 1. Check each function address before use. Translation mutations use the same ready/thread/menu-busy guards as option mutations.
