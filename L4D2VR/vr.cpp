#include "vr.h"
#include <Windows.h>
#include "sdk.h"
#include "game.h"
#include "hooks.h"
#include "trace.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <unordered_map>
#include <string>
#include <filesystem>
#include <thread>
#include <type_traits>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>
#include "../dxvk/src/d3d9/d3d9_vr.h"
#include "debuglog.h"
#include "cameracollision.h"
#include "eye_bounds.h"
#include "msvc_openvr_abi.h"
#include "portalpose.h"
#include "optionalgungrip.h"
#include "portaltrace.h"
#include "gunray.h"
#include "pickuptrace.h"
#include "vrsettings.h"
#include "roomscale.h"
#include "aimmarker.h"

namespace
{
    constexpr bool kEnableVrMenuSubmission = true;
    constexpr bool kEnableVrMenuInput = true;
    constexpr bool kEnableConfigWatcher = false;
    constexpr bool kEnableMaterialSystemEyeTargets = true;

    bool GetRuntimeBaseDirectory(char *buffer, size_t bufferSize)
    {
        if (!buffer || bufferSize == 0)
            return false;

        HMODULE module = GetModuleHandleA("d3d9.dll");
        if (!module)
            return false;

        const DWORD length = GetModuleFileNameA(module, buffer, static_cast<DWORD>(bufferSize));
        if (length == 0 || length >= bufferSize)
            return false;

        char *lastSlash = strrchr(buffer, '\\');
        if (!lastSlash)
            return false;

        *lastSlash = '\0';
        return true;
    }

    void GetOverlayWindowSize(const VR &vr, int &width, int &height)
    {
        width = static_cast<int>(vr.m_VKBackBuffer.m_VulkanData.m_nWidth);
        height = static_cast<int>(vr.m_VKBackBuffer.m_VulkanData.m_nHeight);

        if (width <= 0)
            width = static_cast<int>(vr.m_RenderWidth);
        if (height <= 0)
            height = static_cast<int>(vr.m_RenderHeight);
    }
}

VR::VR(Game *game) 
{
    m_Game = game;
    PortalVrLog("VR::VR start");

    vr::HmdError error = vr::VRInitError_None;
    m_System = vr::VR_Init(&error, vr::VRApplication_Scene);

    if (error != vr::VRInitError_None) 
    {
        PortalVrLog("VR_Init failed error=%d (%s); retrying when SteamVR is ready", error, vr::VR_GetVRInitErrorAsEnglishDescription(error));
        return;
    }
    PortalVrLog("VR_Init succeeded");

    if (!vr::VRCompositor())
    {
        PortalVrLog("VRCompositor init failed");
        vr::VR_Shutdown();
        return;
    }
    PortalVrLog("VRCompositor ready");

    m_Input = vr::VRInput();
    m_System = vr::OpenVRInternal_ModuleContext().VRSystem();
    if (!m_Input || !m_System)
    {
        PortalVrLog("OpenVR system or input interface unavailable");
        vr::VR_Shutdown();
        return;
    }

    m_System->GetRecommendedRenderTargetSize(&m_RenderWidth, &m_RenderHeight);
    m_AntiAliasing = 0;
    PortalVrLog("Recommended render target %u x %u", m_RenderWidth, m_RenderHeight);

    float l_left = 0.0f, l_right = 0.0f, l_top = 0.0f, l_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Left, &l_left, &l_right, &l_top, &l_bottom);

    float r_left = 0.0f, r_right = 0.0f, r_top = 0.0f, r_bottom = 0.0f;
    m_System->GetProjectionRaw(vr::EVREye::Eye_Right, &r_left, &r_right, &r_top, &r_bottom);

    float tanHalfFov[2];

    SymmetricHalfTangents(
        l_left, l_right, l_top, l_bottom,
        r_left, r_right, r_top, r_bottom,
        tanHalfFov[0], tanHalfFov[1]);

    // Each eye texture is symmetric overscan. SteamVR stretches the submitted
    // subrect across that eye's raw frustum, and v = 0 is the top of the upright
    // Vulkan image. Full bounds put the texture center on the frustum center, so
    // a Quest 3's asymmetric raw projection diverges the eyes by about 30 degrees.
    // A symmetric report (Pico) produces {0, 0, 1, 1} from the same formula.
    const EyeTextureBounds leftBounds = EyeBoundsFromProjectionRaw(
        l_left, l_right, l_top, l_bottom, tanHalfFov[0], tanHalfFov[1]);
    const EyeTextureBounds rightBounds = EyeBoundsFromProjectionRaw(
        r_left, r_right, r_top, r_bottom, tanHalfFov[0], tanHalfFov[1]);
    m_TextureBounds[0] = { leftBounds.uMin, leftBounds.vMin, leftBounds.uMax, leftBounds.vMax };
    m_TextureBounds[1] = { rightBounds.uMin, rightBounds.vMin, rightBounds.uMax, rightBounds.vMax };
    PortalVrLog(
        "Texture bounds left u[%.3f, %.3f] v[%.3f, %.3f] right u[%.3f, %.3f] v[%.3f, %.3f]",
        m_TextureBounds[0].uMin, m_TextureBounds[0].uMax,
        m_TextureBounds[0].vMin, m_TextureBounds[0].vMax,
        m_TextureBounds[1].uMin, m_TextureBounds[1].uMax,
        m_TextureBounds[1].vMin, m_TextureBounds[1].vMax);

    m_Aspect = tanHalfFov[0] / tanHalfFov[1];
    m_Fov = 2.0f * atan(tanHalfFov[0]) * 360 / (3.14159265358979323846 * 2);

    InstallApplicationManifest("manifest.vrmanifest");
    if (SetActionManifest("action_manifest.json") != 0)
    {
        PortalVrLog("VR initialization stopped: action manifest unavailable");
        vr::VR_Shutdown();
        return;
    }
    ParseConfigFile();

    if (kEnableConfigWatcher)
    {
        std::thread configParser(&VR::WaitForConfigUpdate, this);
        configParser.detach();
        PortalVrLog("Config watcher started");
    }
    else
    {
        PortalVrLog("Config watcher disabled");
    }

    if (!g_D3DVR9)
    {
        PortalVrLog("VR::VR aborted: D3D9 VR interop not ready");
        vr::VR_Shutdown();
        return;
    }
    PortalVrLog("g_D3DVR9 ready");

    g_D3DVR9->GetBackBufferData(&m_VKBackBuffer);
    PortalVrLog(
        "Backbuffer ready width=%u height=%u",
        m_VKBackBuffer.m_VulkanData.m_nWidth,
        m_VKBackBuffer.m_VulkanData.m_nHeight);
    if (kEnableVrMenuSubmission || kEnableVrMenuInput)
    {
        m_Overlay = vr::VROverlay();
        if (m_Overlay)
        {
            m_Overlay->CreateOverlay("MenuOverlayKey", "MenuOverlay", &m_MainMenuHandle);
            m_Overlay->SetOverlayInputMethod(m_MainMenuHandle, vr::VROverlayInputMethod_Mouse);
            m_Overlay->SetOverlayFlag(m_MainMenuHandle, vr::VROverlayFlags_SendVRDiscreteScrollEvents, true);

            const vr::HmdVector2_t mouseScaleMenu = {
                static_cast<float>(m_RenderWidth),
                static_cast<float>(m_RenderHeight)
            };
            m_Overlay->SetOverlayCurvature(m_MainMenuHandle, 0.15f);
            m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScaleMenu);
            CreateAimMarker();
        }
    }
    else
    {
        PortalVrLog("VR menu overlay initialization skipped");
    }

    UpdatePosesAndActions();
    GetPoses();
    ResetPosition();
    PortalVrLog("UpdatePosesAndActions complete");

    if (!kEnableMaterialSystemEyeTargets)
        PortalVrLog("Using backbuffer VR submit path; materialsystem eye targets disabled");

    m_IsInitialized = true;
    m_IsVREnabled = true;
    m_PrevFrameTime = std::chrono::steady_clock::now();
    PortalVrLog("VR::VR complete");
}

int VR::SetActionManifest(const char *fileName) 
{
    char currentDir[MAX_STR_LEN];
    if (!GetRuntimeBaseDirectory(currentDir, ARRAYSIZE(currentDir)))
    {
        PortalVrLog("SetActionManifest failed: runtime base directory unavailable");
        Game::errorMsg("Failed to resolve runtime directory for action manifest");
        return 1;
    }

    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\SteamVRActionManifest\\%s", currentDir, fileName);
    PortalVrLog("SetActionManifest path=%s", path);

    if (m_Input->SetActionManifestPath(path) != vr::VRInputError_None) 
    {
        Game::errorMsg("SetActionManifestPath failed");
        return 1;
    }

    m_Input->GetActionHandle("/actions/main/in/ActivateVR", &m_ActionActivateVR);
    m_Input->GetActionHandle("/actions/main/in/Jump", &m_ActionJump);
    m_Input->GetActionHandle("/actions/main/in/PrimaryAttack", &m_ActionPrimaryAttack);
    m_Input->GetActionHandle("/actions/main/in/Reload", &m_ActionReload);
    m_Input->GetActionHandle("/actions/main/in/Use", &m_ActionUse);
    m_Input->GetActionHandle("/actions/main/in/Walk", &m_ActionWalk);
    m_Input->GetActionHandle("/actions/main/in/Turn", &m_ActionTurn);
    m_Input->GetActionHandle("/actions/main/in/SecondaryAttack", &m_ActionSecondaryAttack);
    m_Input->GetActionHandle("/actions/main/in/NextItem", &m_ActionNextItem);
    m_Input->GetActionHandle("/actions/main/in/PrevItem", &m_ActionPrevItem);
    m_Input->GetActionHandle("/actions/main/in/ResetPosition", &m_ActionResetPosition);
    m_Input->GetActionHandle("/actions/main/in/Crouch", &m_ActionCrouch);
    m_Input->GetActionHandle("/actions/main/in/Flashlight", &m_ActionFlashlight);
    m_Input->GetActionHandle("/actions/main/in/MenuSelect", &m_MenuSelect);
    m_Input->GetActionHandle("/actions/main/in/MenuBack", &m_MenuBack);
    m_Input->GetActionHandle("/actions/main/in/MenuUp", &m_MenuUp);
    m_Input->GetActionHandle("/actions/main/in/MenuDown", &m_MenuDown);
    m_Input->GetActionHandle("/actions/main/in/MenuLeft", &m_MenuLeft);
    m_Input->GetActionHandle("/actions/main/in/MenuRight", &m_MenuRight);
    m_Input->GetActionHandle("/actions/main/in/Spray", &m_Spray);
    m_Input->GetActionHandle("/actions/main/in/Scoreboard", &m_Scoreboard);
    m_Input->GetActionHandle("/actions/main/in/ShowHUD", &m_ShowHUD);
    m_Input->GetActionHandle("/actions/main/in/Pause", &m_Pause);
	if (m_Input->GetActionHandle("/actions/base/in/skeleton_lefthand", &m_ActionSkeletonLeft) != vr::VRInputError_None)
		m_ActionSkeletonLeft = 0;
	if (m_Input->GetActionHandle("/actions/base/in/skeleton_righthand", &m_ActionSkeletonRight) != vr::VRInputError_None)
		m_ActionSkeletonRight = 0;
	if (m_Input->GetActionHandle("/actions/base/in/support_lefthand", &m_ActionSupportLeft) != vr::VRInputError_None)
		m_ActionSupportLeft = 0;
	if (m_Input->GetActionHandle("/actions/base/in/support_righthand", &m_ActionSupportRight) != vr::VRInputError_None)
		m_ActionSupportRight = 0;
	// Standardized SteamVR Input hand poses. The action manifest binds these
	// for every shipped controller type, so they stay valid (and rebindable
	// through Steam Input) where legacy controller roles do not.
	if (m_Input->GetActionHandle("/actions/base/in/pose_lefthand", &m_ActionPoseLeft) != vr::VRInputError_None)
		m_ActionPoseLeft = 0;
	if (m_Input->GetActionHandle("/actions/base/in/pose_righthand", &m_ActionPoseRight) != vr::VRInputError_None)
		m_ActionPoseRight = 0;
	// Haptic outputs are optional: a saved custom binding may not include them.
	if (m_Input->GetActionHandle("/actions/base/out/vibration_left", &m_ActionHapticLeft) != vr::VRInputError_None)
		m_ActionHapticLeft = 0;
	if (m_Input->GetActionHandle("/actions/base/out/vibration_right", &m_ActionHapticRight) != vr::VRInputError_None)
		m_ActionHapticRight = 0;
	// Stable per-hand identifiers for pose restriction and future per-hand
	// queries. Failures are non-fatal: the legacy role path remains.
	if (m_Input->GetInputSourceHandle("/user/hand/left", &m_InputSourceLeft) != vr::VRInputError_None)
		m_InputSourceLeft = vr::k_ulInvalidInputValueHandle;
	if (m_Input->GetInputSourceHandle("/user/hand/right", &m_InputSourceRight) != vr::VRInputError_None)
		m_InputSourceRight = vr::k_ulInvalidInputValueHandle;

    m_Input->GetActionSetHandle("/actions/main", &m_ActionSet);
    m_Input->GetActionSetHandle("/actions/base", &m_BaseActionSet);
    m_Input->GetActionSetHandle("/actions/left_handed", &m_LeftActionSet);
    m_LeftActions.clear();
    for(const char* name : {"ActivateVR","Jump","PrimaryAttack","Reload","Use","Walk","Turn",
        "SecondaryAttack","NextItem","PrevItem","ResetPosition","Crouch","Flashlight",
        "MenuSelect","MenuBack","MenuUp","MenuDown","MenuLeft","MenuRight","Spray",
        "Scoreboard","ShowHUD","Pause"}) {
        vr::VRActionHandle_t normal=0,left=0;
        const std::string suffix=std::string("/in/")+name;
        if(m_Input->GetActionHandle(("/actions/main"+suffix).c_str(),&normal)==vr::VRInputError_None
            && m_Input->GetActionHandle(("/actions/left_handed"+suffix).c_str(),&left)==vr::VRInputError_None)
            m_LeftActions[normal]=left;
    }
    m_ActiveActionSet = {};
    m_ActiveActionSet.ulActionSet = m_ActionSet;

    PortalVrLog("Action handles set=%llu base=%llu left=%llu jump=%llu crouch=%llu use=%llu primary=%llu secondary=%llu walk=%llu turn=%llu poseL=%llu poseR=%llu srcL=%llu srcR=%llu",
        (unsigned long long)m_ActionSet, (unsigned long long)m_BaseActionSet,
        (unsigned long long)m_LeftActionSet, (unsigned long long)m_ActionJump,
        (unsigned long long)m_ActionCrouch, (unsigned long long)m_ActionUse,
        (unsigned long long)m_ActionPrimaryAttack, (unsigned long long)m_ActionSecondaryAttack,
        (unsigned long long)m_ActionWalk, (unsigned long long)m_ActionTurn,
        (unsigned long long)m_ActionPoseLeft, (unsigned long long)m_ActionPoseRight,
        (unsigned long long)m_InputSourceLeft, (unsigned long long)m_InputSourceRight);

    return 0;
}

void VR::InstallApplicationManifest(const char *fileName)
{
    char currentDir[MAX_STR_LEN];
    if (!GetRuntimeBaseDirectory(currentDir, ARRAYSIZE(currentDir)))
    {
        PortalVrLog("InstallApplicationManifest failed: runtime base directory unavailable");
        return;
    }

    char path[MAX_STR_LEN];
    sprintf_s(path, MAX_STR_LEN, "%s\\VR\\%s", currentDir, fileName);
    PortalVrLog("InstallApplicationManifest path=%s", path);

    vr::VRApplications()->AddApplicationManifest(path);
}

void VR::SetScreenSizeOverride(bool bState) {
    bool isOverriding = m_Game->IsScreenSizeOverrideActive();

    if (bState && !isOverriding || !bState && isOverriding) {
        int iOldWidth, iOldHeight;
        if (!m_Game->GetScreenSize(iOldWidth, iOldHeight))
            return;

        m_Game->ForceScreenSizeOverride(bState, m_RenderWidth, m_RenderHeight);
       /*int x = 0, y = 0, w = m_RenderWidth, h = m_RenderHeight;

        if (m_Game->m_ClientMode->GetViewport())
            m_Game->m_ClientMode->AdjustEngineViewport(x, y, w, h);*/

        if (bState) {
            /*IMatRenderContext* renderContext = m_Game->m_MaterialSystem->GetRenderContext();
            renderContext->Viewport(0, 0, m_RenderWidth, m_RenderHeight);
            renderContext->Release();*/
        }
        m_Game->OnScreenSizeChanged(iOldWidth, iOldHeight);
    }
}

