#!/usr/bin/env python3
"""Replay common-before-backend installation and the mandatory start boundary."""
from pathlib import Path
import re,subprocess,tempfile,sys
r=Path(__file__).resolve().parents[1];s=(r/'WhateverXe/kern_xe.cpp').read_text()
names=['processFramebufferKext','processFramebufferCommon','wrapFramebufferStart']
f='\n'.join(re.search(r'bool XE::'+n+r'\(.*?\n\}',s,re.S).group() for n in names)
# Ownership and repeated installation are checked below by executing the
# installer and its start gate. Textual symbol/call counts are not behavior.
code=r'''
#include <cassert>
#include "kern_xelp_combo_clock.hpp"
struct {void *lock=(void*)1;XELPComboClock::ClockAllocation allocation;} comboClockTransaction;
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <fstream>
#include <iterator>
#include <cstdio>
#include "kern_xelp_donor.hpp"
#include "kern_xelp_os.hpp"
using mach_vm_address_t=uintptr_t;
void IORecursiveLockLock(void*){}void IORecursiveLockUnlock(void*){}
struct IOService {void setProperty(const char*,bool) {}};
struct IORegistryEntry {
 static bool headlessPMPropertySeen,headlessPMPropertyValue;
 static IORegistryEntry *getRegistryRoot(){static IORegistryEntry r;return &r;}
 void setProperty(const char *name,bool value) {
  if(!strcmp(name,"ADLSHeadlessPMNotificationPatched")) {
   headlessPMPropertySeen=true;headlessPMPropertyValue=value;
  }
 }
 template<class...T>void setProperty(T...){}
};
bool IORegistryEntry::headlessPMPropertySeen=false;
bool IORegistryEntry::headlessPMPropertyValue=false;
bool surfaceOK=true,nvramOK=true,tglSonomaSurfaceFramebufferReady=false;
bool headlessPMOptIn=false;
const char *version="Darwin Kernel Version 25.6.0: Tue Aug 18 17:46:40 PDT 2026; root:xnu-12377.161.15.700.19~2/RELEASE_X86_64";
bool checkKernelArgument(const char *name) {
 return headlessPMOptIn && !strcmp(name,"-wex-adls-headless-pm");
}
uintptr_t offset(const char *s){
 if(strstr(s,"5start"))return 0x3f1fc;
 if(strstr(s,"__ZTV"))return 0xccad0;
 if(strstr(s,"4init"))return 0x29ec8;
 assert(strstr(s,"computeLaneCount"));return 0x1fc22;
}
struct KernelPatcher {
 enum class Error {NoError,Failed};static constexpr unsigned KernelID=999;
 struct RouteRequest {const char *name;uintptr_t *original;
  template<class F>RouteRequest(const char*s,F,uintptr_t &o):name(s),original(&o) {}};
 unsigned attempts=0,failAt=0;bool error=false;
 template<class T=uintptr_t>T solveSymbol(size_t,const char *s,uintptr_t a=0,size_t=0){return a? a+offset(s):0x1234;}
 bool routeMultiple(size_t,RouteRequest *rs,unsigned count,uintptr_t a,size_t){
  for(unsigned i=0;i<count;++i){++attempts;if(attempts==failAt){error=true;return false;}
   *rs[i].original=a+offset(rs[i].name);*reinterpret_cast<uint8_t *>(*rs[i].original)=0xcc;}
  return true;
 }
 bool routeMultipleShort(size_t n,RouteRequest *r,unsigned c,uintptr_t a,size_t z){return routeMultiple(n,r,c,a,z);}
 void routeBlock(uintptr_t at,const uint8_t *data,size_t size){++attempts;if(attempts==failAt){error=true;return;}memcpy((void*)at,data,size);}
 Error getError(){return error?Error::Failed:Error::NoError;}void clearError(){error=false;}
};
bool tglInstallSonomaSurfaceABI(KernelPatcher&,size_t,uintptr_t,size_t,bool){return surfaceOK;}
bool xelpInstallNVRAMABI(KernelPatcher&,size_t,uintptr_t,size_t){return nvramOK;}
unsigned shutdownCalls=0;
struct XE {
 bool framebufferDonorAttempted=false,framebufferDonorCommonReady=false,framebufferBackendInstalled=false;
 bool display13FramebufferRequested=false,tglPlatformNVRAMReady=false,tglProbeReady=false,tglFramebufferInitReady=false;
 void *tglPathPoolLock=(void*)1,*framebufferStartedController=nullptr;bool framebufferStartInProgress=false,tglLeasesStopping=false;
 bool runtimeOK=true,agdcOK=true,backendOK=true,trainingOK=true,framebufferTrainingHooksReady=false;
 bool adlsDisplaySelected=false;
 uintptr_t orgFramebufferStart=0,orgFramebufferInit=0,orgFramebufferComputeLanes=0,framebufferLaneCaller=0;
 uintptr_t xelpProviderProbe=0x2000,xelpProviderConfigureProbe=0x3000;
 unsigned commonRuntimeCalls=0,backendCalls=0,startCalls=0;bool stopDuringStart=false,nativeOK=true;
 bool processFramebufferKext(KernelPatcher&,size_t,uintptr_t,size_t);
 bool processDisplay13Shutdown(KernelPatcher&,size_t,uintptr_t,size_t){++shutdownCalls;return true;}
 bool processFramebufferCommon(KernelPatcher&,size_t,uintptr_t,size_t);
 bool processFramebufferDonorRuntime(KernelPatcher&,size_t,uintptr_t,size_t){++commonRuntimeCalls;return runtimeOK;}
 bool processFramebufferAGDC(KernelPatcher&,size_t,uintptr_t,size_t){return agdcOK;}
 bool processFramebufferTraining(KernelPatcher&,size_t,uintptr_t,size_t){return trainingOK;}
 bool backend(KernelPatcher&,size_t,uintptr_t,size_t){++backendCalls;
  assert(framebufferDonorAttempted && orgFramebufferStart && commonRuntimeCalls==1);
  IOService c;assert(!wrapFramebufferStart(&c,&c));return backendOK;}
 bool processDisplay13Framebuffer(KernelPatcher&p,size_t i,uintptr_t a,size_t z){assert(display13FramebufferRequested);return backend(p,i,a,z);}
 bool processKextFull(KernelPatcher&p,size_t i,uintptr_t a,size_t z){assert(!display13FramebufferRequested);return backend(p,i,a,z);}
 static bool wrapFramebufferStart(IOService*,IOService*);
 static bool wrapDisplay13FramebufferStart(IOService*,IOService*);
 static bool wrapTGLFramebufferStart(IOService*,IOService*);
 static void wrapDisplay13ProbeMode(){}static void wrapTGLProbePort(){}static void wrapFramebufferInit(){}static void wrapFramebufferComputeLanes(){}
};
XE *callbackXE;
bool XE::wrapDisplay13FramebufferStart(IOService*,IOService*){assert(callbackXE->display13FramebufferRequested);assert(!callbackXE->framebufferStartedController);if(callbackXE->stopDuringStart)callbackXE->tglLeasesStopping=true;++callbackXE->startCalls;return callbackXE->nativeOK;}
bool XE::wrapTGLFramebufferStart(IOService*,IOService*){assert(!callbackXE->display13FramebufferRequested);assert(!callbackXE->framebufferStartedController);if(callbackXE->stopDuringStart)callbackXE->tglLeasesStopping=true;++callbackXE->startCalls;return callbackXE->nativeOK;}
static void checkHeadlessPMCase(const std::vector<uint8_t> &original,bool optIn,bool adls,
                                const char *kernel,bool exact,bool routeFailure,bool patched) {
 using namespace XELPDonor::HeadlessPM;
 auto image=original;
 if(!exact)image[Site]^=1;
 if(exact)assert(matches(image.data(),image.size()));
 else assert(!matches(image.data(),image.size()));
 headlessPMOptIn=optIn;version=kernel;
 IORegistryEntry::headlessPMPropertySeen=false;IORegistryEntry::headlessPMPropertyValue=false;
 shutdownCalls=0;
 uintptr_t pure=0x1234;memcpy(image.data()+0xccad0+0x188,&pure,sizeof(pure));
 XE x;callbackXE=&x;x.adlsDisplaySelected=adls;KernelPatcher patcher;
 if(routeFailure)patcher.failAt=2; // gate hook succeeds; the PM branch write fails
 const bool handled=x.processFramebufferKext(patcher,1,reinterpret_cast<uintptr_t>(image.data()),image.size());
 assert(handled && x.framebufferDonorCommonReady && x.framebufferBackendInstalled && x.backendCalls==1);
 assert(shutdownCalls==0);
 assert(IORegistryEntry::headlessPMPropertySeen==optIn);
 if(optIn)assert(IORegistryEntry::headlessPMPropertyValue==patched);
 assert((memcmp(image.data()+Branch,FallThrough,sizeof(FallThrough))==0)==patched);
 if(patched) {
  constexpr size_t after=Branch-Site+sizeof(FallThrough);
  assert(!memcmp(image.data()+Site+after,Before+after,sizeof(Before)-after));
 } else if(exact) assert(!memcmp(image.data()+Site,Before,sizeof(Before)));
 if(routeFailure)assert(patcher.attempts>=2);
}
FUNCTIONS
int main(int argc,char **argv){
 using namespace XELPDonor::HeadlessPM;
 std::vector<uint8_t> image(0x150000);
 if(argc>1){std::ifstream f(argv[1],std::ios::binary);image.assign(std::istreambuf_iterator<char>(f),{});assert(image.size()>0xcccd8);}
 else memcpy(image.data()+Site,Before,sizeof(Before));
 if(argc>1)assert(matches(image.data(),image.size()));
 const uint8_t start[]={START},init[]={INIT},lane[]={LANE},call[]={CALL},hal[]={HAL};
 for(auto span: {std::pair<unsigned,std::vector<uint8_t>>{0x3f1fc,{start,start+sizeof(start)}},
  {0x29ec8,{init,init+sizeof(init)}},{0x1fc22,{lane,lane+sizeof(lane)}},
  {0x304f3,{call,call+sizeof(call)}},{0x5ee5e,{hal,hal+sizeof(hal)}}}){
  if(argc>1)assert(!memcmp(image.data()+span.first,span.second.data(),span.second.size()));
  else memcpy(image.data()+span.first,span.second.data(),span.second.size());
 }
 for(bool display13:{false,true})for(unsigned failure=0;failure<16;++failure){
  shutdownCalls=0;
  auto b=image;uintptr_t pure=0x1234;memcpy(b.data()+0xccad0+0x188,&pure,sizeof(pure));
  XE x;callbackXE=&x;x.display13FramebufferRequested=display13;KernelPatcher p;
  // The provider must be resolved before this one-shot installation. These
  // are the real consumer guards behind the host notification-order fixture.
  if(failure==14)x.xelpProviderProbe=0;
  if(failure==15)x.xelpProviderConfigureProbe=0;
  surfaceOK=failure!=5;nvramOK=failure!=6;x.runtimeOK=failure!=7;x.agdcOK=failure!=8;x.backendOK=failure!=9;x.trainingOK=failure!=10;x.stopDuringStart=failure==11;x.nativeOK=failure!=12;
  if(failure>=1&&failure<=4)p.failAt=failure;
  bool handled=x.processFramebufferKext(p,1,(uintptr_t)b.data(),b.size());
  assert(shutdownCalls==(display13 && failure!=1?1U:0U));
  assert(handled==(failure!=1));
  IOService c;
  if(failure==13){XELPComboClock::ClockSnapshot clocks;clocks.layout=XELPComboClock::Layout::Display13;
   clocks.mux[0]=0xC01;clocks.enable[1]=0xCC000000;
   assert(comboClockTransaction.allocation.retain(0,1,reinterpret_cast<uintptr_t>(&c),clocks));}
  assert(XE::wrapFramebufferStart(&c,&c)==(failure==0));
  assert(x.startCalls==((failure==0||failure==11||failure==12)?1U:0U));
  assert(x.framebufferStartedController==(failure==0?static_cast<void*>(&c):nullptr));
  if(failure==0)assert(!XE::wrapFramebufferStart(&c,&c)&&x.startCalls==1);
  if(failure==13){assert(!x.framebufferStartInProgress && comboClockTransaction.allocation.binding[0]==2);
   assert(comboClockTransaction.allocation.release(0,reinterpret_cast<uintptr_t>(&c)));
   assert(XE::wrapFramebufferStart(&c,&c) && x.startCalls==1);}
  const auto calls=p.attempts,backends=x.backendCalls;
  x.processFramebufferKext(p,1,(uintptr_t)b.data(),b.size());
  assert(shutdownCalls==(display13 && failure!=1?1U:0U));
  assert(p.attempts==calls&&x.backendCalls==backends); // no second trampoline
 }
 const char *tahoe="Darwin Kernel Version 25.6.0: Tue Aug 18 17:46:40 PDT 2026; root:xnu-12377.161.15.700.19~2/RELEASE_X86_64";
 const char *sequoia="Darwin Kernel Version 24.6.1: future servicing; root:xnu-future/RELEASE_X86_64";
 checkHeadlessPMCase(image,false,true,tahoe,true,false,false); // no opt-in: no property or write
 checkHeadlessPMCase(image,true,true,tahoe,true,false,true);   // qualified ADL-S Tahoe donor
 checkHeadlessPMCase(image,true,false,tahoe,true,false,false); // another selected family
 checkHeadlessPMCase(image,true,true,sequoia,true,false,false); // wrong OS
 checkHeadlessPMCase(image,true,true,tahoe,false,false,false); // donor bytes drifted
 checkHeadlessPMCase(image,true,true,tahoe,true,true,false);  // failed patch route
 puts("PASS common-before-backend order, single ownership, partial failures and mandatory native-start gate");
}
'''.replace('FUNCTIONS',f)
for key,name in [('START','entry'),('INIT','initEntry'),('LANE','laneEntry'),('CALL','laneCall'),('HAL','halSize')]:
 code=code.replace(key,re.search(r'\b'+name+r'\[\]\s*=\s*\{([^}]+)',f).group(1))
with tempfile.TemporaryDirectory(prefix='wex-common-') as d:
 p=Path(d);(p/'test.cpp').write_text(code)
 subprocess.run(['clang++','-std=c++17','-O1','-fsanitize=address,undefined','-I'+str(r/'WhateverXe'),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')]+sys.argv[1:],check=True)
