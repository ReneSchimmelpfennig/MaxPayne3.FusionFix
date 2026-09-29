module;
#include <common.hxx>
#include <charconv>
#include <cmath>
#include <safetyhook.hpp>
#include <unordered_map>
#include <atomic>
#include <mutex>
#include <array>

export module menu;
import common;
import settings;

namespace FusionMenu
{
    enum class Page { Display, Graphics, Controls, Audio, FusionFix };
    enum class Control { Choices, Slider };
    struct Choice { float value; std::string text; };
    inline int DecimalPlaces(float value)
    {
        double scaled = value;
        for (int places = 0; places < 6; ++places, scaled *= 10)
            if (std::abs(scaled - std::round(scaled)) < 0.00001) return places;
        return 6;
    }
    inline std::string FormatNumber(float value, int places)
    {
        char buffer[64];
        auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::fixed, places);
        if (result.ec != std::errc{}) return "?";
        std::string text(buffer, result.ptr);
        if (text.find('.') != std::string::npos)
        {
            text.erase(text.find_last_not_of('0') + 1);
            if (text.back() == '.') text.pop_back();
        }
        return text == "-0" ? "0" : text;
    }
    struct Option
    {
        std::string id;
        std::string label;
        std::string help;
        std::vector<Choice> choices;
        std::function<float()> get;
        std::function<void(float)> set;
        Page page = Page::Display;
        Control control = Control::Choices;
        int decimalPlaces = 0;

        std::string SliderText() const { return FormatNumber(get(), decimalPlaces); }

        int SliderPosition() const
        {
            const float value = get();
            auto nearest = std::min_element(choices.begin(), choices.end(), [value](const Choice& a, const Choice& b)
            {
                return std::abs(a.value - value) < std::abs(b.value - value);
            });
            return static_cast<int>(nearest - choices.begin());
        }

        int Position() const
        {
            const auto value = get();
            auto it = std::find_if(choices.begin(), choices.end(), [value](const Choice& c)
            {
                return std::abs(c.value - value) <= 0.00001f;
            });
            return static_cast<int>(it - choices.begin()); // one extra "Custom" position
        }
        void Select(int position) const
        {
            if (position >= 0 && position < static_cast<int>(choices.size()) && get() != choices[position].value)
                set(choices[position].value);
        }
    };

    class Registry
    {
        std::vector<Option> options;
        bool sealed = false;
        static bool ValidText(std::string_view text)
        {
            return !text.empty() && text.size() < 160 && text.find('\0') == text.npos;
        }
    public:
        const std::vector<Option>& Options() const { return options; }
        void Seal() { sealed = true; }
        void Upsert(std::string id, Option option)
        {
            option.id = std::move(id);
            auto it = std::find_if(options.begin(), options.end(), [&](const Option& o) { return o.id == option.id; });
            if (it == options.end()) options.push_back(std::move(option));
            else *it = std::move(option);
        }
        bool Remove(std::string_view id)
        {
            auto it = std::find_if(options.begin(), options.end(), [&](const Option& o) { return o.id == id; });
            if (it == options.end()) return false;
            options.erase(it);
            return true;
        }

        bool AddChoices(std::string label, std::vector<Choice> choices, std::function<float()> get,
            std::function<void(float)> set, std::string help = {}, Page page = Page::Display)
        {
            if (sealed || options.size() >= 128 || !ValidText(label) || !get || !set || choices.size() < 2 || choices.size() > 254)
                return false;
            if (std::any_of(options.begin(), options.end(), [&](const Option& o) { return o.label == label; })) return false;
            for (size_t i = 0; i < choices.size(); ++i)
            {
                if (!std::isfinite(choices[i].value) || !ValidText(choices[i].text)) return false;
                for (size_t j = 0; j < i; ++j) if (choices[i].value == choices[j].value) return false;
            }
            options.push_back({ label, std::move(label), std::move(help), std::move(choices), std::move(get), std::move(set), page });
            return true;
        }
        bool AddToggle(std::string label, std::function<bool()> get, std::function<void(bool)> set, std::string help = {}, Page page = Page::Display)
        {
            if (!get || !set) return false;
            return AddChoices(std::move(label), { {0, "Off"}, {1, "On"} }, [get] { return get() ? 1.0f : 0.0f; },
                [set](float value) { set(value != 0); }, std::move(help), page);
        }
        bool AddEnum(std::string label, std::initializer_list<Choice> choices, std::function<int()> get,
            std::function<void(int)> set, std::string help = {}, Page page = Page::Display)
        {
            if (!get || !set) return false;
            for (auto& c : choices) if (c.value != std::trunc(c.value)) return false;
            return AddChoices(std::move(label), choices, [get] { return static_cast<float>(get()); },
                [set](float value) { set(static_cast<int>(value)); }, std::move(help), page);
        }
        // Stepped numeric choices use the game's left/right list control.
        bool AddRange(std::string label, float minimum, float maximum, float step,
            std::function<float()> get, std::function<void(float)> set, std::string help = {}, Page page = Page::Display)
        {
            if (!std::isfinite(minimum) || !std::isfinite(maximum) || !std::isfinite(step) || minimum >= maximum || step <= 0)
                return false;
            auto steps = std::ceil((static_cast<double>(maximum) - minimum) / step);
            if (steps < 1 || steps > 253) return false;
            std::vector<Choice> choices;
            const int places = std::max({ DecimalPlaces(minimum), DecimalPlaces(maximum), DecimalPlaces(step) });
            for (int i = 0; i <= static_cast<int>(steps); ++i)
            {
                float value = i == static_cast<int>(steps) ? maximum : static_cast<float>(minimum + i * static_cast<double>(step));
                if (!choices.empty() && value <= choices.back().value) return false;
                auto text = FormatNumber(value, places);
                choices.push_back({ value, std::move(text) });
            }
            if (!AddChoices(std::move(label), std::move(choices), std::move(get), std::move(set), std::move(help), page))
                return false;
            options.back().decimalPlaces = places;
            return true;
        }
        bool AddSlider(std::string label, float minimum, float maximum, float step,
            std::function<float()> get, std::function<void(float)> set, std::string help = {}, Page page = Page::Display)
        {
            if (!AddRange(std::move(label), minimum, maximum, step, std::move(get), std::move(set), std::move(help), page))
                return false;
            options.back().control = Control::Slider;
            return true;
        }
    };
}

using FusionMenu::Page;

namespace
{
    // Only the C functions are public. Keep the module-owned registry local so
    // MSVC never emits an exported inline variable with conflicting module names.
    FusionMenu::Registry& MenuRegistry()
    {
        static FusionMenu::Registry registry;
        return registry;
    }
}

