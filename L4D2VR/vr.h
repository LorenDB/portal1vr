#pragma once
#include "openvr.h"
#include "vector.h"
#include <chrono>
#include <cstdint>
#include <unordered_map>
#include "optionalgungrip.h"
#include "autocalibration.h"
#include "roomscale.h"

#define MAX_STR_LEN 256

class Game;
struct IDirect3DTexture9;
struct IDirect3DSurface9;
class ITexture;


struct TrackedDevicePoseData
{
	bool isValid = false;
	std::string TrackedDeviceName;
	Vector TrackedDevicePos = { 0, 0, 0 };
	Vector TrackedDeviceVel = { 0, 0, 0 };
	QAngle TrackedDeviceAng = { 0, 0, 0 };
	QAngle TrackedDeviceAngVel = { 0, 0, 0 };
};

class SharedTextureHolder
{
public:
	vr::VRVulkanTextureData_t m_VulkanData{};
	vr::Texture_t m_VRTexture{};
};

class VR
{
public:
	Game *m_Game = nullptr;

	vr::IVRSystem *m_System = nullptr;
	vr::IVRInput *m_Input = nullptr;
	vr::IVROverlay *m_Overlay = nullptr;

	vr::VROverlayHandle_t m_MainMenuHandle = vr::k_ulOverlayHandleInvalid;
	//vr::VROverlayHandle_t m_HUDHandle;

	float m_HorizontalOffsetLeft;
	float m_VerticalOffsetLeft;
	float m_HorizontalOffsetRight;
	float m_VerticalOffsetRight;

	uint32_t m_RenderWidth = 0;
	uint32_t m_RenderHeight = 0;
	uint32_t m_AntiAliasing = 0;
	uint32_t m_RenderWindow = 0;
	float m_Aspect;
	float m_Fov;

	vr::VRTextureBounds_t m_TextureBounds[2];
	vr::TrackedDevicePose_t m_Poses[vr::k_unMaxTrackedDeviceCount]{};

	Vector m_EyeToHeadTransformPosLeft = { 0,0,0 };
	Vector m_EyeToHeadTransformPosRight = { 0,0,0 };

	Vector m_HmdForward = { 0, 0, 0 };
	Vector m_HmdRight = { 0, 0, 0 };
	Vector m_HmdUp = { 0, 0, 0 };

	Vector m_HmdPosLocalInWorld = { 0,0,0 };

	Vector m_LeftControllerForward = { 0, 0, 0 };
	Vector m_LeftControllerRight = { 0, 0, 0 };
	Vector m_LeftControllerUp = { 0, 0, 0 };

	Vector m_RightControllerForward = { 0, 0, 0 };
	Vector m_RightControllerRight = { 0, 0, 0 };
	Vector m_RightControllerUp = { 0, 0, 0 };
	Vector m_RightHandForward = { 1, 0, 0 };
	Vector m_RightHandUp = { 0, 0, 1 };
	Vector m_LeftHandForward = { 1, 0, 0 };
	Vector m_LeftHandUp = { 0, 0, 1 };

	Vector m_ViewmodelForward = { 0, 0, 0 };
	Vector m_ViewmodelRight = { 0, 0, 0 };
	Vector m_ViewmodelUp = { 0, 0, 0 };

	QAngle m_HmdAngAbs = { 0, 0, 0 };

	Vector m_HmdPosRelativeRaw = { 0,0,0 };
	Vector m_HmdPosRelativeRawPrev = { 0,0,0 };

	Vector m_HmdPosRelative = { 0,0,0 };
	Vector m_HmdPosRelativePrev = { 0,0,0 };

	Vector m_AimPos = { 0, 0, 0 };
	bool m_Traced = false;

