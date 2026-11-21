#include "kern_tgl_hang_select.hpp"
#include <cassert>
#include <map>
#include <vector>
using namespace TGLHangSelect;
namespace {
constexpr uint64_t K=0xffff800000000000ULL,Origin=K+0x1000,Other=K+0x2000,
 Stream=K+0x3000,Item=K+0x4000,Ctx=K+0x5000,Ring=K+0x6000,
 Item2=K+0x7000,Ctx2=K+0x8000,Ring2=K+0x9000,Owner=K+0x10000,
 Descriptor=K+0x11000,LRC=K+0x12000,Buffer=K+0x14000,Commands=K+0x15000;
struct Fixture {
 std::map<uint64_t,std::vector<uint8_t>> mem;uint8_t regs[176]{};
 unsigned slotReads{};bool changeSlots{},changeOwner{},changeItem{},missingID{};
 template<class T>void put(uint64_t p,T v){auto it=mem.upper_bound(p);assert(it!=mem.begin());--it;assert(p-it->first+sizeof(v)<=it->second.size());memcpy(it->second.data()+p-it->first,&v,sizeof(v));}
 void pair(uint64_t ctx,uint64_t ring,uint32_t id,uint8_t engine) {
  put(ctx+0x50,Owner);put(ctx+0x7c,id);put(ctx+0x6c,engine);put(ctx+0xb0,ring);
  put(ring+0x10,Owner);put(ring+0x20,ctx);put(ring+0x40,engine);
 }
 Fixture() {
  for(auto p:{Origin,Other,Stream,Item,Ctx,Ring,Item2,Ctx2,Ring2})mem[p].resize(4096);
  pair(Other,Origin,145,1);pair(Ctx,Ring,6,0);pair(Ctx2,Ring2,6,0);
  put(Stream+0x10,Owner);put(Stream+0x30,Item);put(Stream+0x40,Item);put(Item,Ctx);put(Item2,Ctx2);
  uint32_t id=6<<5,ctrl=1,start=0x40000000;memcpy(regs+0xac,&id,4);memcpy(regs+8,&ctrl,4);memcpy(regs+12,&start,4);
 }
 uint64_t read(uint64_t p,void *out,unsigned n) {
  if(missingID && p==Ctx+0x7c)return 0;
  if(p==Stream+0x30 && ++slotReads==2){if(changeSlots)put(Stream+0x40,uint64_t(0));if(changeOwner)put(Ctx+0x50,Owner+8);if(changeItem)put(Item,Ctx2);}
  auto it=mem.upper_bound(p);if(it==mem.begin())return 0;--it;
  if(p-it->first+n>it->second.size())return 0;memcpy(out,it->second.data()+p-it->first,n);return n;
 }
 Selection run(uint64_t origin=Origin){Selection s{};select(s,origin,Stream,regs,[&](uint64_t p,void *o,unsigned n){return read(p,o,n);});return s;}
 void blit() {
  for(auto p:{Descriptor,Buffer,Commands})mem[p].resize(4096);
  mem[LRC].resize(8192);
  // The report was for an inactive BCS engine while callback 'this' was RCS.
  regs[0x94]=2;regs[8]=0;
  pair(Other,Origin,6,0);pair(Ctx,Ring,6,2);
  put(Stream+0x28,Descriptor);put(Descriptor,uint8_t(2));
  put(Ring+0x18,LRC);put(LRC+0x1024,uint32_t(0x40000000));
  put(Ring+0x80,Buffer);put(Ring+0x8c,uint32_t(4096));
  put(Buffer+0x20,uint64_t(4096));put(Buffer+0x38,Commands);
  put(Commands+4064,uint32_t(0xa5a5a5a5));put(Commands,uint32_t(0x0e009002));
 }
 BlitWindow window(const Selection &s,bool drift=false) {
  unsigned headReads=0;BlitWindow out{};
  captureBlitWindow(out,s,regs,[&](uint64_t p,void *o,unsigned n) {
   if(drift && p==Commands && ++headReads==2)put(Commands,uint32_t(0));
   return read(p,o,n);
  });
  return out;
 }
};
}
int main(){
 {Fixture f;auto s=f.run();assert(!s.q[1] && s.q[4]==Ring && s.q[5]==Ctx && s.q[7]==2);}
 {Fixture f;auto s=f.run(Ring);assert(!s.q[1] && s.q[7]==1);}
 {Fixture f;f.put(Stream+0x40,Item2);assert(f.run().q[1]==4);}
 {Fixture f;f.put(Ctx+0x7c,uint32_t(7));assert(f.run().q[1]==5);}
 {Fixture f;f.put(Stream+0x10,Owner+8);assert(f.run().q[1]==2);}
 {Fixture f;f.put(Ctx+0x50,Owner+8);assert(f.run().q[1]==3);}
 {Fixture f;f.put(Ring+0x20,Ctx2);assert(f.run().q[1]==3);}
 {Fixture f;f.put(Ring+0x40,uint8_t(1));assert(f.run().q[1]==3);}
 {Fixture f;f.put(Ctx+0x6c,uint8_t(1));assert(f.run().q[1]==3);}
 {Fixture f;f.changeSlots=true;assert(f.run().q[1]==6);}
 {Fixture f;f.changeOwner=true;assert(f.run().q[1]==6);}
 {Fixture f;f.changeItem=true;assert(f.run().q[1]==6);}
 {Fixture f;f.missingID=true;assert(f.run().q[1]==3);}
 {Fixture f;f.regs[0x94]=1;assert(f.run().q[1]==1);}
 {Fixture f;f.regs[8]=0;assert(f.run().q[1]==1);}
 {Fixture f;memset(f.regs+12,0,4);assert(f.run().q[1]==1);}
 {Fixture f;f.blit();auto s=f.run();
  assert(!s.q[1] && s.q[4]==Ring && s.q[7]==2 && !nativeBlitDumpAllowed(s));
  auto w=f.window(s);
  assert(w.captured && w.head==0 && w.words[0]==0xa5a5a5a5 && w.words[8]==0x0e009002);
  assert(!f.window(s,true).captured);}
 {Fixture f;f.blit();assert(nativeBlitDumpAllowed(f.run(Ring)));}
 {Fixture f;f.blit();f.put(LRC+0x1024,uint32_t(0x40001000));
  auto s=f.run();assert(s.q[1] && !nativeBlitDumpAllowed(s) && !f.window(s).captured);}
 {Fixture f;f.blit();f.put(Descriptor,uint8_t(0));assert(f.run().q[1]==2);}
 {Fixture f;f.blit();f.put(Stream+0x30,uint64_t(0));f.put(Stream+0x40,uint64_t(0));
  auto s=f.run();assert(s.q[1]==5 && !nativeBlitDumpAllowed(s) && !f.window(s).captured);}
 {Fixture f;f.blit();auto s=f.run();f.put(Buffer+0x20,uint64_t(4095));assert(!f.window(s).captured);}
 {Fixture f;f.blit();auto s=f.run();f.put(Ctx+0x50,Owner+8);assert(!f.window(s).captured);}
}
