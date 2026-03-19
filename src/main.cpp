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

    constexpr uint32_t kMacroNumKeyboardKeys = 256;
    constexpr uint32_t kMacroNumMouseButtons = 8;
    constexpr uint32_t kMacroMouseWheelOffset = kMacroNumKeyboardKeys + kMacroNumMouseButtons;
    constexpr uint32_t kMacroGamepadOffset = kMacroMouseWheelOffset + 2;
    constexpr uint32_t kMaxMacros = kMacroGamepadOffset + 16;

    constexpr std::array<KeyOption, 10> kMouseOptions{ {
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

    constexpr std::array<KeyOption, 16> kGamepadOptions{ {
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
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;
    BSTEventSink<InputEvents>* g_rawInputSink = nullptr;
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
        auto* weap = equippedObj ? skyrim_cast<TESObjectWEAP*>(equippedObj) : nullptr;
        if (weap && (weap->IsBow() || weap->IsStaff() || weap->IsCrossbow())) {
            return false;
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

        return rightWeap->IsOneHandedSword() || rightWeap->IsOneHandedDagger() ||
               rightWeap->IsOneHandedAxe() || rightWeap->IsOneHandedMace();
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

    bool HandleButtonEvent(ButtonEvent* button)
    {
        if (!button) {
            return false;
        }

        const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
        if (!isDown) {
            return false;
        }

        const uint32_t keyCode = ToMacroKeyCode(button);
        const bool isRightKey = keyCode == g_altPowerAttackKey.load();
        const bool isLeftKey = keyCode == g_altLeftPowerAttackKey.load();
        if (!isRightKey && !isLeftKey) return false;

        auto* player = PlayerCharacter::GetSingleton();
        if (!player || !IsGameplayInputAllowed() || !IsPlayerInValidCombatState(player)) return false;

        if (isRightKey) {
            TriggerPowerAttack(player);
            return true;
        }

        if (isLeftKey && IsLeftHandValidForPowerAttack(player)) {
            TriggerLeftPowerAttack(player);
            return true;
        }

        return false;
    }

    bool ProcessInputChain(RE::InputEvent* events)
    {
        for (auto* event = events; event; event = event->next) {
            if (event->eventType.get() != INPUT_EVENT_TYPE::kButton) {
                continue;
            }

            auto* button = event->AsButtonEvent();
            if (HandleButtonEvent(button)) {
                return true;
            }
        }

        return false;
    }

    void TriggerPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_rightPowerAttackAction || !player) return;

        BGSAction* action = (g_dualPowerAttackAction && IsDualWielding(player))
            ? g_dualPowerAttackAction
            : g_rightPowerAttackAction;

        g_task->AddTask([player, action]() {
            std::unique_ptr<TESActionData> data(TESActionData::Create());
            data->source = NiPointer<TESObjectREFR>(player);
            data->action = action;

            using ProcessAction_t = bool (*)(TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            processAction(data.get());
        });
    }

    void TriggerLeftPowerAttack(PlayerCharacter* player)
    {
        if (!g_task || !g_leftPowerAttackAction || !player) return;

        g_task->AddTask([player, action = g_leftPowerAttackAction]() {
            std::unique_ptr<TESActionData> data(TESActionData::Create());
            data->source = NiPointer<TESObjectREFR>(player);
            data->action = action;

            using ProcessAction_t = bool (*)(TESActionData*);
            REL::Relocation<ProcessAction_t> processAction{ RELOCATION_ID(40551, 41557) };
            processAction(data.get());
        });
    }

    uint32_t SanitizeKeyCode(uint32_t keyCode)
    {
        if (keyCode == 0 || keyCode >= kMaxMacros) {
            return kDefaultAltPowerAttackKey;
        }
        return keyCode;
    }

    bool SaveConfig(uint32_t keyCode, uint32_t leftKeyCode)
    {
        keyCode = SanitizeKeyCode(keyCode);
        leftKeyCode = SanitizeKeyCode(leftKeyCode);
        const std::string savePath = GetPrimaryConfigPath();

        g_ini.Reset();
        g_ini.SetLongValue("General", "iKeycode", static_cast<long>(keyCode));
        g_ini.SetLongValue("General", "iLeftKeycode", static_cast<long>(leftKeyCode));

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

        SKSE::log::info("Saved keycodes right={} left={} to '{}'", keyCode, leftKeyCode, savePath);
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

        ImGuiMCP::Text("Simple Power Attack");
        ImGuiMCP::Separator();
        ImGuiMCP::Text("Right power attack key: %u (pending: %u)", currentCode, pendingCode);
        ImGuiMCP::Text("Left power attack key: %u (pending: %u)", currentLeftCode, pendingLeftCode);
        ImGuiMCP::TextWrapped("Vanilla long-press power attack is disabled by this plugin.");

        uint32_t chosenCode = pendingCode;
        uint32_t chosenLeftCode = pendingLeftCode;
        bool changed = false;
        bool leftChanged = false;

        if (ImGuiMCP::CollapsingHeader("Right Power Attack - Mouse Keys", 0)) {
            changed = DrawKeyOptionCombo("Right mouse binding", kMouseOptions, pendingCode, chosenCode) || changed;
        }

        if (ImGuiMCP::CollapsingHeader("Right Power Attack - Gamepad Keys", 0)) {
            changed = DrawKeyOptionCombo("Right gamepad binding", kGamepadOptions, pendingCode, chosenCode) || changed;
        }

        if (ImGuiMCP::CollapsingHeader("Left Power Attack - Mouse Keys", 0)) {
            leftChanged = DrawKeyOptionCombo("Left mouse binding", kMouseOptions, pendingLeftCode, chosenLeftCode) || leftChanged;
        }

        if (ImGuiMCP::CollapsingHeader("Left Power Attack - Gamepad Keys", 0)) {
            leftChanged = DrawKeyOptionCombo("Left gamepad binding", kGamepadOptions, pendingLeftCode, chosenLeftCode) || leftChanged;
        }

        if (changed) {
            g_pendingAltPowerAttackKey = SanitizeKeyCode(chosenCode);
            g_hasUnsavedKeyChange = (g_pendingAltPowerAttackKey != currentCode);
        }

        if (leftChanged) {
            g_pendingAltLeftPowerAttackKey = SanitizeKeyCode(chosenLeftCode);
            g_hasUnsavedLeftKeyChange = (g_pendingAltLeftPowerAttackKey != currentLeftCode);
        }

        if (g_hasUnsavedKeyChange || g_hasUnsavedLeftKeyChange) {
            ImGuiMCP::TextWrapped("Unsaved key selection. Click Save to write to INI and apply.");
        }

        if (ImGuiMCP::Button("Save to INI")) {
            const uint32_t saveCode = SanitizeKeyCode(g_pendingAltPowerAttackKey);
            const uint32_t saveLeftCode = SanitizeKeyCode(g_pendingAltLeftPowerAttackKey);
            if (SaveConfig(saveCode, saveLeftCode)) {
                g_altPowerAttackKey = saveCode;
                g_pendingAltPowerAttackKey = saveCode;
                g_hasUnsavedKeyChange = false;
                g_altLeftPowerAttackKey = saveLeftCode;
                g_pendingAltLeftPowerAttackKey = saveLeftCode;
                g_hasUnsavedLeftKeyChange = false;
            }
        }

        if (ImGuiMCP::Button("Discard Unsaved Changes")) {
            g_pendingAltPowerAttackKey = currentCode;
            g_hasUnsavedKeyChange = false;
            g_pendingAltLeftPowerAttackKey = currentLeftCode;
            g_hasUnsavedLeftKeyChange = false;
        }

        if (ImGuiMCP::Button("Reload from INI")) {
            LoadConfig();
        }
    }

    void DisableVanillaLongPressPowerAttack()
    {
        g_initialPowerAttackDelay->data.f = 10.0f;
    }

    void LoadConfig()
    {
        const std::string path = GetPrimaryConfigPath();

        const SI_Error result = g_ini.LoadFile(path.c_str());
        if (result < 0) {
            SKSE::log::warn("Config not found at '{}', using defaults right={} left={}", path, kDefaultAltPowerAttackKey, kDefaultAltLeftPowerAttackKey);
            g_altPowerAttackKey = kDefaultAltPowerAttackKey;
            g_pendingAltPowerAttackKey = kDefaultAltPowerAttackKey;
            g_hasUnsavedKeyChange = false;
            g_altLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_pendingAltLeftPowerAttackKey = kDefaultAltLeftPowerAttackKey;
            g_hasUnsavedLeftKeyChange = false;
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
        g_ini.Reset();
        SKSE::log::info("Loaded keycodes right={} left={} from '{}'", parsedKey, parsedLeftKey, path);
    }

    bool __stdcall OnMenuFrameworkInput(RE::InputEvent* events)
    {
        return events ? ProcessInputChain(events) : false;
    }

    class RawInputEventSink : public BSTEventSink<InputEvents> {
    public:
        BSEventNotifyControl ProcessEvent(const InputEvents* events, BSTEventSource<InputEvents>*) override
        {
            if (events && *events) {
                ProcessInputChain(const_cast<InputEvent*>(*events));
            }
            return BSEventNotifyControl::kContinue;
        }

        TES_HEAP_REDEFINE_NEW();
    };

    void __stdcall OnMenuFrameworkEvent(SKSEMenuFramework::Model::EventType eventType)
    {
        if (eventType == SKSEMenuFramework::Model::kCloseMenu) {
            DisableVanillaLongPressPowerAttack();
            if (g_hasUnsavedKeyChange) {
                g_pendingAltPowerAttackKey = g_altPowerAttackKey.load();
                g_hasUnsavedKeyChange = false;
            }
            if (g_hasUnsavedLeftKeyChange) {
                g_pendingAltLeftPowerAttackKey = g_altLeftPowerAttackKey.load();
                g_hasUnsavedLeftKeyChange = false;
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

        LoadConfig();
        DisableVanillaLongPressPowerAttack();

        SKSEMenuFramework::SetSection("SimplePowerAttack");
        SKSEMenuFramework::AddSectionItem("Simple Power Attack", RenderMenuFrameworkSection);

        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }
        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }
        if (!g_rawInputSink) {
            if (auto* inputManager = BSInputDeviceManager::GetSingleton()) {
                g_rawInputSink = new RawInputEventSink();
                inputManager->AddEventSink(g_rawInputSink);
            }
        }

        SKSE::log::info("Initialized — right keycode={}, left keycode={}", g_altPowerAttackKey.load(), g_altLeftPowerAttackKey.load());
    });
    return true;
}