	Vector m_Center = { 0,0,0 };
	bool m_CenterPending = true;
	bool m_AutoCalibration = true;
 	AutoCalibration::Origin m_CalibrationOrigin;
 	AutoCalibration::Drift m_CalibrationDrift;
 	AutoCalibration::Stability m_CalibrationStability;
 	bool m_CalibrationTracked = false;
 	std::uint64_t m_CalibrationTime = 0, m_CalibrationSuppressUntil = 0, m_LastCarryUpdate = 0;
 	Vector m_CalibrationPlayerPosition = {0,0,0};
 	void UpdateAutoCalibration();
	// Menu-environment viewpoint. While a GameUI menu is open (main menu over
	// the background map, or the in-game pause menu) the engine drives a
	// scripted camera that can pan outside the room geometry. VR instead
	// freezes a standing anchor inside the environment: horizontal position
	// from the scripted camera, height snapped to a standing eye above the
	// floor below it (punched through roof layers), or the configured spawn
	// when no floor is found.
	// Roomscale offsets and head collision are solved from the anchor like
	// gameplay.
	Vector m_MenuAnchor = { 0, 0, 0 };
	bool m_MenuAnchorValid = false;
	bool m_MenuReanchorRequested = true;
	Vector m_MenuSpawn = { 0, 0, 0 }; // Source units; (0,0,0) disables the fallback.
	float m_MenuPanelDistance = 0.7f; // Menu overlay meters in front of the head when it opens.
	float m_MenuPanelWidth = 1.0f;    // Menu overlay meters wide.
	bool UpdateMenuAnchor(const Vector& scriptedOrigin);
	void ClearMenuAnchor() { m_MenuAnchorValid = false; }
	const Vector& MenuAnchorPosition() const { return m_MenuAnchor; }
	Vector m_SetupOrigin = { 0,0,0 };
	Vector m_CameraCollisionOffset = { 0,0,0 };
	bool m_CameraBlocked = false;

	float m_HeightOffset = 0.0;
	// Roomscale body-follow (roomscale.h). The camera already follows the
	// headset; the player body is steered after it, and the distance the body
	// covers is handed over from the head offset by moving m_Center, so the
	// camera does not move a second time.
	bool m_Roomscale = true;
	Roomscale::Follow m_RoomscaleFollow;
	Vector RoomscaleOffset() const;
	bool RoomscaleGroundAt(const Vector& offset);
	bool RoomscaleMove(bool stickWalking, float& forwardMove, float& sideMove);
	void UpdateRoomscaleFollow();

	Vector m_LeftControllerPosRel = { 0, 0, 0 };
	QAngle m_LeftControllerAngAbs = { 0, 0, 0 };
	Vector m_RightControllerPosRel = { 0, 0, 0 };
	QAngle m_RightControllerAngAbs = { 0, 0, 0 };

	Vector m_ViewmodelPosOffset = { 0, 0, 0 };
	QAngle m_ViewmodelAngOffset = { 0, 0, 0 };

	Vector m_ViewmodelPosCustomOffset = { 0, 0, 0 }; // Custom (from config) viewmodel position offset applied on top of hardcoded ones
    QAngle m_ViewmodelAngCustomOffset = { 0, 0, 0 }; // Custom (from config) viewmodel angle offset applied on top of hardcoded ones

	float m_Ipd = 0.0f;
	float m_EyeZ = 0.0f;

	Vector m_IntendedPositionOffset = { 0,0,0 };

	enum TextureID
	{
		Texture_None = -1,
		Texture_LeftEye,
		Texture_RightEye,
		Texture_HUD,
		Texture_Blank
	};

	ITexture *m_LeftEyeTexture = nullptr;
	ITexture *m_RightEyeTexture = nullptr;
	ITexture *m_HUDTexture = nullptr;
	ITexture *m_BlankTexture = nullptr;

	IDirect3DSurface9 *m_D9LeftEyeSurface = nullptr;
	IDirect3DSurface9 *m_D9RightEyeSurface = nullptr;
	IDirect3DSurface9 *m_D9HUDSurface = nullptr;
	IDirect3DSurface9 *m_D9BlankSurface = nullptr;

	SharedTextureHolder m_VKLeftEye;
	SharedTextureHolder m_VKRightEye;
	SharedTextureHolder m_VKBackBuffer;
	SharedTextureHolder m_VKHUD;
	SharedTextureHolder m_VKBlankTexture;

	// Frames without a rendered world (loading screens, startup) show only the
	// menu panel, on a black compositor background.
	int m_FramesWithoutScene = 0;
	bool m_SceneHidden = false;
	std::uint64_t m_SceneBackgroundRestoreAt = 0;

