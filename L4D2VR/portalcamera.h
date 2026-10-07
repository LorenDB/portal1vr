#pragma once
#include "portalpose.h"
#include "portaltrace.h"

namespace PortalCamera {
// Portal 1's opening is 64 by 108 units, centered on the portal origin.
constexpr float HalfWidth = 32.0f;
constexpr float HalfHeight = 54.0f;
// The head hull is kept inside the hole by collision; allow for its radius.
constexpr float OpeningMargin = 8.0f;
// Further behind the plane than this is not a head leaning through a hole.
constexpr float MaxDepth = 96.0f;

// CalcPortalView can move the eye through a portal before PlayerPortalled.
// The eye origin and orientation must stay in the same space. Tracked offsets
// belong in the player's space until the complete frame is transformed.
struct Frame {
    bool transformed = false;
    matrix3x4_t toLinked=PortalPose::Frame({0,0,0},{0,0,0});
    // The opening of the portal the player is standing in, in the player's
    // space. The engine only asks whether its own eye is through it. A VR
    // head leads or trails the body, so each rendered eye asks for itself.
    bool hasOpening = false;
    Vector center = {0,0,0}, forward = {1,0,0}, left = {0,1,0}, up = {0,0,1};

    Vector Unmap(const Vector& point) const {
        return transformed ? PortalPose::Position(HandPose::Concat(
            HandPose::InverseRigid(toLinked),PortalPose::Frame(point,{0,0,0}))) : point;
    }
    Vector Map(const Vector& point) const {
        return transformed ? PortalPose::Position(HandPose::Concat(
            toLinked,PortalPose::Frame(point,{0,0,0}))) : point;
    }
    QAngle Map(const QAngle& angles) const {
        return transformed ? PortalPose::Angles(HandPose::Concat(
            toLinked,PortalPose::Frame({0,0,0},angles))) : angles;
    }
    QAngle Unmap(const QAngle& angles) const {
        return transformed ? PortalPose::Angles(HandPose::Concat(
            HandPose::InverseRigid(toLinked),PortalPose::Frame({0,0,0},angles))) : angles;
    }
    void Map(matrix3x4_t* bones, int count) const {
        if(transformed) for(int i=0;i<count;++i) bones[i]=HandPose::Concat(toLinked,bones[i]);
    }

    // Has this point, in the player's space, passed through the opening?
    bool Through(const Vector& point) const {
        if(!hasOpening) return false;
        const Vector offset=point-center;
        const float depth=offset.x*forward.x+offset.y*forward.y+offset.z*forward.z;
        const float across=offset.x*left.x+offset.y*left.y+offset.z*left.z;
        const float along=offset.x*up.x+offset.y*up.y+offset.z*up.z;
        return std::isfinite(depth) && depth<0 && depth>-MaxDepth
            && fabsf(across)<=HalfWidth+OpeningMargin && fabsf(along)<=HalfHeight+OpeningMargin;
    }
    // The frame for a camera at this point in the player's space. Without a
    // verified opening this keeps the engine's own decision.
    Frame For(const Vector& camera) const {
        Frame result=*this;
        if(hasOpening) result.transformed=Through(camera);
        return result;
    }
};

inline bool Supported(uintptr_t client) {
    // Verify CalcView's call and CalcPortalView's transform flag/matrix reads.
    const unsigned char call[]={0xff,0x75,0x0c,0x8a,0x45,0xff,0x8b,0xcf,0xff,0x75,0x08,
        0x88,0x87,0x54,0x16,0,0,0xe8,0x1a,0xf8,0xff,0xff};
    const unsigned char transform[]={0x8d,0x87,0x64,0x08,0,0,0xc6,0x83,0x54,0x16,0,0,1,
        0x8b,0xf0,0x8d,0x7d,0x84,0xb9,0x10,0,0,0,0xf3,0xa5};
    return client && SigScanner::IsReadable(client+0x228eb0,sizeof(call))
        && SigScanner::IsReadable(client+0x2289c8,sizeof(transform))
        && !memcmp(reinterpret_cast<void*>(client+0x228eb0),call,sizeof(call))
        && !memcmp(reinterpret_cast<void*>(client+0x2289c8),transform,sizeof(transform));
}

inline bool Rigid(const matrix3x4_t& frame) {
    for(int r=0;r<3;++r) for(int c=0;c<4;++c)
        if(!std::isfinite(frame[r][c])) return false;
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) {
        float dot=0;
        for(int k=0;k<3;++k) dot+=frame[k][r]*frame[k][c];
        if(fabsf(dot-(r==c ? 1.f : 0.f))>.001f) return false;
    }
    return true;
}