void VR::Update()
{
    if (!m_IsInitialized || !m_Game->m_Initialized)
        return;

    const bool hasGameplayInterfaces = m_Game->GetLocalPortalPlayer() != nullptr
        && m_Game->GetEngineTrace() != nullptr;
    const bool cursorVisible = m_Game->IsCursorVisible();
    const bool inGame = m_Game->IsInGame();
    static bool loggedUpdateEntry = false;
    static bool loggedAfterSubmit = false;
    static bool loggedAfterPoses = false;
    static bool loggedAfterTracking = false;
    static bool loggedSkippedMenuInput = false;
    static bool loggedAfterInput = false;
    static bool loggedSkippedGameplayInput = false;
    static bool wasInGame = false;

    if (!loggedUpdateEntry)
    {
        PortalVrLog("VR::Update first entry cursorVisible=%d inGame=%d gameplayInterfaces=%d", cursorVisible, inGame, hasGameplayInterfaces);
        loggedUpdateEntry = true;
    }

    if (m_IsVREnabled && g_D3DVR9)
    {
        //SetScreenSizeOverride(inGame);

        // Named Source targets survive map changes. Reopening allocation here
        // interrupts the material/lightmap restore during a save load. DXVK
        // refreshes the underlying eye surfaces when Source next binds them.
        if (!inGame)
        {
            m_CameraCollisionOffset = { 0, 0, 0 };
            m_CameraBlocked = false;
            m_Game->m_CachedArmsModel = false;
            if (wasInGame)
                PortalVrLog("Leaving gameplay: retaining named VR render targets");
            m_RenderedNewFrame = false;
            m_GrabPoseValid = false;
        } 

        if (kEnableMaterialSystemEyeTargets && !m_CreatedVRTextures)
        {
            PortalVrLog("VR::Update creating VR textures outside RenderView");
            CreateVRTextures();
        }
    }

    wasInGame = inGame;

    // Held-object haptics: the grab controller stops reporting once the prop
    // is released. The server does not tick behind a menu, so hold the timer.
    if (m_Carrying)
    {
        const auto now = GetTickCount64();
        if (cursorVisible || !inGame)
            m_LastCarryUpdate = now;
        else if (now - m_LastCarryUpdate > 400)
        {
            m_Carrying = false;
            TriggerHaptic(Hand::Gun, 0.03f, 50.0f, 0.35f);
        }
    }

    SubmitVRTextures();
    if (!loggedAfterSubmit)
    {
        PortalVrLog("VR::Update completed SubmitVRTextures");
        loggedAfterSubmit = true;
    }

    UpdatePosesAndActions();
    if (!loggedAfterPoses)
    {
        PortalVrLog("VR::Update completed UpdatePosesAndActions");
        loggedAfterPoses = true;
    }

    UpdateTracking();
    if (!loggedAfterTracking)
    {
        PortalVrLog("VR::Update completed UpdateTracking");
        loggedAfterTracking = true;
    }

    if (cursorVisible) {
        if (!kEnableVrMenuInput)
        {
            if (!loggedSkippedMenuInput)
            {
                PortalVrLog("VR::Update skipped VR menu input");
                loggedSkippedMenuInput = true;
            }
            return;
        }

        ProcessMenuInput();
    } else if (hasGameplayInterfaces) {
        ProcessInput();
        if (!loggedAfterInput)
        {
            PortalVrLog("VR::Update completed ProcessInput");
            loggedAfterInput = true;
        }
    } else if (!loggedSkippedGameplayInput) {
        PortalVrLog("VR::Update skipped gameplay input because gameplay interfaces are unavailable");
        loggedSkippedGameplayInput = true;
    }
}

void VR::CreateVRTextures()
{
    if (m_CreatedVRTextures)
        return;

    IMaterialSystem *materialSystem = m_Game->GetMaterialSystem();
    if (!materialSystem)
    {
        PortalVrLog("CreateVRTextures skipped: material system unavailable");
        return;
    }

    PortalVrLog("CreateVRTextures start width=%u height=%u", m_RenderWidth, m_RenderHeight);

    materialSystem->BeginRenderTargetAllocation();

    m_CreatingTextureID = Texture_LeftEye;
    m_LeftEyeTexture = materialSystem->CreateNamedRenderTargetTextureEx("leftEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_LITERAL, materialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);
    
    m_CreatingTextureID = Texture_RightEye;
    m_RightEyeTexture = materialSystem->CreateNamedRenderTargetTextureEx("rightEye0", m_RenderWidth, m_RenderHeight, RT_SIZE_LITERAL, materialSystem->GetBackBufferFormat(), MATERIAL_RT_DEPTH_SEPARATE, TEXTUREFLAGS_NOMIP);
    m_CreatingTextureID = Texture_None;

    materialSystem->EndRenderTargetAllocation();

    m_CreatedVRTextures = m_LeftEyeTexture && m_RightEyeTexture
        && m_D9LeftEyeSurface && m_D9RightEyeSurface
        && m_VKLeftEye.m_VulkanData.m_nImage && m_VKRightEye.m_VulkanData.m_nImage;
    PortalVrLog(
        "CreateVRTextures complete this=%p left=%p right=%p created=%d",
        this,
        m_LeftEyeTexture,
        m_RightEyeTexture,
        m_CreatedVRTextures);
}

void VR::SubmitVRTextures()
{
    if (!g_D3DVR9 || FAILED(g_D3DVR9->GetBackBufferData(&m_VKBackBuffer)))
        return;
    auto *compositor = vr::VRCompositor();
    if (!compositor)
        return;

    const bool eyeFrame = m_RenderedNewFrame && m_CreatedVRTextures;
    // One skipped world frame is not worth reacting to: the compositor
    // reprojects the last one, and the flat screen must not flash up.
    if (eyeFrame)
        m_FramesWithoutScene = 0;
    else if (m_FramesWithoutScene < 3)
        ++m_FramesWithoutScene;
    const bool sceneMissing = m_FramesWithoutScene >= 3;
    if (m_Overlay)
    {
        // The stock menu, and any frame without a rendered world (loading
        // screens, startup), is shown as the desktop frame on a flat screen.
        if ((sceneMissing || m_Game->IsCursorVisible()) && kEnableVrMenuSubmission)
        {
            if (!m_Overlay->IsOverlayVisible(m_MainMenuHandle))
                RepositionOverlays();
            vr::VRTextureBounds_t bounds{0, 0, 1, 1};
            const vr::HmdVector2_t mouseScale = {
                static_cast<float>(m_VKBackBuffer.m_VulkanData.m_nWidth),
                static_cast<float>(m_VKBackBuffer.m_VulkanData.m_nHeight) };
            m_Overlay->SetOverlayMouseScale(m_MainMenuHandle, &mouseScale);
            m_Overlay->SetOverlayTexelAspect(m_MainMenuHandle, 1.0f);
            m_Overlay->SetOverlayTextureBounds(m_MainMenuHandle, &bounds);
            auto error = m_Overlay->SetOverlayTexture(m_MainMenuHandle, &m_VKBackBuffer.m_VRTexture);
            static int lastOverlayError = -1;
            if (lastOverlayError != error)
            {
                PortalVrLog("Menu texture submission result=%d", error);
                lastOverlayError = error;
            }
            m_Overlay->ShowOverlay(m_MainMenuHandle);
        }
        else
            m_Overlay->HideOverlay(m_MainMenuHandle);
    }

    // A frame without a rendered world (loading screen, startup, a menu with
    // no map behind it) is already on the flat screen. Submitting the flat
    // backbuffer to the eyes as well stretched it across the whole view,
    // locked to the face. Fade the scene out to a black compositor background
    // instead, so only the screen is visible until the world is back.
    constexpr float sceneFadeSeconds = 0.25f;
    const auto now = GetTickCount64();
    vr::EVRCompositorError left = vr::VRCompositorError_None;
    vr::EVRCompositorError right = vr::VRCompositorError_None;
    if (eyeFrame)
    {
        if (m_SceneHidden)
        {
            compositor->FadeGrid(sceneFadeSeconds, false);
            m_SceneHidden = false;
            // The background stays black until the fade back has finished.
            m_SceneBackgroundRestoreAt = now + 1000;
        }
        else if (m_SceneBackgroundRestoreAt && now >= m_SceneBackgroundRestoreAt)
        {
            compositor->FadeToColor(0.0f, 0.0f, 0.0f, 0.0f, 0.0f, true);
            m_SceneBackgroundRestoreAt = 0;
        }
        left = compositor->Submit(vr::Eye_Left, &m_VKLeftEye.m_VRTexture, &m_TextureBounds[0]);
        right = compositor->Submit(vr::Eye_Right, &m_VKRightEye.m_VRTexture, &m_TextureBounds[1]);
    }
    else if (!m_SceneHidden && sceneMissing)
    {
        compositor->FadeToColor(0.0f, 0.0f, 0.0f, 0.0f, 1.0f, true);
        compositor->FadeGrid(sceneFadeSeconds, true);
        m_SceneHidden = true;
        m_SceneBackgroundRestoreAt = 0;
    }
    UpdateAimMarker(eyeFrame);

    static int lastLeft = -1, lastRight = -1;
    static unsigned frames = 0;
    static bool lastEyeFrame = false;
    // State changes are always logged; the periodic snapshot is opt-in.
    const bool periodic = (++frames % 600) == 0 && PortalVrDebugLogging();
    if (periodic || lastEyeFrame != eyeFrame || lastLeft != left || lastRight != right)
    {
        PortalVrLog("VR frame=%u eyes=%d submitLeft=%d submitRight=%d inGame=%d hmdTracked=%d cursor=%d", frames, eyeFrame, left, right,
            m_Game->IsInGame(), m_Poses[vr::k_unTrackedDeviceIndex_Hmd].bPoseIsValid, m_Game->IsCursorVisible());
        lastLeft = left;
        lastRight = right;
        lastEyeFrame = eyeFrame;
        vr::InputAnalogActionData_t walk{};
        auto inputError = m_Input->GetAnalogActionData(m_ActionWalk, &walk, sizeof(walk), vr::k_ulInvalidInputValueHandle);
        PortalVrLog("Tracking yaw=%f walkError=%d active=%d axes=%f,%f", m_HmdAngAbs.y, inputError, walk.bActive, walk.x, walk.y);
        vr::InputAnalogActionData_t turn{};
        const bool turnOk = GetAnalogActionData(m_ActionTurn, turn);
        PortalVrLog("Turn action sampled=%d active=%d axes=%f,%f rotOffset=%f",
            turnOk, turn.bActive, turn.x, turn.y, m_RotationOffset.y);
        // One periodic snapshot of the gameplay-input gate and hand-tracking
        // prerequisites: if the local player or engine trace never resolves,
        // ProcessInput stays off (no stick turning) and UpdateTracking bails
        // before computing hand poses (frozen gun/arms) even while headlook,
        // firing, and locomotion keep working through other hooks.
        auto *diagPlayer = m_Game->GetLocalPortalPlayer();
        auto *diagTrace = m_Game->GetEngineTrace();
        PortalVrLog("Gameplay state player=%p trace=%p hmdValid=%d leftValid=%d rightValid=%d handRel=%f,%f,%f",
            diagPlayer, diagTrace,
            m_HmdPose.isValid, m_LeftControllerPose.isValid, m_RightControllerPose.isValid,
            m_RightControllerPosRel.x, m_RightControllerPosRel.y, m_RightControllerPosRel.z);
        // Break down a null player into list/index/slot suspects. Worldspawn
        // (entity 0) always exists in-game: if it resolves but entity 1 does
        // not, the player is genuinely absent; if neither resolves, the list
        // pointer or GetClientEntity slot is wrong for this client build.
        IClientEntityList *diagList = m_Game->GetClientEntityList();
        int diagIndex = -999;
        if (IEngineClient *diagEng = m_Game->GetEngineClient())
            diagIndex = diagEng->GetLocalPlayer();
        void *diagWorld = diagList ? diagList->GetClientEntity(0) : nullptr;
        void *diagOne = diagList ? diagList->GetClientEntity(1) : nullptr;
        PortalVrLog("Entity chain list=%p localIndex=%d world=%p ent1=%p",
            diagList, diagIndex, diagWorld, diagOne);
        // Read-only vtable survey: log where slots 0-9 point. A genuine
        // client.dll method lands inside client.dll's image; anything else
        // is past the table or garbage. No slot is CALLED here, so this
        // cannot crash or corrupt state like call-probing can.
        {
            static bool surveyed = false;
            if (!surveyed && diagList)
            {
                surveyed = true;
                HMODULE clientModule = GetModuleHandleA("client.dll");
                uintptr_t *vtable = *reinterpret_cast<uintptr_t **>(diagList);
                if (clientModule && SigScanner::IsReadable(reinterpret_cast<uintptr_t>(vtable), 10 * sizeof(uintptr_t)))
                {
                    const uintptr_t base = reinterpret_cast<uintptr_t>(clientModule);
                    for (int slot = 0; slot < 10; ++slot)
                    {
                        const uintptr_t fn = vtable[slot];
                        PortalVrLog("Entity vtable slot=%d fn=%p inClient=%d",
                            slot, reinterpret_cast<void *>(fn),
                            fn >= base && fn < base + 0x600000);
                    }
                }
                else
                {
                    PortalVrLog("Entity vtable survey skipped list=%p module=%p",
                        diagList, clientModule);
                }
            }
        }
        // 6DOF/crouch diagnosis: roomscale arriving at the HMD, what head
        // collision does with it, and where the engine eye sits. A healthy
        // roomscale lean shows hmdRelLen > 0 with blocked=0; a view pinned
        // while hmdRelLen moves means collision (or a bad origin) eats it.
        // During an IRL crouch, setupZ diving further than the physical HMD
        // drop proves the engine duck dip stacks on top (double-dip).
        PortalVrLog("Space origin setup=%f,%f,%f hmdRelLen=%f blocked=%d collideLen=%f hmdZ=%f standingZ=%f physCrouch=%d comp=%f btnDuck=%d standEye=%f",
            m_SetupOrigin.x, m_SetupOrigin.y, m_SetupOrigin.z,
            sqrtf(m_HmdPosRelative.LengthSqr()), m_CameraBlocked,
            sqrtf(m_CameraCollisionOffset.LengthSqr()),
            m_HmdPose.isValid ? m_HmdPose.TrackedDevicePos.z : -1.0f,
            m_StandingHeightValid ? m_StandingHeight : -1.0f,
            m_PhysicalCrouchHeld ? 1 : 0,
            m_DuckCompensation, IsButtonCrouchHeld() ? 1 : 0,
            m_EngineStandEyeValid ? m_EngineStandEyeZ : -1.0f);
    }
    m_RenderedNewFrame = false;
}

void VR::GetPoseData(vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut)
{
    poseOut.isValid = poseRaw.bPoseIsValid && poseRaw.bDeviceIsConnected;
    for(int row=0;row<3 && poseOut.isValid;++row) {
        for(int col=0;col<4;++col)
            poseOut.isValid=poseOut.isValid && std::isfinite(poseRaw.mDeviceToAbsoluteTracking.m[row][col]);
        poseOut.isValid=poseOut.isValid && std::isfinite(poseRaw.vVelocity.v[row])
            && std::isfinite(poseRaw.vAngularVelocity.v[row]);
    }
    if (poseOut.isValid)
    {
        vr::HmdMatrix34_t mat = poseRaw.mDeviceToAbsoluteTracking;
        Vector pos;
        Vector vel;
        QAngle ang;
        QAngle angvel;
        pos.x = -mat.m[2][3];
        pos.y = -mat.m[0][3];
        pos.z = mat.m[1][3];
        ang.x = asinf(std::clamp(mat.m[1][2],-1.f,1.f)) * (180.0 / 3.141592654);
        ang.y = atan2f(mat.m[0][2], mat.m[2][2]) * (180.0 / 3.141592654);
        ang.z = atan2f(-mat.m[1][0], mat.m[1][1]) * (180.0 / 3.141592654);
        vel.x = -poseRaw.vVelocity.v[2];
        vel.y = -poseRaw.vVelocity.v[0];
        vel.z = poseRaw.vVelocity.v[1];
        angvel.x = -poseRaw.vAngularVelocity.v[2] * (180.0 / 3.141592654);
        angvel.y = -poseRaw.vAngularVelocity.v[0] * (180.0 / 3.141592654);
        angvel.z = poseRaw.vAngularVelocity.v[1] * (180.0 / 3.141592654);

        poseOut.TrackedDevicePos = pos;
        poseOut.TrackedDeviceVel = vel;
        poseOut.TrackedDeviceAng = ang;
        poseOut.TrackedDeviceAngVel = angvel;
    }
}

void VR::RepositionOverlays()
{
    if (!m_Overlay || m_MainMenuHandle == vr::k_ulOverlayHandleInvalid)
        return;
    if (!vr::VRCompositor())
        return;

    const vr::TrackedDevicePose_t& hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!hmdPose.bPoseIsValid)
        return;
    const vr::HmdMatrix34_t& hmdMat = hmdPose.mDeviceToAbsoluteTracking;

    // The stock menu is a flat screen in front of the user, as it always
    // was, but near enough to read: the menu's text is only about one
    // percent of the screen's height at desktop resolutions. It is placed
    // from where the head is when it appears and then stays put (it does not
    // follow the face); recentering or pausing again places it afresh.
    Vector anchor(hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3]);

    Vector hmdForward = { -hmdMat.m[0][2], 0, -hmdMat.m[2][2] };
    hmdForward[1] = 0;
    if (VectorNormalize(hmdForward) < 1e-3f)
        return; // Gaze is vertical; keep the previous placement.

    const float distance = std::isfinite(m_MenuScreenDistance)
        ? std::clamp(m_MenuScreenDistance, 0.5f, 3.0f) : 1.0f;
    const float width = std::isfinite(m_MenuScreenWidth)
        ? std::clamp(m_MenuScreenWidth, 0.5f, 4.0f) : 1.6f;

    Vector screenPos = anchor + hmdForward * distance;
    // Slightly below eye level, where Portal keeps its menu entries.
    screenPos.y = anchor.y - 0.05f * distance;

    const float hmdRotation = atan2f(hmdMat.m[0][2], hmdMat.m[2][2]);
    const float cosYaw = cosf(hmdRotation);
    const float sinYaw = sinf(hmdRotation);
    vr::HmdMatrix34_t menuTransform =
    {
        cosYaw, 0.0f, sinYaw, screenPos.x,
        0.0f, 1.0f, 0.0f, screenPos.y,
        -sinYaw, 0.0f, cosYaw, screenPos.z
    };

    vr::ETrackingUniverseOrigin trackingOrigin = vr::VRCompositor()->GetTrackingSpace();
    vr::VROverlay()->SetOverlayTransformAbsolute(m_MainMenuHandle, trackingOrigin, &menuTransform);
    vr::VROverlay()->SetOverlayWidthInMeters(m_MainMenuHandle, width);
    // A wide screen this close reads better with its edges turned in a little.
    vr::VROverlay()->SetOverlayCurvature(m_MainMenuHandle, 0.15f);

    // Reposition HUD overlay
    /*vr::HmdMatrix34_t hudTransform =
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    Vector hudDistance = hmdForward * m_HudDistance;
    Vector hudNewPos = hudDistance + hmdPosition;

    hudTransform.m[0][3] = hudNewPos.x;
    hudTransform.m[1][3] = hudNewPos.y - 0.25;
    hudTransform.m[2][3] = hudNewPos.z;

    hudTransform.m[0][0] *= cos(hmdRotationDegrees);
    hudTransform.m[0][2] = sin(hmdRotationDegrees);
    hudTransform.m[2][0] = -sin(hmdRotationDegrees);
    hudTransform.m[2][2] *= cos(hmdRotationDegrees);

    vr::VROverlay()->SetOverlayTransformAbsolute(m_HUDHandle, trackingOrigin, &hudTransform);
    vr::VROverlay()->SetOverlayWidthInMeters(m_HUDHandle, m_HudSize);*/
}

