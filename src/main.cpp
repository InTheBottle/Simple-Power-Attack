#include <SimpleIni.h>
#include <Xinput.h>
#include <ShlObj.h>

#include "SKSEMenuFramework.h"

using namespace RE;
using namespace SKSE;

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    using InputEvents = InputEvent*;

    struct KeyOption {
        const char* label;
        uint32_t code;
    };

    constexpr uint32_t kDefaultAltPowerAttackKey = 257;  // Mouse Right
    constexpr uint32_t kDefaultAltLeftPowerAttackKey = 259;  // Mouse Button 4
    constexpr uint32_t kKeyDisabled = 0;  // 0 = key disabled
    constexpr uint32_t kModifierNone = 0;  // 0 = no modifier 

    constexpr uint32_t kMacroNumKeyboardKeys = 256;
    constexpr uint32_t kMacroNumMouseButtons = 8;
    constexpr uint32_t kMacroMouseWheelOffset = kMacroNumKeyboardKeys + kMacroNumMouseButtons;
    constexpr uint32_t kMacroGamepadOffset = kMacroMouseWheelOffset + 2;
    constexpr uint32_t kMaxMacros = kMacroGamepadOffset + 16;

    constexpr std::array<KeyOption, 11> kMouseOptions{ {
        { "None (Disabled)", 0 },
        { "Mouse Left (256)", 256 },
        { "Mouse Right (257)", 257 },
        { "Mouse Middle (258)", 258 },
        { "Mouse Button 4 (259)", 259 },
        { "Mouse Button 5 (260)", 260 },
        { "Mouse Button 6 (261)", 261 },
        { "Mouse Button 7 (262)", 262 },
        { "Mouse Button 8 (263)", 263 },
        { "Mouse Wheel Up (264)", 264 },
        { "Mouse Wheel Down (265)", 265 }
    } };

    constexpr std::array<KeyOption, 17> kGamepadOptions{ {
        { "None (Disabled)", 0 },
        { "DPad Up (266)", 266 },
        { "DPad Down (267)", 267 },
        { "DPad Left (268)", 268 },
        { "DPad Right (269)", 269 },
        { "Start (270)", 270 },
        { "Back (271)", 271 },
        { "Left Thumb (272)", 272 },
        { "Right Thumb (273)", 273 },
        { "Left Shoulder (274)", 274 },
        { "Right Shoulder (275)", 275 },
        { "A (276)", 276 },
        { "B (277)", 277 },
        { "X (278)", 278 },
        { "Y (279)", 279 },
        { "Left Trigger (280)", 280 },
        { "Right Trigger (281)", 281 }
    } };

    constexpr std::array<KeyOption, 7> kModifierOptions{ {
        { "None (No modifier)", 0 },
        { "Left Shift (42)", 42 },
        { "Right Shift (54)", 54 },
        { "Left Control (29)", 29 },
        { "Right Control (157)", 157 },
        { "Left Alt (56)", 56 },
        { "Right Alt (184)", 184 }
    } };

    constexpr std::array<KeyOption, 9> kGamepadModifierOptions{ {
        { "None (No modifier)", 0 },
        { "Left Shoulder / LB (274)", 274 },
        { "Right Shoulder / RB (275)", 275 },
        { "Left Thumb / LS (272)", 272 },
        { "Right Thumb / RS (273)", 273 },
        { "A (276)", 276 },
        { "B (277)", 277 },
        { "Left Trigger / LT (280)", 280 },
        { "Right Trigger / RT (281)", 281 }
    } };

    constexpr const char* kPrimaryConfigFileName = "SimplePowerAttack.ini";

    const TaskInterface* g_task = nullptr;
    BGSAction* g_rightPowerAttackAction = nullptr;
    BGSAction* g_leftPowerAttackAction = nullptr;
    BGSAction* g_dualPowerAttackAction = nullptr;
    std::atomic<uint32_t> g_altPowerAttackKey = kDefaultAltPowerAttackKey;
    uint32_t g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
    bool g_hasUnsavedKeyChange = false;
    std::atomic<uint32_t> g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    uint32_t g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
    bool g_hasUnsavedLeftKeyChange = false;
    std::atomic<uint32_t> g_altBlockKey = kKeyDisabled;
    uint32_t g_pendingAltBlockKey = kKeyDisabled;
    bool g_hasUnsavedBlockKeyChange = false;
    std::atomic<uint32_t> g_rightModifierKey = kModifierNone;
    uint32_t g_pendingRightModifierKey = kModifierNone;
    bool g_hasUnsavedRightModChange = false;
    std::atomic<uint32_t> g_leftModifierKey = kModifierNone;
    uint32_t g_pendingLeftModifierKey = kModifierNone;
    bool g_hasUnsavedLeftModChange = false;
    std::atomic<uint32_t> g_rightGamepadModifier = kModifierNone;
    uint32_t g_pendingRightGamepadModifier = kModifierNone;
    bool g_hasUnsavedRightGPModChange = false;
    std::atomic<uint32_t> g_leftGamepadModifier = kModifierNone;
    uint32_t g_pendingLeftGamepadModifier = kModifierNone;
    bool g_hasUnsavedLeftGPModChange = false;
    std::atomic<bool> g_mcoMode = false;
    bool g_pendingMcoMode = false;
    bool g_hasUnsavedMcoChange = false;
    std::atomic<bool> g_noStaminaPowerAttack = false;
    bool g_pendingNoStaminaPowerAttack = false;
    bool g_hasUnsavedNoStaminaChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    float g_vanillaPowerAttackDelay = 0.0f;
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;

    CSimpleIniA g_ini(true, false, false);
    REL::Relocation<Setting*> g_initialPowerAttackDelay{ RELOCATION_ID(509496, 381954) };

    void LoadConfig();
    void TriggerPowerAttack(PlayerCharacter* player);
    void TriggerLeftPowerAttack(PlayerCharacter* player);

    std::string GetPrimaryConfigPath()
    {
        char modulePath[MAX_PATH]{};
        const auto written = GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        if (written > 0) {
            std::filesystem::path path(modulePath);
            path.replace_filename(kPrimaryConfigFileName);
            return path.string();
        }

        return std::string("Data\\SKSE\\Plugins\\") + kPrimaryConfigFileName;
    }

    uint32_t GamepadMaskToKeycode(uint32_t keyMask)
    {
        switch (keyMask) {
        case XINPUT_GAMEPAD_DPAD_UP:
            return kMacroGamepadOffset;
        case XINPUT_GAMEPAD_DPAD_DOWN:
            return kMacroGamepadOffset + 1;
        case XINPUT_GAMEPAD_DPAD_LEFT:
            return kMacroGamepadOffset + 2;
        case XINPUT_GAMEPAD_DPAD_RIGHT:
            return kMacroGamepadOffset + 3;
        case XINPUT_GAMEPAD_START:
            return kMacroGamepadOffset + 4;
        case XINPUT_GAMEPAD_BACK:
            return kMacroGamepadOffset + 5;
        case XINPUT_GAMEPAD_LEFT_THUMB:
            return kMacroGamepadOffset + 6;
        case XINPUT_GAMEPAD_RIGHT_THUMB:
            return kMacroGamepadOffset + 7;
        case XINPUT_GAMEPAD_LEFT_SHOULDER:
            return kMacroGamepadOffset + 8;
        case XINPUT_GAMEPAD_RIGHT_SHOULDER:
            return kMacroGamepadOffset + 9;
        case XINPUT_GAMEPAD_A:
            return kMacroGamepadOffset + 10;
        case XINPUT_GAMEPAD_B:
            return kMacroGamepadOffset + 11;
        case XINPUT_GAMEPAD_X:
            return kMacroGamepadOffset + 12;
        case XINPUT_GAMEPAD_Y:
            return kMacroGamepadOffset + 13;
        case 0x9:
            return kMacroGamepadOffset + 14;  // LT
        case 0xA:
            return kMacroGamepadOffset + 15;  // RT
        default:
            return kMaxMacros;
        }
    }

    uint32_t ToMacroKeyCode(const ButtonEvent* event)
    {
        const auto keyMask = event->GetIDCode();
        if (event->device.get() == INPUT_DEVICE::kMouse) {
            return kMacroNumKeyboardKeys + keyMask;
        }
        if (event->device.get() == INPUT_DEVICE::kGamepad) {
            return GamepadMaskToKeycode(keyMask);
        }
        return keyMask;
    }

    bool IsPlayerInValidCombatState(PlayerCharacter* player)
    {
        if (!player) {
            return false;
        }

        const auto actorState = player->AsActorState();
        if (!actorState) {
            return false;
        }

        // states
        if (actorState->GetWeaponState() != WEAPON_STATE::kDrawn) {
            return false;
        }

        if (actorState->actorState1.sitSleepState != SIT_SLEEP_STATE::kNormal) {
            return false;
        }

        if (actorState->actorState1.knockState != KNOCK_STATE_ENUM::kNormal) {
            return false;
        }

        if (actorState->actorState1.flyState != FLY_STATE::kNone) {
            return false;
        }

        if (player->IsInKillMove()) {
            return false;
        }

        TESForm* equippedObj = player->GetEquippedObject(false);
        if (equippedObj) {
            auto* weap = skyrim_cast<TESObjectWEAP*>(equippedObj);
            if (!weap) return false;  // spell or non-weapon in right hand
            if (weap->IsBow() || weap->IsStaff() || weap->IsCrossbow()) return false;
        }

        return true;
    }

    bool IsDualWielding(PlayerCharacter* player)
    {
        auto* rightForm = player->GetEquippedObject(false);
        auto* leftForm = player->GetEquippedObject(true);
        if (!rightForm || !leftForm) return false;

        auto* rightWeap = skyrim_cast<TESObjectWEAP*>(rightForm);
        auto* leftWeap = skyrim_cast<TESObjectWEAP*>(leftForm);
        if (!rightWeap || !leftWeap) return false;

        auto isOneHandedMelee = [](TESObjectWEAP* w) {
            return w->IsOneHandedSword() || w->IsOneHandedDagger() ||
                   w->IsOneHandedAxe() || w->IsOneHandedMace();
        };
        return isOneHandedMelee(rightWeap) && isOneHandedMelee(leftWeap);
    }

    bool IsLeftHandValidForPowerAttack(PlayerCharacter* player)
    {
        TESForm* leftObj = player->GetEquippedObject(true);
        if (!leftObj) return false;

        auto* weap = skyrim_cast<TESObjectWEAP*>(leftObj);
        if (!weap) return false;

        if (weap->IsBow() || weap->IsStaff() || weap->IsCrossbow()) return false;

        return true;
    }

    bool IsGameplayInputAllowed()
    {
        auto* ui = UI::GetSingleton();
        auto* controlMap = ControlMap::GetSingleton();

        if (!ui || !controlMap) return false;

        const uint32_t requiredFlags =
            static_cast<uint32_t>(UserEvents::USER_EVENT_FLAG::kMovement);

        if (ui->numPausesGame > 0) return false;
        if ((controlMap->GetRuntimeData().enabledControls.underlying() & requiredFlags) != requiredFlags) return false;
        if (ui->IsMenuOpen("Dialogue Menu"sv)) return false;
        if (SKSEMenuFramework::IsAnyBlockingWindowOpened()) return false;

        return true;
    }

    bool IsModifierKeyPressed(uint32_t modifierKey)
    {
        if (modifierKey == kModifierNone) return true;

        int vk = 0;
        switch (modifierKey) {
        case 42:  vk = VK_LSHIFT; break;
        case 54:  vk = VK_RSHIFT; break;
        case 29:  vk = VK_LCONTROL; break;
        case 157: vk = VK_RCONTROL; break;
        case 56:  vk = VK_LMENU; break;
        case 184: vk = VK_RMENU; break;
        default:  return false;
        }

        return (GetAsyncKeyState(vk) & 0x8000) != 0;
    }

    bool IsGamepadModifierPressed(uint32_t modifierKey)
    {
        if (modifierKey == kModifierNone) return true;

        XINPUT_STATE state{};
        if (XInputGetState(0, &state) != ERROR_SUCCESS) return false;

        switch (modifierKey) {
        case 272: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
        case 273: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_THUMB) != 0;
        case 274: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
        case 275: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
        case 276: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_A) != 0;
        case 277: return (state.Gamepad.wButtons & XINPUT_GAMEPAD_B) != 0;
        case 280: return state.Gamepad.bLeftTrigger > 128;
        case 281: return state.Gamepad.bRightTrigger > 128;
        default:  return false;
        }
    }

    bool HandleBlockEvent(ButtonEvent* button, uint32_t keyCode)
    {
        const uint32_t blockKey = g_altBlockKey.load();
        if (blockKey == kKeyDisabled || keyCode != blockKey) return false;

        auto* player = PlayerCharacter::GetSingleton();
        if (!player) return false;

        auto* actorState = player->AsActorState();
        if (!actorState) return false;

        if (!IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return false;

        if (button->IsPressed()) {
            actorState->actorState2.wantBlocking = 1;
            bool isBlocking = false;
            if (player->GetGraphVariableBool("IsBlocking", isBlocking) && !isBlocking) {
                player->NotifyAnimationGraph("blockStart");
            }
        } else if (button->IsUp()) {
            actorState->actorState2.wantBlocking = 0;
            bool isBlocking = false;
            if (player->GetGraphVariableBool("IsBlocking", isBlocking) && isBlocking) {
                player->NotifyAnimationGraph("blockStop");
            }
        }

        return true;
    }

    bool HandleButtonEvent(ButtonEvent* button)
    {
        if (!button || !g_pluginEnabled.load()) {
            return false;
        }

        const uint32_t keyCode = ToMacroKeyCode(button);

        HandleBlockEvent(button, keyCode);

        const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
        if (!isDown) {
            return false;
        }

        const uint32_t rightKey = g_altPowerAttackKey.load();
        const uint32_t leftKey = g_altLeftPowerAttackKey.load();
        const bool isRightKey = (rightKey != kKeyDisabled) && (keyCode == rightKey);
        const bool isLeftKey = (leftKey != kKeyDisabled) && (keyCode == leftKey);
        if (!isRightKey && !isLeftKey) return false;

        auto* player = PlayerCharacter::GetSingleton();
        if (!player || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return false;

        if (g_noStaminaPowerAttack.load() && player->AsActorValueOwner()->GetActorValue(ActorValue::kStamina) <= 0.0f) {
            return false;
        }

        const bool isGamepad = button->device.get() == INPUT_DEVICE::kGamepad;
        bool consumed = false;

        if (isRightKey) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_rightGamepadModifier.load())
                : IsModifierKeyPressed(g_rightModifierKey.load());
            if (modPressed) {
                TriggerPowerAttack(player);
                consumed = true;
            }
        }

        if (isLeftKey && !consumed) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_leftGamepadModifier.load())
                : IsModifierKeyPressed(g_leftModifierKey.load());
            if (IsLeftHandValidForPowerAttack(player) && modPressed) {
                TriggerLeftPowerAttack(player);
                consumed = true;
            }
        }

        return consumed;
    }

    void ProcessInputChain(RE::InputEvent* events)
    {
        for (auto* event = events; event; event = event->next) {
            if (event->eventType.get() != INPUT_EVENT_TYPE::kButton) {
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (HandleButtonEvent(button)) {
                button->GetRuntimeData().value = 0.0f;
            }
        }
    }

    void TriggerPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_rightPowerAttackAction || !player) return;

        BGSAction* action = (g_dualPowerAttackAction && IsDualWielding(player))
            ? g_dualPowerAttackAction : g_rightPowerAttackAction;

        const bool mcoMode = g_mcoMode.load();
        g_task->AddTask([player, action, mcoMode]() {
            std::unique_ptr<TESActionData> data(TESActionData::Create());
            data->source = NiPointer<TESObjectREFR>(player);
            data->action = action;

            using ProcessAction_t = bool (*)(TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            processAction(data.get());

            if (mcoMode) {
                player->NotifyAnimationGraph("Hitframe");
            }
        });
    }

    void TriggerLeftPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_leftPowerAttackAction || !player) return;

        const bool mcoMode = g_mcoMode.load();
        g_task->AddTask([player, action = g_leftPowerAttackAction, mcoMode]() {
            std::unique_ptr<TESActionData> data(TESActionData::Create());
            data->source = NiPointer<TESObjectREFR>(player);
            data->action = action;

            using ProcessAction_t = bool (*)(TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            processAction(data.get());

            if (mcoMode) {
                player->NotifyAnimationGraph("Hitframe");
            }
        });
    }

    uint32_t SanitizeKeyCode(uint32_t keyCode)
    {
        if (keyCode >= kMaxMacros) {
            return kKeyDisabled;
        }
        return keyCode;  
    }

    uint32_t SanitizeModifierKeyCode(uint32_t keyCode)
    {
        if (keyCode >= kMacroNumKeyboardKeys) {
            return kModifierNone;
        }
        return keyCode;  
    }

    uint32_t SanitizeGamepadModifierCode(uint32_t keyCode)
    {
        if (keyCode == kModifierNone) return kModifierNone;
        if (keyCode < kMacroGamepadOffset || keyCode >= kMaxMacros) return kModifierNone;
        return keyCode;
    }

    void ApplyPluginEnabledState(bool enabled)
    {
        if (enabled) {
            g_initialPowerAttackDelay->data.f = 10.0f;
        } else {
            g_initialPowerAttackDelay->data.f = g_vanillaPowerAttackDelay;
        }
    }

    bool SaveConfig(uint32_t keyCode, uint32_t leftKeyCode, uint32_t blockKeyCode,
                     uint32_t rightMod, uint32_t leftMod,
                     uint32_t rightGPMod, uint32_t leftGPMod,
                     bool mcoMode, bool noStaminaPA, bool pluginEnabled)
    {
        keyCode = SanitizeKeyCode(keyCode);
        leftKeyCode = SanitizeKeyCode(leftKeyCode);
        blockKeyCode = SanitizeKeyCode(blockKeyCode);
        rightMod = SanitizeModifierKeyCode(rightMod);
        leftMod = SanitizeModifierKeyCode(leftMod);
        rightGPMod = SanitizeGamepadModifierCode(rightGPMod);
        leftGPMod = SanitizeGamepadModifierCode(leftGPMod);
        const std::string savePath = GetPrimaryConfigPath();

        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iKeycode", static_cast<long>(keyCode));
        g_ini.SetLongValue("General", "iLeftKeycode", static_cast<long>(leftKeyCode));
        g_ini.SetLongValue("General", "iBlockKeycode", static_cast<long>(blockKeyCode));
        g_ini.SetLongValue("General", "iRightModifier", static_cast<long>(rightMod));
        g_ini.SetLongValue("General", "iLeftModifier", static_cast<long>(leftMod));
        g_ini.SetLongValue("General", "iRightGamepadModifier", static_cast<long>(rightGPMod));
        g_ini.SetLongValue("General", "iLeftGamepadModifier", static_cast<long>(leftGPMod));
        g_ini.SetBoolValue("MCO", "bMCOMode", mcoMode);
        g_ini.SetBoolValue("General", "bPowerAttackNoStamina", noStaminaPA);

        try {
            std::filesystem::path savePathFs(savePath);
            std::filesystem::create_directories(savePathFs.parent_path());
        } catch (const std::exception& e) {
            SKSE::log::error("Failed to create config directory: {}", e.what());
            g_ini.Reset();
            return false;
        }

        const SI_Error saveResult = g_ini.SaveFile(savePath.c_str());
        g_ini.Reset();

        if (saveResult < 0) {
            SKSE::log::error("Failed to save config to '{}'", savePath);
            return false;
        }

        SKSE::log::info("Saved config enabled={} right={} left={} block={} rmod={} lmod={} rgmod={} lgmod={} mco={} noStaminaPA={} to '{}'",
            pluginEnabled, keyCode, leftKeyCode, blockKeyCode, rightMod, leftMod, rightGPMod, leftGPMod, mcoMode, noStaminaPA, savePath);
        return true;
    }

    template <size_t N>
    int FindOptionIndex(const std::array<KeyOption, N>& options, uint32_t code)
    {
        for (size_t i = 0; i < N; ++i) {
            if (options[i].code == code) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    template <size_t N>
    bool DrawKeyOptionCombo(const char* label, const std::array<KeyOption, N>& options, uint32_t currentCode, uint32_t& selectedCode)
    {
        const int currentIndex = FindOptionIndex(options, currentCode);
        const char* preview = currentIndex >= 0 ? options[static_cast<size_t>(currentIndex)].label : "(not selected)";

        bool changed = false;
        if (ImGuiMCP::BeginCombo(label, preview)) {
            for (size_t i = 0; i < N; ++i) {
                const bool isSelected = static_cast<int>(i) == currentIndex;
                if (ImGuiMCP::Selectable(options[i].label, isSelected)) {
                    selectedCode = options[i].code;
                    changed = true;
                }
            }
            ImGuiMCP::EndCombo();
        }

        return changed;
    }

    void __stdcall RenderMenuFrameworkSection()
    {
        const uint32_t currentCode = g_altPowerAttackKey.load();
        const uint32_t pendingCode = g_pendingAltPowerAttackKey;
        const uint32_t currentLeftCode = g_altLeftPowerAttackKey.load();
        const uint32_t pendingLeftCode = g_pendingAltLeftPowerAttackKey;
        const uint32_t currentBlockCode = g_altBlockKey.load();
        const uint32_t pendingBlockCode = g_pendingAltBlockKey;
        const uint32_t currentRMod = g_rightModifierKey.load();
        const uint32_t pendingRMod = g_pendingRightModifierKey;
        const uint32_t currentLMod = g_leftModifierKey.load();
        const uint32_t pendingLMod = g_pendingLeftModifierKey;
        const uint32_t currentRGPMod = g_rightGamepadModifier.load();
        const uint32_t pendingRGPMod = g_pendingRightGamepadModifier;
        const uint32_t currentLGPMod = g_leftGamepadModifier.load();
        const uint32_t pendingLGPMod = g_pendingLeftGamepadModifier;
        const bool currentMco = g_mcoMode.load();
        bool pendingMco = g_pendingMcoMode;
        const bool currentNoStamina = g_noStaminaPowerAttack.load();
        bool pendingNoStamina = g_pendingNoStaminaPowerAttack;
        const bool currentEnabled = g_pluginEnabled.load();
        bool pendingEnabled = g_pendingPluginEnabled;

        ImGuiMCP::Text("Simple Power Attack");
        ImGuiMCP::Separator();

        uint32_t chosenCode = pendingCode;
        uint32_t chosenLeftCode = pendingLeftCode;
        uint32_t chosenBlockCode = pendingBlockCode;
        uint32_t chosenRMod = pendingRMod;
        uint32_t chosenLMod = pendingLMod;
        uint32_t chosenRGPMod = pendingRGPMod;
        uint32_t chosenLGPMod = pendingLGPMod;
        bool chosenMco = pendingMco;
        bool chosenNoStamina = pendingNoStamina;
        bool chosenEnabled = pendingEnabled;
        bool changed = false;
        bool leftChanged = false;
        bool blockChanged = false;
        bool rModChanged = false;
        bool lModChanged = false;
        bool rGPModChanged = false;
        bool lGPModChanged = false;
        bool mcoChanged = false;
        bool noStaminaChanged = false;
        bool enabledChanged = false;

        // --- Mouse / Keyboard Section ---
        if (ImGuiMCP::CollapsingHeader("Mouse / Keyboard", 0)) {
            ImGuiMCP::Text("Right Power Attack");
            changed = DrawKeyOptionCombo("Key##mk_right", kMouseOptions, pendingCode, chosenCode) || changed;
            rModChanged = DrawKeyOptionCombo("Modifier##mk_rmod", kModifierOptions, pendingRMod, chosenRMod) || rModChanged;

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Left Power Attack");
            leftChanged = DrawKeyOptionCombo("Key##mk_left", kMouseOptions, pendingLeftCode, chosenLeftCode) || leftChanged;
            lModChanged = DrawKeyOptionCombo("Modifier##mk_lmod", kModifierOptions, pendingLMod, chosenLMod) || lModChanged;

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Block");
            blockChanged = DrawKeyOptionCombo("Key##mk_block", kMouseOptions, pendingBlockCode, chosenBlockCode) || blockChanged;
        }

        // --- Controller Section ---
        if (ImGuiMCP::CollapsingHeader("Controller", 0)) {
            ImGuiMCP::Text("Right Power Attack");
            changed = DrawKeyOptionCombo("Button##gp_right", kGamepadOptions, pendingCode, chosenCode) || changed;
            rGPModChanged = DrawKeyOptionCombo("Modifier##gp_rmod", kGamepadModifierOptions, pendingRGPMod, chosenRGPMod) || rGPModChanged;

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Left Power Attack");
            leftChanged = DrawKeyOptionCombo("Button##gp_left", kGamepadOptions, pendingLeftCode, chosenLeftCode) || leftChanged;
            lGPModChanged = DrawKeyOptionCombo("Modifier##gp_lmod", kGamepadModifierOptions, pendingLGPMod, chosenLGPMod) || lGPModChanged;

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Block");
            blockChanged = DrawKeyOptionCombo("Button##gp_block", kGamepadOptions, pendingBlockCode, chosenBlockCode) || blockChanged;
        }

        ImGuiMCP::Separator();

        if (ImGuiMCP::Checkbox("MCO Dual Wield Fix", &chosenMco)) {
            mcoChanged = true;
        }

        if (ImGuiMCP::Checkbox("Disable Power Attack at 0 Stamina", &chosenNoStamina)) {
            noStaminaChanged = true;
        }

        bool disableChecked = !pendingEnabled;
        if (ImGuiMCP::Checkbox("Disable Plugin", &disableChecked)) {
            chosenEnabled = !disableChecked;
            enabledChanged = true;
        }

        if (changed) {
            g_pendingAltPowerAttackKey = SanitizeKeyCode(chosenCode);
            g_hasUnsavedKeyChange = (g_pendingAltPowerAttackKey != currentCode);
        }

        if (leftChanged) {
            g_pendingAltLeftPowerAttackKey = SanitizeKeyCode(chosenLeftCode);
            g_hasUnsavedLeftKeyChange = (g_pendingAltLeftPowerAttackKey != currentLeftCode);
        }

        if (blockChanged) {
            g_pendingAltBlockKey = SanitizeKeyCode(chosenBlockCode);
            g_hasUnsavedBlockKeyChange = (g_pendingAltBlockKey != currentBlockCode);
        }

        if (rModChanged) {
            g_pendingRightModifierKey = SanitizeModifierKeyCode(chosenRMod);
            g_hasUnsavedRightModChange = (g_pendingRightModifierKey != currentRMod);
        }

        if (lModChanged) {
            g_pendingLeftModifierKey = SanitizeModifierKeyCode(chosenLMod);
            g_hasUnsavedLeftModChange = (g_pendingLeftModifierKey != currentLMod);
        }

        if (rGPModChanged) {
            g_pendingRightGamepadModifier = SanitizeGamepadModifierCode(chosenRGPMod);
            g_hasUnsavedRightGPModChange = (g_pendingRightGamepadModifier != currentRGPMod);
        }

        if (lGPModChanged) {
            g_pendingLeftGamepadModifier = SanitizeGamepadModifierCode(chosenLGPMod);
            g_hasUnsavedLeftGPModChange = (g_pendingLeftGamepadModifier != currentLGPMod);
        }

        if (mcoChanged) {
            g_pendingMcoMode = chosenMco;
            g_hasUnsavedMcoChange = (g_pendingMcoMode != currentMco);
        }

        if (noStaminaChanged) {
            g_pendingNoStaminaPowerAttack = chosenNoStamina;
            g_hasUnsavedNoStaminaChange = (g_pendingNoStaminaPowerAttack != currentNoStamina);
        }

        if (enabledChanged) {
            g_pendingPluginEnabled = chosenEnabled;
            g_hasUnsavedEnabledChange = (g_pendingPluginEnabled != currentEnabled);
        }

        const bool hasUnsaved = g_hasUnsavedKeyChange || g_hasUnsavedLeftKeyChange ||
            g_hasUnsavedBlockKeyChange || g_hasUnsavedRightModChange || g_hasUnsavedLeftModChange ||
            g_hasUnsavedRightGPModChange || g_hasUnsavedLeftGPModChange ||
            g_hasUnsavedMcoChange || g_hasUnsavedNoStaminaChange || g_hasUnsavedEnabledChange;

        ImGuiMCP::Separator();

        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }

        if (ImGuiMCP::Button("Save to INI")) {
            const uint32_t saveCode = SanitizeKeyCode(g_pendingAltPowerAttackKey);
            const uint32_t saveLeftCode = SanitizeKeyCode(g_pendingAltLeftPowerAttackKey);
            const uint32_t saveBlockCode = SanitizeKeyCode(g_pendingAltBlockKey);
            const uint32_t saveRMod = SanitizeModifierKeyCode(g_pendingRightModifierKey);
            const uint32_t saveLMod = SanitizeModifierKeyCode(g_pendingLeftModifierKey);
            const uint32_t saveRGPMod = SanitizeGamepadModifierCode(g_pendingRightGamepadModifier);
            const uint32_t saveLGPMod = SanitizeGamepadModifierCode(g_pendingLeftGamepadModifier);
            const bool saveMco = g_pendingMcoMode;
            const bool saveNoStamina = g_pendingNoStaminaPowerAttack;
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveCode, saveLeftCode, saveBlockCode, saveRMod, saveLMod, saveRGPMod, saveLGPMod, saveMco, saveNoStamina, saveEnabled)) {
                g_altPowerAttackKey = saveCode;
                g_pendingAltPowerAttackKey = saveCode;
                g_hasUnsavedKeyChange = false;
                g_altLeftPowerAttackKey = saveLeftCode;
                g_pendingAltLeftPowerAttackKey = saveLeftCode;
                g_hasUnsavedLeftKeyChange = false;
                g_altBlockKey = saveBlockCode;
                g_pendingAltBlockKey = saveBlockCode;
                g_hasUnsavedBlockKeyChange = false;
                g_rightModifierKey = saveRMod;
                g_pendingRightModifierKey = saveRMod;
                g_hasUnsavedRightModChange = false;
                g_leftModifierKey = saveLMod;
                g_pendingLeftModifierKey = saveLMod;
                g_hasUnsavedLeftModChange = false;
                g_rightGamepadModifier = saveRGPMod;
                g_pendingRightGamepadModifier = saveRGPMod;
                g_hasUnsavedRightGPModChange = false;
                g_leftGamepadModifier = saveLGPMod;
                g_pendingLeftGamepadModifier = saveLGPMod;
                g_hasUnsavedLeftGPModChange = false;
                g_mcoMode = saveMco;
                g_pendingMcoMode = saveMco;
                g_hasUnsavedMcoChange = false;
                g_noStaminaPowerAttack = saveNoStamina;
                g_pendingNoStaminaPowerAttack = saveNoStamina;
                g_hasUnsavedNoStaminaChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
                ApplyPluginEnabledState(saveEnabled);
            }
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Discard")) {
            g_pendingAltPowerAttackKey = currentCode;
            g_hasUnsavedKeyChange = false;
            g_pendingAltLeftPowerAttackKey = currentLeftCode;
            g_hasUnsavedLeftKeyChange = false;
            g_pendingAltBlockKey = currentBlockCode;
            g_hasUnsavedBlockKeyChange = false;
            g_pendingRightModifierKey = currentRMod;
            g_hasUnsavedRightModChange = false;
            g_pendingLeftModifierKey = currentLMod;
            g_hasUnsavedLeftModChange = false;
            g_pendingRightGamepadModifier = currentRGPMod;
            g_hasUnsavedRightGPModChange = false;
            g_pendingLeftGamepadModifier = currentLGPMod;
            g_hasUnsavedLeftGPModChange = false;
            g_pendingMcoMode = currentMco;
            g_hasUnsavedMcoChange = false;
            g_pendingNoStaminaPowerAttack = currentNoStamina;
            g_hasUnsavedNoStaminaChange = false;
            g_pendingPluginEnabled = currentEnabled;
            g_hasUnsavedEnabledChange = false;
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Reload from INI")) {
            LoadConfig();
        }
    }

    void LoadConfig()
    {
        const std::string path = GetPrimaryConfigPath();

        const SI_Error result = g_ini.LoadFile(path.c_str());
        if (result < 0) {
            SKSE::log::warn("Config not found at '{}', using defaults", path);
            g_altPowerAttackKey = kDefaultAltPowerAttackKey;
            g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
            g_hasUnsavedKeyChange = false;
            g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_hasUnsavedLeftKeyChange = false;
            g_altBlockKey = kKeyDisabled;
            g_pendingAltBlockKey = kKeyDisabled;
            g_hasUnsavedBlockKeyChange = false;
            g_rightModifierKey = kModifierNone;
            g_pendingRightModifierKey = kModifierNone;
            g_hasUnsavedRightModChange = false;
            g_leftModifierKey = kModifierNone;
            g_pendingLeftModifierKey = kModifierNone;
            g_hasUnsavedLeftModChange = false;
            g_rightGamepadModifier = kModifierNone;
            g_pendingRightGamepadModifier = kModifierNone;
            g_hasUnsavedRightGPModChange = false;
            g_leftGamepadModifier = kModifierNone;
            g_pendingLeftGamepadModifier = kModifierNone;
            g_hasUnsavedLeftGPModChange = false;
            g_mcoMode = false;
            g_pendingMcoMode = false;
            g_hasUnsavedMcoChange = false;
            g_noStaminaPowerAttack = false;
            g_pendingNoStaminaPowerAttack = false;
            g_hasUnsavedNoStaminaChange = false;
            g_pluginEnabled = true;
            g_pendingPluginEnabled = true;
            g_hasUnsavedEnabledChange = false;
            g_ini.Reset();
            ApplyPluginEnabledState(true);
            return;
        }

        const long rawValue = g_ini.GetLongValue("General", "iKeycode", static_cast<long>(kDefaultAltPowerAttackKey));
        uint32_t parsedKey = SanitizeKeyCode(static_cast<uint32_t>(rawValue < 0 ? 0 : rawValue));

        const long rawLeftValue = g_ini.GetLongValue("General", "iLeftKeycode", static_cast<long>(kDefaultAltLeftPowerAttackKey));
        uint32_t parsedLeftKey = SanitizeKeyCode(static_cast<uint32_t>(rawLeftValue < 0 ? 0 : rawLeftValue));

        g_altPowerAttackKey = parsedKey;
        g_pendingAltPowerAttackKey = parsedKey;
        g_hasUnsavedKeyChange = false;
        g_altLeftPowerAttackKey = parsedLeftKey;
        g_pendingAltLeftPowerAttackKey = parsedLeftKey;
        g_hasUnsavedLeftKeyChange = false;

        const long rawBlockValue = g_ini.GetLongValue("General", "iBlockKeycode", static_cast<long>(kKeyDisabled));
        uint32_t parsedBlockKey = SanitizeKeyCode(static_cast<uint32_t>(rawBlockValue < 0 ? 0 : rawBlockValue));

        g_altBlockKey = parsedBlockKey;
        g_pendingAltBlockKey = parsedBlockKey;
        g_hasUnsavedBlockKeyChange = false;

        const long rawRMod = g_ini.GetLongValue("General", "iRightModifier", static_cast<long>(kModifierNone));
        uint32_t parsedRMod = SanitizeModifierKeyCode(static_cast<uint32_t>(rawRMod < 0 ? 0 : rawRMod));

        const long rawLMod = g_ini.GetLongValue("General", "iLeftModifier", static_cast<long>(kModifierNone));
        uint32_t parsedLMod = SanitizeModifierKeyCode(static_cast<uint32_t>(rawLMod < 0 ? 0 : rawLMod));

        g_rightModifierKey = parsedRMod;
        g_pendingRightModifierKey = parsedRMod;
        g_hasUnsavedRightModChange = false;
        g_leftModifierKey = parsedLMod;
        g_pendingLeftModifierKey = parsedLMod;
        g_hasUnsavedLeftModChange = false;

        const long rawRGPMod = g_ini.GetLongValue("General", "iRightGamepadModifier", static_cast<long>(kModifierNone));
        uint32_t parsedRGPMod = SanitizeGamepadModifierCode(static_cast<uint32_t>(rawRGPMod < 0 ? 0 : rawRGPMod));

        const long rawLGPMod = g_ini.GetLongValue("General", "iLeftGamepadModifier", static_cast<long>(kModifierNone));
        uint32_t parsedLGPMod = SanitizeGamepadModifierCode(static_cast<uint32_t>(rawLGPMod < 0 ? 0 : rawLGPMod));

        g_rightGamepadModifier = parsedRGPMod;
        g_pendingRightGamepadModifier = parsedRGPMod;
        g_hasUnsavedRightGPModChange = false;
        g_leftGamepadModifier = parsedLGPMod;
        g_pendingLeftGamepadModifier = parsedLGPMod;
        g_hasUnsavedLeftGPModChange = false;

        const bool parsedMco = g_ini.GetBoolValue("MCO", "bMCOMode", false);
        g_mcoMode = parsedMco;
        g_pendingMcoMode = parsedMco;
        g_hasUnsavedMcoChange = false;

        const bool parsedNoStamina = g_ini.GetBoolValue("General", "bPowerAttackNoStamina", false);
        g_noStaminaPowerAttack = parsedNoStamina;
        g_pendingNoStaminaPowerAttack = parsedNoStamina;
        g_hasUnsavedNoStaminaChange = false;

        const bool parsedEnabled = g_ini.GetBoolValue("General", "bEnabled", true);
        g_pluginEnabled = parsedEnabled;
        g_pendingPluginEnabled = parsedEnabled;
        g_hasUnsavedEnabledChange = false;

        g_ini.Reset();
        ApplyPluginEnabledState(parsedEnabled);
        SKSE::log::info("Loaded config enabled={} right={} left={} block={} rmod={} lmod={} rgpmod={} lgpmod={} mco={} noStaminaPA={} from '{}'",
            parsedEnabled, parsedKey, parsedLeftKey, parsedBlockKey, parsedRMod, parsedLMod, parsedRGPMod, parsedLGPMod, parsedMco, parsedNoStamina, path);
    }

    bool __stdcall OnMenuFrameworkInput(RE::InputEvent* events)
    {
        if (events) ProcessInputChain(events);
        return false;
    }



    void __stdcall OnMenuFrameworkEvent(SKSEMenuFramework::Model::EventType eventType)
    {
        if (eventType == SKSEMenuFramework::Model::kCloseMenu) {
            if (g_hasUnsavedKeyChange) {
                g_pendingAltPowerAttackKey = g_altPowerAttackKey.load();
                g_hasUnsavedKeyChange = false;
            }
            if (g_hasUnsavedLeftKeyChange) {
                g_pendingAltLeftPowerAttackKey = g_altLeftPowerAttackKey.load();
                g_hasUnsavedLeftKeyChange = false;
            }
            if (g_hasUnsavedBlockKeyChange) {
                g_pendingAltBlockKey = g_altBlockKey.load();
                g_hasUnsavedBlockKeyChange = false;
            }
            if (g_hasUnsavedRightModChange) {
                g_pendingRightModifierKey = g_rightModifierKey.load();
                g_hasUnsavedRightModChange = false;
            }
            if (g_hasUnsavedLeftModChange) {
                g_pendingLeftModifierKey = g_leftModifierKey.load();
                g_hasUnsavedLeftModChange = false;
            }
            if (g_hasUnsavedRightGPModChange) {
                g_pendingRightGamepadModifier = g_rightGamepadModifier.load();
                g_hasUnsavedRightGPModChange = false;
            }
            if (g_hasUnsavedLeftGPModChange) {
                g_pendingLeftGamepadModifier = g_leftGamepadModifier.load();
                g_hasUnsavedLeftGPModChange = false;
            }
            if (g_hasUnsavedMcoChange) {
                g_pendingMcoMode = g_mcoMode.load();
                g_hasUnsavedMcoChange = false;
            }
            if (g_hasUnsavedNoStaminaChange) {
                g_pendingNoStaminaPowerAttack = g_noStaminaPowerAttack.load();
                g_hasUnsavedNoStaminaChange = false;
            }
            if (g_hasUnsavedEnabledChange) {
                g_pendingPluginEnabled = g_pluginEnabled.load();
                g_hasUnsavedEnabledChange = false;
            }
        }
    }

    std::filesystem::path GetLogPath()
    {
        wchar_t* docPath = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docPath))) {
            std::filesystem::path logDir(docPath);
            CoTaskMemFree(docPath);
            logDir /= "My Games";
            logDir /= "Skyrim Special Edition";
            logDir /= "SKSE";
            std::filesystem::create_directories(logDir);
            return logDir / "SimplePowerAttack.log";
        }
        if (docPath) CoTaskMemFree(docPath);
        char modulePath[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        std::filesystem::path fallback(modulePath);
        fallback.replace_filename("SimplePowerAttack.log");
        return fallback;
    }

    void InitializeLogging()
    {
        auto logPath = GetLogPath();

        auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logPath.string(), true);

        std::shared_ptr<spdlog::logger> log;
        if (IsDebuggerPresent()) {
            auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
            log = std::make_shared<spdlog::logger>("Global",
                spdlog::sinks_init_list{ fileSink, msvcSink });
        } else {
            log = std::make_shared<spdlog::logger>("Global", fileSink);
        }

        log->set_level(spdlog::level::info);
        log->flush_on(spdlog::level::info);

        spdlog::set_default_logger(std::move(log));
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] %v");
    }
}

