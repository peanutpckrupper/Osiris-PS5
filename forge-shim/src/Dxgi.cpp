#include "NativeDesktopHost.h"
#include <dxgi1_4.h>
#include <wrl.h>
#include <mutex>
#include <map>
#include <cstdio>
#include <cstring>
using namespace Microsoft::WRL;
namespace h5::desktop {HWND WindowHandle() noexcept;}
void HangWatchSwapChain(IDXGISwapChain1*);
using CoreCreate=HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*,IUnknown*,IUnknown*,const DXGI_SWAP_CHAIN_DESC1*,IDXGIOutput*,IDXGISwapChain1**);
static std::mutex gate;
static std::map<void**,CoreCreate> originals;
static HRESULT STDMETHODCALLTYPE CreateForDesktop(IDXGIFactory2* factory,IUnknown* queue,IUnknown*,const DXGI_SWAP_CHAIN_DESC1* desc,IDXGIOutput* output,IDXGISwapChain1** swap){
 HWND hwnd=h5::desktop::WindowHandle();if(!hwnd||!desc)return E_INVALIDARG;
 DXGI_SWAP_CHAIN_DESC1 desktop=*desc;
 char details[200];sprintf_s(details,"[ForgeShim] swap descriptor alpha=%u effect=%u buffers=%u scaling=%u sample=%u usage=%x\n",desc->AlphaMode,desc->SwapEffect,desc->BufferCount,desc->Scaling,desc->SampleDesc.Count,desc->BufferUsage);OutputDebugStringA(details);
 desktop.AlphaMode=DXGI_ALPHA_MODE_IGNORE;
 if(desktop.Scaling==DXGI_SCALING_ASPECT_RATIO_STRETCH)desktop.Scaling=DXGI_SCALING_STRETCH;
 HRESULT hr=factory->CreateSwapChainForHwnd(queue,hwnd,&desktop,nullptr,output,swap);
 if(SUCCEEDED(hr)&&swap)HangWatchSwapChain(*swap);
 char line[240];sprintf_s(line,"[ForgeShim] SwapChain CoreWindow -> HWND %ux%u format=%u flags=%x hr=%08X\n",desc->Width,desc->Height,desc->Format,desc->Flags,hr);OutputDebugStringA(line);return hr;
}
static void PatchFactory(IUnknown* unknown){
 if(!unknown)return;ComPtr<IDXGIFactory2> factory;if(FAILED(unknown->QueryInterface(IID_PPV_ARGS(&factory))))return;
 auto table=*reinterpret_cast<void***>(factory.Get());std::lock_guard<std::mutex> lock(gate);if(originals.count(table))return;
 DWORD old;if(VirtualProtect(table+16,sizeof(void*),PAGE_READWRITE,&old)){originals[table]=reinterpret_cast<CoreCreate>(table[16]);InterlockedExchangePointer(table+16,reinterpret_cast<void*>(&CreateForDesktop));DWORD ignored;VirtualProtect(table+16,sizeof(void*),old,&ignored);OutputDebugStringA("[ForgeShim] DXGI factory CoreWindow slot redirected\n");}
}
HRESULT WINAPI Factory1(REFIID iid,void** out){HRESULT hr=CreateDXGIFactory1(iid,out);if(SUCCEEDED(hr))PatchFactory(reinterpret_cast<IUnknown*>(*out));return hr;}
HRESULT WINAPI Factory2(UINT flags,REFIID iid,void** out){HRESULT hr=CreateDXGIFactory2(flags,iid,out);if(SUCCEEDED(hr))PatchFactory(reinterpret_cast<IUnknown*>(*out));return hr;}
void* DxgiReplacement(const char* name){if(!strcmp(name,"CreateDXGIFactory1"))return reinterpret_cast<void*>(Factory1);if(!strcmp(name,"CreateDXGIFactory2"))return reinterpret_cast<void*>(Factory2);return nullptr;}