void VR::CreateAimMarker()
{
    if (m_Overlay->CreateOverlay("Portal1VRAimMarkerKey", "Portal1VRAimMarker", &m_AimMarkerHandle) != vr::VROverlayError_None)
    {
        m_AimMarkerHandle = vr::k_ulOverlayHandleInvalid;
        PortalVrLog("Aim marker overlay unavailable");
        return;
    }
    static std::uint8_t pixels[AimMarker::TextureSize * AimMarker::TextureSize * 4];
    AimMarker::Paint(pixels);
    const auto error = m_Overlay->SetOverlayRaw(m_AimMarkerHandle, pixels,
        AimMarker::TextureSize, AimMarker::TextureSize, 4);
    PortalVrLog("Aim marker overlay created texture=%d", error);
}

void VR::UpdateAimMarker(bool eyeFrame)
{
    if (!m_Overlay || m_AimMarkerHandle == vr::k_ulOverlayHandleInvalid)
        return;

    // The marker follows the ray that both portal shots and pickups use.
    // Before the gun is found it is the only sign of where the hand points.
    bool show = m_AimMode == 2 && eyeFrame && !m_EyeViewThroughPortal
        && m_HmdPose.isValid && m_RightControllerPose.isValid
        && m_Game->IsInGame() && !m_Game->IsCursorVisible();
    vr::HmdMatrix34_t transform{};
    float width = 0.0f;
    Vector aimOrigin, aimDirection;
    if (show && GetPortalAimRay(aimOrigin, aimDirection))
    {
        // m_AimPos is this frame's trace along the same ray. A trace that ran
        // its full length hit nothing, so there is no surface to mark.
        const float range = sqrtf((m_AimPos - aimOrigin).LengthSqr());
        show = std::isfinite(range) && range > 1.0f && range < MAX_TRACE_LENGTH * 0.99f;
    }
    else
        show = false;
    if (show)
    {
        // Where the rendered eyes are, in both spaces: the head pose moved
        // along its own Z by the eye offset, and the matching view origin.
        const vr::HmdMatrix34_t& hmd = m_Poses[vr::k_unTrackedDeviceIndex_Hmd].mDeviceToAbsoluteTracking;
        const Vector eyeTracking(hmd.m[0][3] + hmd.m[0][2] * m_EyeZ,
            hmd.m[1][3] + hmd.m[1][2] * m_EyeZ, hmd.m[2][3] + hmd.m[2][2] * m_EyeZ);
        const Vector eyeWorld = GetViewOrigin(m_SetupOrigin);
        Vector marker;
        show = AimMarker::WorldToTracking(m_AimPos, eyeWorld, eyeTracking,
            m_RotationOffset.y, m_VRScale, marker);
        const Vector toMarker = marker - eyeTracking;
        const float distance = show ? sqrtf(toMarker.LengthSqr()) : 0.0f;
        show = show && distance > 0.05f;
        if (show)
        {
            // Face the head, and keep a constant apparent size at any range.
            const Vector placed = eyeTracking + toMarker * AimMarker::DepthBias;
            transform = hmd;
            transform.m[0][3] = placed.x;
            transform.m[1][3] = placed.y;
            transform.m[2][3] = placed.z;
            width = std::max(AimMarker::MinimumWidth, distance * AimMarker::WidthPerMeter);
        }
    }

    if (show)
    {
        m_Overlay->SetOverlayTransformAbsolute(m_AimMarkerHandle,
            vr::VRCompositor()->GetTrackingSpace(), &transform);
        m_Overlay->SetOverlayWidthInMeters(m_AimMarkerHandle, width);
    }
    if (show != m_AimMarkerVisible)
    {
        if (show)
            m_Overlay->ShowOverlay(m_AimMarkerHandle);
        else
            m_Overlay->HideOverlay(m_AimMarkerHandle);
        m_AimMarkerVisible = show;
    }
}

void VR::TriggerHaptic(vr::VRActionHandle_t action, float seconds, float frequency, float amplitude)
{
    if (action && m_Input && m_IsVREnabled)
        m_Input->TriggerHapticVibrationAction(action, 0.0f, seconds, frequency, amplitude,
            vr::k_ulInvalidInputValueHandle);
}

void VR::TriggerHaptic(Hand hand, float seconds, float frequency, float amplitude)
{
    const bool physicalLeft = (hand == Hand::Gun) == m_LeftHanded;
    TriggerHaptic(physicalLeft ? m_ActionHapticLeft : m_ActionHapticRight, seconds, frequency, amplitude);
}

void VR::NoteCarryUpdate()
{
    if (!m_Carrying)
    {
        m_Carrying = true;
        TriggerHaptic(Hand::Gun, 0.06f, 70.0f, 0.6f);
    }
    m_LastCarryUpdate = GetTickCount64();
}

static double RoomscaleSeconds()
{
    // Not GetTickCount64: its resolution can be coarser than a frame, and a
    // frame that appears to take no time cannot be measured.
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Vector VR::RoomscaleOffset() const
{
    // Horizontal head position relative to the body, in world units.
    Vector offset = m_HmdPose.TrackedDevicePos - m_Center;
    offset.z = 0;
    VectorPivotXY(offset, { 0, 0, 0 }, m_RotationOffset.y);
    return offset * m_VRScale;
}

bool VR::RoomscaleMove(bool stickWalking, float& forwardMove, float& sideMove)
{
    forwardMove = sideMove = 0.0f;
    Vector wish;
    const double now = RoomscaleSeconds();
    const Vector offset = RoomscaleOffset();
    // The last rendered eyes were through a portal the body has not crossed.
    m_RoomscaleFollow.urgent = m_EyeViewThroughPortal;
    const bool wasFollowing = m_RoomscaleFollow.following;
    const bool follow = m_RoomscaleFollow.Command(now, offset, m_SetupOrigin,
            stickWalking, IsCrouchHeld(), wish);
    // A short record of what the follow did and why it did not, so a play
    // session's log can show whether it ran. Changes only, and capped.
    {
        const float distance = Roomscale::Length2D(offset);
        const char* state = follow ? "following"
            : distance < Roomscale::StartDistance ? "idle"
            : !m_RoomscaleFollow.originValid ? "inactive"
            : now < m_RoomscaleFollow.stickUntil ? "waiting for stick"
            : m_RoomscaleFollow.blocked ? "blocked"
            : fabsf(m_RoomscaleFollow.verticalSpeed) > Roomscale::MaxVerticalSpeed ? "airborne" : "idle";
        static const char* lastState = nullptr;
        static unsigned reports = 0;
        if (state != lastState && reports < 200)
        {
            ++reports;
            PortalVrLog("Roomscale follow %s distance=%f urgent=%d speed=%f offset=%f,%f body=%f,%f,%f",
                state, distance, m_RoomscaleFollow.urgent, wasFollowing || follow ? m_RoomscaleFollow.commandSpeed : 0.0f,
                offset.x, offset.y, m_SetupOrigin.x, m_SetupOrigin.y, m_SetupOrigin.z);
            lastState = state;
        }
    }
    if (!follow)
        return false;
    // The engine builds its move axes from the command's view angles.
    Vector forward, right;
    QAngle::AngleVectors(EngineViewAngles(), &forward, &right, nullptr);
    return Roomscale::WishToMoves(wish, forward, right, forwardMove, sideMove);
}

void VR::UpdateRoomscaleFollow()
{
    // Only while playing: an open menu substitutes its own viewpoint for the
    // player's eye, and without 6DOF the camera does not leave the body.
    const bool usable = m_Roomscale && m_6DOF && m_IsVREnabled && m_HmdPose.isValid
        && !m_CenterPending && !m_MenuAnchorValid
        && m_Game->IsInGame() && !m_Game->IsCursorVisible();
    const bool wasBlocked = m_RoomscaleFollow.blocked;
    Vector covered = m_RoomscaleFollow.Credit(RoomscaleSeconds(), usable, m_SetupOrigin, RoomscaleOffset());
    if (m_RoomscaleFollow.blocked && !wasBlocked)
        PortalVrLog("Roomscale follow blocked: the body cannot reach the head");
    if (covered.LengthSqr() <= 0.0f)
        return;

    // Move the recenter point toward the headset by the distance the body
    // covered: the head offset shrinks by exactly what the body gained, so
    // the camera stays where the head is.
    covered *= 1.0f / m_VRScale;
    VectorPivotXY(covered, { 0, 0, 0 }, -m_RotationOffset.y);
    m_Center += covered;
}

bool VR::GetPoseActionPose(vr::VRActionHandle_t action, vr::TrackedDevicePose_t &poseOut)
{
    if (!action || !m_Input)
        return false;
    vr::InputPoseActionData_t data{};
    vr::ETrackingUniverseOrigin origin = vr::TrackingUniverseStanding;
    if (vr::VRCompositor())
        origin = vr::VRCompositor()->GetTrackingSpace();
    if (m_Input->GetPoseActionDataForNextFrame(action, origin, &data,
            sizeof(data), vr::k_ulInvalidInputValueHandle) != vr::VRInputError_None)
        return false;
    if (!data.bActive || !data.pose.bPoseIsValid || !data.pose.bDeviceIsConnected)
        return false;
    poseOut = data.pose;
    return true;
}

void VR::GetPoses()
{
    vr::TrackedDevicePose_t hmdPose = m_Poses[vr::k_unTrackedDeviceIndex_Hmd];

    // Prefer standardized SteamVR Input pose actions over legacy controller
    // roles. Roles can return invalid indices on Quest 3 while the bound pose
    // actions still track; keep roles only as a fallback for custom bindings
    // without poses or drivers that predate the Input system.
    m_PhysicalHandPoseValid[0] = GetPoseActionPose(m_ActionPoseLeft, m_PhysicalHandPose[0]);
    m_PhysicalHandPoseValid[1] = GetPoseActionPose(m_ActionPoseRight, m_PhysicalHandPose[1]);
    const int logicalLeft = m_LeftHanded ? 1 : 0;
    vr::TrackedDevicePose_t leftControllerPose{};
    vr::TrackedDevicePose_t rightControllerPose{};
    const bool leftViaAction = m_PhysicalHandPoseValid[logicalLeft];
    const bool rightViaAction = m_PhysicalHandPoseValid[1 - logicalLeft];
    if (leftViaAction) leftControllerPose = m_PhysicalHandPose[logicalLeft];
    if (rightViaAction) rightControllerPose = m_PhysicalHandPose[1 - logicalLeft];
    if (!leftViaAction || !rightViaAction)
    {
        vr::TrackedDeviceIndex_t leftControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
        vr::TrackedDeviceIndex_t rightControllerIndex = m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_RightHand);

        if (m_LeftHanded)
            std::swap(leftControllerIndex, rightControllerIndex);

        if (!leftViaAction && leftControllerIndex < vr::k_unMaxTrackedDeviceCount)
            leftControllerPose = m_Poses[leftControllerIndex];
        if (!rightViaAction && rightControllerIndex < vr::k_unMaxTrackedDeviceCount)
            rightControllerPose = m_Poses[rightControllerIndex];
    }

    GetPoseData(hmdPose, m_HmdPose);
    GetPoseData(leftControllerPose, m_LeftControllerPose);
    GetPoseData(rightControllerPose, m_RightControllerPose);
}

