// D3D12 command list hooks.
// Split points (on under Wine; FORGE_GPU_SPLIT=0 turns them off): the PS5's Vulkan driver hands the console's GPU
// at most about a million command words between split points, and it can only split before a draw or dispatch.
// A list of nothing but copies and barriers (Halo 5 uploads thousands of small buffers when a mission spawns) grows
// past that and is refused with VK_ERROR_DEVICE_LOST. So every 96 copies/barriers/clears without a draw or
// dispatch, in a list where the game hasn't bound any state yet, an empty 1x1x1 compute dispatch is added: the
// driver gets its split point and nothing the game draws changes.
// GPU submission tracer (FORGE_GPU_TRACE=1, off by default): finds the work that makes the GPU fail.
// Every pipeline state the game creates gets a number and its shaders' DXBC checksums; command lists record which
// pipelines they bind; each ExecuteCommandLists is waited for (the queue is serialised, so only one submission is
// on the GPU at a time). When a submission doesn't finish (device lost, or 3 s without completing) the log gets
// the failing submission and the ones before it, each with its pipelines, and the pipelines first used just
// before the failure: the shaders on the GPU when it died. FORGE_GPU_TRACE=nowait records without waiting.
#include <windows.h>
#include <d3d12.h>
#include <d3dcommon.h>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "../third_party/minhook/include/MinHook.h"

namespace {
struct Pso { unsigned id; bool compute; unsigned long long vs, ps, ds, hs, gs, cs; bool used; };
std::mutex g_lock;
std::unordered_map<void*, Pso> g_psos;
std::vector<Pso*> g_byId(1, nullptr);   // pipeline number -> its record (unordered_map nodes don't move)
enum Op : unsigned char { OpCopyBuf = 1, OpCopyTex, OpCopyRes, OpCopyTiles, OpResolve, OpBarrier, OpClearDs, OpClearRt, OpClearUavU, OpClearUavF,
    OpDiscard, OpBeginQ, OpEndQ, OpResolveQ, OpPredicate, OpDraws, OpDispatches, OpBundle, OpIndirect, OpQWait, OpQSignal, OpTileMap, OpTileCopy, OpCount };
const char* const kOpName[] = { "?", "CopyBufferRegion", "CopyTextureRegion", "CopyResource", "CopyTiles", "ResolveSubresource", "ResourceBarrier",
    "ClearDepthStencil", "ClearRenderTarget", "ClearUavUint", "ClearUavFloat", "DiscardResource", "BeginQuery", "EndQuery", "ResolveQueryData",
    "SetPredication", "draws", "dispatches", "ExecuteBundle", "ExecuteIndirect", "queue Wait", "queue Signal", "queue UpdateTileMappings", "queue CopyTileMappings" };
struct OpRec { unsigned char op; unsigned a, b; };
struct ListRec { std::vector<unsigned> psos; unsigned indirect = 0, bundles = 0; std::vector<OpRec> ops; unsigned draws = 0, dispatches = 0;
    unsigned heavy = 0; bool state = false; int type = -1; };
std::unordered_map<void*, ListRec> g_lists;
void Note(void* list, unsigned char op, unsigned a = 0, unsigned b = 0);
void SplitPointPassed(void* l);
unsigned g_nextPso = 1;
bool g_wait = true, g_trace = false, g_guard = false, g_split = false; volatile LONG g_failed = 0;
unsigned long long g_submit = 0;
struct Submit { unsigned long long n; void* queue; D3D12_COMMAND_LIST_TYPE type; unsigned lists, indirect; std::vector<unsigned> psos; std::vector<OpRec> ops; };
Submit g_ring[12]; unsigned g_ringAt = 0;
std::vector<unsigned> g_recentFirstUse;   // last pipelines used for the first time

unsigned long long Hash(const D3D12_SHADER_BYTECODE& b)
{
    if (!b.pShaderBytecode || b.BytecodeLength < 20) return 0;
    auto p = static_cast<const unsigned char*>(b.pShaderBytecode);
    unsigned long long h = 0;
    if (!memcmp(p, "DXBC", 4)) { memcpy(&h, p + 4, 8); return h; }   // the container's own checksum
    h = 1469598103934665603ull; for (SIZE_T i = 0; i < b.BytecodeLength; ++i) { h ^= p[i]; h *= 1099511628211ull; } return h;
}
void Describe(const Pso& p, char* out, size_t cap)
{
    if (p.compute) { sprintf_s(out, cap, "#%u cs=%016llx", p.id, p.cs); return; }
    int n = sprintf_s(out, cap, "#%u vs=%016llx ps=%016llx", p.id, p.vs, p.ps);
    if (p.hs || p.ds) n += sprintf_s(out + n, cap - n, " hs=%016llx ds=%016llx", p.hs, p.ds);
    if (p.gs) sprintf_s(out + n, cap - n, " gs=%016llx", p.gs);
}

using CreateGfxFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_GRAPHICS_PIPELINE_STATE_DESC*, REFIID, void**);
using CreateCsFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMPUTE_PIPELINE_STATE_DESC*, REFIID, void**);
using CreateQueueFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, const D3D12_COMMAND_QUEUE_DESC*, REFIID, void**);
using CreateListFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12Device*, UINT, D3D12_COMMAND_LIST_TYPE, ID3D12CommandAllocator*, ID3D12PipelineState*, REFIID, void**);
using ExecuteFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, UINT, ID3D12CommandList* const*);
using ResetFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandAllocator*, ID3D12PipelineState*);
using SetPsoFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12PipelineState*);
using BundleFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12GraphicsCommandList*);
using IndirectFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12CommandSignature*, UINT, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64);
using CreateDeviceFn = HRESULT(WINAPI*)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
CreateGfxFn g_createGfx; CreateCsFn g_createCs; CreateQueueFn g_createQueue; CreateListFn g_createList; ExecuteFn g_execute;
ResetFn g_reset; SetPsoFn g_setPso; BundleFn g_bundle; IndirectFn g_indirect; CreateDeviceFn g_createDevice;
ID3D12Device* g_device = nullptr;