	// AimMode 2: a dot where the gun is aiming, drawn as a SteamVR overlay
	// (aimmarker.h). Hidden while the eyes render through a portal crossing,
	// where the plain tracking-to-world mapping does not hold.
	vr::VROverlayHandle_t m_AimMarkerHandle = vr::k_ulOverlayHandleInvalid;
	bool m_AimMarkerVisible = false;
	bool m_EyeViewThroughPortal = false;
	void CreateAimMarker();
	void UpdateAimMarker(bool eyeFrame);

	bool m_IsVREnabled = false;
	bool m_IsInitialized = false;
	bool m_RenderedNewFrame = false;
	bool m_RenderedHud = false;
	bool m_CreatedVRTextures = false;
	bool m_DrawCrosshair = false;
	TextureID m_CreatingTextureID = Texture_None;
	TextureID m_BindingEyeTexture = Texture_None;

	bool m_PressedTurn = false;
	bool m_PushingThumbstick = false;
	bool m_PointerCreated = false;

	// action set
	vr::VRActionSetHandle_t m_ActionSet = 0;
	vr::VRActionSetHandle_t m_BaseActionSet = 0;
	vr::VRActiveActionSet_t m_ActiveActionSet{};

	// actions
	vr::VRActionHandle_t m_ActionJump = 0;
	vr::VRActionHandle_t m_ActionPrimaryAttack = 0;
	vr::VRActionHandle_t m_ActionSecondaryAttack = 0;
	vr::VRActionHandle_t m_ActionReload = 0;
	vr::VRActionHandle_t m_ActionWalk = 0;
	vr::VRActionHandle_t m_ActionTurn = 0;
	vr::VRActionHandle_t m_ActionUse = 0;
	vr::VRActionHandle_t m_ActionNextItem = 0;
	vr::VRActionHandle_t m_ActionPrevItem = 0;
	vr::VRActionHandle_t m_ActionResetPosition = 0;
	vr::VRActionHandle_t m_ActionCrouch = 0;
	vr::VRActionHandle_t m_ActionFlashlight = 0;
	vr::VRActionHandle_t m_ActionActivateVR = 0;
	vr::VRActionHandle_t m_MenuSelect = 0;
	vr::VRActionHandle_t m_MenuBack = 0;
	vr::VRActionHandle_t m_MenuUp = 0;
	vr::VRActionHandle_t m_MenuDown = 0;
	vr::VRActionHandle_t m_MenuLeft = 0;
	vr::VRActionHandle_t m_MenuRight = 0;
	vr::VRActionHandle_t m_Spray = 0;
	vr::VRActionHandle_t m_Scoreboard = 0;
	vr::VRActionHandle_t m_ShowHUD = 0;
	vr::VRActionHandle_t m_Pause = 0;
	vr::VRActionHandle_t m_ActionSkeletonLeft = 0;
	vr::VRActionHandle_t m_ActionSkeletonRight = 0;
	// Standardized SteamVR Input pose actions (declared in /actions/base).
	// The compositor pose array plus GetTrackedDeviceIndexForControllerRole is
	// the legacy path: roles can be invalid on Quest 3 while Input poses are
	// still tracked. Prefer pose actions, fall back to legacy roles.
	vr::VRActionHandle_t m_ActionPoseLeft = 0;
	vr::VRActionHandle_t m_ActionPoseRight = 0;
	vr::VRInputValueHandle_t m_InputSourceLeft = 0;
	vr::VRInputValueHandle_t m_InputSourceRight = 0;
	bool GetPoseActionPose(vr::VRActionHandle_t action, vr::TrackedDevicePose_t &poseOut);
	// Haptic outputs (declared in /actions/base). The actions are bound to
	// physical hands; Hand follows LeftHanded, like the pose actions.
	vr::VRActionHandle_t m_ActionHapticLeft = 0;
	vr::VRActionHandle_t m_ActionHapticRight = 0;
	enum class Hand { Gun, Off };
	void TriggerHaptic(vr::VRActionHandle_t action, float seconds, float frequency, float amplitude);
	void TriggerHaptic(Hand hand, float seconds, float frequency, float amplitude);
	// Set while the grab controller keeps reporting a held object, so pickup
	// and release can each be felt once.
	bool m_Carrying = false;
	void NoteCarryUpdate();
	// A relaxed hand is slightly cupped.  Pico's SteamVR bridge does not expose
	// a skeleton stream, so these values are also the procedural fallback until
	// a controller reports real curl data.
	float m_LeftFingerCurl[5] = { 0.42f, 0.50f, 0.56f, 0.52f, 0.46f };
	float m_RightFingerCurl[5] = { 0.42f, 0.50f, 0.56f, 0.52f, 0.46f };
	bool m_LeftSkeletonValid = false;
	bool m_RightSkeletonValid = false;
	bool m_LeftGripPressed = false;
	vr::VRActionHandle_t m_ActionSupportLeft = 0;
	vr::VRActionHandle_t m_ActionSupportRight = 0;
	bool m_OptionalSupportActive = false;
	OptionalGunGrip::State m_OptionalGripState;
	matrix3x4_t m_SupportFromController{};
	std::uint64_t m_SupportLastSeen = 0;
	bool UpdateOptionalGunSupport(matrix3x4_t *target = nullptr);
	bool m_LeftHandGunGrip = true;
	float m_LeftHandGunGripRadius = 6.0f;

