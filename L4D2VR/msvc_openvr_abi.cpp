// Compiled only for the MinGW d3d9.dll, and only with an MSVC x86 target.
// MinGW Clang uses the Itanium member-call ABI: a returned HmdMatrix34_t
// comes back through ECX and `this` is pushed. OpenVR's Windows DLL is MSVC
// thiscall, so ECX is the interface and the return slot is on the stack.
// Swapping them crashes inside VR::UpdateAutoCalibration the first time a
// pose matrix is returned by value.
//
// These wrappers are extern "C" and return through a pointer, which is the
// same cdecl on both ABIs. The virtual call inside uses the MSVC convention.

struct HmdMatrix34_t {
    float m[3][4];
};

static_assert(sizeof(HmdMatrix34_t) == 48, "OpenVR HmdMatrix34_t is 3x4 floats");

class IVRSystemThunk {
public:
    virtual void Slot0() = 0;
    virtual void Slot1() = 0;
    virtual void Slot2() = 0;
    virtual void Slot3() = 0;
    virtual HmdMatrix34_t GetEyeToHeadTransform(int eye) = 0;                 // 4
    virtual void Slot5() = 0;
    virtual void Slot6() = 0;
    virtual void Slot7() = 0;
    virtual void Slot8() = 0;
    virtual void Slot9() = 0;
    virtual void Slot10() = 0;
    virtual void Slot11() = 0;
    virtual HmdMatrix34_t GetSeatedZeroPoseToStandingAbsoluteTrackingPose() = 0; // 12
    virtual HmdMatrix34_t GetRawZeroPoseToStandingAbsoluteTrackingPose() = 0;    // 13
};

extern "C" void MsvcVR_GetEyeToHeadTransform(void *system, int eye, HmdMatrix34_t *out) {
    *out = static_cast<IVRSystemThunk *>(system)->GetEyeToHeadTransform(eye);
}

extern "C" void MsvcVR_GetSeatedZeroPose(void *system, HmdMatrix34_t *out) {
    *out = static_cast<IVRSystemThunk *>(system)->GetSeatedZeroPoseToStandingAbsoluteTrackingPose();
}

extern "C" void MsvcVR_GetRawZeroPose(void *system, HmdMatrix34_t *out) {
    *out = static_cast<IVRSystemThunk *>(system)->GetRawZeroPoseToStandingAbsoluteTrackingPose();
}
