module;

#include <common.hxx>
#include <variant>
#include <cmath>
#include <charconv>
#include <ShlObj.h>
#include <fstream>
#pragma comment(lib, "shell32.lib")

export module settings;

import common;
import comvars;

export enum Pref
{
    PREF_SKIPINTRO, PREF_HIDESKIP, PREF_DISABLELEADERBOARDS, PREF_SUBTITLESIZE,
    PREF_OUTLINESIZE, PREF_BORDERLESS, PREF_LEDILLUMINATION, PREF_BUTTONS,
    PREF_DEVICECHANGE, PREF_HUDASPECTRATIOCONSTRAINT, PREF_CUSTOMFOV,
    PREF_CONSOLEGAMMA, PREF_SMAA, PREF_BLUR, PREF_BLURSTRENGTH, COUNT,
};

export class CSettings
{
    using Value = std::variant<int32_t, float, std::string>;
    struct Definition { const char* key; Value initial; float minimum; float maximum; };
    static inline const std::array<Definition, COUNT> definitions = { {
        {"SkipIntro", 1, 0, 1}, {"HideSkipButton", 1, 0, 1},
        {"DisableGlobalLeaderboards", 1, 0, 1}, {"SubtitlesSizeMultiplier", 1.0f, 0, 10},
        {"OutlinesSizeMultiplier", 1.0f, 0, 10}, {"BorderlessWindowed", 1, 0, 1},
        {"LightSyncRGB", 1, 0, 1}, {"GamepadIcons", 0, 0, gLastControllerTextureIndex},
        {"DisableDeviceChangeEvent", 1, 0, 1}, {"HudAspectRatioConstraint", -1.0f, -1, 10000},
        {"CustomFOV", 0.0f, 0, 45}, {"ConsoleGamma", 0, 0, 2},
        {"SMAA", 0, 0, 2}, {"Blur", 0, 0, 1}, {"BlurStrength", 1.0f, 0, 10},
    } };
    static inline std::array<Value, COUNT> values;
    static inline std::mutex mutex;
    static inline std::filesystem::path cfgPath;
    static inline std::vector<std::filesystem::path> cfgPaths;
    static inline bool dirty = false;
    static inline bool saveSucceeded = true;

    static Value Read(CIniReader& reader, size_t i, Value fallback)
    {
        const auto& d = definitions[i];
        auto text = reader.ReadString("MAIN", d.key, "");
        if (text.empty()) return fallback;
        // Older distributed INIs use trailing // comments.
        if (auto comment = text.find("//"); comment != text.npos) text.resize(comment);
        if (i == PREF_HUDASPECTRATIOCONSTRAINT)
        {
            auto ratio = ParseWidescreenHudOffset(text);
            if (ratio && std::isfinite(*ratio) && *ratio > 0 && *ratio <= d.maximum) return *ratio;
            auto first = text.find_first_not_of(" \t");
            auto last = text.find_last_not_of(" \t\r\n");
            if (first != text.npos) text = text.substr(first, last - first + 1);
            std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return text == "auto" || text == "-1" ? Value(-1.0f) : fallback;
        }
        char* end = nullptr;
        auto number = std::strtof(text.c_str(), &end);
        if (end == text.c_str() || !std::isfinite(number)) return fallback;
        while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
        if (*end) return fallback;
        number = std::clamp(number, d.minimum, d.maximum);
        return std::holds_alternative<int32_t>(d.initial) ? Value(static_cast<int32_t>(number)) : Value(number);
    }

    static bool SaveLocked()
    {
        if (!dirty) return saveSucceeded;
        try
        {
            mINI::INIStructure ini;
            mINI::INIFile(cfgPath).read(ini);
            for (size_t i = 0; i < values.size(); ++i)
            {
                auto& text = ini["MAIN"][definitions[i].key];
                if (i == PREF_HUDASPECTRATIOCONSTRAINT && std::get<float>(values[i]) < 0) text = "Auto";
                else std::visit([&](const auto& value)
                {
                    if constexpr (std::is_same_v<std::decay_t<decltype(value)>, std::string>) text = value;
                    else
                    {
                        char buffer[64];
                        auto result = std::to_chars(std::begin(buffer), std::end(buffer), value);
                        text.assign(buffer, result.ptr);
                    }
                }, values[i]);
            }
            // Keep the loaded location when possible, then try IV's fallback
            // order. Actual writes also check ACLs that filesystem permissions
            // alone do not reflect on Windows.
            auto destinations = cfgPaths;
            destinations.insert(destinations.begin(), cfgPath);
            saveSucceeded = false;
            for (const auto& destination : destinations)
            {
                std::error_code ec;
                std::filesystem::create_directories(destination.parent_path(), ec);
                if (ec) continue;
                wchar_t temporaryName[MAX_PATH];
                if (!GetTempFileNameW(destination.parent_path().c_str(), L"FF", 0, temporaryName)) continue;
                const std::filesystem::path temporary(temporaryName);
                try
                {
                    saveSucceeded = mINI::INIFile(temporary).generate(ini, true) &&
                        MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
                }
                catch (...) { saveSucceeded = false; }
                std::filesystem::remove(temporary, ec);
                if (saveSucceeded)
                {
                    cfgPath = destination;
                    dirty = false;
                    break;
                }
            }
        } catch (...) { saveSucceeded = false; }
        if (!saveSucceeded) OutputDebugStringA("Fusion Fix: unable to save settings CFG. Changes remain in memory.\n");
        return saveSucceeded;
    }

