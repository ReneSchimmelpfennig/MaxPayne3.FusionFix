module;
#include <common.hxx>
export module fov;
import common;
import settings;

class FOV
{
public:
    FOV()
    {
        FusionFix::onInitEvent() += []()
        {
            auto pattern = hook::pattern("F3 0F 10 0D ? ? ? ? 0F 2F C8 77 ? F3 0F 11 4C 24 ? ? ? ? ? 51 8B CE");
            static auto camThirdPersonPedAimHook = safetyhook::create_mid(pattern.get_first(), [](SafetyHookContext& regs)
            {
                auto& fov = *reinterpret_cast<float*>(regs.esp + 4);
                fov = std::clamp(fov + FusionFixSettings.GetFloat(PREF_CUSTOMFOV), 1.0f, 130.0f);
            });
        };
    }
} FOV;