	TrackedDevicePoseData m_HmdPose;
	TrackedDevicePoseData m_LeftControllerPose;
	TrackedDevicePoseData m_RightControllerPose;

	QAngle m_RotationOffset = { 0, 0, 0 };
	bool m_OverrideEyeAngles = false;
	// Snapshot the hand pose at CreateMove time. Server-side pickup callbacks
	// can run between SteamVR action samples, so they must not query live input
	// and accidentally fall back to the HMD pose.
	Vector m_GrabControllerPos = { 0, 0, 0 };
	QAngle m_GrabControllerAng = { 0, 0, 0 };
	matrix3x4_t m_GrabHandRelative;
	bool m_GrabPoseValid = false;
	matrix3x4_t m_PickupAimRelative{};
	bool m_PickupAimValid = false;
	matrix3x4_t m_PortalAimFromController{};
	std::uint64_t m_PortalAimLastSeen = 0;
	bool GetPortalAimRay(Vector& origin, Vector& direction);
	QAngle m_ServerGrabAngles = { 0, 0, 0 };
	void SnapshotGrabPose();
	bool m_UseCommandHeld = false;
	std::chrono::steady_clock::time_point m_PrevFrameTime;

	float m_TurnSpeed = 0.15f;
	bool m_SnapTurning = false;
	float m_SnapTurnAngle = 45.0f;
	bool m_LeftHanded = false;
	vr::VRActionSetHandle_t m_LeftActionSet = 0;
	std::unordered_map<vr::VRActionHandle_t,vr::VRActionHandle_t> m_LeftActions;
	std::uint64_t m_HandSwitchSuppressUntil = 0;
	vr::VRActionHandle_t ResolveAction(vr::VRActionHandle_t action) const;
	bool HandleSettingsCommand(const char* command);
	float m_VRScale = 43.2f;
	float m_IpdScale = 1.0f;
	bool m_6DOF = true;
	// Physical crouch: an IRL headset drop also asserts duck, independent of
	// the crouch button so Quest 3 roomscale ducking works without rebinding.
	bool m_PhysicalCrouchEnabled = true;
	float m_PhysicalCrouchDrop = 0.25f;
	float m_StandingHeight = 0.0f;
	bool m_StandingHeightValid = false;
	bool m_PhysicalCrouchHeld = false;
	bool IsPhysicalCrouchHeld() const { return m_PhysicalCrouchHeld; }
	bool IsCrouchHeld();
	bool IsButtonCrouchHeld();
	bool IsJumpHeld();
	bool IsUseHeld();
	bool IsPrimaryAttackHeld();
	bool IsSecondaryAttackHeld();
	void UpdatePhysicalCrouch();
	// Engine standing eye height (Source units), tracked while unducked.
	// A physical crouch keeps IN_DUCK pressed so the server hull shrinks, but
	// the engine also dips the eye on top of the real head drop (double dip).
	// The compensation below adds that engine dip back at the rendered camera
	// for physical-only ducks, so the view follows the head 1:1. Button ducks
	// keep the classic view dip. Elevators/portals re-snap on stand.
	float m_EngineStandEyeZ = 0.0f;
	bool m_EngineStandEyeValid = false;
	void UpdateEngineStandEye(const Vector& setupOrigin);
	float PhysicalDuckViewCompensation(const Vector& setupOrigin);
	float m_HudDistance = 1.3f;
	float m_HudSize = 4.0f;
	bool m_HudAlwaysVisible = false;
	int m_AimMode = 2;
	bool m_FirstPersonBody = true;
	bool m_FirstPersonBodyHideUpper = true;
	// Show hand/arm models. False hides the bare hands and collapses the
	// gun model's integrated arm into the receiver, leaving only the portal
	// gun mesh. Aim, pickup, and muzzle logic are untouched.
	bool m_ShowHands = true;
	float m_FirstPersonBodyBackOffset = 8.0f;