SKSEPluginLoad(const LoadInterface* skse)
{
    Init(skse);
    InitializeLogging();

    auto* plugin = PluginDeclaration::GetSingleton();
    SKSE::log::info("{} {} loading", plugin->GetName(), plugin->GetVersion());

    g_task = GetTaskInterface();

    GetMessagingInterface()->RegisterListener([](MessagingInterface::Message* msg) {
        if (!msg || msg->type != MessagingInterface::kDataLoaded) return;

        g_rightPowerAttackAction = TESForm::LookupByID<BGSAction>(0x13383);
        if (!g_rightPowerAttackAction) {
            SKSE::log::critical("Failed to resolve ActionRightPowerAttack (0x13383).");
            return;
        }

        g_leftPowerAttackAction = TESForm::LookupByID<BGSAction>(0x02E2F6);
        if (!g_leftPowerAttackAction) {
            SKSE::log::warn("Failed to resolve ActionLeftPowerAttack (0x02E2F6). Left power attacks disabled.");
        }

        g_dualPowerAttackAction = TESForm::LookupByID<BGSAction>(0x2E2F7);
        if (!g_dualPowerAttackAction) {
            SKSE::log::warn("Failed to resolve ActionDualPowerAttack (0x2E2F7). Dual power attacks disabled.");
        }

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::critical("SKSE Menu Framework not installed.");
            return;
        }

        g_vanillaPowerAttackDelay = g_initialPowerAttackDelay->data.f;

        LoadConfig();

        SKSEMenuFramework::SetSection("SimplePowerAttack");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuFrameworkSection);

        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }
        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }


        SKSE::log::info("Initialized — right keycode={}, left keycode={}, block keycode={}", g_altPowerAttackKey.load(), g_altLeftPowerAttackKey.load(), g_altBlockKey.load());
    });
    return true;
}
