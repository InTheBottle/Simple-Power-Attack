#include <SimpleIni.h>
#include <ShlObj.h>

#include "SKSEMenuFramework.h"

using namespace RE;
using namespace SKSE;

extern "C" IMAGE_DOS_HEADER __ImageBase;

namespace {
    constexpr uint32_t kKeyDisabled = 0;  // 0 = key disabled

    constexpr uint32_t kMacroNumKeyboardKeys = 256;
    constexpr uint32_t kMacroNumMouseButtons = 8;
    constexpr uint32_t kMacroMouseWheelOffset = kMacroNumKeyboardKeys + kMacroNumMouseButtons;
    constexpr uint32_t kMacroGamepadOffset = kMacroMouseWheelOffset + 2;
    constexpr uint32_t kMaxMacros = kMacroGamepadOffset + 16;

    constexpr uint32_t kMouseLeftMacro = kMacroNumKeyboardKeys;  // 256 == Mouse Left (used to interact with the menu)
    constexpr uint32_t kEscapeScancode = 0x01;                   // DirectInput scancode for Escape (cancels capture)
    constexpr uint32_t kCaptureCancelled = 0xFFFFFFFFu;          // sentinel: capture ended without a new binding

    // Skyrim gamepad button IDs (from ButtonEvent::GetIDCode for kGamepad device), controller-agnostic.
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

    constexpr const char* kPrimaryConfigFileName = "SimpleBlock.ini";
    constexpr const char* kLegacyConfigFileName = "SimplePowerAttack.ini";  // migration source for old block binding

    std::atomic<uint32_t> g_altBlockKey = kKeyDisabled;
    uint32_t g_pendingAltBlockKey = kKeyDisabled;
    bool g_hasUnsavedBlockKeyChange = false;
    std::atomic<bool> g_pluginEnabled = true;
    bool g_pendingPluginEnabled = true;
    bool g_hasUnsavedEnabledChange = false;
    SKSEMenuFramework::Model::InputEvent* g_menuFrameworkInputHook = nullptr;
    SKSEMenuFramework::Model::Event* g_menuFrameworkEventHook = nullptr;

    std::atomic<bool> g_captureActive = false;
    std::atomic<uint32_t> g_capturedCode = kCaptureCancelled;
    bool g_renderCaptureActive = false;

    CSimpleIniA g_ini(true, false, false);

    void LoadConfig();

    std::string GetConfigPath(const char* fileName)
    {
        char modulePath[MAX_PATH]{};
        const auto written = GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        if (written > 0) {
            std::filesystem::path path(modulePath);
            path.replace_filename(fileName);
            return path.string();
        }

        return std::string("Data\\SKSE\\Plugins\\") + fileName;
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
            if (weap->IsBow() || weap->IsCrossbow()) return false;
        }

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

