// Offline camera take. Same renderer, shaders, seeded world and controller as the game.
#include "maze.h"
#include "light.h"
#include <cstdio>
#include <string>
int main(int argc,char** argv) {
    if(argc!=3){ std::fprintf(stderr,"rain_capture shader-directory existing-frame-directory\n"); return 2; }
    Renderer r; SkyState sky; sky.sunDir=normalize({.55f,.42f,-.72f});
    if(!r.init(nullptr,640,360,argv[1],std::atan2(sky.sunDir.z,sky.sunDir.x))) return 1;
    MeshData world; std::vector<AABB> solids;
    auto maze=buildMaze(42,9,9,world,solids);
    Player p; p.pos=maze.spawn; p.yaw=maze.spawnYaw; p.pitch=radians(-15);
    // Frame the longest eastward corridor rather than pointing into the spawn wall.
    int longest=0;
    for(int i=0;i<81;++i){ int n=0; while(maze.passages[i+n]&1) ++n;
        if(n>longest){longest=n; p.pos=maze.center(i); p.yaw=radians(89);}}

    Platform input; sky.storm=sky.rain=1; sky.coverage=.92f; sky.cloudBase=640; sky.exposure=.52f;
    for(int i=0;i<96;++i){
        sky.time=i/24.f; sky.flashlight=i>=24; sky.lightDir=p.forward();
        sky.wetness=clampf(.35f+sky.time*.22f,0,1);
        input.keys['W']=i>36 && i<80;
        for(int step=0;step<3;++step) p.update(input,solids,1.f/72);
        CameraState c; c.pos=p.eye(); c.yaw=p.yaw; c.pitch=p.pitch;
        prepareLamp(sky,c.pos,solids);
        if(!r.beginFrame(640,360,c,sky)) return 1;
        r.drawWorld(world); char name[32]; std::snprintf(name,sizeof(name),"/%03d.bmp",i);
        r.requestScreenshot((std::string(argv[2])+name).c_str()); r.endFrame();
    }
    r.shutdown();
}