namespace
{
// UTF-8 text catalog, selected by the game's GXT language (not Windows locale).
// English keys remain stable; missing translations fall back to the input text.
const unsigned int* gameLanguage = nullptr;
struct MenuText { std::string token; std::array<std::wstring, 10> languages; };
std::unordered_map<std::string, MenuText> menuText;
std::unordered_map<std::string, const MenuText*> gxtText;
std::mutex textMutex;
safetyhook::InlineHook gxtHook;
std::wstring WideText(const char* text)
{
    const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 menu text");
    std::wstring result(count, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), count);
    result.pop_back();
    return result;
}
// Store only a short key in the native 256-byte UIFontString buffer. Like IV's
// GXT extension, the lookup returns full UTF-16 text with the game's fallback.
const char* Text(const char* key)
{
    std::lock_guard lock(textMutex);
    auto& entry = menuText[key];
    if (entry.token.empty())
    {
        entry.token = "FFMENU_" + std::to_string(gxtText.size());
        if (entry.languages[0].empty()) entry.languages[0] = WideText(key);
        gxtText.emplace(entry.token, &entry);
    }
    return entry.token.c_str();
}
const wchar_t* __fastcall GetMenuText(void* text, void*, const char* key)
{
    if (key && std::strncmp(key, "FFMENU_", 7) == 0)
    {
        std::lock_guard lock(textMutex);
        auto found = gxtText.find(key);
        if (found != gxtText.end())
        {
            const unsigned int language = gameLanguage && *gameLanguage < 10 ? *gameLanguage : 0;
            const auto& values = found->second->languages;
            return (values[language].empty() ? values[0] : values[language]).c_str();
        }
    }
    return gxtHook.thiscall<const wchar_t*>(text, key);
}
void LoadMenuText()
{
    gameLanguage = *hook::get_pattern<const unsigned int*>("A1 ? ? ? ? 83 F8 09 77 43 FF 24 85 ? ? ? ? B8 ? ? ? ? C3 B8", 1);
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&LoadMenuText), &module);
    for (unsigned int language = 0; language < 10; ++language)
    {
        const auto name = L"FF_MENU_TEXT" + std::to_wstring(language);
        auto resource = FindResourceW(module, name.c_str(), RT_RCDATA);
        if (!resource) continue; // missing languages retain the English fallback
        auto data = static_cast<const char*>(LockResource(LoadResource(module, resource)));
        if (!data) continue;
        std::string_view remaining(data, SizeofResource(module, resource));
        if (remaining.starts_with("\xEF\xBB\xBF")) remaining.remove_prefix(3);
        std::string key, value;
        auto store = [&]
        {
            if (key.empty() || value.empty()) return;
            try { menuText[key].languages[language] = WideText(value.c_str()); }
            catch (...) { OutputDebugStringA("Fusion Fix: invalid UTF-8 translation; using English.\n"); }
        };
        // IV GXT source layout: [key], followed by text, with blank separators.
        while (!remaining.empty())
        {
            auto end = remaining.find('\n');
            auto line = remaining.substr(0, end);
            remaining.remove_prefix(end == remaining.npos ? remaining.size() : end + 1);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            if (line.empty()) continue;
            if (line.front() == '[' && line.back() == ']')
            {
                store();
                key = line.substr(1, line.size() - 2);
                value.clear();
            }
            else if (!key.empty())
            {
                if (!value.empty()) value += '\n';
                value += line;
            }
        }
        store();
    }
}

// RAGE UI classes are owned by the game. Offsets and virtual slots were checked
// against MS_Display, UISimpleList and UIScrollingStackLayout in MaxPayne3.exe.
using UI = void*;
template<class T> T& Field(UI object, size_t offset)
{ return *reinterpret_cast<T*>(static_cast<char*>(object) + offset); }
template<class R = void, class... A> R Virtual(UI object, size_t slot, A... args)
{ return reinterpret_cast<R(__thiscall*)(UI, A...)>((*reinterpret_cast<void***>(object))[slot / 4])(object, args...); }

void* (__cdecl* Allocate)(size_t);
UI (__thiscall* ConstructRow)(UI, const char*, const char*);
UI (__thiscall* ConstructList)(UI, const char*, const char*);
UI (__thiscall* ConstructScroll)(UI, const char*, const char*);
UI (__thiscall* ConstructBar)(UI, const char*, const char*);
void (__thiscall* InitializeBar)(UI, float, float, int, int, const char*, int, const char*, int, const char*);
void (__thiscall* SetBar)(UI, float);
void (__thiscall* InitializeList)(UI, int, const char*, int, int);
void (__thiscall* InitializeScroll)(UI, int, float, int);
void (__thiscall* AddChoice)(UI, const char*, int, int);
void (__thiscall* SelectChoice)(UI, int);
float (__stdcall* TuningFloat)(int);
const float* (__stdcall* TuningVector)(int);
int (__thiscall* FindTextureDictionary)(UI, const char*);
UI textureDictionary;
safetyhook::InlineHook findHook, updateHook, barUpdateHook;
safetyhook::InlineHook optionsBuildHook, optionsCloseHook, optionsInputHook, optionsClickHook, optionsHoverHook;
UI (__thiscall* GetInput)(UI);
void (__cdecl* PlayMenuSelect)();
void (__cdecl* PlayMenuBack)();
const int* (__thiscall* ReadInput)(UI, int, int);
void (__thiscall* ConsumeInput)(UI, int);
UI inputManager;
std::atomic<DWORD> apiThread = 0;
std::atomic<bool> apiReady = false;
struct MenuCallback { void (__cdecl* function)(void*); void* context; };
std::mutex menuCallbackMutex;
std::vector<MenuCallback> menuCallbacks;
struct CustomPage
{
    std::string id, title, help;
    void (__cdecl* reset)(void*) = nullptr;
    void* context = nullptr;
};
std::vector<CustomPage> customPages = {{"FusionFix", "FUSION FIX OPTIONS", "Customize Fusion Fix features.",
    [](void*) { CSettings::ResetDefaults(); }, nullptr}};
struct OptionsState
{
    std::vector<std::pair<UI, unsigned int>> links;
    UI reset = nullptr;
    UI focus = nullptr;
    bool open = false;
    unsigned int page = 4;
};
void ResetPage(const OptionsState& options)
{
    const auto& page = customPages.at(options.page - 4);
    if (page.reset) try { page.reset(page.context); } catch (...) { OutputDebugStringA("Fusion Fix: reset callback failed.\n"); }
}
std::unordered_map<UI, OptionsState> optionsScreens;
struct PageAdapter
{
    FusionMenu::Page page;
    size_t stack, header, help;
    safetyhook::InlineHook build, close;
};
PageAdapter pages[] = {
    {Page::Display, 268, 288, 292}, {Page::Graphics, 340, 344, 348},
    {Page::Controls, 272, 264, 268}, {Page::Audio, 280, 272, 276}
};

struct Binding { const FusionMenu::Option* option; int shown; UI row; };
struct Screen
{
    UI scroll;
    UI stack;
    UI help;
    std::string stackName;
    std::unordered_map<UI, Binding> lists;
};
std::unordered_map<UI, Screen> screens;