    static void Set(Pref name, Value value)
    {
        const auto i = static_cast<size_t>(name);
        if (i >= definitions.size() || value.index() != definitions[i].initial.index()) return;
        const auto& d = definitions[i];
        if (auto f = std::get_if<float>(&value))
        {
            if (!std::isfinite(*f)) return;
            *f = std::clamp(*f, d.minimum, d.maximum);
            if (name == PREF_HUDASPECTRATIOCONSTRAINT && *f <= 0) *f = -1;
        }
        if (auto n = std::get_if<int32_t>(&value)) *n = std::clamp(*n, static_cast<int32_t>(d.minimum), static_cast<int32_t>(d.maximum));
        {
            std::lock_guard lock(mutex);
            if (values[i] == value) return;
            values[i] = std::move(value);
            dirty = true;
            SaveLocked();
        }
        // Existing feature callbacks apply window styles, controller textures and RGB.
        // Called on the frontend/game thread, never from a filesystem watcher.
        FusionFix::onIniFileChange().executeAll();
    }

public:
    static void ResetDefaults()
    {
        {
            std::lock_guard lock(mutex);
            for (size_t i = 0; i < values.size(); ++i) values[i] = definitions[i].initial;
            dirty = true;
            SaveLocked();
        }
        FusionFix::onIniFileChange().executeAll();
    }
    static void ReadIniSettings()
    {
        std::lock_guard lock(mutex);
        CIniReader legacy("");
        cfgPath = legacy.GetIniPath();
        cfgPath.replace_extension(L".cfg");
        const auto cfgName = cfgPath.filename();
        const auto moduleName = cfgName.stem();
        cfgPaths = {cfgPath, GetExeModulePath() / cfgName};
        wchar_t folder[MAX_PATH];
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, 0, folder)))
        {
            cfgPaths.push_back(std::filesystem::path(folder) / L"Rockstar Games" / L"Max Payne 3" / cfgName);
            cfgPaths.push_back(std::filesystem::path(folder) / moduleName / cfgName);
        }
        if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_MYDOCUMENTS, nullptr, 0, folder)))
            cfgPaths.push_back(std::filesystem::path(folder) / moduleName / cfgName);
        // A fallback may contain newer changes than a readable but protected
        // plugin-local CFG. Ties retain the normal path priority.
        auto newest = std::filesystem::file_time_type::min();
        for (const auto& candidate : cfgPaths)
        {
            std::error_code ec;
            auto modified = std::filesystem::last_write_time(candidate, ec);
            if (ec || modified <= newest || !std::ifstream(candidate).good()) continue;
            cfgPath = candidate;
            newest = modified;
        }
        CIniReader cfg(cfgPath);
        for (size_t i = 0; i < values.size(); ++i)
            values[i] = Read(cfg, i, Read(legacy, i, definitions[i].initial));
        dirty = true;
        SaveLocked();
    }
    static bool Save() { std::lock_guard lock(mutex); return SaveLocked(); }
    static bool LastSaveSucceeded() { std::lock_guard lock(mutex); return saveSucceeded; }
    static int32_t GetInt(Pref name) { std::lock_guard lock(mutex); return std::get<int32_t>(values.at(name)); }
    static float GetFloat(Pref name) { std::lock_guard lock(mutex); return std::get<float>(values.at(name)); }
    static std::string GetString(Pref name) { std::lock_guard lock(mutex); return std::get<std::string>(values.at(name)); }
    static void SetInt(Pref name, int32_t value) { Set(name, value); }
    static void SetFloat(Pref name, float value) { Set(name, value); }
    static void SetString(Pref name, std::string value) { Set(name, std::move(value)); }
} FusionFixSettings;