void VR::UpdatePosesAndActions() 
{
    static unsigned int loggedPoses = 0;
    if (loggedPoses++ < 3)
        PortalVrLog("Pose update this=%p poses=%p compositor=%p input=%p", this, m_Poses, vr::VRCompositor(), m_Input);
    vr::VRCompositor()->WaitGetPoses(m_Poses, vr::k_unMaxTrackedDeviceCount, NULL, 0);
    m_ActiveActionSet.ulActionSet=m_LeftHanded && m_LeftActionSet ? m_LeftActionSet : m_ActionSet;
    vr::VRActiveActionSet_t actionSets[2] = { m_ActiveActionSet, {} };
    actionSets[1].ulActionSet = m_BaseActionSet;
    m_Input->UpdateActionState(actionSets, sizeof(vr::VRActiveActionSet_t), m_BaseActionSet ? 2 : 1);

	// Presence comes from the action state, never from curl magnitude: zero
	// is a valid fully open hand, including Pico's estimated skeleton stream.
	auto updateFingerSummary = [this](vr::VRActionHandle_t action, float *curl, bool &valid) {
		vr::InputSkeletalActionData_t state{};
		vr::VRSkeletalSummaryData_t summary{};
		valid = action && m_Input->GetSkeletalActionData(action, &state, sizeof(state)) == vr::VRInputError_None
			&& state.bActive
			&& m_Input->GetSkeletalSummaryData(action, vr::VRSummaryType_FromDevice, &summary) == vr::VRInputError_None;
		for (int i = 0; i < 5 && valid; ++i) valid = std::isfinite(summary.flFingerCurl[i]);
		static const float relaxed[5] = {0.15f, 0.20f, 0.25f, 0.25f, 0.25f};
		for (int i = 0; i < 5; ++i)
			curl[i] = valid ? std::clamp(summary.flFingerCurl[i], 0.0f, 1.0f) : relaxed[i];
	};
	updateFingerSummary(m_LeftHanded ? m_ActionSkeletonRight : m_ActionSkeletonLeft, m_LeftFingerCurl, m_LeftSkeletonValid);
	updateFingerSummary(m_LeftHanded ? m_ActionSkeletonLeft : m_ActionSkeletonRight, m_RightFingerCurl, m_RightSkeletonValid);
	// Standardized support-grip input: the bound SteamVR Input boolean drives
	// the optional two-handed grip so users can rebind it through Steam Input.
	// The legacy IVRSystem::GetControllerState fallback is intentionally gone:
	// it bypasses rebinding and under-reports squeeze-style grips on Quest 3.
	// Without a bound support action, finger-curl squeeze still engages.
	vr::InputDigitalActionData_t supportInput{};
	const auto supportAction = m_LeftHanded ? m_ActionSupportRight : m_ActionSupportLeft;
	const bool supportActionValid = supportAction && m_Input->GetDigitalActionData(supportAction, &supportInput,
		sizeof(supportInput), vr::k_ulInvalidInputValueHandle) == vr::VRInputError_None
		&& supportInput.bActive;
	m_LeftGripPressed = supportActionValid ? supportInput.bState
		: OptionalGunGrip::Squeeze(false, m_LeftSkeletonValid, m_LeftFingerCurl, m_LeftGripPressed);
	if (!m_LeftGripPressed || !m_LeftHandGunGrip) m_OptionalSupportActive = false;
}

void VR::GetViewParameters() 
{
    vr::HmdMatrix34_t eyeToHeadLeft;
    vr::HmdMatrix34_t eyeToHeadRight;
    MsvcVR_GetEyeToHeadTransform(m_System, vr::Eye_Left, &eyeToHeadLeft);
    MsvcVR_GetEyeToHeadTransform(m_System, vr::Eye_Right, &eyeToHeadRight);
    m_EyeToHeadTransformPosLeft.x = eyeToHeadLeft.m[0][3];
    m_EyeToHeadTransformPosLeft.y = eyeToHeadLeft.m[1][3];
    m_EyeToHeadTransformPosLeft.z = eyeToHeadLeft.m[2][3];

    m_EyeToHeadTransformPosRight.x = eyeToHeadRight.m[0][3];
    m_EyeToHeadTransformPosRight.y = eyeToHeadRight.m[1][3];
    m_EyeToHeadTransformPosRight.z = eyeToHeadRight.m[2][3];
}

bool VR::PressedDigitalAction(vr::VRActionHandle_t &actionHandle, bool checkIfActionChanged)
{
    if(GetTickCount64()<m_HandSwitchSuppressUntil) return false;
    vr::InputDigitalActionData_t digitalActionData{};
    vr::EVRInputError result = m_Input->GetDigitalActionData(ResolveAction(actionHandle), &digitalActionData, sizeof(digitalActionData), vr::k_ulInvalidInputValueHandle);
    
    if (result == vr::VRInputError_None && digitalActionData.bActive)
    {
        if (checkIfActionChanged)
            return digitalActionData.bState && digitalActionData.bChanged;
        else
            return digitalActionData.bState;
    }

    return false;
}

bool VR::GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut)
{
    analogDataOut={};
    if(GetTickCount64()<m_HandSwitchSuppressUntil) return false;
    vr::EVRInputError result = m_Input->GetAnalogActionData(ResolveAction(actionHandle), &analogDataOut, sizeof(analogDataOut), vr::k_ulInvalidInputValueHandle);

    if (result == vr::VRInputError_None && analogDataOut.bActive)
        return true;

    return false;
}

vr::VRActionHandle_t VR::ResolveAction(vr::VRActionHandle_t action) const {
    const auto found=m_LeftActions.find(action);
    return m_LeftHanded && m_LeftActionSet && found!=m_LeftActions.end() ? found->second : action;
}

