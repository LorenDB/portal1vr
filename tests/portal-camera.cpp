#include <Windows.h>
#include <cassert>
#include <cstdio>
#include <utility>
#include "sdk/sdk.h"
#include "portalcamera.h"

static void Near(const Vector& a,const Vector& b,float tolerance=.003f) {
    assert((a-b).LengthSqr()<tolerance*tolerance);
}
int main(int argc,char** argv) {
    unsigned cases=0;float previousError=0;
    for(const auto& turn : {QAngle(0,180,0),QAngle(90,0,0),QAngle(-90,83,0),QAngle(0,12,180)}) {
        PortalCamera::Frame camera;
        camera.transformed=true;
        camera.toLinked=PortalPose::Frame({-1200,600,2000},turn);
        for(float pitch:{-89.f,-40.f,0.f,40.f,89.f})
        for(float yaw:{-179.f,-80.f,0.f,80.f,179.f})
        for(float roll:{-90.f,0.f,90.f})
        for(float eye:{-1.4f,1.4f}) {
            const Vector bodyEye(512,120,48),roomscale(12,6,-8);
            const QAngle head(pitch,yaw,roll);
            const auto headFrame=PortalPose::Frame(bodyEye+roomscale,head);
            const Vector side(headFrame[0][1],headFrame[1][1],headFrame[2][1]);
            const Vector entryEye=bodyEye+roomscale+side*eye;
            const Vector nativeEye=camera.Map(bodyEye);
            Near(camera.Unmap(nativeEye),bodyEye);
            const auto entryView=PortalPose::Frame(entryEye,head);
            const auto exitView=PortalPose::Frame(camera.Map(entryEye),camera.Map(head));
            const Vector point=bodyEye+roomscale+Vector(60,-8,-5);
            matrix3x4_t gun=PortalPose::Frame(point,QAngle(15,70,-33));
            const auto reference=HandPose::Concat(HandPose::InverseRigid(entryView),gun);
            camera.Map(&gun,1);
            const auto actual=HandPose::Concat(HandPose::InverseRigid(exitView),gun);
            Near(PortalPose::Position(actual),PortalPose::Position(reference));
            for(int r=0;r<3;++r) for(int c=0;c<3;++c)
                assert(fabsf(actual[r][c]-reference[r][c])<.0001f);
            const auto oldView=PortalPose::Frame(nativeEye+roomscale+side*eye,head);
            const auto old=HandPose::Concat(HandPose::InverseRigid(oldView),gun);
            previousError=std::fmax(previousError,sqrtf((PortalPose::Position(old)-PortalPose::Position(reference)).LengthSqr()));
            // A light/attachment and the gun use the same transform, preserving
            // their relative placement across either eye and every portal axis.
            auto light=PortalPose::Frame(point+Vector(2,3,4),{0,0,0});
            const Vector before=camera.Unmap(PortalPose::Position(gun));
            camera.Map(&light,1);
            Near(camera.Unmap(PortalPose::Position(light))-before,{2,3,4});
            ++cases;
        }
        PortalCamera::Frame current;
        { PortalCamera::Scope outer(current,camera);
          assert(current.transformed);
          {PortalCamera::Scope nested(current,PortalCamera::Frame{});assert(!current.transformed);}
          assert(current.transformed);
        }
        assert(!current.transformed);
    }
    PortalCamera::Frame off;
    Near(off.Map(Vector(1,2,3)),{1,2,3});
    Near(off.Unmap(Vector(1,2,3)),{1,2,3});
    assert(!PortalCamera::Read(nullptr,{},false).transformed);
    assert(previousError>50);
    // Exercise the actual native-state reader with valid handles and the
    // same transformed-eye flag observed during the installed play session.
    unsigned char player[0x1660]{},portal[0xabc]{},simulator=1,flags[2]{};
    uint32_t entities[8]{};
    uintptr_t list=reinterpret_cast<uintptr_t>(entities);
    entities[5]=reinterpret_cast<uintptr_t>(portal);entities[6]=1;
    const uint32_t handle=0x1001;
    memcpy(player+0x1658,&handle,4);player[0x1654]=1;portal[0xab4]=1;
    const uintptr_t simulatorAddress=reinterpret_cast<uintptr_t>(&simulator);
    memcpy(portal+0xab8,&simulatorAddress,4);
    const auto transition=PortalPose::Frame({100,200,300},{90,12,180});
    memcpy(portal+0x864,&transition,sizeof(transition));
    PortalTrace::Binding binding{reinterpret_cast<PortalTrace::TraceFn>(1),&list,flags};
    const auto read=PortalCamera::Read(player,binding,true);
    assert(read.transformed);Near(read.Map(Vector(0,0,0)),{100,200,300});
    player[0x1654]=0;assert(!PortalCamera::Read(player,binding,true).transformed);
    player[0x1654]=1;assert(!PortalCamera::Read(player,binding,false).transformed);
    entities[6]=2;assert(!PortalCamera::Read(player,binding,true).transformed);
    entities[6]=1;portal[0xab4]=0;assert(!PortalCamera::Read(player,binding,true).transformed);
    portal[0xab4]=1;
    const float invalid=NAN;memcpy(portal+0x864,&invalid,4);
    assert(!PortalCamera::Read(player,binding,true).transformed);
    // Each eye decides for itself whether it is through the opening. Give
    // the reader a linked pair whose placements reproduce the engine matrix.
    unsigned openingCases=0;
    for(const auto& pair : {std::pair<QAngle,QAngle>{{0,0,0},{0,90,0}},{{0,135,0},{0,-20,0}},
            {{-90,0,0},{0,180,0}},{{0,45,0},{90,10,0}}}) {
        unsigned char exitPortal[0xabc]{};
        const Vector entryOrigin(256,-128,96),exitOrigin(-900,400,64);
        const auto entry=PortalPose::Frame(entryOrigin,pair.first);
        const auto exit=PortalPose::Frame(exitOrigin,pair.second);
        const auto linkage=PortalCamera::Linkage(entry,exit);
        memcpy(portal+0x864,&linkage,sizeof(linkage));
        memcpy(portal+0x25c,&entryOrigin,12);memcpy(portal+0x268,&pair.first,12);
        memcpy(exitPortal+0x25c,&exitOrigin,12);memcpy(exitPortal+0x268,&pair.second,12);
        entities[1]=reinterpret_cast<uintptr_t>(exitPortal);entities[2]=7;
        const uint32_t linkedHandle=7u<<12;
        memcpy(portal+0xa70,&linkedHandle,4);
        player[0x1654]=0;
        const auto opening=PortalCamera::Read(player,binding,true);
        assert(opening.hasOpening && !opening.transformed);
        const Vector forward(entry[0][0],entry[1][0],entry[2][0]),left(entry[0][1],entry[1][1],entry[2][1]),
            up(entry[0][2],entry[1][2],entry[2][2]);
        const Vector exitForward(exit[0][0],exit[1][0],exit[2][0]);
        // The pair's matrix carries the entry opening onto the exit opening,
        // facing the other way.
        PortalCamera::Frame through=opening;through.transformed=true;
        Near(through.Map(entryOrigin),exitOrigin);
        for(float across:{-30.f,0.f,30.f}) for(float along:{-50.f,0.f,50.f}) for(float depth:{.01f,3.f,20.f}) {
            const Vector inFront=entryOrigin+left*across+up*along+forward*depth;
            const Vector behind=entryOrigin+left*across+up*along-forward*depth;
            assert(!opening.Through(inFront) && opening.Through(behind));
            // An eye in front of the entry portal stays where it is; one that
            // has passed the plane is drawn that far in front of the exit.
            const auto front=opening.For(inFront),back=opening.For(behind);
            assert(!front.transformed && back.transformed);
            Near(front.Map(inFront),inFront);
            const Vector mapped=back.Map(behind);
            const Vector fromExit=mapped-exitOrigin;
            const float exitDepth=fromExit.x*exitForward.x+fromExit.y*exitForward.y+fromExit.z*exitForward.z;
            assert(fabsf(exitDepth-depth)<.01f);
            // The engine's own decision does not matter either way.
            PortalCamera::Frame engineSaysThrough=opening;engineSaysThrough.transformed=true;
            assert(!engineSaysThrough.For(inFront).transformed && engineSaysThrough.For(behind).transformed);
            openingCases+=2;
        }
        // Beside the opening, or far behind it, is a wall and not a crossing.
        assert(!opening.Through(entryOrigin+left*(PortalCamera::HalfWidth+PortalCamera::OpeningMargin+1)-forward*2));
        assert(!opening.Through(entryOrigin+up*(PortalCamera::HalfHeight+PortalCamera::OpeningMargin+1)-forward*2));
        assert(!opening.Through(entryOrigin-forward*(PortalCamera::MaxDepth+1)));
        // Placements that do not reproduce the engine's matrix are not trusted:
        // the engine's decision for its own eye stands.
        const Vector moved=exitOrigin+Vector(40,0,0);
        memcpy(exitPortal+0x25c,&moved,12);
        auto untrusted=PortalCamera::Read(player,binding,true);
        assert(!untrusted.hasOpening && !untrusted.For(entryOrigin-forward*5).transformed);
        player[0x1654]=1;untrusted=PortalCamera::Read(player,binding,true);
        assert(!untrusted.hasOpening && untrusted.For(entryOrigin+forward*5).transformed);
        memcpy(exitPortal+0x25c,&exitOrigin,12);
        // A portal cannot be its own exit, and a stale handle is no exit.
        entities[2]=8;assert(!PortalCamera::Read(player,binding,true).hasOpening);
        entities[2]=7;entities[1]=reinterpret_cast<uintptr_t>(portal);
        assert(!PortalCamera::Read(player,binding,true).hasOpening);
    }
    assert(openingCases==4*54);
    if(argc==2) {
        const auto client=LoadLibraryExA(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        assert(client && PortalCamera::Supported(reinterpret_cast<uintptr_t>(client)));
        assert(PortalTrace::Binding::Resolve(reinterpret_cast<uintptr_t>(client)).function);
        // The opening reader's offsets in the installed client: CalcPortalView
        // loads the linked-portal handle from portal+0xa70, and the absolute
        // origin/angle getters return entity+0x25c and entity+0x268.
        const auto base=reinterpret_cast<const unsigned char*>(client);
        const unsigned char linkedLoad[]={0x8b,0x8f,0x70,0x0a,0,0,0x83,0xf9,0xff};
        const unsigned char originGetter[]={0x8d,0x86,0x5c,0x02,0,0,0x5e,0xc3};
        const unsigned char anglesGetter[]={0x8d,0x86,0x68,0x02,0,0,0x5e,0xc3};
        assert(!memcmp(base+0x228768,linkedLoad,sizeof(linkedLoad)));
        assert(!memcmp(base+0xb5c38,originGetter,sizeof(originGetter)));
        assert(!memcmp(base+0xb5c28,anglesGetter,sizeof(anglesGetter)));
        FreeLibrary(client);
    }
    printf("PASS: %u portal stereo camera/model/light cases; %u per-eye opening cases; old mixed-space error=%f; native ABI=%s\n",
        cases,openingCases,previousError,argc==2?"verified":"not supplied");
}
