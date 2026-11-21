#include "maze.h"
#include <cassert>
int main(){
    for(unsigned seed=0;seed<1000;++seed){
        MeshData m; std::vector<AABB> boxes; auto maze=buildMaze(seed,9,9,m,boxes);
        assert(maze.relay!=0 && maze.cellAt(maze.spawn)==0);
        for(int i=0;i<81;++i){
            int id=i; for(int n=0;id!=maze.relay;++n){
                assert(n<81); int next=maze.nextRelay[id]; assert(next>=0 && next<81);
                int delta=next-id,dir=delta==1?0:delta==-1?1:delta==9?2:3;
                assert(maze.passages[id]&(1<<dir)); id=next;
            }
        }
    }
    Player p; Platform in; std::vector<AABB> boxes={{{-3,-1,-3},{3,0,3}}};
    in.keys['C']=true; for(int i=0;i<60;++i)p.update(in,boxes,1.f/60);
    boxes.push_back({{-2,1.3f,-2},{2,2,2}}); in.keys['C']=false;
    for(int i=0;i<60;++i)p.update(in,boxes,1.f/60);
    assert(p.grounded && p.height()<1.3f); boxes.pop_back();
    for(int i=0;i<60;++i)p.update(in,boxes,1.f/60);
    assert(p.height()>1.79f);
    boxes.push_back({{1,0,-3},{2,3,3}}); boxes.push_back({{-3,0,-2},{3,3,-1}});
    in.keys['W']=in.keys['D']=true;
    for(int i=0;i<120;++i)p.update(in,boxes,1.f/60);
    assert(p.pos.x<=.701f && p.pos.z>=-.701f && p.grounded);

}