bool VR::HandleSettingsCommand(const char* command) {
    const auto selection=VrSettings::Parse(command ? command : "");
    if(selection==VrSettings::Command::None) return false;
    if(!m_IsInitialized) return true;
    if(selection==VrSettings::Command::Recenter) {
        ResetPosition();
        PortalVrLog("VR menu: manual recenter requested");
        return true;
    }
    const bool left=selection==VrSettings::Command::Left;
    if(left && (!m_LeftActionSet || m_LeftActions.size()!=23)) {
        Game::errorMsg("Left-handed bindings are unavailable. Reinstall the complete Portal1VR update.");
        return true;
    }
    char directory[MAX_PATH]{};
    if(!GetRuntimeBaseDirectory(directory,sizeof(directory))) return true;
    const auto config=std::filesystem::path(directory)/"VR"/"config.txt";
    const auto temporary=std::filesystem::path(directory)/"VR"/"config.txt.tmp";
    try {
        std::ifstream input(config,std::ios::binary);
        if(!input) throw std::runtime_error("cannot read config.txt");
        const std::string original((std::istreambuf_iterator<char>(input)),{});
        input.close();
        std::ofstream output(temporary,std::ios::binary|std::ios::trunc);
        output<<VrSettings::SetBool(original,"LeftHanded",left);
        output.close();
        if(!output || !MoveFileExW(temporary.c_str(),config.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("cannot save config.txt");
    } catch(const std::exception& error) {
        PortalVrLog("VR menu: hand preference save failed: %s",error.what());
        Game::errorMsg("Could not save the VR hand preference. Check that bin/VR/config.txt is writable.");
        return true;
    }
    if(left!=m_LeftHanded) {
        // Release old action state before changing pose roles. A menu click must
        // not become a shot or pickup when the new action set becomes active.
        m_Game->ClientCmd_Unrestricted("-attack;-attack2;-use;-jump;-duck;-reload");
        m_UseCommandHeld=false;
        for(bool& held : m_CommandHeld) held=false;
        m_LeftHanded=left;
        m_HandSwitchSuppressUntil=GetTickCount64()+500;
        m_GrabPoseValid=m_PickupAimValid=false;
        m_PortalAimLastSeen=m_SupportLastSeen=0;
        m_OptionalGripState={};m_OptionalSupportActive=m_LeftGripPressed=false;
        m_PressedTurn=false;
        m_CalibrationDrift.Reset();m_CalibrationStability.Reset();
        m_CalibrationSuppressUntil=GetTickCount64()+2000;
    }
    PortalVrLog("VR menu: %s-handed controls selected and saved",left ? "left" : "right");
    return true;
}

void VR::ProcessMenuInput()
{
    //vr::VROverlayHandle_t currentOverlay = m_Game->m_EngineClient->IsInGame() ? m_HUDHandle : m_MainMenuHandle;
    vr::VROverlayHandle_t currentOverlay = m_MainMenuHandle;

    // Check if left or right hand controller is pointing at the overlay
    const bool isHoveringOverlay = CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_LeftHand) ||
                                   CheckOverlayIntersectionForController(currentOverlay, vr::TrackedControllerRole_RightHand);

    // Overlays can't process action inputs if the laser is active, so
    // only activate laser if a controller is pointing at the overlay
    if (isHoveringOverlay)
    {
        vr::VROverlay()->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, true);

        int windowWidth, windowHeight;
        GetOverlayWindowSize(*this, windowWidth, windowHeight);

        vr::VREvent_t vrEvent;
        while (vr::VROverlay()->PollNextOverlayEvent(currentOverlay, &vrEvent, sizeof(vrEvent)))
        {
            INPUT input{};
            switch (vrEvent.eventType)
            {
            case vr::VREvent_MouseMove:
            {
                float laserX = vrEvent.data.mouse.x;
                float laserY = vrEvent.data.mouse.y;

                laserY = windowHeight - laserY;

                m_Game->SetCursorPos(static_cast<int>(laserX), static_cast<int>(laserY));
                break;
            }

            case vr::VREvent_MouseButtonDown:
                // Don't allow holding down the mouse down in the pause menu. The resume button can be clicked before
                // the MouseButtonUp event is polled, which causes issues with the overlay.
                if (currentOverlay == m_MainMenuHandle)
                {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                    // Confirm the click in the hand that made it.
                    const bool leftHand = vrEvent.trackedDeviceIndex
                        == m_System->GetTrackedDeviceIndexForControllerRole(vr::TrackedControllerRole_LeftHand);
                    TriggerHaptic(leftHand ? m_ActionHapticLeft : m_ActionHapticRight, 0.02f, 120.0f, 0.4f);
                }
                break;

            case vr::VREvent_MouseButtonUp:
                /*if (currentOverlay == m_HUDHandle)
                {
                    input.type = INPUT_MOUSE;
                    input.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
                    SendInput(1, &input, sizeof(INPUT));
                }*/
                input.type = INPUT_MOUSE;
                input.mi.dwFlags = MOUSEEVENTF_LEFTUP;
                SendInput(1, &input, sizeof(INPUT));
                break;

            case vr::VREvent_ScrollDiscrete:
                m_Game->InternalMouseWheeled((int)vrEvent.data.scroll.ydelta);
                break;
            }
        }
    }
    else
    {
        vr::VROverlay()->SetOverlayFlag(currentOverlay, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);
        
        if (PressedDigitalAction(m_MenuSelect, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RETURN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuBack, true) || PressedDigitalAction(m_Pause, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_ESCAPE;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuUp, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_UP;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuDown, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_DOWN;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuLeft, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_LEFT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
        if (PressedDigitalAction(m_MenuRight, true))
        {
            INPUT input {};
            input.type = INPUT_KEYBOARD;
            input.ki.wVk = VK_RIGHT;
            SendInput(1, &input, sizeof(INPUT));
            input.ki.dwFlags = KEYEVENTF_KEYUP;
            SendInput(1, &input, sizeof(INPUT));
        }
    }
}

bool VR::UpdateOptionalGunSupport(matrix3x4_t *target)
{
    // Without visible hands there is nothing to seat on the gun: the grip
    // stays a plain crouch button and support never engages.
    if (!m_ShowArms)
    {
        m_OptionalSupportActive = false;
        return false;
    }
    const auto controller = HandPose::Frame(-m_RightControllerRight,
        m_RightControllerUp, m_RightControllerForward, GetRightHandAbsPos());
    const auto support = HandPose::RigidOrientation(HandPose::Concat(controller, m_SupportFromController));
    const Vector position(support[0][3],support[1][3],support[2][3]);
    // This is model-local metadata, recomposed with current controller poses.
    // Looking away must not release a grip merely because the gun was culled.
    // CalcViewModelView invalidates it when the weapon/model changes.
    const bool fresh = m_SupportLastSeen != 0;
    const bool previouslyActive = m_OptionalSupportActive;
    m_OptionalSupportActive = m_OptionalGripState.Update(m_LeftHandGunGrip && m_IsVREnabled,
        m_LeftControllerPose.isValid && m_RightControllerPose.isValid,
        fresh && OptionalGunGrip::Finite(support), m_LeftGripPressed,
        sqrtf((GetLeftHandAbsPos()-position).LengthSqr()), m_LeftHandGunGripRadius);
    if (m_OptionalSupportActive && target) *target = support;
    if (previouslyActive != m_OptionalSupportActive)
    {
        PortalVrLog("Optional support active=%d socketValid=%d pressed=%d distance=%f",
            m_OptionalSupportActive,fresh,m_LeftGripPressed,sqrtf((GetLeftHandAbsPos()-position).LengthSqr()));
        if (m_OptionalSupportActive)
            TriggerHaptic(Hand::Off, 0.04f, 90.0f, 0.5f);
    }
    return m_OptionalSupportActive;
}

void VR::ProcessInput()
{
    if (!m_IsVREnabled)
        return;

    //vr::VROverlay()->SetOverlayFlag(m_HUDHandle, vr::VROverlayFlags_MakeOverlaysInteractiveIfVisible, false);

    typedef std::chrono::duration<float, std::milli> duration;
    auto currentTime = std::chrono::steady_clock::now();
    duration elapsed = currentTime - m_PrevFrameTime;
    float deltaTime = elapsed.count();
    m_PrevFrameTime = currentTime;
    // m_PrevFrameTime starts at the clock epoch, so the first frame's delta
    // is enormous; hitches can also spike it. Clamp so neither produces a
    // giant snap/smooth turn from a single tick (smooth rate is deg/ms).
    if (!(deltaTime >= 0.0f && deltaTime <= 100.0f))
        deltaTime = 0.0f;

    vr::InputAnalogActionData_t analogActionData;

    if (GetAnalogActionData(m_ActionTurn, analogActionData))
    {
        float turnAngle = 0.0f;
        if (m_SnapTurning)
        {
            if (!m_PressedTurn && analogActionData.x > 0.5)
            {
                turnAngle = -m_SnapTurnAngle;
                m_PressedTurn = true;
            }
            else if (!m_PressedTurn && analogActionData.x < -0.5)
            {
                turnAngle = m_SnapTurnAngle;
                m_PressedTurn = true;
            }
            else if (analogActionData.x < 0.3 && analogActionData.x > -0.3)
                m_PressedTurn = false;
        }
        // Smooth turning
        else
        {
            float deadzone = 0.2;
            // smoother turning. fabsf (not abs): unqualified abs on a float
            // resolves to abs(int) under some toolchains, truncating the
            // deflection and inverting/slowing smooth turns.
            float xNormalized = (fabsf(analogActionData.x) - deadzone) / (1 - deadzone);
            if (analogActionData.x > deadzone)
            {
                turnAngle = -m_TurnSpeed * deltaTime * xNormalized;
            }
            if (analogActionData.x < -deadzone)
            {
                turnAngle = m_TurnSpeed * deltaTime * xNormalized;
            }
        }

        if (turnAngle != 0.0f)
        {
            m_RotationOffset.y += turnAngle;

            // Keep the real HMD fixed in the room while artificial turning
            // rotates the playspace. Without this counter-pivot, the tracked
            // head offset is rotated around the recenter point and the player
            // appears to walk in an arc around that point.
            Vector hmdRelative = m_HmdPose.TrackedDevicePos - m_Center;
            VectorPivotXY(hmdRelative, { 0, 0, 0 }, -turnAngle);
            m_Center = m_HmdPose.TrackedDevicePos - hmdRelative;

            // The center change is an intentional rotation, not roomscale
            // locomotion. Keep the roomscale delta baseline in the same frame.
            m_HmdPosRelativeRaw = hmdRelative;
            m_HmdPosRelativeRawPrev = hmdRelative;
        }

        // Wrap from 0 to 360
        m_RotationOffset.y -= 360 * std::floor(m_RotationOffset.y / 360);
		if (turnAngle != 0.0f)
			UpdateHMDAngles();
        static bool wasTurning = false;
        const bool turning = turnAngle != 0.0f;
        if (turning != wasTurning)
        {
            PortalVrLog("Stick turning %s x=%f rotOffset=%f",
                turning ? "started" : "released", analogActionData.x, m_RotationOffset.y);
            wasTurning = turning;
        }
    }

    // Gameplay buttons are SteamVR Input booleans so they stay rebindable
    // through Steam Input. Portal fire is PrimaryAttack (blue) plus
    // SecondaryAttack (orange); interact is Use; locomotion extras are Jump
    // and Crouch. CreateMove mirrors the same actions into the usercmd
    // buttons, so a missed console-command tick cannot drop a shot, jump,
    // duck, or pickup. The commands below keep legacy key state in sync.
    // Only a change is sent. Repeating every command every frame put several
    // hundred console commands a second through the engine's command buffer.
    const auto mirror = [this](bool held, bool& sent, const char* press, const char* release)
    {
        if (held == sent)
            return;
        m_Game->ClientCmd_Unrestricted(held ? press : release);
        sent = held;
    };
    mirror(IsPrimaryAttackHeld(), m_CommandHeld[0], "+attack", "-attack");
    mirror(IsSecondaryAttackHeld(), m_CommandHeld[1], "+attack2", "-attack2");
    mirror(IsJumpHeld(), m_CommandHeld[2], "+jump", "-jump");

    // The saved Pico/Touch mapping shares grip with crouch. Consume the
    // button only during the deliberate gun support gesture. An IRL headset
    // drop always crouches, independent of that button, so roomscale ducking
    // works even while two-handing the gun.
    UpdateOptionalGunSupport();
    mirror(IsCrouchHeld(), m_CommandHeld[3], "+duck", "-duck");

    // Keep the normal Source input state in sync, but only emit the console
    // command on an edge. CreateMove also mirrors this state into IN_USE so
    // the pickup controller sees a stable button during the same tick.
    const bool useHeld = IsUseHeld();
    // Publish the controller pose as soon as the action state is sampled. The
    // server-side grab callbacks can run before the next CreateMove callback;
    // keeping this snapshot here prevents that first pickup tick from falling
    // back to the HMD pose.
    SnapshotGrabPose();
    if (useHeld != m_UseCommandHeld)
    {
        m_Game->ClientCmd_Unrestricted(useHeld ? "+use" : "-use");
        m_UseCommandHeld = useHeld;
    }

    mirror(PressedDigitalAction(m_ActionReload), m_CommandHeld[4], "+reload", "-reload");

    if (PressedDigitalAction(m_ActionPrevItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invprev");
    }
    else if (PressedDigitalAction(m_ActionNextItem, true))
    {
        m_Game->ClientCmd_Unrestricted("invnext");
    }

    if (PressedDigitalAction(m_ActionResetPosition, true))
    {
        ResetPosition();
    }

    if (PressedDigitalAction(m_ActionFlashlight, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 100");
    }

    if (PressedDigitalAction(m_Spray, true))
    {
        m_Game->ClientCmd_Unrestricted("impulse 201");
    }
    
    /*bool isControllerVertical = m_RightControllerAngAbs.x > 60 || m_RightControllerAngAbs.x < -45;
    if ((PressedDigitalAction(m_ShowHUD) || PressedDigitalAction(m_Scoreboard) || isControllerVertical || m_HudAlwaysVisible)
        && m_RenderedHud)
    {
        if (!vr::VROverlay()->IsOverlayVisible(m_HUDHandle) || m_HudAlwaysVisible)
            RepositionOverlays();

        if (PressedDigitalAction(m_Scoreboard))
            m_Game->ClientCmd_Unrestricted("+showscores");
        else
            m_Game->ClientCmd_Unrestricted("-showscores");

        vr::VROverlay()->ShowOverlay(m_HUDHandle);
    }
    else
    {
        vr::VROverlay()->HideOverlay(m_HUDHandle);
    }*/

    m_RenderedHud = false;

    if (PressedDigitalAction(m_Pause, true))
    {
        m_Game->ClientCmd_Unrestricted("gameui_activate");
        RepositionOverlays();
    }
}

VMatrix VR::VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat)
{
    // VMatrix has a different implicit coordinate system than HmdMatrix34_t, but this function does not convert between them
    VMatrix vMat(
        hmdMat.m[0][0], hmdMat.m[1][0], hmdMat.m[2][0], 0.0f,
        hmdMat.m[0][1], hmdMat.m[1][1], hmdMat.m[2][1], 0.0f,
        hmdMat.m[0][2], hmdMat.m[1][2], hmdMat.m[2][2], 0.0f,
        hmdMat.m[0][3], hmdMat.m[1][3], hmdMat.m[2][3], 1.0f
    );

    return vMat;
}

vr::HmdMatrix34_t VR::VMatrixToHmdMatrix(const VMatrix &vMat)
{
    vr::HmdMatrix34_t hmdMat = {0};

    hmdMat.m[0][0] = vMat.m[0][0];
    hmdMat.m[1][0] = vMat.m[0][1];
    hmdMat.m[2][0] = vMat.m[0][2];

    hmdMat.m[0][1] = vMat.m[1][0];
    hmdMat.m[1][1] = vMat.m[1][1];
    hmdMat.m[2][1] = vMat.m[1][2];

    hmdMat.m[0][2] = vMat.m[2][0];
    hmdMat.m[1][2] = vMat.m[2][1];
    hmdMat.m[2][2] = vMat.m[2][2];

    hmdMat.m[0][3] = vMat.m[3][0];
    hmdMat.m[1][3] = vMat.m[3][1];
    hmdMat.m[2][3] = vMat.m[3][2];

    return hmdMat;
}

vr::HmdMatrix34_t VR::GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole)
{
    vr::VRInputValueHandle_t inputValue = vr::k_ulInvalidInputValueHandle;

    if (controllerRole == vr::TrackedControllerRole_RightHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/right", &inputValue);
    }
    else if (controllerRole == vr::TrackedControllerRole_LeftHand)
    {
        m_Input->GetInputSourceHandle("/user/hand/left", &inputValue);
    }

    if (inputValue != vr::k_ulInvalidInputValueHandle)
    {
        char buffer[vr::k_unMaxPropertyStringSize];

        m_System->GetStringTrackedDeviceProperty(vr::VRSystem()->GetTrackedDeviceIndexForControllerRole(controllerRole), vr::Prop_RenderModelName_String, 
                                                 buffer, vr::k_unMaxPropertyStringSize);

        vr::RenderModel_ControllerMode_State_t controllerState = {0};
        vr::RenderModel_ComponentState_t componentState = {0};

        if (vr::VRRenderModels()->GetComponentStateForDevicePath(buffer, vr::k_pch_Controller_Component_Tip, inputValue, &controllerState, &componentState))
        {
            return componentState.mTrackingToComponentLocal;
        }
    }

    // Not a hand controller role or tip lookup failed, return identity
    const vr::HmdMatrix34_t identity = 
    {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };

    return identity;
}

bool VR::CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole)
{
    vr::TrackedDeviceIndex_t deviceIndex = m_System->GetTrackedDeviceIndexForControllerRole(controllerRole);

    const bool viaRole = deviceIndex < vr::k_unMaxTrackedDeviceCount && m_Poses[deviceIndex].bPoseIsValid;
    // Controller roles can be missing (Quest 3) while the bound hand poses
    // still track. Without this fallback the laser never switched on.
    const int hand = controllerRole == vr::TrackedControllerRole_LeftHand ? 0 : 1;
    if (!viaRole && !m_PhysicalHandPoseValid[hand])
        return false;

    VMatrix controllerVMatrix = VMatrixFromHmdMatrix(viaRole
        ? m_Poses[deviceIndex].mDeviceToAbsoluteTracking
        : m_PhysicalHandPose[hand].mDeviceToAbsoluteTracking);
    if (viaRole)
    {
        VMatrix tipVMatrix = VMatrixFromHmdMatrix(GetControllerTipMatrix(controllerRole));
        tipVMatrix.MatrixMul(controllerVMatrix, controllerVMatrix);
    }

    vr::VROverlayIntersectionParams_t  params  = {0};
    vr::VROverlayIntersectionResults_t results = {0};

    params.eOrigin    = vr::VRCompositor()->GetTrackingSpace();
    params.vSource    = { controllerVMatrix.m[3][0],  controllerVMatrix.m[3][1],  controllerVMatrix.m[3][2]};
    params.vDirection = {-controllerVMatrix.m[2][0], -controllerVMatrix.m[2][1], -controllerVMatrix.m[2][2]};

    return m_Overlay->ComputeOverlayIntersection(overlayHandle, &params, &results);
}

QAngle VR::GetRightControllerAbsAngle()
{
    return m_RightControllerAngAbs;
}

QAngle& VR::GetRightControllerAbsAngleConst()
{
    return m_RightControllerAngAbs;
}

Vector VR::GetRightControllerAbsPos(Vector eyePosition)
{
    Vector offset = eyePosition;

    if (offset.x == 0 && offset.y == 0 && offset.z == 0) {
        /*int playerIndex = m_Game->m_EngineClient->GetLocalPlayer();
        C_BasePlayer* localPlayer = (C_BasePlayer*)m_Game->GetClientEntity(playerIndex);
        if (!localPlayer)
            return {0, 0, 0};

        offset = localPlayer->EyePosition();*/

        offset = m_SetupOrigin;
    }

    Vector position = offset + m_RightControllerPosRel;

    if (m_6DOF)
        position += m_HmdPosRelative;

    position.z += m_DuckCompensation;
    return position + m_CameraCollisionOffset;
}

Vector VR::GetRecommendedViewmodelAbsPos(Vector eyePosition)
{
    Vector viewmodelPos = GetRightControllerAbsPos(eyePosition);
    viewmodelPos -= m_ViewmodelForward * m_ViewmodelPosOffset.x;
    viewmodelPos -= m_ViewmodelRight * m_ViewmodelPosOffset.y;
    viewmodelPos -= m_ViewmodelUp * m_ViewmodelPosOffset.z;

    return viewmodelPos;
}

QAngle VR::GetRecommendedViewmodelAbsAngle()
{
    QAngle result{};

    QAngle::VectorAngles(m_ViewmodelForward, m_ViewmodelUp, result);

    return result;
}

void VR::UpdateHMDAngles() {
    QAngle hmdAngLocal = m_HmdPose.TrackedDeviceAng;

    //hmdAngLocal += m_RotationOffset;
    hmdAngLocal.x += m_RotationOffset.x;
    hmdAngLocal.y += m_RotationOffset.y;
    hmdAngLocal.z += m_RotationOffset.z;

    //hmdAngLocal.Normalize();

    QAngle::AngleVectors(hmdAngLocal, &m_HmdForward, &m_HmdRight, &m_HmdUp);

    //hmdAngLocal.x = (hmdAngLocal.x > 180 ? 180)
    hmdAngLocal.Normalize();

    m_HmdAngAbs = hmdAngLocal;
}

void VR::ResetPosition()
{
    // The compositor may not have supplied a valid HMD pose during startup.
    // Defer centering instead of treating the tracking origin as the player.
    m_CenterPending = !m_HmdPose.isValid;
    if (m_CenterPending) return;
    m_Center = m_HmdPose.TrackedDevicePos;
    m_HmdPosRelativeRaw = m_HmdPosRelative = {0,0,0};
    m_HmdPosRelativeRawPrev = {0,0,0};
    m_CameraCollisionOffset = {0,0,0};
    m_CameraBlocked = false;
    // Recalibrate standing eye height here so a physical crouch is measured
    // against the current user and floor setup. TrackedDevicePos.z carries
    // SteamVR up (meters) through GetPoseData's Y-up to Z-up mapping.
    if (m_HmdPose.isValid && std::isfinite(m_HmdPose.TrackedDevicePos.z)
        && m_HmdPose.TrackedDevicePos.z > 0.5f && m_HmdPose.TrackedDevicePos.z < 2.5f)
    {
        m_StandingHeight = m_HmdPose.TrackedDevicePos.z;
        m_StandingHeightValid = true;
    }
    m_PhysicalCrouchHeld = false;
    m_CalibrationDrift.Reset();
    m_CalibrationStability.Reset();
    m_CalibrationSuppressUntil = GetTickCount64()+10000;
    // A recenter also re-anchors an open menu: the menu-scene viewpoint is
    // found again from the current scripted camera and the flat screen is
    // placed in front of the head again.
    m_MenuReanchorRequested = true;
    if (m_Overlay && m_MainMenuHandle != vr::k_ulOverlayHandleInvalid
        && m_Overlay->IsOverlayVisible(m_MainMenuHandle))
        RepositionOverlays();
}

void VR::UpdatePhysicalCrouch()
{
    if (!m_PhysicalCrouchEnabled)
    {
        m_PhysicalCrouchHeld = false;
        return;
    }
    if (!m_HmdPose.isValid || !std::isfinite(m_HmdPose.TrackedDevicePos.z))
        return;
    // Adopt the tallest recent standing height instead of freezing a seated
    // calibration: players who recenter while seated still get IRL crouch.
    const float height = m_HmdPose.TrackedDevicePos.z;
    if (!m_StandingHeightValid && height > 0.5f && height < 2.5f)
    {
        m_StandingHeight = height;
        m_StandingHeightValid = true;
    }
    if (m_StandingHeightValid && height > m_StandingHeight && height < 2.5f)
        m_StandingHeight = m_StandingHeight * 0.995f + height * 0.005f;
    if (!m_StandingHeightValid)
        return;
    const float drop = m_StandingHeight - height;
    const float engage = std::clamp(m_PhysicalCrouchDrop, 0.1f, 0.8f);
    const float release = std::max(engage - 0.1f, 0.05f);
    if (!m_PhysicalCrouchHeld && drop >= engage)
    {
        m_PhysicalCrouchHeld = true;
        PortalVrLog("Physical crouch engaged drop=%f hmdZ=%f standingZ=%f setupZ=%f",
            drop, height, m_StandingHeight, m_SetupOrigin.z);
    }
    else if (m_PhysicalCrouchHeld && drop <= release)
    {
        m_PhysicalCrouchHeld = false;
        PortalVrLog("Physical crouch released drop=%f hmdZ=%f standingZ=%f setupZ=%f",
            drop, height, m_StandingHeight, m_SetupOrigin.z);
    }
}

bool VR::IsJumpHeld()
{
    return PressedDigitalAction(m_ActionJump);
}

bool VR::IsUseHeld()
{
    return PressedDigitalAction(m_ActionUse);
}

bool VR::IsPrimaryAttackHeld()
{
    return PressedDigitalAction(m_ActionPrimaryAttack);
}

bool VR::IsSecondaryAttackHeld()
{
    return PressedDigitalAction(m_ActionSecondaryAttack);
}

bool VR::IsCrouchHeld()
{
    // The crouch button shares the offhand grip with the optional gun
    // support, so it is consumed while supporting. IRL crouch always counts.
    if (m_PhysicalCrouchHeld)
        return true;
    return IsButtonCrouchHeld();
}

bool VR::IsButtonCrouchHeld()
{
    return PressedDigitalAction(m_ActionCrouch) && !m_OptionalSupportActive;
}

void VR::UpdateEngineStandEye(const Vector& setupOrigin)
{
    if (!m_IsVREnabled || !m_HmdPose.isValid || !std::isfinite(setupOrigin.z))
        return;
    // Freeze while any duck is commanded so the dipped eye is never learned
    // as standing. Otherwise snap on teleports/spawns and ease after slow
    // rides (elevators) so later compensation stays exact.
    if (IsCrouchHeld())
        return;
    if (!m_EngineStandEyeValid || fabsf(setupOrigin.z - m_EngineStandEyeZ) > 24.0f)
    {
        m_EngineStandEyeZ = setupOrigin.z;
        m_EngineStandEyeValid = true;
    }
    else
    {
        m_EngineStandEyeZ += (setupOrigin.z - m_EngineStandEyeZ) * 0.02f;
    }
}

float VR::PhysicalDuckViewCompensation(const Vector& setupOrigin)
{
    // Physical-only duck: the hull (IN_DUCK) still shrinks, but the rendered
    // camera gets the engine's eye dip back, cancelling the double dip. With
    // the button also held this stays classic (no compensation). Bounded so a
    // teleport-while-crouched cannot fling the camera; standing re-snaps.
    if (IsButtonCrouchHeld())
    {
        m_PhysicalDuckView = false;
        return 0.0f;
    }
    // The menu scene stands the player on its own anchor; nothing ducks there.
    if (m_MenuAnchorValid && !m_MenuAnchorIsPause)
        return 0.0f;
    if (m_PhysicalCrouchHeld)
        m_PhysicalDuckView = true;
    if (!m_PhysicalDuckView)
        return 0.0f;
    // Exact when the eye's height above the feet is known: Portal's eye
    // stands at 64 units and ducks to 28.
    constexpr float standingEye = 64.0f, duckDip = 36.0f;
    if (m_EngineViewOffsetValid)
    {
        const float dip = std::clamp(standingEye - m_EngineViewOffsetZ, 0.0f, duckDip);
        // Standing up IRL releases the duck at once, but the engine's eye
        // takes a moment to rise. Stopping here with the head made the view
        // drop by what was left of the dip and then ride back up.
        if (!m_PhysicalCrouchHeld && dip < 0.5f)
            m_PhysicalDuckView = false;
        return dip;
    }
    if (!m_PhysicalCrouchHeld)
        m_PhysicalDuckView = false;
    if (!m_PhysicalCrouchHeld || !m_EngineStandEyeValid || !std::isfinite(setupOrigin.z))
        return 0.0f;
    return std::clamp(m_EngineStandEyeZ - setupOrigin.z, 0.0f, 48.0f);
}

void VR::SetEngineViewOffset(bool valid, float offsetZ)
{
    m_EngineViewOffsetValid = valid && std::isfinite(offsetZ) && offsetZ > -16.0f && offsetZ < 96.0f;
    m_EngineViewOffsetZ = m_EngineViewOffsetValid ? offsetZ : 0.0f;
}

void VR::UpdateAutoCalibration()
{
    const auto now=GetTickCount64();
    const float dt=m_CalibrationTime ? (now-m_CalibrationTime)*.001f : 0;
    m_CalibrationTime=now;
    const Vector playerDelta=m_SetupOrigin-m_CalibrationPlayerPosition;
    m_CalibrationPlayerPosition=m_SetupOrigin;
    const bool tracked=m_HmdPose.isValid
        && m_Poses[vr::k_unTrackedDeviceIndex_Hmd].eTrackingResult==vr::TrackingResult_Running_OK;
    if(!tracked || !m_CalibrationTracked || dt>.1f || playerDelta.LengthSqr()>64.f*64.f) {
        m_CalibrationDrift.Reset();
        m_CalibrationStability.Reset();
        m_CalibrationSuppressUntil=std::max(m_CalibrationSuppressUntil,now+2000);
        // Never turn movement during lost tracking/a paused frame into a large
        // roomscale locomotion impulse on the first recovered frame.
        m_HmdPosRelativeRawPrev=m_HmdPose.TrackedDevicePos-m_Center;
    }
    m_CalibrationTracked=tracked;
    if(!tracked) return;
    // SteamVR supplies an explicit transform for origin/floor/heading changes.
    // Compensate that transform exactly rather than treating head turns or
    // crouches as calibration errors.
    vr::HmdMatrix34_t rawStanding;
    MsvcVR_GetRawZeroPose(m_System, &rawStanding);
    auto frame=AutoCalibration::SourceSpace(rawStanding.m);
    const auto space=vr::VRCompositor()->GetTrackingSpace();
    if(space==vr::TrackingUniverseSeated) {
        vr::HmdMatrix34_t seated;
        MsvcVR_GetSeatedZeroPose(m_System, &seated);
        frame=HandPose::Concat(HandPose::InverseRigid(AutoCalibration::SourceSpace(seated.m)),frame);
    } else if(space==vr::TrackingUniverseRawAndUncalibrated) frame=PortalPose::Frame({0,0,0},{0,0,0});
    if(m_CalibrationOrigin.Update(frame,m_AutoCalibration && !m_CenterPending,m_Center,m_RotationOffset.y)) {
        m_HmdPosRelativeRawPrev=m_HmdPose.TrackedDevicePos-m_Center;
        m_CameraCollisionOffset={0,0,0};
        m_CalibrationDrift.Reset();
        m_CalibrationStability.Reset();
        m_CalibrationSuppressUntil=now+2000;
        PortalVrLog("Auto calibration: preserved position, floor height and heading after tracking-origin change");
    }
    vr::InputAnalogActionData_t walk{};
    const bool moving=GetAnalogActionData(m_ActionWalk,walk) && walk.bActive && walk.x*walk.x+walk.y*walk.y>.36f;
    const bool stable=m_CalibrationStability.Step(dt,m_HmdPose.TrackedDevicePos,m_HmdPose.TrackedDeviceAng,tracked);
    vr::InputAnalogActionData_t turn{};
    const bool turning=GetAnalogActionData(m_ActionTurn,turn) && turn.bActive && fabsf(turn.x)>.2f;
    auto* player=reinterpret_cast<C_BasePlayer*>(m_Game->GetLocalPortalPlayer());
    bool eligible=m_AutoCalibration && m_6DOF && stable && !turning
        && m_Game->IsInGame() && !m_Game->IsCursorVisible() && now>=m_CalibrationSuppressUntil
        && player
        && now-m_LastCarryUpdate>500 && moving && dt>0 && playerDelta.LengthSqr()<dt*dt
        && m_HmdPose.TrackedDeviceVel.LengthSqr()<.04f*.04f
        && Vector(m_HmdPose.TrackedDeviceAngVel.x,m_HmdPose.TrackedDeviceAngVel.y,m_HmdPose.TrackedDeviceAngVel.z).LengthSqr()<15.f*15.f
        && fabsf(m_HmdPose.TrackedDeviceAng.x)<25 && fabsf(m_HmdPose.TrackedDeviceAng.z)<25
        && !PressedDigitalAction(m_ActionPrimaryAttack) && !PressedDigitalAction(m_ActionSecondaryAttack)
        && !PressedDigitalAction(m_ActionUse) && !PressedDigitalAction(m_ActionCrouch);
    if(eligible) {
        Vector forward,right;
        QAngle::AngleVectors({0,m_HmdAngAbs.y,0},&forward,&right,nullptr);
        Vector movement=forward*walk.y+right*walk.x;
        VectorNormalize(movement);movement*=24.f;
        const Vector mins(-16,-16,-1),maxs(16,16,1);
        Ray_t bodyRay{},headRay{};
        Vector headOffset=m_CalibrationStability.mean-m_Center;
        headOffset.z=0;
        VectorPivotXY(headOffset,{0,0,0},m_RotationOffset.y);
        headOffset*=m_VRScale;
        bodyRay.Init(m_SetupOrigin,m_SetupOrigin+movement,mins,maxs);
        headRay.Init(m_SetupOrigin+headOffset,m_SetupOrigin+headOffset+movement,mins,maxs);
        CGameTrace bodyTrace{},headTrace{};
        CTraceFilterSkipEntity filter(reinterpret_cast<IHandleEntity*>(player),0);
        static const auto binding=PortalTrace::Binding::Resolve(m_Game->m_BaseClient);
        constexpr unsigned mask=CONTENTS_SOLID|CONTENTS_WINDOW|CONTENTS_GRATE|CONTENTS_MOVEABLE;
        auto trace=[&](Ray_t& ray,CGameTrace& hit) {
            return binding.Trace(player,ray,mask,&filter,&hit) || m_Game->TraceRay(ray,mask,&filter,&hit);
        };
        eligible=trace(bodyRay,bodyTrace) && trace(headRay,headTrace)
            && AutoCalibration::NeedsBodyAlignment(bodyTrace.fraction,bodyTrace.startsolid||bodyTrace.allsolid,
                headTrace.fraction,headTrace.startsolid||headTrace.allsolid);
    }
    const bool wasActive=m_CalibrationDrift.active;
    const Vector adjustment=m_CalibrationDrift.Step(dt,m_CalibrationStability.mean-m_Center,eligible);
    if(adjustment.LengthSqr()>0) {
        m_Center+=adjustment;
        m_HmdPosRelativeRawPrev=m_HmdPose.TrackedDevicePos-m_Center;
        if(!wasActive) PortalVrLog("Auto calibration: stable headset/player, blocked body and clear head route; easing horizontal alignment");
    } else if(wasActive) m_CalibrationSuppressUntil=now+10000;
}

void VR::SnapshotGrabPose()
{
    m_GrabPoseValid = m_RightControllerPose.isValid;
    m_GrabControllerPos = GetRightHandAbsPos();
    // Carry forward from the visible wrist in the same aim orientation as the
    // gun. The uncorrected grip direction is 30 degrees higher on this binding.
    m_GrabControllerAng = PickupTrace::CarryAngles(m_RightControllerForward,m_RightControllerUp);
    // The server rebuilds these from its own eye angles, which are the
    // command's: the same roll-free angles must frame them here.
    const QAngle head = EngineViewAngles();
    m_GrabHandRelative = PortalPose::RelativeHand(
        m_GrabControllerPos - m_SetupOrigin, m_GrabControllerAng, head);
    // Selection follows the visible gun's centerline. Carry physics continues
    // to use the wrist snapshot above, so acquiring a prop does not move it.
    Vector aimOrigin,aimDirection;
    m_PickupAimValid = GetPortalAimRay(aimOrigin,aimDirection);
    if (m_PickupAimValid) {
        QAngle aimAngles;
        QAngle::VectorAngles(aimDirection,m_RightControllerUp,aimAngles);
        m_PickupAimRelative = PortalPose::RelativeHand(
            aimOrigin-m_SetupOrigin,aimAngles,head);
    }
}

void VR::UpdateTracking()
{
    GetPoses();
    UpdateAutoCalibration();
    if (m_CenterPending && m_HmdPose.isValid) ResetPosition();
    UpdatePhysicalCrouch();
    UpdateRoomscaleFollow();

    // HMD tracking
    Vector hmdPosLocal = m_HmdPose.TrackedDevicePos;
    Vector hmdPosCentered = hmdPosLocal - m_Center;

    m_HmdPosRelativeRaw = hmdPosCentered;

    //std::cout << "HMD - X: " << hmdWorldPos.x << ", Y: " << hmdWorldPos.y << ", Z: " << hmdWorldPos.z << "\n";

    Vector hmdPosCorrected = hmdPosCentered;
    VectorPivotXY(hmdPosCorrected, { 0, 0, 0 }, m_RotationOffset.y);
    
    UpdateHMDAngles();

    m_HmdPosRelative = hmdPosCorrected * m_VRScale;

    C_BasePlayer* localPlayer = reinterpret_cast<C_BasePlayer *>(m_Game->GetLocalPortalPlayer());
    {
        // Edge-log hand-tracking stalls: poses keep updating above, but the
        // controller/gun/arm transforms below need the player entity. A stuck
        // "paused" here plus a frozen gun means entity resolution is broken.
        static bool wasMissing = false;
        const bool missing = (localPlayer == nullptr);
        if (missing != wasMissing)
        {
            PortalVrLog("Hand tracking %s: local player entity %s",
                missing ? "paused" : "resumed", missing ? "unavailable" : "available");
            wasMissing = missing;
        }
        if (missing)
            return;
    }

    // Roomscale setup
    /*Vector cameraMovingDirection = m_Center - m_SetupOriginPrev;
    Vector cameraToPlayer = m_HmdPosAbsPrev - m_SetupOriginPrev;
    cameraMovingDirection.z = 0;
    cameraToPlayer.z = 0;
    float cameraFollowing = DotProduct(cameraMovingDirection, cameraToPlayer);
    float cameraDistance = VectorLength(cameraToPlayer);

    if (localPlayer->m_hGroundEntity != -1 && localPlayer->m_vecVelocity.IsZero())
        m_RoomscaleActive = true;

    // TODO: Get roomscale to work while using thumbstick
    if ((cameraFollowing < 0 && cameraDistance > 1) || (m_PushingThumbstick))
        m_RoomscaleActive = false;*/

    // Check if camera is clipping inside wall
    /*CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipEntity tracefilter((IHandleEntity*)localPlayer, 0);

    Vector extendedHmdPos = m_HmdPosAbs - m_SetupOrigin;
    VectorNormalize(extendedHmdPos);
    extendedHmdPos = m_HmdPosAbs + (extendedHmdPos * 10);
    ray.Init(m_SetupOrigin, extendedHmdPos);

    if (!m_Game->TraceRay(ray, STANDARD_TRACE_MASK, &tracefilter, &trace))
        return vecEnd;
    if (trace.fraction < 1 && trace.fraction > 0)
    {
        Vector distanceInsideWall = trace.endpos - extendedHmdPos;
        m_CameraAnchor += distanceInsideWall;
        m_HmdPosAbs = m_CameraAnchor - Vector(0, 0, 64) + m_HmdPosLocalInWorld;
    }

    // Reset camera if it somehow gets too far
    m_SetupOriginToHMD = m_HmdPosAbs - m_SetupOrigin;
    if (VectorLength(m_SetupOriginToHMD) > 150)
        ResetPosition();

    m_HmdPosAbsPrev = m_HmdPosAbs;
    m_SetupOriginPrev = m_SetupOrigin;*/

    GetViewParameters();
    m_Ipd = m_EyeToHeadTransformPosRight.x * 2;
    m_EyeZ = m_EyeToHeadTransformPosRight.z;

    // Hand tracking
    Vector leftControllerPosLocal = m_LeftControllerPose.TrackedDevicePos;
    QAngle leftControllerAngLocal = m_LeftControllerPose.TrackedDeviceAng;

    Vector rightControllerPosLocal = m_RightControllerPose.TrackedDevicePos;
    QAngle rightControllerAngLocal = m_RightControllerPose.TrackedDeviceAng;

    //std::cout << "Right Controller - X: " << rightControllerPosLocal.x << "Y: " << rightControllerPosLocal.y << "Z: " << rightControllerPosLocal.z << "\n";

    Vector hmdToController = rightControllerPosLocal - hmdPosLocal;
    //Vector rightControllerPosCorrected = hmdPosCorrected + hmdToController;

    // When using stick turning, pivot the controllers around the HMD
    VectorPivotXY(hmdToController, { 0, 0, 0 }, m_RotationOffset.y);

    m_RightControllerPosRel = hmdToController * m_VRScale;
    Vector hmdToLeftController = leftControllerPosLocal - hmdPosLocal;
    VectorPivotXY(hmdToLeftController, { 0, 0, 0 }, m_RotationOffset.y);
    m_LeftControllerPosRel = hmdToLeftController * m_VRScale;
    leftControllerAngLocal.x += m_RotationOffset.x;
    leftControllerAngLocal.y += m_RotationOffset.y;
    leftControllerAngLocal.z += m_RotationOffset.z;

    //rightControllerAngLocal += m_RotationOffset;
    rightControllerAngLocal.x += m_RotationOffset.x;
    rightControllerAngLocal.y += m_RotationOffset.y;
    rightControllerAngLocal.z += m_RotationOffset.z;

    // Wrap angle from -180 to 180
    //rightControllerAngLocal.Normalize();

    QAngle::AngleVectors(leftControllerAngLocal, &m_LeftControllerForward, &m_LeftControllerRight, &m_LeftControllerUp);
    QAngle::AngleVectors(rightControllerAngLocal, &m_RightControllerForward, &m_RightControllerRight, &m_RightControllerUp);

    // Bare wrists use the tracked grip orientation, without the gun aim tilt.
    m_RightHandForward = m_RightControllerForward;
    m_RightHandUp = m_RightControllerUp;
    m_LeftHandForward = m_LeftControllerForward;
    m_LeftHandUp = m_LeftControllerUp;

    const float offset = -30;

    // Adjust controller angle downward
    m_LeftControllerForward = VectorRotate(m_LeftControllerForward, m_LeftControllerRight, offset);
    m_LeftControllerUp = VectorRotate(m_LeftControllerUp, m_LeftControllerRight, offset);

    m_RightControllerForward = VectorRotate(m_RightControllerForward, m_RightControllerRight, offset);
    m_RightControllerUp = VectorRotate(m_RightControllerUp, m_RightControllerRight, offset);

    // controller angles
    QAngle::VectorAngles(m_LeftControllerForward, m_LeftControllerUp, m_LeftControllerAngAbs);
    QAngle::VectorAngles(m_RightControllerForward, m_RightControllerUp, m_RightControllerAngAbs);
    m_RightControllerAngAbs.Normalize();

    // Apply both hardcoded and custom (from config) viewmodel offsets here.
    // The 8.5 forward seats the gun's grip in the palm (see GetRightHandAbsPos:
    // the pair must move together). ViewmodelPosCustomOffset still applies on top.
    PositionAngle viewmodelOffset = PositionAngle{ {8.5, -1, 1.5}, {0,0,0} };

    m_ViewmodelPosOffset = viewmodelOffset.position + m_ViewmodelPosCustomOffset;
    m_ViewmodelAngOffset = viewmodelOffset.angle + m_ViewmodelAngCustomOffset;

    m_ViewmodelForward = m_RightControllerForward;
    m_ViewmodelUp = m_RightControllerUp;
    m_ViewmodelRight = m_RightControllerRight;

    // Viewmodel yaw offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelUp, m_ViewmodelAngOffset.y);
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelUp, m_ViewmodelAngOffset.y);

    // Viewmodel pitch offset
    m_ViewmodelForward = VectorRotate(m_ViewmodelForward, m_ViewmodelRight, m_ViewmodelAngOffset.x);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelRight, m_ViewmodelAngOffset.x);

    // Viewmodel roll offset
    m_ViewmodelRight = VectorRotate(m_ViewmodelRight, m_ViewmodelForward, m_ViewmodelAngOffset.z);
    m_ViewmodelUp = VectorRotate(m_ViewmodelUp, m_ViewmodelForward, m_ViewmodelAngOffset.z);

    // Trace only after this frame's controller positions and orientations are ready.
    // AimMode 2 shows this point through UpdateAimMarker. The ping-pointer
    // beam Portal 2 drew from here has no counterpart in Portal 1's client:
    // its three signatures are absent, so that path never ran.
    m_AimPos = Trace((uint32_t*)localPlayer);
}

