// Bounded CPU observations for the qualified donor's Blit failure report.
#ifndef kern_tgl_blit_report_hpp
#define kern_tgl_blit_report_hpp
#include "kern_tgl_hang_select.hpp"

namespace TGLBlitReport {
constexpr unsigned Blit = TGLHangSelect::Blit;
constexpr unsigned SnapshotBytes = 512;
constexpr unsigned SlotCount = 32;
constexpr uint32_t OffsetMask = 0x001ffffc;
constexpr uint32_t TailMask = 0x001ffff8;

struct Ring {
 uint64_t context, ring, lrc, descriptor, buffer, cpu, mappedSize;
 uint32_t id, start, tail, length;
};

template<class Read>
bool readField(Read &read,uint64_t address,unsigned offset,void *out,unsigned size) {
 return TGLHangPending::kernelRange(address,offset+size) && read(address+offset,out,size)==size;
}

template<class Read>
bool readRing(Ring &r,uint64_t context,uint64_t owner,Read &read) {
 r={};r.context=context;
 uint64_t contextOwner=0,ringOwner=0,ringContext=0,image=0;
 uint8_t contextEngine=0,ringEngine=0;
 auto get=[&](uint64_t p,unsigned offset,auto &value) {
  return readField(read,p,offset,&value,sizeof(value));
 };
 return get(context,0x50,contextOwner) && contextOwner==owner &&
  get(context,0x6c,contextEngine) && contextEngine==Blit &&
  get(context,0x7c,r.id) && r.id<0x7ff &&
  get(context,0x89,r.descriptor) && (r.descriptor&1) && ((r.descriptor>>37)&0x7ff)==r.id &&
  get(context,0xb0,r.ring) && get(r.ring,0x10,ringOwner) && ringOwner==owner &&
  get(r.ring,0x20,ringContext) && ringContext==context &&
  get(r.ring,0x40,ringEngine) && ringEngine==Blit &&
  get(r.ring,0x18,r.lrc) && get(context,0x98,image) &&
  get(image,0x38,contextOwner) && contextOwner==r.lrc &&
  get(r.lrc,0x101c,r.tail) && get(r.lrc,0x1024,r.start) &&
  r.start && !(r.start&4095) && get(r.ring,0x8c,r.length) &&
  r.length>=4096 && r.length<=32768 && !(r.length&(r.length-1)) &&
  r.tail<r.length && !(r.tail&7) &&
  get(r.ring,0x80,r.buffer) && get(r.buffer,0x20,r.mappedSize) && r.mappedSize>=r.length &&
  get(r.buffer,0x38,r.cpu) && !(r.cpu&4095) && TGLHangPending::kernelRange(r.cpu,r.length);
}

struct Snapshot {
 uint64_t epoch;
 uint32_t id,start,tail,length;
 uint32_t words[SnapshotBytes/4];
};

struct History {
 struct Slot { uint32_t busy;Snapshot saved; };
 Slot slots[SlotCount]{};