void DispatchMenuCallbacks()
{
    if (!apiReady) return;
    apiThread = GetCurrentThreadId();
    if (!screens.empty() || !optionsScreens.empty()) return;
    std::vector<MenuCallback> pending;
    {
        std::lock_guard lock(menuCallbackMutex);
        pending.swap(menuCallbacks);
    }
    for (const auto& callback : pending)
    {
        try { callback.function(callback.context); }
        catch (...) { OutputDebugStringA("Fusion Fix: menu registration callback failed.\n"); }
    }
}

void Anchor(UI object, int point, UI relative, int relativePoint, float x = 0, float y = 0)
{
    // UIOffset consists of two { float pixels; float scale; uint32_t reference; } axes.
    Virtual(object, 276, point, Virtual<uint32_t>(relative, 88), relativePoint, x, 0.0f, 0, y, 0.0f, 0);
}

void __cdecl Debug(const char* text) { OutputDebugStringA(text); }

UI __cdecl FindUI(const char* name)
{
    // The stock Display input handlers find their stack by this literal name.
    // Redirect only while that screen's scrolling replacement is alive.
    for (auto& [owner, state] : screens)
        if (name && state.stackName == name) return state.stack;
    return findHook.call<UI>(name);
}

void AddRow(UI screen, Screen& state, const FusionMenu::Option& option, size_t index, int dictionary)
{
    auto prefix = std::string("FF.") + Virtual<const char*>(screen, 84) + "." + std::to_string(index);
    auto listName = prefix + ".List";
    const bool slider = option.control == FusionMenu::Control::Slider;
    auto list = slider ? ConstructBar(Allocate(328), listName.c_str(), Virtual<const char*>(screen, 84)) :
        ConstructList(Allocate(340), listName.c_str(), Virtual<const char*>(screen, 84));
    auto row = ConstructRow(Allocate(316), prefix.c_str(), Virtual<const char*>(state.scroll, 84));
    Virtual(row, 656, Text(option.label.c_str()));
    // Localized labels use the native GXT lookup, including UTF-16 fonts.
    Field<uint8_t>(Field<UI>(row, 224), 474) = 0;
    Virtual(row, 684, 0);
    Virtual(row, 672, 4, 10, 8);
    Virtual(row, 56, 1);
    Virtual(row, 712, list, 0, 0.0f);
    Virtual(row, 724, static_cast<int>(0x100 + index));
    Virtual(row, 520, Virtual<uint32_t>(state.scroll, 88));
    // Scrolling stacks enable row activation when attaching children. Settings
    // labels must not invoke the stack's page-navigation action; only their
    // value controls handle clicks. Keep hover/focus enabled independently.
    Virtual(row, 740, 0); // prevent SetSelectable from enabling clicks again
    Virtual(row, 548, 0);
    const auto position = slider ? option.SliderPosition() : option.Position();
    if (slider)
    {
        InitializeBar(list, 0.0f, static_cast<float>(option.choices.size() - 1), 0,
            dictionary, "bar_fill", dictionary, "bar_fill", dictionary, "bar_icon");
        Virtual(list, 156, 1.0f, Virtual<uint32_t>(row, 88));
        Virtual(list, 148, TuningFloat(62));
        Virtual(Field<UI>(list, 256), 348, 13);
        Virtual(Field<UI>(list, 252), 348, 14);
        SetBar(list, static_cast<float>(position));
        const auto text = option.SliderText();
        Virtual(Field<UI>(list, 264), 92, text.c_str());
    }
    else
    {
        InitializeList(list, dictionary, "list_arrow", 1, 0);
        Virtual(Field<UI>(list, 232), 360, 0);
        for (const auto& choice : option.choices) AddChoice(list, Text(choice.text.c_str()), -1, 0);
        AddChoice(list, Text("Custom"), -1, 0);
        SelectChoice(list, position);
    }
    Virtual(list, 56, 0);
    state.lists.emplace(list, Binding{&option, position, row});
}

UI AddAction(UI screen, UI parent, const char* name, const char* label, int id)
{
    auto row = ConstructRow(Allocate(316), name, Virtual<const char*>(parent, 84));
    Virtual(row, 656, Text(label));
    Field<uint8_t>(Field<UI>(row, 224), 474) = 0;
    Virtual(row, 684, 0);
    Virtual(row, 672, 4, 10, 8);
    Virtual(row, 56, 1);
    Virtual(row, 724, id);
    Virtual(row, 548, 1);
    Virtual(row, 520, Virtual<uint32_t>(screen, 88));
    Virtual(row, 532, Virtual<uint32_t>(screen, 88));
    return row;
}

void PositionOptionsHeader(UI screen, bool custom)
{
    auto header = Field<UI>(screen, 264);
    auto offset = TuningVector(custom ? 5 : 2);
    Virtual(header, 296);
    Virtual(header, 276, 2, uint32_t(2139856676), 2, offset[0], 0.0f, 0,
        offset[1] - (custom ? 60.0f : 0.0f), 0.0f, 0);
}

void OpenFusionPage(UI screen, unsigned int page)
{
    auto& options = optionsScreens.at(screen);
    if (options.open) return;
    options.open = true;
    options.page = page;
    options.focus = Field<UI>(screen, 232);
    auto oldStack = Field<UI>(screen, 272);
    Virtual(oldStack, 60, 0);
    Virtual(oldStack, 308, 0);
    auto scroll = ConstructScroll(Allocate(344), "FF.Options.Scroll", Virtual<const char*>(screen, 84));
    InitializeScroll(scroll, 0, TuningFloat(6), 0);
    Field<int>(scroll, 264) = 12;
    Field<uint8_t>(scroll, 277) = 1;
    Field<uint8_t>(scroll, 278) = 1;
    auto stack = Field<UI>(scroll, 288);
    auto header = Field<UI>(screen, 264);
    auto help = Field<UI>(screen, 268);
    auto& state = screens[screen];
    state = {scroll, stack, help, "FF.Options.Stack", {}};
    PositionOptionsHeader(screen, true);
    Virtual(header, 92, Text(customPages.at(page - 4).title.c_str()));
    Field<uint8_t>(header, 474) = 0;
    auto offset = TuningVector(0);
    Anchor(scroll, 6, header, 18, offset[0], offset[1]);
    std::memcpy(static_cast<char*>(stack) + 264, static_cast<char*>(oldStack) + 264, 20);
    int dictionary = FindTextureDictionary(textureDictionary, "titlescreen");
    if (dictionary == -1) dictionary = FindTextureDictionary(textureDictionary, "pausemenu");
    for (size_t i = 0; i < MenuRegistry().Options().size(); ++i)
        if (static_cast<unsigned int>(MenuRegistry().Options()[i].page) == page)
            AddRow(screen, state, MenuRegistry().Options()[i], i, dictionary);
    if (customPages.at(page - 4).reset)
        options.reset = AddAction(screen, scroll, "FF.Options.RestoreDefaults", "RESTORE DEFAULTS", 0x400);
    Virtual(help, 296);
    offset = TuningVector(8);
    Anchor(help, 6, header, 6, offset[0], offset[1]);
    Anchor(help, 8, scroll, 8);
    Field<UI>(screen, 232) = stack;
    Virtual(stack, 764, 0);
    Virtual(scroll, 308, 1);
    Virtual(scroll, 488);
    Virtual(scroll, 60, 1);
    PlayMenuSelect();
}