void Hook(void* target, void* detour, void** original) { if (MH_CreateHook(target, detour, original) == MH_OK) MH_EnableHook(target); }

void Record(void* list, ID3D12PipelineState* pso)
{
    if (!pso) return; std::lock_guard<std::mutex> g(g_lock);
    auto& rec = g_lists[list]; rec.state = true; if (!g_trace) return;
    auto it = g_psos.find(pso); unsigned id = it == g_psos.end() ? 0 : it->second.id;
    if (rec.psos.empty() || rec.psos.back() != id) rec.psos.push_back(id);
}
HRESULT STDMETHODCALLTYPE HookCreateGfx(ID3D12Device* d, const D3D12_GRAPHICS_PIPELINE_STATE_DESC* desc, REFIID iid, void** out)
{
    HRESULT hr = g_createGfx(d, desc, iid, out);
    if (g_trace && SUCCEEDED(hr) && out && *out && desc)
    { std::lock_guard<std::mutex> g(g_lock); Pso& r = g_psos[*out]; r = Pso{ g_nextPso++, false, Hash(desc->VS), Hash(desc->PS), Hash(desc->DS), Hash(desc->HS), Hash(desc->GS), 0, false }; if (g_byId.size() <= r.id) g_byId.resize(r.id + 1, nullptr); g_byId[r.id] = &r; }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreateCs(ID3D12Device* d, const D3D12_COMPUTE_PIPELINE_STATE_DESC* desc, REFIID iid, void** out)
{
    HRESULT hr = g_createCs(d, desc, iid, out);
    if (g_trace && SUCCEEDED(hr) && out && *out && desc)
    { std::lock_guard<std::mutex> g(g_lock); Pso& r = g_psos[*out]; r = Pso{ g_nextPso++, true, 0, 0, 0, 0, 0, Hash(desc->CS), false }; if (g_byId.size() <= r.id) g_byId.resize(r.id + 1, nullptr); g_byId[r.id] = &r; }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookReset(ID3D12GraphicsCommandList* l, ID3D12CommandAllocator* a, ID3D12PipelineState* p)
{
    { std::lock_guard<std::mutex> g(g_lock); g_lists[l] = ListRec{}; }
    Record(l, p); return g_reset(l, a, p);
}
void STDMETHODCALLTYPE HookSetPso(ID3D12GraphicsCommandList* l, ID3D12PipelineState* p) { Record(l, p); g_setPso(l, p); }
void STDMETHODCALLTYPE HookBundle(ID3D12GraphicsCommandList* l, ID3D12GraphicsCommandList* b)
{
    if (g_trace) { std::lock_guard<std::mutex> g(g_lock); auto& rec = g_lists[l]; ++rec.bundles; auto it = g_lists.find(b); if (it != g_lists.end()) for (unsigned id : it->second.psos) rec.psos.push_back(id); }
    Note(l, OpBundle); SplitPointPassed(l);
    g_bundle(l, b);
}
void STDMETHODCALLTYPE HookIndirect(ID3D12GraphicsCommandList* l, ID3D12CommandSignature* s, UINT n, ID3D12Resource* a, UINT64 ao, ID3D12Resource* c, UINT64 co)
{
    if (g_trace) { std::lock_guard<std::mutex> g(g_lock); ++g_lists[l].indirect; }
    Note(l, OpIndirect, n); SplitPointPassed(l);
    g_indirect(l, s, n, a, ao, c, co);
}


// Non-pipeline work: draws and dispatches are counted (folded into one entry per run), everything else is listed.
void Note(void* list, unsigned char op, unsigned a, unsigned b)
{
    if (!g_trace) return;
    std::lock_guard<std::mutex> g(g_lock); auto& rec = g_lists[list];
    if ((op == OpDraws || op == OpDispatches) && !rec.ops.empty() && rec.ops.back().op == op) { ++rec.ops.back().a; return; }
    if (op == OpBarrier && !rec.ops.empty() && rec.ops.back().op == OpBarrier) { rec.ops.back().a += a; rec.ops.back().b |= b; return; }
    if (rec.ops.size() < 8192) rec.ops.push_back(OpRec{ op, op == OpDraws || op == OpDispatches ? 1u : a, b });
}

void HeavyOp(ID3D12GraphicsCommandList* l);
void SplitPointPassed(void* l);
#define FWD(name, idx, sig, call, note) using name##Fn = void(STDMETHODCALLTYPE*) sig; name##Fn g_##name; \
    void STDMETHODCALLTYPE Hook##name sig { note; g_##name call; }
#define FWDH(name, idx, sig, call, note) using name##Fn = void(STDMETHODCALLTYPE*) sig; name##Fn g_##name; \
    void STDMETHODCALLTYPE Hook##name sig { note; g_##name call; HeavyOp(l); }
FWD(Draw, 12, (ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, UINT d), (l, a, b, c, d), Note(l, OpDraws); SplitPointPassed(l))
FWD(DrawIdx, 13, (ID3D12GraphicsCommandList* l, UINT a, UINT b, UINT c, INT d, UINT e), (l, a, b, c, d, e), Note(l, OpDraws); SplitPointPassed(l))
FWD(Dispatch, 14, (ID3D12GraphicsCommandList* l, UINT x, UINT y, UINT z), (l, x, y, z), Note(l, OpDispatches, 0, x * y * z); SplitPointPassed(l))
using CopyBufFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12Resource*, UINT64, ID3D12Resource*, UINT64, UINT64); CopyBufFn g_CopyBuf;
volatile LONG g_badCopies = 0, g_skipped = 0;
void STDMETHODCALLTYPE HookCopyBuf(ID3D12GraphicsCommandList* l, ID3D12Resource* d, UINT64 doff, ID3D12Resource* s, UINT64 soff, UINT64 n)
{
    // A copy outside either buffer is invalid D3D12; Windows drivers tend to survive it, the PS5's GPU faults.
    if (!g_trace && !g_guard) { g_CopyBuf(l, d, doff, s, soff, n); HeavyOp(l); return; }
    UINT64 dw = d ? d->GetDesc().Width : 0, sw = s ? s->GetDesc().Width : 0;
    unsigned bad = (!d || doff + n > dw || doff + n < doff ? 1u : 0u) | (!s || soff + n > sw || soff + n < soff ? 2u : 0u)
                 | (d && d == s && doff < soff + n && soff < doff + n ? 4u : 0u);
    if (g_trace) Note(l, OpCopyBuf, static_cast<unsigned>(n), bad);
    if (bad)
    {
        LONG k = InterlockedIncrement(&g_badCopies);
        if (k <= 40)
        {
            D3D12_HEAP_PROPERTIES dh{}, sh{}; D3D12_HEAP_FLAGS f;
            if (d) d->GetHeapProperties(&dh, &f); if (s) s->GetHeapProperties(&sh, &f);
            char line[400]; sprintf_s(line, "[ForgeShim] GPU bad copy %ld: %llu bytes from %p+%llu (buffer %llu bytes, heap %d) to %p+%llu (buffer %llu bytes, heap %d)%s%s%s%s\n",
                k, n, s, soff, sw, static_cast<int>(sh.Type), d, doff, dw, static_cast<int>(dh.Type),
                bad & 1 ? " [past the destination]" : "", bad & 2 ? " [past the source]" : "", bad & 4 ? " [overlapping]" : "", g_guard && (bad & 3) ? " -> skipped" : "");
            OutputDebugStringA(line);
        }
        if (g_guard && (bad & 3)) { InterlockedIncrement(&g_skipped); return; }
    }
    g_CopyBuf(l, d, doff, s, soff, n);
    HeavyOp(l);
}
FWDH(CopyTex, 16, (ID3D12GraphicsCommandList* l, const D3D12_TEXTURE_COPY_LOCATION* d, UINT x, UINT y, UINT z, const D3D12_TEXTURE_COPY_LOCATION* s, const D3D12_BOX* box), (l, d, x, y, z, s, box),
    Note(l, OpCopyTex, (d ? d->Type : 9u) | (s ? s->Type : 9u) << 4, d && d->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT ? d->PlacedFootprint.Footprint.Format : s && s->Type == D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT ? s->PlacedFootprint.Footprint.Format : 0))
FWDH(CopyRes, 17, (ID3D12GraphicsCommandList* l, ID3D12Resource* d, ID3D12Resource* s), (l, d, s), Note(l, OpCopyRes, d ? static_cast<unsigned>(d->GetDesc().Dimension) : 0, d ? static_cast<unsigned>(d->GetDesc().Format) : 0))
FWDH(CopyTiles, 18, (ID3D12GraphicsCommandList* l, ID3D12Resource* r, const D3D12_TILED_RESOURCE_COORDINATE* c, const D3D12_TILE_REGION_SIZE* sz, ID3D12Resource* b, UINT64 o, D3D12_TILE_COPY_FLAGS f), (l, r, c, sz, b, o, f), Note(l, OpCopyTiles, sz ? sz->NumTiles : 0, f))
FWDH(Resolve, 19, (ID3D12GraphicsCommandList* l, ID3D12Resource* d, UINT ds, ID3D12Resource* s, UINT ss, DXGI_FORMAT fmt), (l, d, ds, s, ss, fmt), Note(l, OpResolve, fmt))
FWDH(Barrier, 26, (ID3D12GraphicsCommandList* l, UINT n, const D3D12_RESOURCE_BARRIER* b), (l, n, b),
    { unsigned types = 0; for (UINT i = 0; b && i < n; ++i) types |= 1u << b[i].Type; Note(l, OpBarrier, n, types); })
FWDH(ClearDs, 47, (ID3D12GraphicsCommandList* l, D3D12_CPU_DESCRIPTOR_HANDLE h, D3D12_CLEAR_FLAGS f, FLOAT d, UINT8 st, UINT n, const D3D12_RECT* r), (l, h, f, d, st, n, r), Note(l, OpClearDs, f, n))
FWDH(ClearRt, 48, (ID3D12GraphicsCommandList* l, D3D12_CPU_DESCRIPTOR_HANDLE h, const FLOAT* c, UINT n, const D3D12_RECT* r), (l, h, c, n, r), Note(l, OpClearRt, n))
FWDH(ClearUavU, 49, (ID3D12GraphicsCommandList* l, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const UINT* v, UINT n, const D3D12_RECT* rc), (l, g, c, r, v, n, rc),
    Note(l, OpClearUavU, r ? static_cast<unsigned>(r->GetDesc().Dimension) : 0, r ? static_cast<unsigned>(r->GetDesc().Format) : 0))
FWDH(ClearUavF, 50, (ID3D12GraphicsCommandList* l, D3D12_GPU_DESCRIPTOR_HANDLE g, D3D12_CPU_DESCRIPTOR_HANDLE c, ID3D12Resource* r, const FLOAT* v, UINT n, const D3D12_RECT* rc), (l, g, c, r, v, n, rc),
    Note(l, OpClearUavF, r ? static_cast<unsigned>(r->GetDesc().Dimension) : 0, r ? static_cast<unsigned>(r->GetDesc().Format) : 0))
FWD(Discard, 51, (ID3D12GraphicsCommandList* l, ID3D12Resource* r, const D3D12_DISCARD_REGION* g), (l, r, g), Note(l, OpDiscard))
FWD(BeginQ, 52, (ID3D12GraphicsCommandList* l, ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT i), (l, h, t, i), Note(l, OpBeginQ, t, i))
FWD(EndQ, 53, (ID3D12GraphicsCommandList* l, ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT i), (l, h, t, i), Note(l, OpEndQ, t, i))
FWD(ResolveQ, 54, (ID3D12GraphicsCommandList* l, ID3D12QueryHeap* h, D3D12_QUERY_TYPE t, UINT s, UINT n, ID3D12Resource* d, UINT64 o), (l, h, t, s, n, d, o), Note(l, OpResolveQ, t, s << 16 | (n & 0xffff)))
FWD(Predicate, 55, (ID3D12GraphicsCommandList* l, ID3D12Resource* b, UINT64 o, D3D12_PREDICATION_OP op), (l, b, o, op), Note(l, OpPredicate, b ? 1u : 0u, op))
#undef FWD
#undef FWDH

// ---- split points --------------------------------------------------------------------------------
using SetRootFn = void(STDMETHODCALLTYPE*)(ID3D12GraphicsCommandList*, ID3D12RootSignature*);
SetRootFn g_SetComputeRoot, g_SetGraphicsRoot;
ID3D12RootSignature* g_splitRoot = nullptr; ID3D12PipelineState* g_splitPso = nullptr; volatile LONG g_splitReady = 0;
volatile LONG g_splits = 0;
bool MakeSplitPipeline(ID3D12GraphicsCommandList* l)
{
    static std::mutex m; std::lock_guard<std::mutex> g(m);
    if (g_splitReady) return g_splitReady > 0;
    g_splitReady = -1;
    ID3D12Device* dev = nullptr; if (FAILED(l->GetDevice(IID_PPV_ARGS(&dev))) || !dev) return false;
    using SerializeFn = HRESULT(WINAPI*)(const D3D12_ROOT_SIGNATURE_DESC*, D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
    using CompileFn = HRESULT(WINAPI*)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO*, ID3DInclude*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**, ID3DBlob**);
    auto serialize = reinterpret_cast<SerializeFn>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"), "D3D12SerializeRootSignature"));
    HMODULE dc = GetModuleHandleW(L"d3dcompiler_47.dll"); if (!dc) dc = LoadLibraryW(L"d3dcompiler_47.dll");
    auto compile = dc ? reinterpret_cast<CompileFn>(GetProcAddress(dc, "D3DCompile")) : nullptr;
    ID3DBlob *rs = nullptr, *cs = nullptr, *err = nullptr; HRESULT hr = E_FAIL;
    D3D12_ROOT_SIGNATURE_DESC rd{}; static const char src[] = "[numthreads(1,1,1)] void main() {}";
    if (serialize && compile && SUCCEEDED(serialize(&rd, D3D_ROOT_SIGNATURE_VERSION_1, &rs, &err)) &&
        SUCCEEDED(dev->CreateRootSignature(0, rs->GetBufferPointer(), rs->GetBufferSize(), IID_PPV_ARGS(&g_splitRoot))) &&
        SUCCEEDED(compile(src, sizeof(src) - 1, "forge_split", nullptr, nullptr, "main", "cs_5_0", 0, 0, &cs, &err)))
    {
        D3D12_COMPUTE_PIPELINE_STATE_DESC pd{}; pd.pRootSignature = g_splitRoot; pd.CS = { cs->GetBufferPointer(), cs->GetBufferSize() };
        hr = dev->CreateComputePipelineState(&pd, IID_PPV_ARGS(&g_splitPso));
    }
    if (rs) rs->Release(); if (cs) cs->Release(); if (err) err->Release(); dev->Release();
    char line[160]; sprintf_s(line, "[ForgeShim] GPU split pipeline %s (hr %08lX)\n", SUCCEEDED(hr) ? "ready" : "could not be made; split points off", static_cast<unsigned long>(hr)); OutputDebugStringA(line);
    g_splitReady = SUCCEEDED(hr) ? 1 : -1;
    return SUCCEEDED(hr);
}
// After each copy/barrier/clear: past the threshold, in a direct/compute list with no state of the game's, dispatch.
void HeavyOp(ID3D12GraphicsCommandList* l)
{
    if (!g_split) return;
    bool inject = false;
    {
        std::lock_guard<std::mutex> g(g_lock); auto& rec = g_lists[l];
        if (rec.type < 0) rec.type = static_cast<int>(l->GetType());
        if (++rec.heavy < 96) return;
        rec.heavy = 0;
        inject = !rec.state && (rec.type == D3D12_COMMAND_LIST_TYPE_DIRECT || rec.type == D3D12_COMMAND_LIST_TYPE_COMPUTE);
    }
    if (!inject || g_splitReady < 0 || !MakeSplitPipeline(l)) return;
    g_SetComputeRoot(l, g_splitRoot); g_setPso(l, g_splitPso); g_Dispatch(l, 1, 1, 1);
    LONG k = InterlockedIncrement(&g_splits);
    if (k == 1 || k % 5000 == 0) { char line[140]; sprintf_s(line, "[ForgeShim] GPU split points added: %ld (lists of only copies and barriers)\n", k); OutputDebugStringA(line); }
}
void SplitPointPassed(void* l) { std::lock_guard<std::mutex> g(g_lock); g_lists[l].heavy = 0; }
void GameState(void* l) { std::lock_guard<std::mutex> g(g_lock); g_lists[l].state = true; }
void STDMETHODCALLTYPE HookSetComputeRoot(ID3D12GraphicsCommandList* l, ID3D12RootSignature* r) { GameState(l); g_SetComputeRoot(l, r); }
void STDMETHODCALLTYPE HookSetGraphicsRoot(ID3D12GraphicsCommandList* l, ID3D12RootSignature* r) { GameState(l); g_SetGraphicsRoot(l, r); }
void Describe(const OpRec& o, char* out, size_t cap)
{
    switch (o.op)
    {
    case OpDraws: case OpDispatches: sprintf_s(out, cap, "%u %s", o.a, kOpName[o.op]); break;
    case OpBarrier: sprintf_s(out, cap, "ResourceBarrier x%u (types %x)", o.a, o.b); break;
    case OpBeginQ: case OpEndQ: sprintf_s(out, cap, "%s type %u index %u", kOpName[o.op], o.a, o.b); break;
    case OpResolveQ: sprintf_s(out, cap, "ResolveQueryData type %u start %u count %u", o.a, o.b >> 16, o.b & 0xffff); break;
    case OpCopyBuf: sprintf_s(out, cap, "CopyBufferRegion %u bytes%s", o.a, o.b & 3 ? " OUT OF BOUNDS" : o.b & 4 ? " overlapping" : ""); break;
    case OpCopyTex: sprintf_s(out, cap, "CopyTextureRegion dst/src kind %x format %u", o.a, o.b); break;
    case OpCopyRes: case OpClearUavU: case OpClearUavF: sprintf_s(out, cap, "%s dimension %u format %u", kOpName[o.op], o.a, o.b); break;
    case OpPredicate: sprintf_s(out, cap, "SetPredication %s op %u", o.a ? "on" : "off", o.b); break;
    case OpQWait: sprintf_s(out, cap, "queue Wait for fence value %u (fence was at %u)", o.a, o.b); break;
    case OpQSignal: sprintf_s(out, cap, "queue Signal fence value %u", o.a); break;
    case OpTileMap: sprintf_s(out, cap, "queue UpdateTileMappings %u regions, %u ranges%s", o.a, o.b & 0x7fffffff, o.b & 0x80000000u ? " (unmapping)" : ""); break;
    default: sprintf_s(out, cap, "%s %u %u", o.op < OpCount ? kOpName[o.op] : "?", o.a, o.b); break;
    }
}

void DumpFailure(const Submit& bad, const char* why)
{
    if (InterlockedExchange(&g_failed, 1)) return;
    char line[2400];
    sprintf_s(line, "[ForgeShim] GPU FAILED during submission %llu (%s); last submissions, oldest first:\n", bad.n, why); OutputDebugStringA(line);
    std::lock_guard<std::mutex> g(g_lock);
    for (unsigned k = 0; k < 12; ++k)
    {
        const Submit& s = g_ring[(g_ringAt + k) % 12]; if (!s.n) continue;
        int n = sprintf_s(line, "[ForgeShim] GPU %s submit %llu queue %p type %d: %u lists, %u ExecuteIndirect, %zu pipelines:", s.n == bad.n ? ">>" : "  ", s.n, s.queue, static_cast<int>(s.type), s.lists, s.indirect, s.psos.size());
        for (size_t i = 0; i < s.psos.size() && n < 2300; ++i) n += sprintf_s(line + n, sizeof(line) - n, " #%u", s.psos[i]);
        strcat_s(line, "\n"); OutputDebugStringA(line);
    }
    for (unsigned k = 9; k < 12; ++k)   // the failing submission and the two before it, command by command
    {
        const Submit& s = g_ring[(g_ringAt + k) % 12]; if (!s.n) continue;
        sprintf_s(line, "[ForgeShim] GPU commands of submit %llu (%zu entries):\n", s.n, s.ops.size()); OutputDebugStringA(line);
        unsigned counts[OpCount] = {}; for (auto& o : s.ops) if (o.op < OpCount) ++counts[o.op];
        int n = sprintf_s(line, "[ForgeShim] GPU   totals:"); for (unsigned c = 1; c < OpCount; ++c) if (counts[c]) n += sprintf_s(line + n, sizeof(line) - n, " %s=%u", kOpName[c], counts[c]);
        strcat_s(line, "\n"); OutputDebugStringA(line);
        for (size_t i = 0; i < s.ops.size(); ++i)
        {
            if (s.ops.size() > 120 && i == 40) { sprintf_s(line, "[ForgeShim] GPU     ... %zu more ...\n", s.ops.size() - 120); OutputDebugStringA(line); i = s.ops.size() - 80; }
            char d[160]; Describe(s.ops[i], d, sizeof(d)); sprintf_s(line, "[ForgeShim] GPU     %s\n", d); OutputDebugStringA(line);
        }
    }
    OutputDebugStringA("[ForgeShim] GPU pipelines in the failing submission:\n");
    for (unsigned id : bad.psos) { if (id >= g_byId.size() || !g_byId[id]) continue; char d[200]; Describe(*g_byId[id], d, sizeof(d)); sprintf_s(line, "[ForgeShim] GPU   %s\n", d); OutputDebugStringA(line); }
    OutputDebugStringA("[ForgeShim] GPU pipelines used for the first time just before the failure (newest last):\n");
    size_t from = g_recentFirstUse.size() > 24 ? g_recentFirstUse.size() - 24 : 0;
    for (size_t i = from; i < g_recentFirstUse.size(); ++i) { unsigned id = g_recentFirstUse[i]; if (id >= g_byId.size() || !g_byId[id]) continue; char d[200]; Describe(*g_byId[id], d, sizeof(d)); sprintf_s(line, "[ForgeShim] GPU   %s\n", d); OutputDebugStringA(line); }
}


std::unordered_map<void*, std::vector<OpRec>> g_queueOps;   // queue-level work since that queue's last ExecuteCommandLists
void QueueNote(void* q, unsigned char op, unsigned a, unsigned b)
{ if (!g_trace) return; std::lock_guard<std::mutex> g(g_lock); auto& v = g_queueOps[q]; if (v.size() < 64) v.push_back(OpRec{ op, a, b }); }
using QSignalFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64); QSignalFn g_qsignal;
using QWaitFn = HRESULT(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Fence*, UINT64); QWaitFn g_qwait;
using TileMapFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Resource*, UINT, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, ID3D12Heap*, UINT, const D3D12_TILE_RANGE_FLAGS*, const UINT*, const UINT*, D3D12_TILE_MAPPING_FLAGS); TileMapFn g_tilemap;
using TileCopyFn = void(STDMETHODCALLTYPE*)(ID3D12CommandQueue*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, ID3D12Resource*, const D3D12_TILED_RESOURCE_COORDINATE*, const D3D12_TILE_REGION_SIZE*, D3D12_TILE_MAPPING_FLAGS); TileCopyFn g_tilecopy;
thread_local bool t_ownSignal = false;
HRESULT STDMETHODCALLTYPE HookQSignal(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { if (!t_ownSignal) QueueNote(q, OpQSignal, static_cast<unsigned>(v), 0); return g_qsignal(q, f, v); }
HRESULT STDMETHODCALLTYPE HookQWait(ID3D12CommandQueue* q, ID3D12Fence* f, UINT64 v) { QueueNote(q, OpQWait, static_cast<unsigned>(v), f ? static_cast<unsigned>(f->GetCompletedValue()) : 0); return g_qwait(q, f, v); }
void STDMETHODCALLTYPE HookTileMap(ID3D12CommandQueue* q, ID3D12Resource* r, UINT nr, const D3D12_TILED_RESOURCE_COORDINATE* c, const D3D12_TILE_REGION_SIZE* sz, ID3D12Heap* h, UINT nrng, const D3D12_TILE_RANGE_FLAGS* rf, const UINT* st, const UINT* cnt, D3D12_TILE_MAPPING_FLAGS fl)
{ QueueNote(q, OpTileMap, nr, nrng | (h ? 0u : 0x80000000u)); g_tilemap(q, r, nr, c, sz, h, nrng, rf, st, cnt, fl); }
void STDMETHODCALLTYPE HookTileCopy(ID3D12CommandQueue* q, ID3D12Resource* d, const D3D12_TILED_RESOURCE_COORDINATE* dc, ID3D12Resource* s, const D3D12_TILED_RESOURCE_COORDINATE* sc, const D3D12_TILE_REGION_SIZE* sz, D3D12_TILE_MAPPING_FLAGS f)
{ QueueNote(q, OpTileCopy, sz ? sz->NumTiles : 0, 0); g_tilecopy(q, d, dc, s, sc, sz, f); }

struct QueueSync { ID3D12Fence* fence = nullptr; UINT64 value = 0; HANDLE event = nullptr; };
std::unordered_map<void*, QueueSync> g_sync; std::mutex g_syncLock;

void STDMETHODCALLTYPE HookExecute(ID3D12CommandQueue* q, UINT count, ID3D12CommandList* const* lists)
{
    if (!g_trace) { g_execute(q, count, lists); return; }
    Submit s{}; s.queue = q; s.lists = count; s.type = q->GetDesc().Type;
    {
        std::lock_guard<std::mutex> g(g_lock); s.n = ++g_submit;
        { auto qit = g_queueOps.find(q); if (qit != g_queueOps.end()) { s.ops = std::move(qit->second); qit->second.clear(); } }
        for (UINT i = 0; i < count; ++i)
        {
            auto it = g_lists.find(lists[i]); if (it == g_lists.end()) continue;
            s.indirect += it->second.indirect;
            for (unsigned id : it->second.psos) { if (s.psos.size() < 256) s.psos.push_back(id); }
            for (auto& o : it->second.ops) if (s.ops.size() < 8192) s.ops.push_back(o);
        }
        for (unsigned id : s.psos)
            if (id < g_byId.size() && g_byId[id] && !g_byId[id]->used)
            { g_byId[id]->used = true; g_recentFirstUse.push_back(id); if (g_recentFirstUse.size() > 256) g_recentFirstUse.erase(g_recentFirstUse.begin(), g_recentFirstUse.begin() + 128); }
        g_ring[g_ringAt] = s; g_ringAt = (g_ringAt + 1) % 12;
    }
    g_execute(q, count, lists);
    if (!g_wait || g_failed || !g_device) return;
    QueueSync* sync;
    {
        std::lock_guard<std::mutex> g(g_syncLock); sync = &g_sync[q];
        if (!sync->fence) { g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&sync->fence)); sync->event = CreateEventW(nullptr, FALSE, FALSE, nullptr); }
        if (!sync->fence) return;
        ++sync->value; t_ownSignal = true; q->Signal(sync->fence, sync->value); t_ownSignal = false;
        sync->fence->SetEventOnCompletion(sync->value, sync->event);
    }
    DWORD w = WaitForSingleObject(sync->event, 3000);
    UINT64 done = sync->fence->GetCompletedValue();
    HRESULT removed = g_device->GetDeviceRemovedReason();
    if (w != WAIT_OBJECT_0 || done == UINT64_MAX || FAILED(removed))
    {
        char why[120]; sprintf_s(why, "%s, device removed reason %08lX", w != WAIT_OBJECT_0 ? "did not finish in 3 s" : "fence reports device lost", static_cast<unsigned long>(removed));
        DumpFailure(s, why);
    }
}
HRESULT STDMETHODCALLTYPE HookCreateQueue(ID3D12Device* d, const D3D12_COMMAND_QUEUE_DESC* desc, REFIID iid, void** out)
{
    HRESULT hr = g_createQueue(d, desc, iid, out);
    static volatile LONG once = 0;
    if (SUCCEEDED(hr) && out && *out && !InterlockedExchange(&once, 1))
    {
        void** vt = *reinterpret_cast<void***>(*out);
        Hook(vt[10], reinterpret_cast<void*>(&HookExecute), reinterpret_cast<void**>(&g_execute));
        Hook(vt[8], reinterpret_cast<void*>(&HookTileMap), reinterpret_cast<void**>(&g_tilemap));
        Hook(vt[9], reinterpret_cast<void*>(&HookTileCopy), reinterpret_cast<void**>(&g_tilecopy));
        Hook(vt[14], reinterpret_cast<void*>(&HookQSignal), reinterpret_cast<void**>(&g_qsignal));
        Hook(vt[15], reinterpret_cast<void*>(&HookQWait), reinterpret_cast<void**>(&g_qwait));
    }
    return hr;
}
HRESULT STDMETHODCALLTYPE HookCreateList(ID3D12Device* d, UINT mask, D3D12_COMMAND_LIST_TYPE t, ID3D12CommandAllocator* a, ID3D12PipelineState* p, REFIID iid, void** out)
{
    HRESULT hr = g_createList(d, mask, t, a, p, iid, out);
    static volatile LONG once = 0;
    if (SUCCEEDED(hr) && out && *out)
    {
        if (!InterlockedExchange(&once, 1))
        {
            void** vt = *reinterpret_cast<void***>(*out);
            Hook(vt[10], reinterpret_cast<void*>(&HookReset), reinterpret_cast<void**>(&g_reset));
            Hook(vt[25], reinterpret_cast<void*>(&HookSetPso), reinterpret_cast<void**>(&g_setPso));
            Hook(vt[27], reinterpret_cast<void*>(&HookBundle), reinterpret_cast<void**>(&g_bundle));
            Hook(vt[59], reinterpret_cast<void*>(&HookIndirect), reinterpret_cast<void**>(&g_indirect));
#define H(name, idx) Hook(vt[idx], reinterpret_cast<void*>(&Hook##name), reinterpret_cast<void**>(&g_##name))
            H(Draw, 12); H(DrawIdx, 13); H(Dispatch, 14); H(CopyBuf, 15); H(CopyTex, 16); H(CopyRes, 17); H(CopyTiles, 18); H(Resolve, 19);
            H(Barrier, 26); H(ClearDs, 47); H(ClearRt, 48); H(ClearUavU, 49); H(ClearUavF, 50); H(Discard, 51); H(BeginQ, 52); H(EndQ, 53);
            H(ResolveQ, 54); H(Predicate, 55); H(SetComputeRoot, 29); H(SetGraphicsRoot, 30);
#undef H
        }
        { std::lock_guard<std::mutex> g(g_lock); g_lists[*out] = ListRec{}; }
        Record(*out, p);
    }
    return hr;
}
HRESULT WINAPI HookCreateDevice(IUnknown* adapter, D3D_FEATURE_LEVEL level, REFIID iid, void** out)
{
    HRESULT hr = g_createDevice(adapter, level, iid, out);
    if (FAILED(hr) || !out || !*out) return hr;
    ID3D12Device* dev = nullptr;
    if (FAILED(static_cast<IUnknown*>(*out)->QueryInterface(IID_PPV_ARGS(&dev))) || !dev) return hr;
    static volatile LONG once = 0;
    if (!InterlockedExchange(&once, 1))
    {
        void** vt = *reinterpret_cast<void***>(dev);
        Hook(vt[8], reinterpret_cast<void*>(&HookCreateQueue), reinterpret_cast<void**>(&g_createQueue));
        Hook(vt[10], reinterpret_cast<void*>(&HookCreateGfx), reinterpret_cast<void**>(&g_createGfx));
        Hook(vt[11], reinterpret_cast<void*>(&HookCreateCs), reinterpret_cast<void**>(&g_createCs));
        Hook(vt[12], reinterpret_cast<void*>(&HookCreateList), reinterpret_cast<void**>(&g_createList));
        { char l[160]; sprintf_s(l, "[ForgeShim] GPU hooks on: split points %s, trace %s, copy guard %s\n", g_split ? "on" : "off", !g_trace ? "off" : g_wait ? "on (each submission waited for)" : "on (not waiting)", g_guard ? "on" : "off"); OutputDebugStringA(l); }
    }
    if (g_device) g_device->Release();
    g_device = dev;   // the most recent device (the game makes a probe device first)
    return hr;
}
}