// A portal's placement, from C_BaseEntity's absolute origin and angles
// (client.dll 0x68362d89: the getters in vtable slots 9 and 10 return
// entity+0x25c and entity+0x268).
inline bool Placement(uintptr_t portal, matrix3x4_t& frame) {
    if(!portal || !SigScanner::IsReadable(portal+0x25c,24)) return false;
    Vector origin; QAngle angles;
    memcpy(&origin,reinterpret_cast<void*>(portal+0x25c),12);
    memcpy(&angles,reinterpret_cast<void*>(portal+0x268),12);
    for(int i=0;i<3;++i) if(!std::isfinite(origin[i]) || !std::isfinite(angles[i])) return false;
    frame=PortalPose::Frame(origin,angles);
    return true;
}

// The matrix a portal pair must have: out of the entry portal's frame, half
// a turn about its up axis, into the exit portal's frame.
inline matrix3x4_t Linkage(const matrix3x4_t& entry, const matrix3x4_t& exit) {
    const matrix3x4_t halfTurn(-1,0,0,0, 0,-1,0,0, 0,0,1,0);
    return HandPose::Concat(exit,HandPose::Concat(halfTurn,HandPose::InverseRigid(entry)));
}

inline bool Matches(const matrix3x4_t& a, const matrix3x4_t& b) {
    for(int r=0;r<3;++r) {
        for(int c=0;c<3;++c) if(fabsf(a[r][c]-b[r][c])>.01f) return false;
        if(fabsf(a[r][3]-b[r][3])>1.0f) return false;
    }
    return true;
}

inline Frame Read(void* player, const PortalTrace::Binding& binding, bool supported) {
    Frame result;
    if(!supported || !player
        || !SigScanner::IsReadable(reinterpret_cast<uintptr_t>(player)+0x1654,1)) return result;
    const auto portal=reinterpret_cast<uintptr_t>(binding.Environment(player));
    if(!portal || !SigScanner::IsReadable(portal+0x864,sizeof(result.toLinked))) return result;
    matrix3x4_t toLinked;
    memcpy(&toLinked,reinterpret_cast<void*>(portal+0x864),sizeof(toLinked));
    if(!Rigid(toLinked)) return result;
    result.toLinked=toLinked;
    result.transformed=*(static_cast<const unsigned char*>(player)+0x1654)!=0;
    // Only trust the opening when both placements reproduce the engine's own
    // matrix; anything else keeps the engine's decision for the eye.
    matrix3x4_t entry,exit;
    uint32_t linkedHandle=0xffffffff;
    if(SigScanner::IsReadable(portal+0xa70,4))
        memcpy(&linkedHandle,reinterpret_cast<void*>(portal+0xa70),4);
    const auto linked=reinterpret_cast<uintptr_t>(binding.Entity(linkedHandle));
    if(linked && linked!=portal && Placement(portal,entry) && Placement(linked,exit)
        && Matches(Linkage(entry,exit),toLinked)) {
        result.hasOpening=true;
        result.center=PortalPose::Position(entry);
        result.forward={entry[0][0],entry[1][0],entry[2][0]};
        result.left={entry[0][1],entry[1][1],entry[2][1]};
        result.up={entry[0][2],entry[1][2],entry[2][2]};
    }
    return result;
}

class Scope {
    Frame& current;
    Frame saved;
public:
    Scope(Frame& current,const Frame& next):current(current),saved(current) {current=next;}
    ~Scope() {current=saved;}
    Scope(const Scope&)=delete;
    Scope& operator=(const Scope&)=delete;
};
}