void CloseFusionPage(UI screen)
{
    auto& options = optionsScreens.at(screen);
    if (!options.open) return;
    auto state = screens.at(screen);
    screens.erase(screen);
    options.open = false;
    options.reset = nullptr;
    Field<UI>(screen, 232) = options.focus;
    Virtual(state.scroll, 60, 0);
    Virtual(state.scroll, 308, 0);
    Virtual(state.scroll, 332, 1);
    auto header = Field<UI>(screen, 264);
    auto stack = Field<UI>(screen, 272);
    PositionOptionsHeader(screen, false);
    Virtual(header, 92, "MO_OPTIONS");
    Field<uint8_t>(header, 474) = 0;
    Virtual(state.help, 296);
    auto offset = TuningVector(8);
    Anchor(state.help, 6, header, 6, offset[0], offset[1]);
    Anchor(state.help, 8, stack, 8);
    Virtual(stack, 308, 1);
    Virtual(stack, 488);
    Virtual(stack, 60, 1);
    CSettings::Save();
}

int __fastcall BuildOptions(UI screen, void*, UI previous)
{
    DispatchMenuCallbacks(); // register before the Settings/Fusion Fix UI is built
    auto result = optionsBuildHook.thiscall<int>(screen, previous);
    MenuRegistry().Seal();
    auto stack = Field<UI>(screen, 272);
    std::vector<UI> tail;
    bool afterGraphics = false;
    for (int i = 0, n = Virtual<int>(stack, 612); i < n; ++i)
    {
        auto row = Virtual<UI>(stack, 632, i);
        if (afterGraphics) tail.push_back(row);
        if (std::string_view(Virtual<const char*>(row, 84)) == "MS_Options.Graphics") afterGraphics = true;
    }
    for (auto row : tail) Virtual(stack, 620, Virtual<uint32_t>(row, 88));
    auto& options = optionsScreens[screen];
    for (unsigned int i = 0; i < customPages.size(); ++i)
    {
        auto name = "FF.Options.Link." + std::to_string(i);
        auto link = AddAction(screen, stack, name.c_str(), customPages[i].title.c_str(), 0x400 + i);
        options.links.emplace_back(link, i + 4);
    }
    for (auto row : tail) Virtual(stack, 616, Virtual<uint32_t>(row, 88));
    Virtual(stack, 488);
    return result;
}

int __fastcall CloseOptions(UI screen, void*)
{
    if (optionsScreens.contains(screen)) CloseFusionPage(screen);
    optionsScreens.erase(screen);
    return optionsCloseHook.thiscall<int>(screen);
}

int __fastcall OptionsInput(UI screen, void*)
{
    auto found = optionsScreens.find(screen);
    if (found == optionsScreens.end()) return optionsInputHook.thiscall<int>(screen);
    auto& options = found->second;
    auto input = GetInput(inputManager);
    const int event = *ReadInput(input, 96, 0);
    if (options.open)
    {
        if (event == 7 || event == 13)
        {
            CloseFusionPage(screen);
            PlayMenuBack();
        }
        else if (event == 8 && Virtual<UI>(screens.at(screen).stack, 768) == options.reset)
            ResetPage(options);
        else if (event != 8) return 0;
        ConsumeInput(input, 1);
        return 1;
    }
    if (event == 8)
        for (const auto& [link, page] : options.links)
            if (Virtual<UI>(Field<UI>(screen, 272), 768) == link)
            {
                OpenFusionPage(screen, page);
                ConsumeInput(input, 1);
                return 1;
            }
    return optionsInputHook.thiscall<int>(screen);
}

int __fastcall OptionsClick(UI screen, void*, uint32_t hash)
{
    auto found = optionsScreens.find(screen);
    if (found != optionsScreens.end())
    {
        if (found->second.open)
        {
            if (found->second.reset && hash == Virtual<uint32_t>(found->second.reset, 88)) ResetPage(found->second);
            return 1;
        }
        for (const auto& [link, page] : found->second.links)
            if (hash == Virtual<uint32_t>(link, 88)) { OpenFusionPage(screen, page); return 1; }
    }
    return optionsClickHook.thiscall<int>(screen, hash);
}

int __fastcall OptionsHover(UI screen, void*, uint32_t hash)
{
    auto found = optionsScreens.find(screen);
    if (found == optionsScreens.end()) return optionsHoverHook.thiscall<int>(screen, hash);
    auto stack = found->second.open ? screens.at(screen).stack : Field<UI>(screen, 272);
    for (int i = 0, n = Virtual<int>(stack, 612); i < n; ++i)
        if (Virtual<uint32_t>(Virtual<UI>(stack, 632, i), 88) == hash)
        { Virtual(stack, 696, i, 1); break; }
    return 1;
}