    void HandleCaptureInput(uint32_t macroKey)
    {
        if (macroKey == kEscapeScancode) {
            g_capturedCode.store(kCaptureCancelled, std::memory_order_relaxed);
        } else {
            g_capturedCode.store(macroKey, std::memory_order_relaxed);
        }
        g_captureActive.store(false, std::memory_order_release);
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

            // Press-to-bind: while listening, the next fresh button-down becomes the new binding.
            // Mouse Left is skipped so the user can still click menu buttons to cancel/switch.
            if (g_captureActive.load(std::memory_order_acquire)) {
                const bool isDown = button->Value() != 0.0f && button->HeldDuration() == 0.0f;
                if (isDown && macroKey < kMaxMacros && macroKey != kMouseLeftMacro) {
                    HandleCaptureInput(macroKey);
                    button->GetRuntimeData().value = 0.0f;  // consume so it doesn't leak to game/menu
                }
                continue;  // suppress gameplay handling while capturing
            }

            if (g_pluginEnabled.load()) {
                HandleBlockEvent(button, macroKey);
            }
        }
    }

    uint32_t SanitizeKeyCode(uint32_t keyCode)
    {
        if (keyCode >= kMaxMacros) {
            return kKeyDisabled;
        }
        return keyCode;
    }

    bool SaveConfig(uint32_t blockKeyCode, bool pluginEnabled)
    {
        blockKeyCode = SanitizeKeyCode(blockKeyCode);
        const std::string savePath = GetConfigPath(kPrimaryConfigFileName);

        g_ini.Reset();
        g_ini.SetBoolValue("General", "bEnabled", pluginEnabled);
        g_ini.SetLongValue("General", "iBlockKeycode", static_cast<long>(blockKeyCode));

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

        SKSE::log::info("Saved config enabled={} block={} to '{}'", pluginEnabled, blockKeyCode, savePath);
        return true;
    }

    const char* GetMacroKeyName(uint32_t code)
    {
        if (code == kKeyDisabled) {
            return "Not Set";
        }

        if (code >= kMacroNumKeyboardKeys && code < kMacroGamepadOffset) {
            static const char* mouseNames[] = {
                "Mouse Left", "Mouse Right", "Mouse Middle",
                "Mouse 4", "Mouse 5", "Mouse 6", "Mouse 7", "Mouse 8",
                "Mouse Wheel Up", "Mouse Wheel Down"
            };
            const uint32_t idx = code - kMacroNumKeyboardKeys;
            if (idx < (sizeof(mouseNames) / sizeof(mouseNames[0]))) return mouseNames[idx];
            return "Mouse ?";
        }

        if (code >= kMacroGamepadOffset && code < kMaxMacros) {
            static const char* gamepadNames[] = {
                "D-Pad Up", "D-Pad Down", "D-Pad Left", "D-Pad Right",
                "Start", "Back", "Left Thumb (LS)", "Right Thumb (RS)",
                "Left Shoulder (LB)", "Right Shoulder (RB)",
                "A", "B", "X", "Y",
                "Left Trigger (LT)", "Right Trigger (RT)"
            };
            const uint32_t idx = code - kMacroGamepadOffset;
            if (idx < (sizeof(gamepadNames) / sizeof(gamepadNames[0]))) return gamepadNames[idx];
            return "Gamepad ?";
        }

        // Keyboard DirectInput scancodes (1..255)
        if (code < kMacroNumKeyboardKeys) {
            static const char* keyNames[] = {
                "None",       "Escape",     "1",          "2",          "3",          "4",          "5",          "6",
                "7",          "8",          "9",          "0",          "-",          "=",          "Backspace",  "Tab",
                "Q",          "W",          "E",          "R",          "T",          "Y",          "U",          "I",
                "O",          "P",          "[",          "]",          "Enter",      "Left Ctrl",  "A",          "S",
                "D",          "F",          "G",          "H",          "J",          "K",          "L",          ";",
                "'",          "`",          "Left Shift", "\\",         "Z",          "X",          "C",          "V",
                "B",          "N",          "M",          ",",          ".",          "/",          "Right Shift","Num *",
                "Left Alt",   "Space",      "Caps Lock",  "F1",         "F2",         "F3",         "F4",         "F5",
                "F6",         "F7",         "F8",         "F9",         "F10",        "Num Lock",   "Scroll Lock","Num 7",
                "Num 8",      "Num 9",      "Num -",      "Num 4",      "Num 5",      "Num 6",      "Num +",      "Num 1",
                "Num 2",      "Num 3",      "Num 0",      "Num .",      nullptr,      nullptr,      nullptr,      "F11",
                "F12"
            };
            if (code < (sizeof(keyNames) / sizeof(keyNames[0])) && keyNames[code]) {
                return keyNames[code];
            }

            switch (code) {
            case 0x9C: return "Num Enter";
            case 0x9D: return "Right Ctrl";
            case 0xB5: return "Num /";
            case 0xB8: return "Right Alt";
            case 0xC7: return "Home";
            case 0xC8: return "Up Arrow";
            case 0xC9: return "Page Up";
            case 0xCB: return "Left Arrow";
            case 0xCD: return "Right Arrow";
            case 0xCF: return "End";
            case 0xD0: return "Down Arrow";
            case 0xD1: return "Page Down";
            case 0xD2: return "Insert";
            case 0xD3: return "Delete";
            default: break;
            }
            return "Unknown Key";
        }

        return "Unknown";
    }

    void StartCapture()
    {
        g_renderCaptureActive = true;
        g_capturedCode.store(kCaptureCancelled, std::memory_order_relaxed);
        g_captureActive.store(true, std::memory_order_release);
    }

    void CancelCapture()
    {
        g_renderCaptureActive = false;
        g_captureActive.store(false, std::memory_order_release);
    }

    // Render thread: if the input thread finished a capture, apply the result to the pending value.
    void PollCaptureResult()
    {
        if (!g_renderCaptureActive) return;
        if (g_captureActive.load(std::memory_order_acquire)) return;  // still listening

        const uint32_t raw = g_capturedCode.load(std::memory_order_relaxed);
        g_renderCaptureActive = false;

        if (raw == kCaptureCancelled) return;

        const uint32_t sanitized = SanitizeKeyCode(raw);
        g_pendingAltBlockKey = sanitized;
        g_hasUnsavedBlockKeyChange = (sanitized != g_altBlockKey.load());
    }

    // Draws a "press to bind" button plus a Clear button for the block binding.
    void DrawBindButton(const char* idSuffix)
    {
        const bool listening = g_renderCaptureActive;

        std::string label = listening
            ? std::string(">> Press any key / button...  (Esc to cancel) <<")
            : std::string(GetMacroKeyName(g_pendingAltBlockKey));
        label += "##";
        label += idSuffix;

        if (ImGuiMCP::Button(label.c_str(), ImGuiMCP::ImVec2{ 300.0f, 0.0f })) {
            if (listening) {
                CancelCapture();
            } else {
                StartCapture();
            }
        }

        ImGuiMCP::SameLine();
        std::string clearLabel = std::string("Clear##clr_") + idSuffix;
        if (ImGuiMCP::Button(clearLabel.c_str())) {
            g_pendingAltBlockKey = kKeyDisabled;
            g_hasUnsavedBlockKeyChange = (kKeyDisabled != g_altBlockKey.load());
            if (listening) CancelCapture();
        }
    }

    void __stdcall RenderMenuFrameworkSection()
    {
        PollCaptureResult();

        ImGuiMCP::Text("Simple Block");
        ImGuiMCP::Separator();
        ImGuiMCP::TextWrapped(
            "Click the binding, then press any keyboard key, mouse button, or controller button to assign it. "
            "Press Esc to cancel. Works with keyboard, mouse, and any controller.");
        ImGuiMCP::Separator();

        ImGuiMCP::Text("Block");
        ImGuiMCP::Text("Key / Button");
        ImGuiMCP::SameLine(170.0f);
        DrawBindButton("block_key");
        ImGuiMCP::Separator();

        bool disableChecked = !g_pendingPluginEnabled;
        if (ImGuiMCP::Checkbox("Disable Plugin", &disableChecked)) {
            g_pendingPluginEnabled = !disableChecked;
            g_hasUnsavedEnabledChange = (g_pendingPluginEnabled != g_pluginEnabled.load());
        }

        const bool hasUnsaved = g_hasUnsavedBlockKeyChange || g_hasUnsavedEnabledChange;

        ImGuiMCP::Separator();

        if (hasUnsaved) {
            ImGuiMCP::TextWrapped("You have unsaved changes.");
        }

        if (ImGuiMCP::Button("Save to INI")) {
            const uint32_t saveBlockCode = SanitizeKeyCode(g_pendingAltBlockKey);
            const bool saveEnabled = g_pendingPluginEnabled;
            if (SaveConfig(saveBlockCode, saveEnabled)) {
                g_altBlockKey = saveBlockCode;
                g_pendingAltBlockKey = saveBlockCode;
                g_hasUnsavedBlockKeyChange = false;
                g_pluginEnabled = saveEnabled;
                g_pendingPluginEnabled = saveEnabled;
                g_hasUnsavedEnabledChange = false;
            }
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Discard")) {
            CancelCapture();
            g_pendingAltBlockKey = g_altBlockKey.load();
            g_hasUnsavedBlockKeyChange = false;
            g_pendingPluginEnabled = g_pluginEnabled.load();
            g_hasUnsavedEnabledChange = false;
        }

        ImGuiMCP::SameLine();

        if (ImGuiMCP::Button("Reload from INI")) {
            CancelCapture();
            LoadConfig();
        }
    }

    void LoadConfig()
    {
        const std::string path = GetConfigPath(kPrimaryConfigFileName);

        const SI_Error result = g_ini.LoadFile(path.c_str());
        if (result < 0) {
            // One-time migration: pull the old block binding from SimplePowerAttack.ini if our INI is absent.
            uint32_t migratedKey = kKeyDisabled;
            const std::string legacyPath = GetConfigPath(kLegacyConfigFileName);
            if (g_ini.LoadFile(legacyPath.c_str()) >= 0) {
                const long rawLegacy = g_ini.GetLongValue("General", "iBlockKeycode", static_cast<long>(kKeyDisabled));
                migratedKey = SanitizeKeyCode(static_cast<uint32_t>(rawLegacy < 0 ? 0 : rawLegacy));
            }
            SKSE::log::warn("Config not found at '{}', using defaults (migrated block key={})", path, migratedKey);
            g_altBlockKey = migratedKey;
            g_pendingAltBlockKey = migratedKey;
            g_hasUnsavedBlockKeyChange = false;
            g_pluginEnabled = true;
            g_pendingPluginEnabled = true;
            g_hasUnsavedEnabledChange = false;
            g_ini.Reset();
            return;
        }

        const long rawBlockValue = g_ini.GetLongValue("General", "iBlockKeycode", static_cast<long>(kKeyDisabled));
        uint32_t parsedBlockKey = SanitizeKeyCode(static_cast<uint32_t>(rawBlockValue < 0 ? 0 : rawBlockValue));

        g_altBlockKey = parsedBlockKey;
        g_pendingAltBlockKey = parsedBlockKey;
        g_hasUnsavedBlockKeyChange = false;

        const bool parsedEnabled = g_ini.GetBoolValue("General", "bEnabled", true);
        g_pluginEnabled = parsedEnabled;
        g_pendingPluginEnabled = parsedEnabled;
        g_hasUnsavedEnabledChange = false;

        g_ini.Reset();
        SKSE::log::info("Loaded config enabled={} block={} from '{}'", parsedEnabled, parsedBlockKey, path);
    }

    bool __stdcall OnMenuFrameworkInput(RE::InputEvent* events)
    {
        if (events) ProcessInputChain(events);
        return false;
    }

    void __stdcall OnMenuFrameworkEvent(SKSEMenuFramework::Model::EventType eventType)
    {
        if (eventType == SKSEMenuFramework::Model::kCloseMenu) {
            // Stop listening for a keybind and drop any unsaved edits back to the saved values.
            CancelCapture();
            if (g_hasUnsavedBlockKeyChange) {
                g_pendingAltBlockKey = g_altBlockKey.load();
                g_hasUnsavedBlockKeyChange = false;
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
            return logDir / "SimpleBlock.log";
        }
        if (docPath) CoTaskMemFree(docPath);
        char modulePath[MAX_PATH]{};
        GetModuleFileNameA(reinterpret_cast<HMODULE>(&__ImageBase), modulePath, MAX_PATH);
        std::filesystem::path fallback(modulePath);
        fallback.replace_filename("SimpleBlock.log");
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

    GetMessagingInterface()->RegisterListener([](MessagingInterface::Message* msg) {
        if (!msg || msg->type != MessagingInterface::kDataLoaded) return;

        if (!SKSEMenuFramework::IsInstalled()) {
            SKSE::log::critical("SKSE Menu Framework not installed.");
            return;
        }

        LoadConfig();

        SKSEMenuFramework::SetSection("SimpleBlock");
        SKSEMenuFramework::AddSectionItem("Settings", RenderMenuFrameworkSection);

        if (!g_menuFrameworkInputHook) {
            g_menuFrameworkInputHook = SKSEMenuFramework::AddInputEvent(OnMenuFrameworkInput);
        }
        if (!g_menuFrameworkEventHook) {
            g_menuFrameworkEventHook = SKSEMenuFramework::AddEvent(OnMenuFrameworkEvent, 0.0f);
        }

        SKSE::log::info("Initialized — block keycode={}", g_altBlockKey.load());
    });
    return true;
}