Vector VR::GetViewAngle()
{
    return Vector( m_HmdAngAbs.x, m_HmdAngAbs.y, m_HmdAngAbs.z );
}

Vector VR::GetViewOrigin(Vector setupOrigin)
{
    Vector center = setupOrigin;

    if (m_6DOF)
        center += m_HmdPosRelative;

    center.z += m_DuckCompensation;
    return center + (m_HmdForward * -(m_EyeZ * m_VRScale)) + m_CameraCollisionOffset;
}

void VR::UpdateCameraCollision(Vector setupOrigin)
{
    // Always solve from the engine's current player eye position. Never move
    // the tracking origin: leaning back, respawning, or portalling must recover.
    m_CameraCollisionOffset = { 0, 0, 0 };
    // The sweep starts at the engine eye, which is inside the (ducked) hull,
    // and ends at the camera with the compensation applied. A crawlspace
    // therefore holds the view down instead of letting it rise into the roof.
    UpdateEngineStandEye(setupOrigin);
    m_DuckCompensation = PhysicalDuckViewCompensation(setupOrigin);
    auto* player = m_Game->GetLocalPortalPlayer();
    if (!player)
    {
        m_CameraBlocked = false;
        return;
    }

    const Vector desired = GetViewOrigin(setupOrigin);
    const float radius = CameraCollision::HullRadius(m_Ipd * m_IpdScale * m_VRScale, m_Fov, m_Aspect);
    const Vector extent(radius, radius, radius);
    Ray_t ray{};
    ray.Init(setupOrigin, desired, extent * -1.0f, extent);
    CGameTrace trace;
    trace.fraction = 1.0f;
    trace.startsolid = trace.allsolid = false;
    CTraceFilterSkipEntity filter(reinterpret_cast<IHandleEntity*>(player), 0);
    constexpr unsigned mask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_MOVEABLE;
    static const PortalTrace::Binding portalTrace = PortalTrace::Binding::Resolve(m_Game->m_BaseClient);
    static bool loggedBinding = false;
    if (!loggedBinding) {
        PortalVrLog("Portal-aware head collision available=%d", portalTrace.function != nullptr);
        loggedBinding = true;
    }
    const bool usedPortal = portalTrace.Trace(player, ray, mask, &filter, &trace);
    if (!usedPortal && !m_Game->TraceRay(ray, mask, &filter, &trace))
    {
        m_CameraBlocked = false;
        return;
    }

    // An actual engine trace catches ABI errors that pure geometry tests cannot.
    static const bool debugCollision = strstr(GetCommandLineA(), "-portalvr-debug-collision") != nullptr;
    static bool wasPortal = false;
    if (debugCollision && usedPortal != wasPortal) {
        PortalVrLog("Head collision portal environment=%d fraction=%f startsolid=%d allsolid=%d",
            usedPortal, trace.fraction, trace.startsolid, trace.allsolid);
        wasPortal = usedPortal;
    }
    static bool loggedProbe = false;
    if (debugCollision && !loggedProbe)
    {
        Ray_t floorRay{};
        floorRay.Init(setupOrigin, setupOrigin - Vector(0, 0, 4096), extent * -1.0f, extent);
        CGameTrace floorTrace;
        if (m_Game->TraceRay(floorRay, mask, &filter, &floorTrace))
        {
            PortalVrLog("Collision engine probe fraction=%f startsolid=%d allsolid=%d start=%f,%f,%f end=%f,%f,%f rayFlags=%d,%d",
                floorTrace.fraction, floorTrace.startsolid, floorTrace.allsolid,
                setupOrigin.x, setupOrigin.y, setupOrigin.z,
                floorTrace.endpos.x, floorTrace.endpos.y, floorTrace.endpos.z,
                floorRay.m_IsRay, floorRay.m_IsSwept);
            loggedProbe = true;
        }
    }

    const Vector safe = CameraCollision::Constrain(setupOrigin, desired, trace.fraction, trace.startsolid, trace.allsolid);
    m_CameraCollisionOffset = safe - desired;
    const bool blocked = m_CameraCollisionOffset.LengthSqr() > 0.0001f;
    if (blocked != m_CameraBlocked)
    {
        PortalVrLog("Head collision blocked=%d fraction=%f radius=%f correction=%f,%f,%f startsolid=%d allsolid=%d",
            blocked, trace.fraction, radius, m_CameraCollisionOffset.x, m_CameraCollisionOffset.y,
            m_CameraCollisionOffset.z, trace.startsolid, trace.allsolid);
        // Discriminator: the same sweep as a line ray. A line from open air
        // can only start solid via an unfiltered self-hit, so line-solid
        // means the skip filter is broken while line-clear with hull-solid
        // means the hull/extents are misread (or the eye is truly embedded).
        Ray_t lineRay{};
        lineRay.Init(setupOrigin, desired);
        CGameTrace lineTrace;
        lineTrace.fraction = 1.0f;
        lineTrace.startsolid = lineTrace.allsolid = false;
        const bool lineOk = m_Game->TraceRay(lineRay, mask, &filter, &lineTrace);
        PortalVrLog("Head collision line usedPortal=%d ok=%d fraction=%f startsolid=%d allsolid=%d contents=%d start=%f,%f,%f end=%f,%f,%f",
            usedPortal, lineOk, lineTrace.fraction, lineTrace.startsolid, lineTrace.allsolid,
            lineTrace.contents, setupOrigin.x, setupOrigin.y, setupOrigin.z,
            desired.x, desired.y, desired.z);
    }
    m_CameraBlocked = blocked;
}