template<size_t P> int __fastcall BuildPage(UI screen, void*, UI previous)
{
    auto& page = pages[P];
    auto result = page.build.thiscall<int>(screen, previous);
    MenuRegistry().Seal();
    if (screens.contains(screen) || std::none_of(MenuRegistry().Options().begin(), MenuRegistry().Options().end(),
        [&](const auto& option) { return option.page == page.page; })) return result;

    auto oldStack = Field<UI>(screen, page.stack);
    if (!oldStack) return result;
    const std::string stackName = Virtual<const char*>(oldStack, 84);
    std::vector<UI> stockRows;
    auto count = Virtual<int>(oldStack, 612);
    for (int i = 0; i < count; ++i) stockRows.push_back(Virtual<UI>(oldStack, 632, i));
    auto scrollName = std::string("FF.") + Virtual<const char*>(screen, 84) + ".Scroll";
    auto scroll = ConstructScroll(Allocate(344), scrollName.c_str(), Virtual<const char*>(screen, 84));
    InitializeScroll(scroll, 0, TuningFloat(6), 0);
    Field<int>(scroll, 264) = 8; // native clipping, arrows, mouse wheel and controller navigation
    Field<uint8_t>(scroll, 277) = 1;
    Field<uint8_t>(scroll, 278) = 1;
    auto stack = Field<UI>(scroll, 288);
    auto& state = screens[screen];
    state = {scroll, stack, Field<UI>(screen, page.help), stackName, {}};
    auto offset = TuningVector(0);
    auto header = Field<UI>(screen, page.header);
    // Use the native Graphics page's higher title position for the scrolling area.
    auto titleOffset = TuningVector(5);
    Virtual(header, 296);
    Virtual(header, 276, 2, uint32_t(2139856676), 2, titleOffset[0], 0.0f, 0,
        titleOffset[1] - 60.0f, 0.0f, 0);
    Anchor(scroll, 6, header, 18, offset[0], offset[1]);

    int dictionary = FindTextureDictionary(textureDictionary, "titlescreen");
    if (dictionary == -1) dictionary = FindTextureDictionary(textureDictionary, "pausemenu");
    auto footer = std::find_if(stockRows.begin(), stockRows.end(), [](UI row)
    { return std::string_view(Virtual<const char*>(row, 84)).ends_with(".RestoreDefaults"); });
    // Keep the native separator with the reset action at the bottom.
    if (footer != stockRows.end() && footer != stockRows.begin())
    {
        const std::string_view name = Virtual<const char*>(*(footer - 1), 84);
        if (name.find(".Empty") != name.npos || name.find(".Space") != name.npos) --footer;
    }
    auto addOptions = [&]
    {
        for (size_t i = 0; i < MenuRegistry().Options().size(); ++i)
            if (MenuRegistry().Options()[i].page == page.page)
                AddRow(screen, state, MenuRegistry().Options()[i], i, dictionary);
    };

    // Reparent through the native container APIs, so clipping and focus targets
    // are installed by UIScrollingStackLayout rather than copied by hand.
    for (auto it = stockRows.begin(); it != stockRows.end(); ++it)
    {
        if (it == footer) addOptions();
        auto row = *it;
        const auto id = Virtual<uint32_t>(row, 88);
        Virtual(oldStack, 620, id);
        Virtual(row, 104, Virtual<uint32_t>(scroll, 88));
        Virtual(scroll, 616, id);
    }
    // Keep the stock selection sound callback on the new inner stack.
    std::memcpy(static_cast<char*>(stack) + 264, static_cast<char*>(oldStack) + 264, 20);
    if (footer == stockRows.end()) addOptions();

    Field<UI>(screen, page.stack) = stack;
    Field<UI>(screen, 232) = stack;
    // Help text follows the visible container, not the full scrollable content.
    Virtual(state.help, 296);
    auto helpOffset = TuningVector(8);
    Anchor(state.help, 6, header, 6, helpOffset[0], helpOffset[1]);
    Anchor(state.help, 8, scroll, 8);
    if (page.page == Page::Graphics)
    {
        auto memoryTitle = Field<UI>(screen, 364);
        Virtual(memoryTitle, 296);
        Anchor(memoryTitle, 6, scroll, 18, 4.0f, 20.0f);
    }
    Virtual(oldStack, 300, 0);
    Virtual(oldStack, 332, 1);
    Virtual(scroll, 308, 1);
    Virtual(scroll, 488);
    Virtual(scroll, 60, 1);
    return result;
}

template<size_t P> int __fastcall ClosePage(UI screen, void*)
{
    auto& page = pages[P];
    auto found = screens.find(screen);
    if (found == screens.end()) return page.close.thiscall<int>(screen);
    // MS_Display first saves the stock values, then destroys its stack. Let it
    // destroy the owning wrapper, including all rows, lists and the inner stack.
    Field<UI>(screen, page.stack) = found->second.scroll;
    Field<UI>(screen, 232) = nullptr;
    screens.erase(found);
    auto result = page.close.thiscall<int>(screen);
    CSettings::Save();
    return result;
}

void __fastcall UpdateBar(UI bar, void*)
{
    barUpdateHook.thiscall<void>(bar);
    for (auto& [owner, state] : screens)
    {
        auto found = state.lists.find(bar);
        if (found == state.lists.end()) continue;
        auto& binding = found->second;
        const int position = static_cast<int>(std::lround(Field<float>(bar, 216)));
        if (position != binding.shown) binding.option->Select(position);
        const int desired = binding.option->SliderPosition();
        if (position != desired) SetBar(bar, static_cast<float>(desired));
        binding.shown = desired;
        // Native bars count integer steps; show the actual setting, including decimals.
        const auto text = binding.option->SliderText();
        Virtual(Field<UI>(bar, 264), 92, text.c_str());
        break;
    }
}

void __fastcall UpdateList(UI list, void*)
{
    // Observe the final value AFTER wrapping and the game's deferred selection.
    // The old increment hooks observed count/-1 before the native wrap.
    updateHook.thiscall<void>(list);
    for (auto& [owner, state] : screens)
    {
        auto found = state.lists.find(list);
        if (found == state.lists.end()) continue;
        auto& binding = found->second;
        int position = Field<int>(list, 228);
        if (position != binding.shown)
        {
            if (position == static_cast<int>(binding.option->choices.size()))
            {
                position = binding.shown == position - 1 ? 0 : position - 1;
                SelectChoice(list, position);
            }
            binding.option->Select(position);
            binding.shown = position;
        }
        auto desired = binding.option->Position();
        if (desired != binding.shown)
        {
            SelectChoice(list, desired);
            binding.shown = desired;
        }
        break;
    }
}

void UpdateHelp()
{
    DispatchMenuCallbacks();
    for (auto& [owner, state] : screens)
    {
        auto selected = Virtual<UI>(state.stack, 768);
        auto binding = std::find_if(state.lists.begin(), state.lists.end(), [selected](const auto& item)
        { return item.second.row == selected; });
        if (binding != state.lists.end())
        {
            auto& option = *binding->second.option;
            const char* text = CSettings::LastSaveSucceeded() ? option.help.c_str() :
                "Could not save MaxPayne3.FusionFix.cfg in any supported location. Changes remain in memory.";
            Virtual(state.help, 92, Text(text));
            Field<uint8_t>(state.help, 474) = 0;
        }
        else if (auto options = optionsScreens.find(owner); options != optionsScreens.end() && options->second.open)
        {
            Virtual(state.help, 92, Text("Restore this page's defaults."));
            Field<uint8_t>(state.help, 474) = 0;
        }
        else Field<uint8_t>(state.help, 474) = 0;
    }
    for (auto& [owner, options] : optionsScreens)
    {
        if (options.open) continue;
        auto help = Field<UI>(owner, 268);
        for (const auto& [link, page] : options.links)
            if (Virtual<UI>(Field<UI>(owner, 272), 768) == link)
            {
                Virtual(help, 92, Text(customPages.at(page - 4).help.c_str()));
                break;
            }
        Field<uint8_t>(help, 474) = 0;
    }
}