void InstallGpuTrace()
{
    wchar_t v[16]; DWORD n = GetEnvironmentVariableW(L"FORGE_GPU_TRACE", v, 16);
    g_trace = n && n < 16 && v[0] != L'0'; g_wait = g_trace && _wcsicmp(v, L"nowait") != 0;
    wchar_t gv[8]; DWORD gn = GetEnvironmentVariableW(L"FORGE_GPU_COPY_GUARD", gv, 8); g_guard = gn && gn < 8 && gv[0] != L'0';
    wchar_t sv[8]; DWORD sn = GetEnvironmentVariableW(L"FORGE_GPU_SPLIT", sv, 8);
    g_split = sn && sn < 8 ? sv[0] != L'0' : GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version") != nullptr;   // default: under Wine
    if (!g_trace && !g_guard && !g_split) return;
    HMODULE d3d12 = GetModuleHandleW(L"d3d12.dll"); if (!d3d12) d3d12 = LoadLibraryW(L"d3d12.dll");
    void* f = d3d12 ? reinterpret_cast<void*>(GetProcAddress(d3d12, "D3D12CreateDevice")) : nullptr;
    MH_Initialize();
    if (f && MH_CreateHook(f, reinterpret_cast<void*>(&HookCreateDevice), reinterpret_cast<void**>(&g_createDevice)) == MH_OK && MH_EnableHook(f) == MH_OK)
        OutputDebugStringA("[ForgeShim] GPU trace armed (D3D12CreateDevice hooked)\n");
    else OutputDebugStringA("[ForgeShim] GPU trace: could not hook D3D12CreateDevice\n");
}