void VR::NoteGameplayView(const Vector& origin)
{
    ClearMenuAnchor();
    const std::uint64_t now = GetTickCount64();
    if (m_LastGameplayTime == 0 || now - m_LastGameplayTime > 500)
        m_GameplaySince = now;
    m_LastGameplayOrigin = origin;
    m_LastGameplayTime = now;
}

bool VR::UpdateMenuAnchor(const Vector& scriptedOrigin)
{
    const bool finite = std::isfinite(scriptedOrigin.x) && std::isfinite(scriptedOrigin.y) && std::isfinite(scriptedOrigin.z);
    // A paused game keeps following the player's own (frozen) eye.
    if (m_MenuAnchorValid && m_MenuAnchorIsPause)
    {
        m_MenuReanchorRequested = false;
        if (finite)
            m_MenuAnchor = scriptedOrigin;
        return true;
    }
    if (m_MenuAnchorValid && !m_MenuReanchorRequested)
        return true;
    m_MenuReanchorRequested = false;

    // Refuse to anchor from a degenerate camera; keep the previous anchor, or
    // the scripted origin when no anchor exists yet.
    if (!finite)
        return m_MenuAnchorValid;

    // The menu opened over a game that had been running, from where its
    // view already was: that is a pause, not the menu scene. Moving the view
    // to a floor below would jump it on every pause, most of all in mid-air
    // or while ducked. (The menu scene's camera cannot be told apart by
    // position: Portal's background maps park it at the player start.)
    const std::uint64_t now = GetTickCount64();
    const bool overRunningGame = m_LastGameplayTime != 0 && now - m_LastGameplayTime < 500
        && m_LastGameplayTime - m_GameplaySince > 1000
        && (scriptedOrigin - m_LastGameplayOrigin).LengthSqr() < 32.0f * 32.0f;
    if (!m_MenuAnchorValid && overRunningGame)
    {
        m_MenuAnchor = scriptedOrigin;
        m_MenuAnchorValid = m_MenuAnchorIsPause = true;
        PortalVrLog("VR menu anchor=%f,%f,%f source=pause", m_MenuAnchor.x, m_MenuAnchor.y, m_MenuAnchor.z);
        return true;
    }

    constexpr float eyeHeight = 64.0f;
    constexpr unsigned mask = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_GRATE | CONTENTS_MOVEABLE;
    IEngineTrace* engineTrace = m_Game ? m_Game->GetEngineTrace() : nullptr;
    // A scripted camera that is already at about head height over a floor is
    // a place to stand: leave it exactly where the map put it. Portal's
    // second menu scene (after the ending) is like this, its camera 77 units
    // over the floor; the first one hangs 221 units up. The scene's frozen
    // player stands around the camera, so this one trace must pass through it.
    if (engineTrace)
    {
        Ray_t ray{};
        ray.Init(scriptedOrigin, scriptedOrigin - Vector(0, 0, 128.0f));
        CGameTrace below;
        below.fraction = 1.0f;
        below.startsolid = below.allsolid = false;
        CTraceFilterSkipEntity filter(reinterpret_cast<IHandleEntity*>(m_Game->GetLocalPortalPlayer()), 0);
        engineTrace->TraceRay(ray, mask, &filter, &below);
        const float height = below.fraction * 128.0f;
        if (!below.startsolid && !below.allsolid && below.fraction < 1.0f
            && below.plane.normal.z > 0.7f && height >= 32.0f && height <= 96.0f)
        {
            m_MenuAnchor = scriptedOrigin;
            m_MenuAnchorValid = true;
            m_MenuAnchorIsPause = false;
            PortalVrLog("VR menu anchor=%f,%f,%f source=standing height=%f",
                m_MenuAnchor.x, m_MenuAnchor.y, m_MenuAnchor.z, height);
            return true;
        }
    }

    // Otherwise stand the player inside the menu scene. The stock scripted
    // camera can hang in the air or ride outside the room, so the floor
    // straight below it can be far down, a roof, an outside ledge, or
    // nothing. Take the first indoor floor with headroom: below the camera,
    // else on a widening ring around it, else at the configured spawn. Falls
    // back to the raw scripted origin when nothing is found.
    const bool spawnConfigured = std::isfinite(m_MenuSpawn.x) && std::isfinite(m_MenuSpawn.y) && std::isfinite(m_MenuSpawn.z)
        && m_MenuSpawn.LengthSqr() > 0.0001f;
    const auto trace = [&](const Vector& from, const Vector& to, CGameTrace& result) {
        Ray_t ray{};
        ray.Init(from, to);
        result.fraction = 1.0f;
        result.startsolid = result.allsolid = false;
        CTraceFilterSkipEntity filter(nullptr, 0);
        engineTrace->TraceRay(ray, mask, &filter, &result);
    };
    // A standing eye above the floor below (x, y), starting from height z.
    const auto probeFloor = [&](float x, float y, float z, Vector& out) -> bool {
        if (!engineTrace)
            return false;
        Vector probe(x, y, z);
        // Walk down through roof layers: an aerial camera first meets the
        // room's roof, a surface facing down, not its floor.
        for (int layer = 0; layer < 6; ++layer)
        {
            CGameTrace down;
            trace(probe, probe - Vector(0, 0, 512.0f), down);
            if (down.fraction <= 0.0f || down.fraction >= 1.0f || down.startsolid || down.allsolid)
                return false; // Void below, or embedded in solid.
            if (down.plane.normal.z <= 0.7f)
            {
                probe = down.endpos - Vector(0, 0, 128.0f);
                continue;
            }
            const Vector candidate(x, y, down.endpos.z + eyeHeight);
            // Room to stand, and a ceiling somewhere above: an outdoor roof
            // or ledge has open sky over it.
            CGameTrace headroom, ceiling;
            trace(candidate, candidate + Vector(0, 0, 72.0f), headroom);
            if (headroom.fraction < 1.0f || headroom.startsolid)
                return false;
            trace(candidate, candidate + Vector(0, 0, 1024.0f), ceiling);
            if (ceiling.fraction >= 1.0f || ceiling.startsolid)
                return false;
            out = candidate;
            return true;
        }
        return false;
    };

    Vector anchor = scriptedOrigin;
    const char* source = "scripted";
    bool found = probeFloor(scriptedOrigin.x, scriptedOrigin.y, scriptedOrigin.z, anchor);
    if (found)
        source = "floor-snap";
    for (float radius : { 64.0f, 128.0f, 256.0f, 512.0f })
    {
        for (int step = 0; step < 8 && !found; ++step)
        {
            const float angle = step * (3.14159265358979323846f / 4.0f);
            found = probeFloor(scriptedOrigin.x + cosf(angle) * radius,
                scriptedOrigin.y + sinf(angle) * radius, scriptedOrigin.z, anchor);
            if (found)
                source = "search";
        }
    }
    if (!found && spawnConfigured)
    {
        // Prefer a floor below the configured spawn; use it as written only
        // when there is none.
        if (!probeFloor(m_MenuSpawn.x, m_MenuSpawn.y, m_MenuSpawn.z, anchor))
            anchor = m_MenuSpawn;
        source = "config";
    }
    m_MenuAnchor = anchor;
    m_MenuAnchorValid = true;
    m_MenuAnchorIsPause = false;
    PortalVrLog("VR menu anchor=%f,%f,%f source=%s scripted=%f,%f,%f",
        m_MenuAnchor.x, m_MenuAnchor.y, m_MenuAnchor.z, source,
        scriptedOrigin.x, scriptedOrigin.y, scriptedOrigin.z);
    return true;
}

Vector VR::GetViewOriginLeft(Vector setupOrigin)
{
    Vector viewOriginLeft = GetViewOrigin(setupOrigin);
    viewOriginLeft -= m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginLeft;
}

Vector VR::GetViewOriginRight(Vector setupOrigin)
{
    Vector viewOriginRight = GetViewOrigin(setupOrigin);
    viewOriginRight += m_HmdRight * ((m_Ipd * m_IpdScale * m_VRScale) / 2);

    return viewOriginRight;
}

bool VR::GetPortalAimRay(Vector& origin, Vector& direction) {
    if (!m_IsVREnabled || !m_RightControllerPose.isValid) return false;
    origin = GetRightHandAbsPos();
    direction = m_RightControllerForward;
    // This is model-local metadata, not a cached world pose. Keep it while the
    // same weapon is equipped, including when an overhead gun is culled.
    if (m_PortalAimLastSeen) {
        const auto controller = HandPose::Frame(-m_RightControllerRight,
            m_RightControllerUp,m_RightControllerForward,origin);
        return GunRay::FromBarrel(HandPose::Concat(controller,m_PortalAimFromController),
            GetRightHandAbsPos(),origin,direction);
    }
    return std::isfinite(direction.LengthSqr()) && direction.LengthSqr() > .9f;
}

Vector VR::Trace(uint32_t* localPlayer) {
    Vector vecStart, direction;
    if (!GetPortalAimRay(vecStart,direction)) return GetRightHandAbsPos();
    Vector vecEnd = vecStart + direction * MAX_TRACE_LENGTH;

    CGameTrace trace;
    Ray_t ray;
    CTraceFilterSkipEntity tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(vecStart, vecEnd);

    if (!m_Game->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trace))
        return vecEnd;

    return trace.endpos;
}

void AngleMatrix(const QAngle& angles, matrix3x4_t& matrix)
{
    float sr, sp, sy, cr, cp, cy;

    SinCos(DEG2RAD(angles[YAW]), &sy, &cy);
    SinCos(DEG2RAD(angles[PITCH]), &sp, &cp);
    SinCos(DEG2RAD(angles[ROLL]), &sr, &cr);

    // matrix = (YAW * PITCH) * ROLL
    matrix[0][0] = cp * cy;
    matrix[1][0] = cp * sy;
    matrix[2][0] = -sp;

    // NOTE: Do not optimize this to reduce multiplies! optimizer bug will screw this up.
    matrix[0][1] = sr * sp * cy + cr * -sy;
    matrix[1][1] = sr * sp * sy + cr * cy;
    matrix[2][1] = sr * cp;
    matrix[0][2] = (cr * sp * cy + -sr * -sy);
    matrix[1][2] = (cr * sp * sy + -sr * cy);
    matrix[2][2] = cr * cp;

    matrix[0][3] = 0.0f;
    matrix[1][3] = 0.0f;
    matrix[2][3] = 0.0f;
}

void MatrixCopy(const matrix3x4_t& in, matrix3x4_t& out)
{
    memcpy(out.Base(), in.Base(), sizeof(float) * 3 * 4);
}


/*
================
R_ConcatTransforms
================
*/