void RegisterOptions()
{
    auto toggle = [](const char* label, Pref pref, const char* help, Page page = Page::FusionFix)
    {
        MenuRegistry().AddToggle(label, [pref] { return CSettings::GetInt(pref) != 0; },
            [pref](bool value) { CSettings::SetInt(pref, value); }, help, page);
    };
    auto range = [](const char* label, Pref pref, float maximum, float step, const char* help, Page page = Page::FusionFix)
    {
        MenuRegistry().AddSlider(label, 0, maximum, step, [pref] { return CSettings::GetFloat(pref); },
            [pref](float value) { CSettings::SetFloat(pref, value); }, help, page);
    };
    toggle("Skip intro", PREF_SKIPINTRO, "Skip the opening videos and legal screens. Takes effect next time the game starts.");
    toggle("Hide skip prompt", PREF_HIDESKIP, "Hide the cutscene skip button prompt.");
    toggle("Disable leaderboards", PREF_DISABLELEADERBOARDS, "Disable global leaderboards to prevent the Hoboken Alleys co-op crash.");
    range("Subtitle size", PREF_SUBTITLESIZE, 2.0f, 0.1f, "Subtitle scale. 1 is the original size.");
    range("Outline size", PREF_OUTLINESIZE, 2.0f, 0.1f, "Subtitle outline scale. 1 is the original size.");
    toggle("Borderless window", PREF_BORDERLESS, "Remove borders in windowed mode. Select windowed mode in Graphics first.", Page::FusionFix);
    toggle("LIGHTSYNC RGB", PREF_LEDILLUMINATION, "Logitech lighting effects. Requires Logitech G HUB and supported hardware.", Page::FusionFix);
    MenuRegistry().AddEnum("Controller icons", {{0,"Xbox 360"},{1,"Xbox One"},{2,"PlayStation 3"},{3,"PlayStation 4"},
        {4,"PlayStation 5"},{5,"Nintendo Switch"},{6,"Steam Deck"},{7,"Steam Controller"}},
        [] { return CSettings::GetInt(PREF_BUTTONS); }, [](int v) { CSettings::SetInt(PREF_BUTTONS, v); },
        "Choose the controller button artwork.", Page::FusionFix);
    toggle("Ignore device changes", PREF_DEVICECHANGE, "Prevent device-change notifications from randomly opening the pause menu.", Page::FusionFix);
    MenuRegistry().AddChoices("HUD aspect ratio", {{-1,"Auto"},{4.0f/3,"4:3"},{16.0f/10,"16:10"},{16.0f/9,"16:9"},
        {21.0f/9,"21:9"},{32.0f/9,"32:9"}}, [] { return CSettings::GetFloat(PREF_HUDASPECTRATIOCONSTRAINT); },
        [](float v) { CSettings::SetFloat(PREF_HUDASPECTRATIOCONSTRAINT, v); },
        "Center the HUD within this aspect ratio, capped to the screen width. Auto uses the full screen.", Page::FusionFix);
    range("Additional FOV", PREF_CUSTOMFOV, 45, 1, "Add up to 45 degrees to the gameplay field of view. 0 uses the original FOV.");
    MenuRegistry().AddEnum("Console gamma", {{0,"Off"},{1,"Xbox 360"},{2,"PlayStation 3"}},
        [] { return CSettings::GetInt(PREF_CONSOLEGAMMA); }, [](int v) { CSettings::SetInt(PREF_CONSOLEGAMMA, v); },
        "Use the original PC gamma or a console gamma curve.", Page::FusionFix);
    toggle("SMAA", PREF_SMAA, "Enhanced Subpixel Morphological Antialiasing.", Page::FusionFix);
}