 static bool acquire(Slot &slot) {
  uint32_t empty=0;
  return __atomic_compare_exchange_n(&slot.busy,&empty,1U,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
 }
 static void release(Slot &slot) {
  __atomic_store_n(&slot.busy,0U,__ATOMIC_RELEASE);
 }
};

// Read the same inactive bank that the exact donor submits next. Use nofault
// throughout, copy only the bytes preceding its published tail, and never
// retain pointers to those objects after this callback.
template<class Read>
void remember(History &history,uint64_t streamer,uint64_t owner,uint64_t epoch,Read read) {
 uint64_t actualOwner=0,descriptor=0;
 uint32_t bank=0;
 uint8_t engine=0;
 auto get=[&](uint64_t p,unsigned offset,auto &value) {
  return readField(read,p,offset,&value,sizeof(value));
 };
 if(!owner || !get(streamer,0x10,actualOwner) || actualOwner!=owner ||
    !get(streamer,0x28,descriptor) || !get(descriptor,0,engine) || engine!=Blit ||
    !get(streamer,0x50,bank) || bank>1)return;

 for(unsigned index=0;index<2;++index) {
  const auto slotOffset=0x30+(1-bank)*16+index*8;
  uint64_t item=0,context=0;
  uint32_t id=0;
  if(!get(streamer,slotOffset,item) || !get(item,0,context) ||
     !get(context,0x7c,id) || id>=0x7ff)continue;
  auto &slot=history.slots[id%SlotCount];
  if(!History::acquire(slot))continue;
  slot.saved.length=0;
  Ring before{},after{};
  if(readRing(before,context,owner,read) && before.id==id) {
   const auto offset=(before.tail+before.length-SnapshotBytes)&(before.length-1);
   const auto first=before.length-offset<SnapshotBytes ? before.length-offset : SnapshotBytes;
   auto copy=[&](uint32_t *words) {
    return readField(read,before.cpu,offset,words,first) &&
     (first==SnapshotBytes || readField(read,before.cpu,0,words+first/4,SnapshotBytes-first));
   };
   uint32_t check[SnapshotBytes/4];
   uint32_t finalBank=0;
   uint64_t finalItem=0,finalContext=0,finalDescriptor=0;
   if(copy(slot.saved.words) && copy(check) && !memcmp(slot.saved.words,check,sizeof(check)) &&
      get(streamer,0x50,finalBank) && finalBank==bank &&
      get(streamer,0x10,actualOwner) && actualOwner==owner &&
      get(streamer,0x28,finalDescriptor) && finalDescriptor==descriptor &&
      get(descriptor,0,engine) && engine==Blit &&
      get(streamer,slotOffset,finalItem) && finalItem==item &&
      get(item,0,finalContext) && finalContext==context &&
      readRing(after,context,owner,read) && !memcmp(&before,&after,sizeof(before))) {
    slot.saved.epoch=epoch;slot.saved.id=id;slot.saved.start=before.start;
    slot.saved.tail=before.tail;slot.saved.length=before.length;
   }
  }
  History::release(slot);
 }
}

enum class Source : uint32_t { Missing, Receiver, Scheduler, Submission };
struct Record {
 Source source;
 uint32_t guard,id,start,head,tail,mask,ipehr,control;
 uint64_t batch;
 uint32_t words[16];
};

inline void fromSubmission(Record &out,History &history,uint64_t epoch,uint32_t length) {
 if(out.id>=0x7ff || length<4096 || length>32768 || (length&(length-1)) ||
    out.head>=length || out.tail>=length || out.head==out.tail)return;
 const auto distance=(out.tail+length-out.head)&(length-1);
 if(distance>SnapshotBytes)return;
 auto &slot=history.slots[out.id%SlotCount];
 if(!History::acquire(slot))return;
 const auto &s=slot.saved;
 if(s.epoch==epoch && s.id==out.id && s.start==out.start && s.tail==out.tail && s.length==length) {
  const auto first=static_cast<int>(SnapshotBytes-distance)-32;
  for(unsigned i=0;i<16;++i) {
   const auto at=first+static_cast<int>(4*i);
   if(at>=0 && at<static_cast<int>(SnapshotBytes)) {
    out.words[i]=s.words[at/4];
    out.mask|=1U<<i;
   }
  }
  if(out.mask&0x100)out.source=Source::Submission;
 }
 History::release(slot);
}

template<class Read>
Record capture(const TGLHangSelect::Selection &selection,const uint8_t *regs,
               History &history,uint64_t epoch,Read read) {
 if(regs[0x94]!=Blit)return {};
 TGLHangSelect::BlitWindow window{};
 TGLHangSelect::captureBlitWindow(window,selection,regs,read);
 Record out{};
 out.guard=window.selectionStatus;out.id=window.contextID;out.start=window.ringStart;
 out.head=window.head;out.tail=TGLHangPending::u32(regs+4)&TailMask;
 out.ipehr=TGLHangPending::u32(regs+0x7c);
 out.control=TGLHangPending::u32(regs+8);
 out.batch=TGLHangPending::u64(regs+0x70);
 if(window.captured) {
  out.source=window.source==1 ? Source::Receiver : Source::Scheduler;
  out.mask=0xffff;
  memcpy(out.words,window.words,sizeof(out.words));
 } else {
  const auto length=(out.control&0x001ff000)+4096;
  fromSubmission(out,history,epoch,length);
 }
 return out;
}

// A donor report line is limited to 149 bytes. Keep each line short and emit
// missing evidence explicitly. Word 8 is the saved HEAD, mask bit 8 qualifies it.
template<class Log>
void write(const Record &out,Log log) {
 log("WEX BCS v2 source=%u guard=%u ctx=%u start=%08x head=%08x tail=%08x mask=%04x ipehr=%08x\n",
     static_cast<unsigned>(out.source),out.guard,out.id,out.start,out.head,out.tail,out.mask,out.ipehr);
 log("WEX BCS ctrl=%08x bbaddr=%016llx\n",out.control,static_cast<unsigned long long>(out.batch));
 if(out.source==Source::Missing)return;
 for(unsigned i=0;i<16;i+=8)
  log("WEX BCS words %02u: %08x %08x %08x %08x %08x %08x %08x %08x\n",i,
      out.words[i],out.words[i+1],out.words[i+2],out.words[i+3],out.words[i+4],
      out.words[i+5],out.words[i+6],out.words[i+7]);
}
}
#endif