	VR() {};
	VR(Game *game);
	int SetActionManifest(const char *fileName);
	void InstallApplicationManifest(const char *fileName);
	void Update();
	void SetScreenSizeOverride(bool bState);
	void CreateVRTextures();
	void SubmitVRTextures();
	void RepositionOverlays();
	void GetPoses();
	void UpdatePosesAndActions();
	void GetViewParameters();
	void ProcessMenuInput();
	void ProcessInput();
	VMatrix VMatrixFromHmdMatrix(const vr::HmdMatrix34_t &hmdMat);
	vr::HmdMatrix34_t VMatrixToHmdMatrix(const VMatrix &vMat);
	vr::HmdMatrix34_t GetControllerTipMatrix(vr::ETrackedControllerRole controllerRole);
	bool CheckOverlayIntersectionForController(vr::VROverlayHandle_t overlayHandle, vr::ETrackedControllerRole controllerRole);
	QAngle GetRightControllerAbsAngle();
	QAngle& GetRightControllerAbsAngleConst();
	Vector GetRightControllerAbsPos(Vector eyePosition = {0, 0, 0});
	Vector GetLeftControllerAbsPos() { return GetRightControllerAbsPos() - m_RightControllerPosRel + m_LeftControllerPosRel; }
	Vector GetRightHandAbsPos() {
		// The controller tracks the grip, while the model anchor is the wrist.
		// The -6.5 seats the gun's grip in the palm instead of leaving the
		// physical hand on the model's wrist joint; it must stay paired with
		// the viewmodel forward offset in UpdateTracking (both shift the
		// gun/hand/socket assembly together so aim, pickup, and the muzzle
		// effect stay glued to the visible gun).
		return GetRightControllerAbsPos() + m_RightControllerForward * (m_ViewmodelPosCustomOffset.x - 6.5f)
			+ m_RightControllerRight * (m_ViewmodelPosCustomOffset.y - 1.0f)
			+ m_RightControllerUp * m_ViewmodelPosCustomOffset.z;
	}
	Vector GetLeftHandAbsPos() {
		return GetLeftControllerAbsPos() - m_LeftControllerForward * 2.5f + m_LeftControllerRight;
	}
	Vector GetRecommendedViewmodelAbsPos(Vector eyePosition);
	QAngle GetRecommendedViewmodelAbsAngle();
	void UpdateHMDAngles();
	void UpdateTracking();
	Vector GetViewAngle();
	Vector GetViewOrigin(Vector setupOrigin);
	void UpdateCameraCollision(Vector setupOrigin);
	Vector GetViewOriginLeft(Vector setupOrigin);
	Vector GetViewOriginRight(Vector setupOrigin);
	bool PressedDigitalAction(vr::VRActionHandle_t &actionHandle, bool checkIfActionChanged = false);
	bool GetAnalogActionData(vr::VRActionHandle_t &actionHandle, vr::InputAnalogActionData_t &analogDataOut);
	void ResetPosition();
	void GetPoseData(vr::TrackedDevicePose_t &poseRaw, TrackedDevicePoseData &poseOut);
	void ParseConfigFile();
	void WaitForConfigUpdate();
	Vector Trace(uint32_t* localPlayer);
	Vector TraceEye(uint32_t* localPlayer, Vector cameraPos, Vector eyePos, QAngle& eyeAngle);
};