void Initialize()
{
    RegisterOptions();
    try
    {
        LoadMenuText();
        auto getText = hook::get_pattern("8B 44 24 04 83 EC 40 56 8B F1 85 C0 75 34 E8 ? ? ? ? C6 44 24 04 00 8D 44 24 04");
        auto build = hook::get_pattern("83 EC 38 53 55 56 8B F1 57 B9 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? 8B E8 83 FD FF");
        auto close = hook::get_pattern("56 57 8B F1 E8 ? ? ? ? 8B 8E 20 01 00 00 33 FF 3B CF 74 12");
        auto optionsBuild = hook::get_pattern("81 EC 34 01 00 00 53 55 56 8B F1 57 B9 ? ? ? ? E8 ? ? ? ? 8B 0D");
        auto optionsClose = hook::get_pattern("56 8B F1 8B 8E 18 01 00 00 57 33 FF 3B CF 74 0E 8B 01 8B 10");
        auto optionsInput = hook::get_pattern("83 EC 0C 55 56 8B F1 B9 ? ? ? ? E8 ? ? ? ? 8B E8 6A 00 6A 60 8B CD E8 ? ? ? ? F3 0F 7E 00 8B 40 08 66 0F D6 44 24 08 89 44 24 10 8B 44 24 08 83 F8 07 0F 84 CC 00 00 00");
        auto optionsClick = hook::get_pattern("8B 44 24 04 53 56 57 50 8B F9 E8 ? ? ? ? 83 C4 04 8B F0 E8 ? ? ? ? 8B 16 8A D8");
        auto optionsHover = hook::get_pattern("56 8B F1 8B 8E 10 01 00 00 8B 01 8B 90 00 03 00 00 FF D2 8B 10 8B C8 8B 42 58 FF D0 8B 4C 24 08");
        inputManager = Field<UI>(optionsInput, 8);
        GetInput = injector::GetBranchDestination(static_cast<char*>(optionsInput) + 12).get();
        auto selectSoundCall = hook::get_pattern("FF D0 8B CF 89 87 F0 00 00 00 E8 ? ? ? ? 5F 5E 5B C2 04 00 83 F8 03", 10);
        PlayMenuSelect = injector::GetBranchDestination(selectSoundCall).get();
        auto backSoundCall = hook::get_pattern("8B CE C7 86 14 01 00 00 00 00 00 00 E8 ? ? ? ? 80 3D ? ? ? ? 00 74 7A", 12);
        PlayMenuBack = injector::GetBranchDestination(backSoundCall).get();
        ReadInput = reinterpret_cast<decltype(ReadInput)>(hook::get_pattern("83 B9 54 01 00 00 00 56 8D B1 54 01 00 00 74 17 80 B9 9C 01 00 00 00 75 0E"));
        ConsumeInput = reinterpret_cast<decltype(ConsumeInput)>(hook::get_pattern("8A 44 24 04 56 8B F1 88 86 9C 01 00 00 84 C0 74 15"));
        auto barUpdate = hook::get_pattern("56 8B F1 8B 86 00 01 00 00 8B 8E FC 00 00 00 8B 11 57 8B 38 8B 42 58 FF D0");
        ConstructBar = reinterpret_cast<decltype(ConstructBar)>(hook::get_pattern("8B 44 24 08 56 6A 00 8B F1 8B 4C 24 0C 50 51 8B CE E8 ? ? ? ? 68 FF 00 00 00 68 FF 00 00 00 68 FF 00 00 00 68 FF 00 00 00 8D 8E F4 00 00 00 C7 06 ? ? ? ? E8 ? ? ? ? 68 FF 00 00 00"));
        InitializeBar = reinterpret_cast<decltype(InitializeBar)>(hook::get_pattern("83 EC 2C 53 56 8B F1 8B 06 8B 90 74 01 00 00 57 FF D2 84 C0 0F 85"));
        SetBar = reinterpret_cast<decltype(SetBar)>(hook::get_pattern("F3 0F 10 54 24 04 83 EC 14 56 8B F1 F3 0F 10 86 DC 00 00 00"));
        void* builds[] = {build,
            hook::get_pattern("81 EC 04 01 00 00 53 55 56 8B F1 33 DB 57 38 9E 61 01 00 00 74 24"),
            hook::get_pattern("83 EC 34 55 56 8B F1 57 B9 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 6A 00"),
            hook::get_pattern("83 EC 38 53 55 56 8B F1 57 B9 ? ? ? ? E8 ? ? ? ? 68 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? 8B 0D ? ? ? ? 6A 02 8B E8")};
        void* closes[] = {close,
            hook::get_pattern("56 57 8B F1 8B 06 8B 90 34 01 00 00 33 FF 57 FF D2 8B 06 8B 90 E8 01 00 00 8B CE FF D2"),
            hook::get_pattern("56 8B F1 8B 8E 10 01 00 00 85 C9 74 28 8B 01 8B 90 2C 01 00 00 6A 00"),
            hook::get_pattern("56 57 8B F1 E8 ? ? ? ? 8B 8E 18 01 00 00 33 FF 3B CF 74 23")};
        auto update = hook::get_pattern("83 EC 0C 56 8B F1 80 BE 13 01 00 00 00 74 1C 83 BE 08 01 00 00 00 74 13");
        auto findCall = hook::get_pattern("E8 ? ? ? ? 8B 10 8B C8 8B 82 00 03 00 00 83 C4 04 FF D0 8B D8 8B 44 24 10 83 F8 07");
        void* find = injector::GetBranchDestination(findCall).get();
        Allocate = reinterpret_cast<decltype(Allocate)>(hook::get_pattern("B8 10 00 00 00 8B 4C 24 04 E9 ? ? ? ? CC CC 8B 4C 24 04 8B 44 24 08 3B C8"));
        ConstructRow = reinterpret_cast<decltype(ConstructRow)>(hook::get_pattern("8B 44 24 08 56 8B F1 8B 4C 24 08 50 51 8B CE E8 ? ? ? ? 33 C0 89 86 2C 01 00 00 89 86 34 01 00 00"));
        ConstructList = reinterpret_cast<decltype(ConstructList)>(hook::get_pattern("8B 44 24 08 53 56 33 DB 53 8B F1 8B 4C 24 10 50 51 8B CE E8 ? ? ? ? C7 06 ? ? ? ? 33 D2 33 C0 8D 8E 18 01 00 00"));
        ConstructScroll = reinterpret_cast<decltype(ConstructScroll)>(hook::get_pattern("8B 44 24 08 53 56 33 DB 53 8B F1 8B 4C 24 10 50 51 8B CE E8 ? ? ? ? 8D 8E D8 00 00 00 C7 06 ? ? ? ? E8 ? ? ? ? 8D 8E EC 00 00 00 E8 ? ? ? ? 0F 57 C0 88 9E 0C 01 00 00 88 9E 0D 01 00 00"));
        InitializeList = reinterpret_cast<decltype(InitializeList)>(hook::get_pattern("83 EC 18 56 57 8B F1 8B 0D ? ? ? ? 6A 04 E8 ? ? ? ? D9 5C 24 08 8A 44 24 2C"));
        InitializeScroll = reinterpret_cast<decltype(InitializeScroll)>(hook::get_pattern("83 EC 1C 53 55 56 33 DB 57 8B F1 38 1D ? ? ? ? 74 10 39 1D ? ? ? ? 75 0F 39 1D ? ? ? ? 75 07 68 ? ? ? ? EB 05 68 ? ? ? ? B9 ? ? ? ? E8 ? ? ? ? 8B E8 83 FD FF"));
        AddChoice = reinterpret_cast<decltype(AddChoice)>(hook::get_pattern("8B 44 24 0C 83 EC 0C 53 56 8B F1 83 F8 FF 75 0E 8B 86 E8 00 00 00"));
        SelectChoice = reinterpret_cast<decltype(SelectChoice)>(hook::get_pattern("0F B7 81 E0 00 00 00 66 85 C0 76 1A 0F B7 D0 8B 44 24 04 3B C2"));
        TuningFloat = reinterpret_cast<decltype(TuningFloat)>(hook::get_pattern("8B 44 24 04 D9 04 85 ? ? ? ? C2 04 00 CC CC"));
        TuningVector = reinterpret_cast<decltype(TuningVector)>(hook::get_pattern("8B 44 24 04 8D 04 C5 ? ? ? ? C2 04 00 CC CC"));
        textureDictionary = Field<UI>(build, 25);
        FindTextureDictionary = injector::GetBranchDestination(static_cast<char*>(build) + 29).get();
        // Find the required functions before installing hooks. Roll back on failure.
        gxtHook = safetyhook::create_inline(getText, GetMenuText);
        findHook = safetyhook::create_inline(find, FindUI);
        updateHook = safetyhook::create_inline(update, UpdateList);
        barUpdateHook = safetyhook::create_inline(barUpdate, UpdateBar);
        optionsBuildHook = safetyhook::create_inline(optionsBuild, BuildOptions);
        optionsCloseHook = safetyhook::create_inline(optionsClose, CloseOptions);
        optionsInputHook = safetyhook::create_inline(optionsInput, OptionsInput);
        optionsClickHook = safetyhook::create_inline(optionsClick, OptionsClick);
        optionsHoverHook = safetyhook::create_inline(optionsHover, OptionsHover);
        if (!optionsBuildHook || !optionsCloseHook || !optionsInputHook || !optionsClickHook || !optionsHoverHook)
            throw std::runtime_error("Could not install Fusion Fix page hooks");
        void* buildCallbacks[] = {BuildPage<0>, BuildPage<1>, BuildPage<2>, BuildPage<3>};
        void* closeCallbacks[] = {ClosePage<0>, ClosePage<1>, ClosePage<2>, ClosePage<3>};
        for (size_t i = 0; i < std::size(pages); ++i)
        {
            pages[i].close = safetyhook::create_inline(closes[i], closeCallbacks[i]);
            pages[i].build = safetyhook::create_inline(builds[i], buildCallbacks[i]);
            if (!pages[i].close || !pages[i].build) throw std::runtime_error("Could not install page hooks");
        }
        if (!gxtHook || !findHook || !updateHook || !barUpdateHook) throw std::runtime_error("Could not install menu hooks");
        apiThread = GetCurrentThreadId();
        apiReady = true;
        FusionFix::onMenuDrawingEvent() += UpdateHelp;
        FusionFix::onGameProcessEvent() += UpdateHelp; // title screen is not always reported as paused
    }
    catch (...)
    {
        for (auto& page : pages) { page.build.reset(); page.close.reset(); }
        updateHook.reset(); barUpdateHook.reset(); findHook.reset(); gxtHook.reset();
        optionsBuildHook.reset(); optionsCloseHook.reset(); optionsInputHook.reset();
        optionsClickHook.reset(); optionsHoverHook.reset();
        Debug("Fusion Fix: native menu API unavailable for this executable. Settings remain available in CFG.\n");
    }
}
struct Install
{
    Install() { FusionFix::onInitEvent() += Initialize; }
} install;
}


