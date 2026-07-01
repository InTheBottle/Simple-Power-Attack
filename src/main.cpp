#include <SimpleIni.h>
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

    // Skyrim gamepad button IDs (from ButtonEvent::GetIDCode for kGamepad device).
    // These match the values the game engine uses internally regardless of controller type,
    // so they work with Xbox, PS5 DualSense, and any other controller via Steam Input.
    constexpr uint32_t kGamepadDpadUp        = 0x0001;
    constexpr uint32_t kGamepadDpadDown      = 0x0002;
    constexpr uint32_t kGamepadDpadLeft      = 0x0004;
    constexpr uint32_t kGamepadDpadRight     = 0x0008;
    constexpr uint32_t kGamepadStart         = 0x0010;
    constexpr uint32_t kGamepadBack          = 0x0020;
    constexpr uint32_t kGamepadLeftThumb     = 0x0040;
    constexpr uint32_t kGamepadRightThumb    = 0x0080;
    constexpr uint32_t kGamepadLeftShoulder  = 0x0100;
    constexpr uint32_t kGamepadRightShoulder = 0x0200;
    constexpr uint32_t kGamepadA             = 0x1000;
    constexpr uint32_t kGamepadB             = 0x2000;
    constexpr uint32_t kGamepadX             = 0x4000;
    constexpr uint32_t kGamepadY             = 0x8000;
    constexpr uint32_t kGamepadLT            = 0x0009;
    constexpr uint32_t kGamepadRT            = 0x000A;

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

    constexpr std::array<KeyOption, 11> kGamepadModifierOptions{ {
        { "None (No modifier)", 0 },
        { "Left Shoulder / LB (274)", 274 },
        { "Right Shoulder / RB (275)", 275 },
        { "Left Thumb / LS (272)", 272 },
        { "Right Thumb / RS (273)", 273 },
        { "A (276)", 276 },
        { "B (277)", 277 },
        { "X (278)", 278 },
        { "Y (279)", 279 },
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
    std::atomic<uint32_t> g_altDualPowerAttackKey = kKeyDisabled;
    uint32_t g_pendingAltDualPowerAttackKey = kKeyDisabled;
    bool g_hasUnsavedDualKeyChange = false;
    std::atomic<uint32_t> g_rightModifierKey = kModifierNone;
    uint32_t g_pendingRightModifierKey = kModifierNone;
    bool g_hasUnsavedRightModChange = false;
    std::atomic<uint32_t> g_leftModifierKey = kModifierNone;
    uint32_t g_pendingLeftModifierKey = kModifierNone;
    bool g_hasUnsavedLeftModChange = false;
    std::atomic<uint32_t> g_dualModifierKey = kModifierNone;
    uint32_t g_pendingDualModifierKey = kModifierNone;
    bool g_hasUnsavedDualModChange = false;
    std::atomic<uint32_t> g_rightGamepadModifier = kModifierNone;
    uint32_t g_pendingRightGamepadModifier = kModifierNone;
    bool g_hasUnsavedRightGPModChange = false;
    std::atomic<uint32_t> g_leftGamepadModifier = kModifierNone;
    uint32_t g_pendingLeftGamepadModifier = kModifierNone;
    bool g_hasUnsavedLeftGPModChange = false;
    std::atomic<uint32_t> g_dualGamepadModifier = kModifierNone;
    uint32_t g_pendingDualGamepadModifier = kModifierNone;
    bool g_hasUnsavedDualGPModChange = false;
    std::atomic<bool> g_mcoMode = false;
    bool g_pendingMcoMode = false;
    bool g_hasUnsavedMcoChange = false;
    std::atomic<bool> g_noStaminaPowerAttack = false;
    bool g_pendingNoStaminaPowerAttack = false;
    bool g_hasUnsavedNoStaminaChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;

    // Controller-agnostic key state tracking, updated from input events each frame.
    // Indexed by macro keycode (0..kMaxMacros-1). Works with any controller type.
    std::array<std::atomic<bool>, kMaxMacros> g_keyStates{};

    CSimpleIniA g_ini(true, false, false);
    REL::Relocation<Setting*> g_initialPowerAttackDelay{ RELOCATION_ID(509496, 381954) };

    void LoadConfig();
    void TriggerPowerAttack(PlayerCharacter* player);
    void TriggerLeftPowerAttack(PlayerCharacter* player);
    void TriggerDualPowerAttack(PlayerCharacter* player);

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
        case kGamepadDpadUp:        return kMacroGamepadOffset;
        case kGamepadDpadDown:      return kMacroGamepadOffset + 1;
        case kGamepadDpadLeft:      return kMacroGamepadOffset + 2;
        case kGamepadDpadRight:     return kMacroGamepadOffset + 3;
        case kGamepadStart:         return kMacroGamepadOffset + 4;
        case kGamepadBack:          return kMacroGamepadOffset + 5;
        case kGamepadLeftThumb:     return kMacroGamepadOffset + 6;
        case kGamepadRightThumb:    return kMacroGamepadOffset + 7;
        case kGamepadLeftShoulder:  return kMacroGamepadOffset + 8;
        case kGamepadRightShoulder: return kMacroGamepadOffset + 9;
        case kGamepadA:             return kMacroGamepadOffset + 10;
        case kGamepadB:             return kMacroGamepadOffset + 11;
        case kGamepadX:             return kMacroGamepadOffset + 12;
        case kGamepadY:             return kMacroGamepadOffset + 13;
        case kGamepadLT:            return kMacroGamepadOffset + 14;
        case kGamepadRT:            return kMacroGamepadOffset + 15;
        default:                    return kMaxMacros;
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
        if (modifierKey >= kMaxMacros) return false;
        return g_keyStates[modifierKey].load(std::memory_order_relaxed);
    }

    bool IsGamepadModifierPressed(uint32_t modifierKey)
    {
        if (modifierKey == kModifierNone) return true;
        if (modifierKey >= kMaxMacros) return false;
        return g_keyStates[modifierKey].load(std::memory_order_relaxed);
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

    bool TryPowerAttack(ButtonEvent* button, uint32_t keyCode)
    {
        const uint32_t rightKey = g_altPowerAttackKey.load();
        const uint32_t leftKey = g_altLeftPowerAttackKey.load();
        const uint32_t dualKey = g_altDualPowerAttackKey.load();
        const bool isRightKey = (rightKey != kKeyDisabled) && (keyCode == rightKey);
        const bool isLeftKey = (leftKey != kKeyDisabled) && (keyCode == leftKey);
        const bool isDualKey = (dualKey != kKeyDisabled) && (keyCode == dualKey);
        if (!isRightKey && !isLeftKey && !isDualKey) return false;

        auto* player = PlayerCharacter::GetSingleton();
        if (!player || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return false;

        if (g_noStaminaPowerAttack.load() && player->AsActorValueOwner()->GetActorValue(ActorValue::kStamina) <= 0.0f) {
            return false;
        }

        const bool isGamepad = button->device.get() == INPUT_DEVICE::kGamepad;
        bool consumed = false;

        if (isDualKey) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_dualGamepadModifier.load())
                : IsModifierKeyPressed(g_dualModifierKey.load());
            if (g_dualPowerAttackAction && modPressed && IsDualWielding(player)) {
                TriggerDualPowerAttack(player);
                consumed = true;
            }
        }

        if (isRightKey && !consumed) {
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

    bool IsPowerAttackChordActive(ButtonEvent* button, uint32_t keyCode)
    {
        const uint32_t rightKey = g_altPowerAttackKey.load();
        const uint32_t leftKey = g_altLeftPowerAttackKey.load();
        const uint32_t dualKey = g_altDualPowerAttackKey.load();
        const bool isRightKey = (rightKey != kKeyDisabled) && (keyCode == rightKey);
        const bool isLeftKey = (leftKey != kKeyDisabled) && (keyCode == leftKey);
        const bool isDualKey = (dualKey != kKeyDisabled) && (keyCode == dualKey);
        if (!isRightKey && !isLeftKey && !isDualKey) return false;

        const bool isGamepad = button->device.get() == INPUT_DEVICE::kGamepad;

        if (isDualKey) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_dualGamepadModifier.load())
                : IsModifierKeyPressed(g_dualModifierKey.load());
            if (modPressed) return true;
        }

        if (isRightKey) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_rightGamepadModifier.load())
                : IsModifierKeyPressed(g_rightModifierKey.load());
            if (modPressed) return true;
        }

        if (isLeftKey) {
            const bool modPressed = isGamepad
                ? IsGamepadModifierPressed(g_leftGamepadModifier.load())
                : IsModifierKeyPressed(g_leftModifierKey.load());
            if (modPressed) return true;
        }

        return false;
    }

    bool HandleButtonEvent(ButtonEvent* button)
    {
        if (!button || !g_pluginEnabled.load()) {
            return false;
        }

        const uint32_t keyCode = ToMacroKeyCode(button);
        const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;

        bool consumed = false;
        if (isDown) {
            consumed = TryPowerAttack(button, keyCode);
        }

        const bool suppressBlockStart = (button->Value() != 0.0f) && IsPowerAttackChordActive(button, keyCode);
        if (!suppressBlockStart) {
            HandleBlockEvent(button, keyCode);
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
            if (!button) continue;

            const uint32_t macroKey = ToMacroKeyCode(button);
            if (macroKey < kMaxMacros) {
                g_keyStates[macroKey].store(button->Value() != 0.0f, std::memory_order_relaxed);
            }

            if (HandleButtonEvent(button)) {
                button->GetRuntimeData().value = 0.0f;
            }
        }
    }

    void TriggerPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_rightPowerAttackAction || !player) return;

        const bool mcoMode = g_mcoMode.load();
        g_task->AddTask([player, action = g_rightPowerAttackAction, mcoMode]() {
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

    void TriggerDualPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_dualPowerAttackAction || !player) return;

        const bool mcoMode = g_mcoMode.load();
        g_task->AddTask([player, action = g_dualPowerAttackAction, mcoMode]() {
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

    bool IsMeleeWeaponInHand(PlayerCharacter* player, bool leftHand)
    {
        if (!player) return false;

        auto* actorState = player->AsActorState();
        if (!actorState || actorState->GetWeaponState() != WEAPON_STATE::kDrawn) return false;

        TESForm* obj = player->GetEquippedObject(leftHand);
        if (!obj) return false;

        auto* weap = skyrim_cast<TESObjectWEAP*>(obj);
        if (!weap) return false;
        if (weap->IsBow() || weap->IsCrossbow() || weap->IsStaff()) return false;

        return true;
    }

    struct AttackBlockHook
    {
        static void Install()
        {
            REL::Relocation<std::uintptr_t> vtbl{ VTABLE_AttackBlockHandler[0] };
            _ProcessButton = reinterpret_cast<ProcessButton_t>(vtbl.write_vfunc(0x4, Hook_ProcessButton));
        }

    private:
        using ProcessButton_t = void (*)(AttackBlockHandler*, ButtonEvent*, PlayerControlsData*);

        static void Hook_ProcessButton(AttackBlockHandler* a_this, ButtonEvent* a_event, PlayerControlsData* a_data)
        {
            if (a_event && g_pluginEnabled.load()) {
                auto* userEvents = UserEvents::GetSingleton();
                if (userEvents) {
                    const auto& userEvent = a_event->GetUserEvent();
                    const bool isLeftAttack = (userEvent == userEvents->leftAttack);
                    const bool isAttackKey = isLeftAttack || (userEvent == userEvents->rightAttack);
                    const float threshold = g_initialPowerAttackDelay->data.f;
                    const float held = a_event->HeldDuration();
                    const bool isHeldPowerAttack = a_event->Value() != 0.0f && held > 0.0f && held >= threshold;
                    if (isAttackKey && isHeldPowerAttack &&
                        IsMeleeWeaponInHand(PlayerCharacter::GetSingleton(), isLeftAttack)) {
                        return;
                    }
                }
            }

            _ProcessButton(a_this, a_event, a_data);
        }

        static inline ProcessButton_t _ProcessButton = nullptr;
    };

    bool SaveConfig(uint32_t keyCode, uint32_t leftKeyCode, uint32_t blockKeyCode, uint32_t dualKeyCode,
                     uint32_t rightMod, uint32_t leftMod, uint32_t dualMod,
                     uint32_t rightGPMod, uint32_t leftGPMod, uint32_t dualGPMod,
                     bool mcoMode, bool noStaminaPA, bool pluginEnabled)
    {
        keyCode = SanitizeKeyCode(keyCode);
        leftKeyCode = SanitizeKeyCode(leftKeyCode);
        blockKeyCode = SanitizeKeyCode(blockKeyCode);
        dualKeyCode = SanitizeKeyCode(dualKeyCode);
        rightMod = SanitizeModifierKeyCode(rightMod);
        leftMod = SanitizeModifierKeyCode(leftMod);
        dualMod = SanitizeModifierKeyCode(dualMod);
        rightGPMod = SanitizeGamepadModifierCode(rightGPMod);
        leftGPMod = SanitizeGamepadModifierCode(leftGPMod);
        dualGPMod = SanitizeGamepadModifierCode(dualGPMod);
        const std::string savePath = GetPrimaryConfigPath();

        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iKeycode", static_cast<long>(keyCode));
        g_ini.SetLongValue("General", "iLeftKeycode", static_cast<long>(leftKeyCode));
        g_ini.SetLongValue("General", "iBlockKeycode", static_cast<long>(blockKeyCode));
        g_ini.SetLongValue("General", "iDualKeycode", static_cast<long>(dualKeyCode));
        g_ini.SetLongValue("General", "iRightModifier", static_cast<long>(rightMod));
        g_ini.SetLongValue("General", "iLeftModifier", static_cast<long>(leftMod));
        g_ini.SetLongValue("General", "iDualModifier", static_cast<long>(dualMod));
        g_ini.SetLongValue("General", "iRightGamepadModifier", static_cast<long>(rightGPMod));
        g_ini.SetLongValue("General", "iLeftGamepadModifier", static_cast<long>(leftGPMod));
        g_ini.SetLongValue("General", "iDualGamepadModifier", static_cast<long>(dualGPMod));
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

        SKSE::log::info("Saved config enabled={} right={} left={} block={} dual={} rmod={} lmod={} dmod={} rgmod={} lgmod={} dgmod={} mco={} noStaminaPA={} to '{}'",
            pluginEnabled, keyCode, leftKeyCode, blockKeyCode, dualKeyCode, rightMod, leftMod, dualMod, rightGPMod, leftGPMod, dualGPMod, mcoMode, noStaminaPA, savePath);
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
        const uint32_t currentDualCode = g_altDualPowerAttackKey.load();
        const uint32_t pendingDualCode = g_pendingAltDualPowerAttackKey;
        const uint32_t currentRMod = g_rightModifierKey.load();
        const uint32_t pendingRMod = g_pendingRightModifierKey;
        const uint32_t currentLMod = g_leftModifierKey.load();
        const uint32_t pendingLMod = g_pendingLeftModifierKey;
        const uint32_t currentDMod = g_dualModifierKey.load();
        const uint32_t pendingDMod = g_pendingDualModifierKey;
        const uint32_t currentRGPMod = g_rightGamepadModifier.load();
        const uint32_t pendingRGPMod = g_pendingRightGamepadModifier;
        const uint32_t currentLGPMod = g_leftGamepadModifier.load();
        const uint32_t pendingLGPMod = g_pendingLeftGamepadModifier;
        const uint32_t currentDGPMod = g_dualGamepadModifier.load();
        const uint32_t pendingDGPMod = g_pendingDualGamepadModifier;
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
        uint32_t chosenDualCode = pendingDualCode;
        uint32_t chosenRMod = pendingRMod;
        uint32_t chosenLMod = pendingLMod;
        uint32_t chosenDMod = pendingDMod;
        uint32_t chosenRGPMod = pendingRGPMod;
        uint32_t chosenLGPMod = pendingLGPMod;
        uint32_t chosenDGPMod = pendingDGPMod;
        bool chosenMco = pendingMco;
        bool chosenNoStamina = pendingNoStamina;
        bool chosenEnabled = pendingEnabled;
        bool changed = false;
        bool leftChanged = false;
        bool blockChanged = false;
        bool dualChanged = false;
        bool rModChanged = false;
        bool lModChanged = false;
        bool dModChanged = false;
        bool rGPModChanged = false;
        bool lGPModChanged = false;
        bool dGPModChanged = false;
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
            ImGuiMCP::Text("Dual Power Attack");
            dualChanged = DrawKeyOptionCombo("Key##mk_dual", kMouseOptions, pendingDualCode, chosenDualCode) || dualChanged;
            dModChanged = DrawKeyOptionCombo("Modifier##mk_dmod", kModifierOptions, pendingDMod, chosenDMod) || dModChanged;

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
            ImGuiMCP::Text("Dual Power Attack");
            dualChanged = DrawKeyOptionCombo("Button##gp_dual", kGamepadOptions, pendingDualCode, chosenDualCode) || dualChanged;
            dGPModChanged = DrawKeyOptionCombo("Modifier##gp_dmod", kGamepadModifierOptions, pendingDGPMod, chosenDGPMod) || dGPModChanged;

            ImGuiMCP::Separator();
            ImGuiMCP::Text("Block");
            blockChanged = DrawKeyOptionCombo("Button##gp_block", kGamepadOptions, pendingBlockCode, chosenBlockCode) || blockChanged;
        }

        ImGuiMCP::Separator();

        if (ImGuiMCP::Checkbox("MCO Compatibility", &chosenMco)) {
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

        if (dualChanged) {
            g_pendingAltDualPowerAttackKey = SanitizeKeyCode(chosenDualCode);
            g_hasUnsavedDualKeyChange = (g_pendingAltDualPowerAttackKey != currentDualCode);
        }

        if (rModChanged) {
            g_pendingRightModifierKey = SanitizeModifierKeyCode(chosenRMod);
            g_hasUnsavedRightModChange = (g_pendingRightModifierKey != currentRMod);
        }

        if (lModChanged) {
            g_pendingLeftModifierKey = SanitizeModifierKeyCode(chosenLMod);
            g_hasUnsavedLeftModChange = (g_pendingLeftModifierKey != currentLMod);
        }

        if (dModChanged) {
            g_pendingDualModifierKey = SanitizeModifierKeyCode(chosenDMod);
            g_hasUnsavedDualModChange = (g_pendingDualModifierKey != currentDMod);
        }

        if (rGPModChanged) {
            g_pendingRightGamepadModifier = SanitizeGamepadModifierCode(chosenRGPMod);
            g_hasUnsavedRightGPModChange = (g_pendingRightGamepadModifier != currentRGPMod);
        }

        if (lGPModChanged) {
            g_pendingLeftGamepadModifier = SanitizeGamepadModifierCode(chosenLGPMod);
            g_hasUnsavedLeftGPModChange = (g_pendingLeftGamepadModifier != currentLGPMod);
        }

        if (dGPModChanged) {
            g_pendingDualGamepadModifier = SanitizeGamepadModifierCode(chosenDGPMod);
            g_hasUnsavedDualGPModChange = (g_pendingDualGamepadModifier != currentDGPMod);
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
            g_hasUnsavedBlockKeyChange || g_hasUnsavedDualKeyChange ||
            g_hasUnsavedRightModChange || g_hasUnsavedLeftModChange || g_hasUnsavedDualModChange ||
            g_hasUnsavedRightGPModChange || g_hasUnsavedLeftGPModChange || g_hasUnsavedDualGPModChange ||
            g_hasUnsavedMcoChange || g_hasUnsavedNoStaminaChange || g_hasUnsavedEnabledChange;

        ImGuiMCP::Separator();

        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }

        if (ImGuiMCP::Button("Save to INI")) {
            const uint32_t saveCode = SanitizeKeyCode(g_pendingAltPowerAttackKey);
            const uint32_t saveLeftCode = SanitizeKeyCode(g_pendingAltLeftPowerAttackKey);
            const uint32_t saveBlockCode = SanitizeKeyCode(g_pendingAltBlockKey);
            const uint32_t saveDualCode = SanitizeKeyCode(g_pendingAltDualPowerAttackKey);
            const uint32_t saveRMod = SanitizeModifierKeyCode(g_pendingRightModifierKey);
            const uint32_t saveLMod = SanitizeModifierKeyCode(g_pendingLeftModifierKey);
            const uint32_t saveDMod = SanitizeModifierKeyCode(g_pendingDualModifierKey);
            const uint32_t saveRGPMod = SanitizeGamepadModifierCode(g_pendingRightGamepadModifier);
            const uint32_t saveLGPMod = SanitizeGamepadModifierCode(g_pendingLeftGamepadModifier);
            const uint32_t saveDGPMod = SanitizeGamepadModifierCode(g_pendingDualGamepadModifier);
            const bool saveMco = g_pendingMcoMode;
            const bool saveNoStamina = g_pendingNoStaminaPowerAttack;
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveCode, saveLeftCode, saveBlockCode, saveDualCode, saveRMod, saveLMod, saveDMod, saveRGPMod, saveLGPMod, saveDGPMod, saveMco, saveNoStamina, saveEnabled)) {
                g_altPowerAttackKey = saveCode;
                g_pendingAltPowerAttackKey = saveCode;
                g_hasUnsavedKeyChange = false;
                g_altLeftPowerAttackKey = saveLeftCode;
                g_pendingAltLeftPowerAttackKey = saveLeftCode;
                g_hasUnsavedLeftKeyChange = false;
                g_altBlockKey = saveBlockCode;
                g_pendingAltBlockKey = saveBlockCode;
                g_hasUnsavedBlockKeyChange = false;
                g_altDualPowerAttackKey = saveDualCode;
                g_pendingAltDualPowerAttackKey = saveDualCode;
                g_hasUnsavedDualKeyChange = false;
                g_rightModifierKey = saveRMod;
                g_pendingRightModifierKey = saveRMod;
                g_hasUnsavedRightModChange = false;
                g_leftModifierKey = saveLMod;
                g_pendingLeftModifierKey = saveLMod;
                g_hasUnsavedLeftModChange = false;
                g_dualModifierKey = saveDMod;
                g_pendingDualModifierKey = saveDMod;
                g_hasUnsavedDualModChange = false;
                g_rightGamepadModifier = saveRGPMod;
                g_pendingRightGamepadModifier = saveRGPMod;
                g_hasUnsavedRightGPModChange = false;
                g_leftGamepadModifier = saveLGPMod;
                g_pendingLeftGamepadModifier = saveLGPMod;
                g_hasUnsavedLeftGPModChange = false;
                g_dualGamepadModifier = saveDGPMod;
                g_pendingDualGamepadModifier = saveDGPMod;
                g_hasUnsavedDualGPModChange = false;
                g_mcoMode = saveMco;
                g_pendingMcoMode = saveMco;
                g_hasUnsavedMcoChange = false;
                g_noStaminaPowerAttack = saveNoStamina;
                g_pendingNoStaminaPowerAttack = saveNoStamina;
                g_hasUnsavedNoStaminaChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
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
            g_pendingAltDualPowerAttackKey = currentDualCode;
            g_hasUnsavedDualKeyChange = false;
            g_pendingRightModifierKey = currentRMod;
            g_hasUnsavedRightModChange = false;
            g_pendingLeftModifierKey = currentLMod;
            g_hasUnsavedLeftModChange = false;
            g_pendingDualModifierKey = currentDMod;
            g_hasUnsavedDualModChange = false;
            g_pendingRightGamepadModifier = currentRGPMod;
            g_hasUnsavedRightGPModChange = false;
            g_pendingLeftGamepadModifier = currentLGPMod;
            g_hasUnsavedLeftGPModChange = false;
            g_pendingDualGamepadModifier = currentDGPMod;
            g_hasUnsavedDualGPModChange = false;
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
            g_altDualPowerAttackKey = kKeyDisabled;
            g_pendingAltDualPowerAttackKey = kKeyDisabled;
            g_hasUnsavedDualKeyChange = false;
            g_rightModifierKey = kModifierNone;
            g_pendingRightModifierKey = kModifierNone;
            g_hasUnsavedRightModChange = false;
            g_leftModifierKey = kModifierNone;
            g_pendingLeftModifierKey = kModifierNone;
            g_hasUnsavedLeftModChange = false;
            g_dualModifierKey = kModifierNone;
            g_pendingDualModifierKey = kModifierNone;
            g_hasUnsavedDualModChange = false;
            g_rightGamepadModifier = kModifierNone;
            g_pendingRightGamepadModifier = kModifierNone;
            g_hasUnsavedRightGPModChange = false;
            g_leftGamepadModifier = kModifierNone;
            g_pendingLeftGamepadModifier = kModifierNone;
            g_hasUnsavedLeftGPModChange = false;
            g_dualGamepadModifier = kModifierNone;
            g_pendingDualGamepadModifier = kModifierNone;
            g_hasUnsavedDualGPModChange = false;
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

        const long rawDualValue = g_ini.GetLongValue("General", "iDualKeycode", static_cast<long>(kKeyDisabled));
        uint32_t parsedDualKey = SanitizeKeyCode(static_cast<uint32_t>(rawDualValue < 0 ? 0 : rawDualValue));

        g_altDualPowerAttackKey = parsedDualKey;
        g_pendingAltDualPowerAttackKey = parsedDualKey;
        g_hasUnsavedDualKeyChange = false;

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

        const long rawDMod = g_ini.GetLongValue("General", "iDualModifier", static_cast<long>(kModifierNone));
        uint32_t parsedDMod = SanitizeModifierKeyCode(static_cast<uint32_t>(rawDMod < 0 ? 0 : rawDMod));

        g_dualModifierKey = parsedDMod;
        g_pendingDualModifierKey = parsedDMod;
        g_hasUnsavedDualModChange = false;

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

        const long rawDGPMod = g_ini.GetLongValue("General", "iDualGamepadModifier", static_cast<long>(kModifierNone));
        uint32_t parsedDGPMod = SanitizeGamepadModifierCode(static_cast<uint32_t>(rawDGPMod < 0 ? 0 : rawDGPMod));

        g_dualGamepadModifier = parsedDGPMod;
        g_pendingDualGamepadModifier = parsedDGPMod;
        g_hasUnsavedDualGPModChange = false;

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
        SKSE::log::info("Loaded config enabled={} right={} left={} block={} dual={} rmod={} lmod={} dmod={} rgpmod={} lgpmod={} dgpmod={} mco={} noStaminaPA={} from '{}'",
            parsedEnabled, parsedKey, parsedLeftKey, parsedBlockKey, parsedDualKey, parsedRMod, parsedLMod, parsedDMod, parsedRGPMod, parsedLGPMod, parsedDGPMod, parsedMco, parsedNoStamina, path);
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
            if (g_hasUnsavedDualKeyChange) {
                g_pendingAltDualPowerAttackKey = g_altDualPowerAttackKey.load();
                g_hasUnsavedDualKeyChange = false;
            }
            if (g_hasUnsavedRightModChange) {
                g_pendingRightModifierKey = g_rightModifierKey.load();
                g_hasUnsavedRightModChange = false;
            }
            if (g_hasUnsavedLeftModChange) {
                g_pendingLeftModifierKey = g_leftModifierKey.load();
                g_hasUnsavedLeftModChange = false;
            }
            if (g_hasUnsavedDualModChange) {
                g_pendingDualModifierKey = g_dualModifierKey.load();
                g_hasUnsavedDualModChange = false;
            }
            if (g_hasUnsavedRightGPModChange) {
                g_pendingRightGamepadModifier = g_rightGamepadModifier.load();
                g_hasUnsavedRightGPModChange = false;
            }
            if (g_hasUnsavedLeftGPModChange) {
                g_pendingLeftGamepadModifier = g_leftGamepadModifier.load();
                g_hasUnsavedLeftGPModChange = false;
            }
            if (g_hasUnsavedDualGPModChange) {
                g_pendingDualGamepadModifier = g_dualGamepadModifier.load();
                g_hasUnsavedDualGPModChange = false;
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

        AttackBlockHook::Install();

        LoadConfig();

        SKSEMenuFramework::SetSection("SimplePowerAttack");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuFrameworkSection);

        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }
        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }


        SKSE::log::info("Initialized — right keycode={}, left keycode={}, dual keycode={}, block keycode={}", g_altPowerAttackKey.load(), g_altLeftPowerAttackKey.load(), g_altDualPowerAttackKey.load(), g_altBlockKey.load());
    });
    return true;
}
