// A native dump may pair one engine's registers with another engine's ring.
// Select only a unique, owned context matching the saved engine and ID.
#ifndef kern_tgl_hang_select_hpp
#define kern_tgl_hang_select_hpp
#include "kern_tgl_hang_pending.hpp"
namespace TGLHangSelect {
struct Selection { uint64_t q[8]; }; // schema, guard, origin, streamer, ring, context, ID, source
constexpr uint8_t Render = 0;
constexpr uint8_t Blit = 2;
template<class Read>
void select(Selection &s,uint64_t origin,uint64_t streamer,const uint8_t *regs,Read read) {
 s={};s.q[0]=1;s.q[1]=1;s.q[2]=origin;s.q[3]=streamer;
 s.q[6]=(TGLHangPending::u32(regs+0xac)>>5)&2047;
 const auto engine=regs[0x94];
 const auto ringStart=TGLHangPending::u32(regs+12);
 auto get=[&](uint64_t p,uint64_t offset,uint64_t &v,unsigned n=8) {
  v=0;return TGLHangPending::kernelRange(p,offset+n) && read(p+offset,&v,n)==n;
 };
 uint64_t owner=0,context=0;
 // A stopped Blit engine can retain HEAD/START after its valid bit clears.
 if((engine!=Render && engine!=Blit) ||
    (engine==Render && !(TGLHangPending::u32(regs+8)&1)) || !ringStart ||
    !get(origin,0x10,owner) || !owner)return;
 auto matches=[&](uint64_t ctx,uint64_t ring)->bool {
  uint64_t id=0,ctxEngine=0,o=0,back=0,rctx=0,re=0,ro=0;
  if(!(get(ctx,0x7c,id,4) && id==s.q[6] && get(ctx,0x6c,ctxEngine,1) && ctxEngine==engine &&
   get(ctx,0x50,o) && o==owner && get(ctx,0xb0,back) && back==ring &&
   get(ring,0x20,rctx) && rctx==ctx && get(ring,0x40,re,1) && re==engine &&
   get(ring,0x10,ro) && ro==owner))return false;
  if(engine==Render)return true;
  uint64_t lrc=0,start=0;
  return get(ring,0x18,lrc) && get(lrc,0x1024,start,4) && start==ringStart;
 };
 if(get(origin,0x20,context) && matches(context,origin)) {
  s.q[4]=origin;s.q[5]=context;s.q[7]=1;s.q[1]=0;return;
 }
 uint64_t so=0,slots[4]{},again[4]{},items[4]{};
 s.q[1]=2;
 if(!get(streamer,0x10,so) || so!=owner ||
    !TGLHangPending::kernelRange(streamer,0x50) || read(streamer+0x30,slots,32)!=32)return;
 if(engine==Blit) {
  uint64_t descriptor=0,type=0;
  if(!get(streamer,0x28,descriptor) || !get(descriptor,0,type,1) || type!=Blit)return;
 }
 for(unsigned i=0;i<4;++i) {
  if(!slots[i])continue;
  uint64_t ctx=0,id=0,ring=0;
  s.q[1]=3;if(!get(slots[i],0,ctx))return;items[i]=ctx;
  if(!get(ctx,0x7c,id,4))return;
  if(id!=s.q[6])continue;
  if(!get(ctx,0xb0,ring) || !matches(ctx,ring))return;
  s.q[1]=4;if(s.q[4] && (s.q[4]!=ring || s.q[5]!=ctx))return;
  s.q[4]=ring;s.q[5]=ctx;
 }
 s.q[1]=5;if(!s.q[4])return;
 s.q[1]=6;
 if(read(streamer+0x30,again,32)!=32 || memcmp(slots,again,32) ||
    !get(streamer,0x10,so) || so!=owner || !get(origin,0x10,so) || so!=owner)return;
 for(unsigned i=0;i<4;++i)if(slots[i]) {
  uint64_t ctx=0;if(!get(slots[i],0,ctx) || ctx!=items[i])return;
 }
 if(!matches(s.q[5],s.q[4]))return;
 s.q[7]=2;s.q[1]=0;
}

struct BlitWindow {
 uint32_t version, selectionStatus, source, contextID, ringStart, head, captured;
 uint32_t words[16]; // Eight DWORDs before HEAD, then eight starting at HEAD.
};

inline bool nativeBlitDumpAllowed(const Selection &selection) {
 return !selection.q[1] && selection.q[7]==1;
}

// A scheduler slot gives an observation, never a pinned ring object. Use only
// nofault copies and publish command bytes only if ownership and data agree.
template<class Read>
void captureBlitWindow(BlitWindow &out,const Selection &s,const uint8_t *regs,Read read) {
 out={};
 out.version=1;out.selectionStatus=static_cast<uint32_t>(s.q[1]);
 out.source=static_cast<uint32_t>(s.q[7]);out.contextID=static_cast<uint32_t>(s.q[6]);
 out.ringStart=TGLHangPending::u32(regs+12);out.head=TGLHangPending::u32(regs)&0x1ffffc;
 if(s.q[1] || regs[0x94]!=Blit)return;
 auto get=[&](uint64_t p,unsigned offset,void *out,unsigned n) {
  return TGLHangPending::kernelRange(p,offset+n) && read(p+offset,out,n)==n;
 };
 uint64_t buffer=0,cpu=0,size=0;
 uint32_t length=0;
 if(!get(s.q[4],0x80,&buffer,8) || !get(s.q[4],0x8c,&length,4) ||
    length<4096 || length>32768 || (length&(length-1)) || out.head>=length ||
    !get(buffer,0x20,&size,8) || size<length ||
    !get(buffer,0x38,&cpu,8) || (cpu&4095) || !TGLHangPending::kernelRange(cpu,length))return;
 uint32_t first[16]{},second[16]{};
 auto copy=[&](uint32_t (&words)[16]) {
  for(unsigned i=0;i<16;++i) {
   const auto offset=(out.head+length-32+4*i)&(length-1);
   if(!get(cpu,offset,words+i,4))return false;
  }
  return true;
 };
 if(!copy(first) || !copy(second) || memcmp(first,second,sizeof(first)))return;
 Selection after{};
 uint64_t finalBuffer=0,finalCPU=0,finalSize=0;
 uint32_t finalLength=0;
 select(after,s.q[2],s.q[3],regs,read);
 if(memcmp(&s,&after,sizeof(s)) ||
    !get(s.q[4],0x80,&finalBuffer,8) || finalBuffer!=buffer ||
    !get(s.q[4],0x8c,&finalLength,4) || finalLength!=length ||
    !get(buffer,0x38,&finalCPU,8) || finalCPU!=cpu ||
    !get(buffer,0x20,&finalSize,8) || finalSize!=size)return;
 memcpy(out.words,first,sizeof(first));out.captured=1;
}
}
#endif
