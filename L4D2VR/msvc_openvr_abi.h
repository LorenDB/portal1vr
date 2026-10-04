#pragma once

namespace vr {
struct HmdMatrix34_t;
class IVRSystem;
}

// MinGW x86 does not match MSVC thiscall when a member function returns a
// struct. OpenVR's matrix methods are called through an MSVC-ABI thunk.
extern "C" {
void MsvcVR_GetEyeToHeadTransform(vr::IVRSystem *system, int eye, vr::HmdMatrix34_t *out);
void MsvcVR_GetSeatedZeroPose(vr::IVRSystem *system, vr::HmdMatrix34_t *out);
void MsvcVR_GetRawZeroPose(vr::IVRSystem *system, vr::HmdMatrix34_t *out);
}