// Public C ABI. Only scalars, UTF-8 strings and caller-owned callbacks cross the
// DLL boundary; see docs/menu-api.md for declarations and lifetime requirements.
extern "C"
{
    struct FFMenuChoice { float value; const char* text; };
    typedef float (__cdecl* FFMenuGetter)(void*);
    typedef void (__cdecl* FFMenuSetter)(void*, float);

    __declspec(dllexport) unsigned int __cdecl FusionFix_MenuVersion() noexcept { return 1; }

    // 1 = ready, 0 = initialization pending/unsupported executable.
    __declspec(dllexport) int __cdecl FusionFix_MenuReady() noexcept { return apiReady ? 1 : 0; }

    // Callable from any thread after DLL loading; callbacks run once on the
    // frontend thread, after initialization and with no settings UI alive.
    __declspec(dllexport) int __cdecl FusionFix_MenuQueueCallback(void (__cdecl* callback)(void*), void* context) noexcept
    {
        if (!callback) return -4;
        try
        {
            std::lock_guard lock(menuCallbackMutex);
            if (menuCallbacks.size() >= 128) return -3;
            menuCallbacks.push_back({callback, context});
            return 1;
        }
        catch (...) { return -5; }
    }
}

namespace
{
    // All mutations are restricted to the frontend thread with no settings UI
    // alive. This also permits safe removal before a callback provider unloads.
    int CheckMenuMutation()
    {
        if (!apiReady) return -1;
        if (GetCurrentThreadId() != apiThread) return -2;
        if (!screens.empty() || !optionsScreens.empty()) return -3;
        return 1;
    }
    bool ApiText(const char* text)
    {
        return text && *text && strnlen(text, 160) < 160;
    }
}

extern "C"
{
    // kind: 0=choices, 1=toggle, 2=slider. page: 0=Display, 1=Graphics,
    // 2=Controls, 3=Audio, 4=Fusion Fix. Updating an ID replaces its definition.
    __declspec(dllexport) int __cdecl FusionFix_MenuSetOption(
        const char* id, unsigned int page, unsigned int kind,
        const char* label, const char* help, const FFMenuChoice* choices,
        unsigned int count, float minimum, float maximum, float step,
        FFMenuGetter getter, FFMenuSetter setter, void* context) noexcept
    {
        try
        {
            const int status = CheckMenuMutation();
            if (status != 1) return status;
            if (!ApiText(id) || !ApiText(label) || !getter || !setter || page >= 4 + customPages.size() || kind > 2 ||
                (help && strnlen(help, 1024) >= 1024)) return -4;
            const auto& registered = MenuRegistry().Options();
            const bool exists = std::any_of(registered.begin(), registered.end(),
                [&](const auto& option) { return option.id == id; });
            if (!exists && registered.size() >= 128) return -4;
            if (std::any_of(registered.begin(), registered.end(), [&](const auto& option)
                { return option.id != id && option.label == label; })) return -4;
            FusionMenu::Registry candidate;
            // Never let callback exceptions escape into the game's UI code.
            auto get = [getter, context, minimum, kind]() -> float
            {
                const float fallback = kind == 2 && std::isfinite(minimum) ? minimum : 0.0f;
                try { const float v = getter(context); return std::isfinite(v) ? v : fallback; }
                catch (...) { return fallback; }
            };
            auto set = [setter, context](float value)
            { try { setter(context, value); } catch (...) { Debug("Fusion Fix: menu setter threw an exception.\n"); } };
            bool valid = false;
            const auto destination = static_cast<Page>(page);
            const std::string description = help ? help : "";
            if (kind == 2)
                valid = candidate.AddSlider(label, minimum, maximum, step, get, set, description, destination);
            else
            {
                std::vector<FusionMenu::Choice> items;
                if (kind == 1) items = {{0, "Off"}, {1, "On"}};
                else
                {
                    if (!choices || count < 2 || count > 254) return -4;
                    for (unsigned int i = 0; i < count; ++i)
                    {
                        if (!ApiText(choices[i].text)) return -4;
                        items.push_back({choices[i].value, choices[i].text});
                    }
                }
                valid = candidate.AddChoices(label, std::move(items), get, set, description, destination);
            }
            if (!valid) return -4;
            MenuRegistry().Upsert(id, candidate.Options().front());
            return 1;
        }
        catch (...) { return -5; }
    }

    __declspec(dllexport) int __cdecl FusionFix_MenuRemoveOption(const char* id) noexcept
    {
        try
        {
            const int status = CheckMenuMutation();
            if (status != 1) return status;
            if (!ApiText(id)) return -4;
            return MenuRegistry().Remove(id) ? 1 : 0;
        }
        catch (...) { return -5; }
    }

    // Returns a stable page handle >= 5. Reusing an ID updates the page in place.
    __declspec(dllexport) int __cdecl FusionFix_MenuSetPage(const char* id, const char* title,
        const char* help, void (__cdecl* reset)(void*), void* context) noexcept
    {
        try
        {
            const int status = CheckMenuMutation();
            if (status != 1) return status;
            if (!ApiText(id) || !ApiText(title) || (help && strnlen(help, 1024) >= 1024) ||
                std::string_view(id) == "FusionFix") return -4;
            for (size_t i = 1; i < customPages.size(); ++i)
                if (customPages[i].id == id)
                { customPages[i] = {id, title, help ? help : "", reset, context}; return static_cast<int>(i + 4); }
            if (customPages.size() >= 8) return -4;
            customPages.push_back({id, title, help ? help : "", reset, context});
            return static_cast<int>(customPages.size() + 3);
        }
        catch (...) { return -5; }
    }

    __declspec(dllexport) int __cdecl FusionFix_MenuSetText(const char* key, unsigned int language, const char* text) noexcept
    {
        try
        {
            const int status = CheckMenuMutation();
            if (status != 1) return status;
            if (!key || !*key || strnlen(key, 1024) >= 1024 || language >= 10 ||
                !text || strnlen(text, 4096) >= 4096) return -4;
            auto wide = WideText(text);
            std::lock_guard lock(textMutex);
            menuText[key].languages[language] = std::move(wide);
            return 1;
        }
        catch (...) { return -5; }
    }

    __declspec(dllexport) int __cdecl FusionFix_MenuSetTitle(const char* title) noexcept
    {
        try
        {
            const int status = CheckMenuMutation();
            if (status != 1) return status;
            if (!ApiText(title)) return -4;
            customPages[0].title = title;
            return 1;
        }
        catch (...) { return -5; }
    }
}