void ConcatTransforms(const matrix3x4_t& in1, const matrix3x4_t& in2, matrix3x4_t& out)
{
    if (&in1 == &out)
    {
        matrix3x4_t in1b;
        MatrixCopy(in1, in1b);
        ConcatTransforms(in1b, in2, out);
        return;
    }
    if (&in2 == &out)
    {
        matrix3x4_t in2b;
        MatrixCopy(in2, in2b);
        ConcatTransforms(in1, in2b, out);
        return;
    }
    out[0][0] = in1[0][0] * in2[0][0] + in1[0][1] * in2[1][0] +
        in1[0][2] * in2[2][0];
    out[0][1] = in1[0][0] * in2[0][1] + in1[0][1] * in2[1][1] +
        in1[0][2] * in2[2][1];
    out[0][2] = in1[0][0] * in2[0][2] + in1[0][1] * in2[1][2] +
        in1[0][2] * in2[2][2];
    out[0][3] = in1[0][0] * in2[0][3] + in1[0][1] * in2[1][3] +
        in1[0][2] * in2[2][3] + in1[0][3];
    out[1][0] = in1[1][0] * in2[0][0] + in1[1][1] * in2[1][0] +
        in1[1][2] * in2[2][0];
    out[1][1] = in1[1][0] * in2[0][1] + in1[1][1] * in2[1][1] +
        in1[1][2] * in2[2][1];
    out[1][2] = in1[1][0] * in2[0][2] + in1[1][1] * in2[1][2] +
        in1[1][2] * in2[2][2];
    out[1][3] = in1[1][0] * in2[0][3] + in1[1][1] * in2[1][3] +
        in1[1][2] * in2[2][3] + in1[1][3];
    out[2][0] = in1[2][0] * in2[0][0] + in1[2][1] * in2[1][0] +
        in1[2][2] * in2[2][0];
    out[2][1] = in1[2][0] * in2[0][1] + in1[2][1] * in2[1][1] +
        in1[2][2] * in2[2][1];
    out[2][2] = in1[2][0] * in2[0][2] + in1[2][1] * in2[1][2] +
        in1[2][2] * in2[2][2];
    out[2][3] = in1[2][0] * in2[0][3] + in1[2][1] * in2[1][3] +
        in1[2][2] * in2[2][3] + in1[2][3];
}

void MatrixAngles(const matrix3x4_t& matrix, float* angles)
{
    float forward[3];
    float left[3];
    float up[3];

    //
    // Extract the basis vectors from the matrix. Since we only need the Z
    // component of the up vector, we don't get X and Y.
    //
    forward[0] = matrix[0][0];
    forward[1] = matrix[1][0];
    forward[2] = matrix[2][0];
    left[0] = matrix[0][1];
    left[1] = matrix[1][1];
    left[2] = matrix[2][1];
    up[2] = matrix[2][2];

    float xyDist = sqrtf(forward[0] * forward[0] + forward[1] * forward[1]);

    // enough here to get angles?
    if (xyDist > 0.001f)
    {
        // (yaw)	y = ATAN( forward.y, forward.x );		-- in our space, forward is the X axis
        angles[1] = RAD2DEG(atan2f(forward[1], forward[0]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // (roll)	z = ATAN( left.z, up.z );
        angles[2] = RAD2DEG(atan2f(left[2], up[2]));
    }
    else	// forward is mostly Z, gimbal lock-
    {
        // (yaw)	y = ATAN( -left.x, left.y );			-- forward is mostly z, so use right for yaw
        angles[1] = RAD2DEG(atan2f(-left[0], left[1]));

        // (pitch)	x = ATAN( -forward.z, sqrt(forward.x*forward.x+forward.y*forward.y) );
        angles[0] = RAD2DEG(atan2f(-forward[2], xyDist));

        // Assume no roll in this case as one degree of freedom has been lost (i.e. yaw == roll)
        angles[2] = 0;
    }
}

inline void MatrixAngles(const matrix3x4_t& matrix, QAngle& angles)
{
    MatrixAngles(matrix, &angles.x);
}



// transform a set of angles in the input space of parentMatrix to the output space
QAngle TransformAnglesToWorldSpace(const QAngle& angles, const matrix3x4_t& parentMatrix)
{
    matrix3x4_t angToParent, angToWorld;
    AngleMatrix(angles, angToParent);
    ConcatTransforms(parentMatrix, angToParent, angToWorld);
    QAngle out;
    MatrixAngles(angToWorld, out);
    return out;
}


Vector VR::TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle) {
    CGameTrace trTestObstructionsNearPortals;
    Ray_t ray;
    CTraceFilterSkipEntity tracefilter((IHandleEntity*)localPlayer, 0);

    ray.Init(cameraPos, eyePos);
    if (!m_Game->TraceRay(ray, MASK_SHOT | MASK_SHOT_HULL, &tracefilter, &trTestObstructionsNearPortals))
        return eyePos;

    float flWallHitFraction = trTestObstructionsNearPortals.fraction + 0.01f;
    CPortal_Base2D* pPortal = (CPortal_Base2D*)m_Game->m_Hooks->UTIL_Portal_FirstAlongRay(ray, flWallHitFraction);

    if (trTestObstructionsNearPortals.DidHit() && pPortal) {
        float flRayHitFraction = m_Game->m_Hooks->UTIL_IntersectRayWithPortal(ray, pPortal);
        //Vector vNewEye;
        Vector vHitPoint = ray.m_Start + ray.m_Delta * flRayHitFraction;
        //vNewEye = m_Game->m_Hooks->UTIL_Portal_PointTransform(pPortal->MatrixThisToLinked(), vHitPoint, vNewEye);

        //VMatrix matrix = *(VMatrix*)((uintptr_t)pPortal + 0x4C4);
        VMatrix matrix = pPortal->MatrixThisToLinked();

   
        /*QAngle newAngle;
        m_Game->m_Hooks->UTIL_Portal_AngleTransform(matrix, eyeAngle, newAngle);*/
        eyeAngle = TransformAnglesToWorldSpace(eyeAngle, matrix.As3x4());

        return matrix * vHitPoint;

        //return pPortal->MatrixThisToLinked() * vHitPoint;
    }

    return eyePos;
}

// [CONFIG PARSING UTILITY FUNCTION]
// Generates an error message by stringifying and concatenating 'args...'.
template <typename... Ts>
static void concatErrorMsg(Game& game, const Ts&... args)
{
    std::ostringstream oss;
    (oss << ... << args);
    game.errorMsg(oss.str().c_str());
}

// [CONFIG PARSING UTILITY FUNCTION]
// Attempts to parse an entry with key 'key' from the provided 'userConfig'. If the key is
// missing or if the parsing fails, 'defaultValue' is returned and a description is
// appended to 'problems'. This never throws: it runs while VR is starting on
// the render thread, where an escaping exception ends the game.
template <typename T>
static T parseConfigEntry(
    const std::unordered_map<std::string, std::string>& userConfig, std::vector<std::string>& problems,
    const char* key, const T& defaultValue)
try
{
    const auto itr = userConfig.find(key);

    if (itr == userConfig.end())
    {
        std::ostringstream problem;
        problem << key << " is missing (using " << defaultValue << ")";
        problems.push_back(problem.str());

        return defaultValue;
    }

    const std::string& configValue = itr->second;

    if constexpr (std::is_same_v<T, bool>)
    {
        // Values are read up to the optional '#' comment delimiter, so a
        // documented setting can contain trailing whitespace.  Accept the
        // numeric form as well because it is convenient when editing the
        // config from scripts.
        std::string normalized = configValue;
        normalized.erase(std::remove_if(normalized.begin(), normalized.end(),
            [](unsigned char character) { return std::isspace(character) != 0; }), normalized.end());
        return normalized == "true" || normalized == "1";
    }
    else if constexpr(std::is_floating_point_v<T>)
    {
        return std::stof(configValue);
    }
    else if constexpr(std::is_integral_v<T>)
    {
        return std::stol(configValue);
    }
    else
    {
        // Just a way of generating a compilation failure in case this branch is taken.
        struct invalid_type;
        return invalid_type{};
    }
}
catch (const std::exception&)
{
    const auto itr = userConfig.find(key);
    std::ostringstream problem;
    problem << key << " has an unreadable value '" << (itr != userConfig.end() ? itr->second : std::string())
        << "' (using " << defaultValue << ")";
    problems.push_back(problem.str());

    return defaultValue;
}

void VR::ParseConfigFile()
{
    char runtimeDirectory[MAX_PATH] = {};
    if (!GetRuntimeBaseDirectory(runtimeDirectory, sizeof(runtimeDirectory)))
        return;
    std::ifstream configStream(std::filesystem::path(runtimeDirectory) / "VR" / "config.txt");
    std::unordered_map<std::string, std::string> userConfig;

    std::string line;
    while (std::getline(configStream, line))
    {
        std::istringstream sLine(line);
        std::string key;
        if (std::getline(sLine, key, '='))
        {
            std::string value;
            if (std::getline(sLine, value, '#'))
                userConfig[key] = value;
            else if (std::getline(sLine, value))
                userConfig[key] = value;
        }
    }

    if (userConfig.empty())
    {
        PortalVrLog("bin\\VR\\config.txt is missing or empty; using built-in defaults");
        return;
    }

    // Every missing or unreadable entry, reported together once parsing ends.
    std::vector<std::string> problems;

    // Parse a single entry with key 'key' from the config into 'target'.
    // If the entry does not exist, or if the parsing fails, sets 'target' to
    // 'defaultValue'.
    const auto parseOrDefault = [&](const char* key, auto& target,
                                    const auto& defaultValue)
    {
        target = parseConfigEntry(userConfig, problems, key, defaultValue);
        std::cout << "Setting '" << key << "' to '" << target << "'\n";
    };

    // Parses a vector or angle from the config into 'target'. The XYZ coordinates
    // are read from three separate config entries with key 'keyPrefix' + 'X'/'Y'/'Z'.
    // If any entry does not exist, or if the parsing fails, sets the corresponding
    // coordinate in 'target' to zero.
    const auto parseXYZOrDefaultZero = [&](std::string keyPrefix, auto& target)
    {
        parseOrDefault((keyPrefix + "X").c_str(), target.x, 0.f);
        parseOrDefault((keyPrefix + "Y").c_str(), target.y, 0.f);
        parseOrDefault((keyPrefix + "Z").c_str(), target.z, 0.f);
    };

    parseOrDefault("SnapTurning", m_SnapTurning, false);
    parseOrDefault("SnapTurnAngle", m_SnapTurnAngle, 45.0f);
    parseOrDefault("TurnSpeed", m_TurnSpeed, 0.15f);
    parseOrDefault("LeftHanded", m_LeftHanded, false);
    parseOrDefault("VRScale", m_VRScale, 43.2f);
    parseOrDefault("IPDScale", m_IpdScale, 1.0f);
    parseOrDefault("6DOF", m_6DOF, true);
    // Optional in older configs: upgrading must not introduce a modal warning.
    if (userConfig.count("AutoCalibration")) parseOrDefault("AutoCalibration", m_AutoCalibration, true);
    // Physical crouch is optional for the same reason: older installs keep
    // working, and the installer appends the shipped defaults on upgrade.
    if (userConfig.count("PhysicalCrouch")) parseOrDefault("PhysicalCrouch", m_PhysicalCrouchEnabled, true);
    if (userConfig.count("PhysicalCrouchDrop")) parseOrDefault("PhysicalCrouchDrop", m_PhysicalCrouchDrop, 0.25f);
    m_PhysicalCrouchDrop = std::isfinite(m_PhysicalCrouchDrop)
        ? std::clamp(m_PhysicalCrouchDrop, 0.1f, 0.8f) : 0.25f;
    // The arm models are opt-in. This replaces ShowHands, which defaulted
    // to drawing them and is no longer read: a config written by an earlier
    // installer would otherwise keep the arms.
    if (userConfig.count("ShowArms")) parseOrDefault("ShowArms", m_ShowArms, false);
    // Roomscale body-follow, optional for the same reason.
    if (userConfig.count("Roomscale")) parseOrDefault("Roomscale", m_Roomscale, true);
    /*parseOrDefault("HudDistance", m_HudDistance, 1.3f);
    parseOrDefault("HudSize", m_HudSize, 4.0f);
    parseOrDefault("HudAlwaysVisible", m_HudAlwaysVisible, false);*/
    parseOrDefault("AimMode", m_AimMode, 2);
    parseOrDefault("FirstPersonBody", m_FirstPersonBody, false);
    parseOrDefault("FirstPersonBodyHideUpper", m_FirstPersonBodyHideUpper, true);
    parseOrDefault("LeftHandGunGrip", m_LeftHandGunGrip, true);
    parseOrDefault("LeftHandGunGripRadius", m_LeftHandGunGripRadius, 6.0f);
    m_LeftHandGunGripRadius = std::isfinite(m_LeftHandGunGripRadius)
        ? std::clamp(m_LeftHandGunGripRadius, 2.0f, 12.0f) : 6.0f;
    parseOrDefault("FirstPersonBodyBackOffset", m_FirstPersonBodyBackOffset, 8.0f);
    m_FirstPersonBodyBackOffset = std::isfinite(m_FirstPersonBodyBackOffset)
        ? std::clamp(m_FirstPersonBodyBackOffset, 0.0f, 24.0f) : 8.0f;
    parseOrDefault("AntiAliasing", m_AntiAliasing, 0);
    parseOrDefault("RenderWindow", m_RenderWindow, 0);
    // Menu-scene viewpoint fallback and the flat menu screen. All optional:
    // MenuSpawn (0,0,0) leaves the fallback off, and the earlier
    // MenuPanelDistance/MenuPanelWidth keys are no longer read (their values
    // described a much smaller panel).
    for (const char* key : { "MenuSpawnX", "MenuSpawnY", "MenuSpawnZ" })
    {
        float& target = key[9] == 'X' ? m_MenuSpawn.x : key[9] == 'Y' ? m_MenuSpawn.y : m_MenuSpawn.z;
        if (userConfig.count(key)) parseOrDefault(key, target, 0.0f);
    }
    if (userConfig.count("MenuScreenDistance")) parseOrDefault("MenuScreenDistance", m_MenuScreenDistance, 1.0f);
    if (userConfig.count("MenuScreenWidth")) parseOrDefault("MenuScreenWidth", m_MenuScreenWidth, 1.6f);
    parseXYZOrDefaultZero("ViewmodelPosCustomOffset", m_ViewmodelPosCustomOffset);
    parseXYZOrDefaultZero("ViewmodelAngCustomOffset", m_ViewmodelAngCustomOffset);

    if (!problems.empty())
    {
        std::ostringstream message;
        message << "bin\\VR\\config.txt: " << problems.size()
            << (problems.size() == 1 ? " setting was" : " settings were")
            << " missing or unreadable, so defaults are in use.\n";
        for (const std::string& problem : problems)
            message << "\n  " << problem;
        message << "\n\nRun the installer again to restore missing settings.";
        m_Game->errorMsg(message.str().c_str());
    }
}

void VR::WaitForConfigUpdate()
{
    char currentDir[MAX_STR_LEN];
    GetCurrentDirectory(MAX_STR_LEN, currentDir);
    char configDir[MAX_STR_LEN];
    sprintf_s(configDir, MAX_STR_LEN, "%s\\VR\\", currentDir);
    HANDLE fileChangeHandle = FindFirstChangeNotificationA(configDir, false, FILE_NOTIFY_CHANGE_LAST_WRITE);

    std::filesystem::file_time_type configLastModified;
    while (1)
    {
        try 
        {
            // Windows only notifies of change within a directory, so extra check here for just config.txt
            auto configModifiedTime = std::filesystem::last_write_time("VR\\config.txt");
            if (configModifiedTime != configLastModified)
            {
                configLastModified = configModifiedTime;
                ParseConfigFile();
                
                std::cout << "Successfully reloaded 'config.txt'\n";
            }
        }
        catch (const std::invalid_argument &e)
        {
            concatErrorMsg(
                *m_Game, "Failed to parse 'config.txt' (", e.what(), ")");
        }
        catch (const std::filesystem::filesystem_error &e)
        {
            concatErrorMsg(
                *m_Game, "'config.txt' not found. (", e.what(), ")");
            
            return;
        }
        
        FindNextChangeNotification(fileChangeHandle);
        WaitForSingleObject(fileChangeHandle, INFINITE);
        Sleep(100); // Sometimes the thread tries to read config.txt before it's finished writing
    }
}
