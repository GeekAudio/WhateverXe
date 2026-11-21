#include "kern_tgl_hang_select.hpp"
#include "kern_tgl_blit_report.hpp"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
#include <vector>
using namespace TGLHangSelect;
namespace {
constexpr uint64_t K=0xffff800000000000ULL,Origin=K+0x1000,Other=K+0x2000,
 Stream=K+0x3000,Item=K+0x4000,Ctx=K+0x5000,Ring=K+0x6000,
 Item2=K+0x7000,Ctx2=K+0x8000,Ring2=K+0x9000,Owner=K+0x10000,
 Descriptor=K+0x11000,LRC=K+0x12000,Buffer=K+0x14000,Commands=K+0x15000,
 LRCBuffer=K+0x16000;
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
 void submittedBlit(uint32_t tail=0x228) {
  blit();
  mem[LRCBuffer].resize(4096);
  put(Ctx+0x89,uint64_t(6)<<37 | 0x40100319);
  put(Ctx+0x98,LRCBuffer);put(LRCBuffer+0x38,LRC);
  put(LRC+0x101c,tail);
  // Native submit reads the inactive bank (0 here). Bank +4 need not be zero.
  put(Stream+0x50,uint32_t(1));put(Stream+0x54,uint32_t(0xfeed));
  const uint32_t head=(tail+4096-60)&4095, control=0, ipehr=0x0e009002;
  const uint32_t packet[]{ipehr,0x22,0x40000034,0};
  for(unsigned i=0;i<4;++i)put(Commands+((head+4*i)&4095),packet[i]);
  memcpy(regs,&head,4);memcpy(regs+4,&tail,4);memcpy(regs+8,&control,4);
  memcpy(regs+0x7c,&ipehr,4);
 }
 void forgetSlots() {
  put(Stream+0x30,uint64_t(0));put(Stream+0x40,uint64_t(0));
 }
};

void persistentBlitReports() {
 using namespace TGLBlitReport;
 Fixture f;
 f.submittedBlit();
 History history{};
 auto read=[&](uint64_t p,void *o,unsigned n){return f.read(p,o,n);};
 remember(history,Stream,Owner,4,read);
 auto live=capture(f.run(),f.regs,history,4,read);
 assert(live.source==Source::Scheduler && live.words[8]==0x0e009002 && live.mask==0xffff);
 f.forgetSlots();
 // The old failure-only path must miss. The persisted report may use the
 // earlier bank snapshot without reading memory from the departed context.
 auto absent=f.run();
 assert(absent.q[1]==5);
 f.mem.erase(Ctx);f.mem.erase(Commands);
 const auto recorded=capture(absent,f.regs,history,4,read);
 assert(recorded.source==Source::Submission && recorded.guard==5 &&
        recorded.head==0x1ec && recorded.tail==0x228 && recorded.mask==0xffff);
 assert(recorded.words[8]==0x0e009002 && recorded.words[9]==0x22 && recorded.words[10]==0x40000034);
 assert(capture(absent,f.regs,history,5,read).source==Source::Missing);
 f.regs[4]^=8;
 assert(capture(absent,f.regs,history,4,read).source==Source::Missing);
 f.regs[4]^=8;
 f.regs[13]^=0x10;
 assert(capture(absent,f.regs,history,4,read).source==Source::Missing);
 f.regs[13]^=0x10;
 history.slots[6].busy=1;
 assert(capture(absent,f.regs,history,4,read).source==Source::Missing);
 history.slots[6].busy=0;
 std::vector<std::string> lines;
 auto log=[&](const char *format,auto... args) {
  char line[150];
  auto n=snprintf(line,sizeof(line),format,args...);
  assert(n>0 && n<150);
  lines.emplace_back(line);
 };
 write(recorded,log);
 assert(lines.size()==4 && lines[0].find("source=3 guard=5 ctx=6")!=std::string::npos &&
        lines[3].find("08: 0e009002 00000022 40000034 00000000")!=std::string::npos);
 lines.clear();
 write(capture(absent,f.regs,history,5,read),log);
 assert(lines.size()==2 && lines[0].find("source=0")!=std::string::npos);

 Fixture wrap;wrap.submittedBlit(0x20);
 auto wrapRead=[&](uint64_t p,void *o,unsigned n){return wrap.read(p,o,n);};
 remember(history,Stream,Owner,6,wrapRead);
 wrap.forgetSlots();
 auto wrapped=capture(wrap.run(),wrap.regs,history,6,wrapRead);
 assert(wrapped.source==Source::Submission && wrapped.head==0xfe4 && wrapped.words[8]==0x0e009002);
 // Partial windows must mark the end of the published command stream.
 const uint32_t nearTail=0x18;
 memcpy(wrap.regs,&nearTail,4);
 wrapped=capture(wrap.run(),wrap.regs,history,6,wrapRead);
 assert(wrapped.source==Source::Submission && wrapped.mask==0x03ff);

 Fixture other;other.submittedBlit();
 other.put(Ctx+0x89,uint64_t(9)<<37 | 0x40100319);
 auto otherRead=[&](uint64_t p,void *o,unsigned n){return other.read(p,o,n);};
 remember(history,Stream,Owner,8,otherRead);
 other.forgetSlots();
 assert(capture(other.run(),other.regs,history,8,otherRead).source==Source::Missing);

 Fixture drift;drift.submittedBlit();
 unsigned copies=0;
 auto driftRead=[&](uint64_t p,void *o,unsigned n) {
  if(p>=Commands && p<Commands+4096 && ++copies==2)drift.put(Commands+0x1ec,uint32_t(0));
  return drift.read(p,o,n);
 };
 remember(history,Stream,Owner,7,driftRead);
 drift.forgetSlots();
 assert(capture(drift.run(),drift.regs,history,7,driftRead).source==Source::Missing);
}
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
 persistentBlitReports();
}
