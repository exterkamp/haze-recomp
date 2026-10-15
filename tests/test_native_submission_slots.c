#define COBJMACROS
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <d3dcompiler.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rsx_postprocess.h"
typedef uint8_t u8;
typedef uint32_t u32;
typedef uint64_t u64;
#define LD_SAMPLER_HEAP_PAGES 2
#define MAX_RETIRED_TEXTURES 8
#define UPLOAD_SIZE 65536u
#define VERT_BUFFER_SIZE 65536u
#define INDEX_BUFFER_SIZE 65536u
#define CB_RING_BYTES 65536u
#define PS_CB_RING_BYTES 65536u
#define SRV_RING_TABLES 16u
#define SRV_TABLE_SIZE 16u
#define SMP_RING_TABLES 16u
#define SMP_TABLE_SIZE 16u
#define OK(x) do { HRESULT h_=(x); if(FAILED(h_)){fprintf(stderr,"HRESULT %08lx line%d\n",h_,__LINE__);abort();} } while(0)
typedef enum {LD_FLUSH_PRESENT} ld_flush_reason;
static struct {
    int ready;
    ID3D12Device* dev;
    ID3D12CommandQueue* queue;
    ID3D12CommandAllocator* alloc;
    ID3D12GraphicsCommandList* list;
    ID3D12Fence* fence;
    HANDLE fence_event;
    u64 fence_value;
    ID3D12Resource *upload,*vb,*ib,*cb,*ps_cb;
    u8 *upload_mapped,*vb_mapped,*cb_mapped,*ps_cb_mapped;
    u32* ib_mapped;
    u32 upload_used,vb_used,ib_used,cb_used,ps_cb_used;
    ID3D12DescriptorHeap *srv_heap,*smp_heap,*smp_pages[LD_SAMPLER_HEAP_PAGES];
    u32 srv_ring_used,smp_ring_used,smp_page,smp_page_count;
    int smp_upload_reclaim_pending;
    ID3D12Resource* retired_textures[MAX_RETIRED_TEXTURES];
    u32 n_retired_textures;
} g;
static rsx_postprocess g_ld_postprocess;
static int g_ld_movie_mode;
static struct {void* pass;} g_ld_native_pass;
static struct {int phase;} g_ld_native_payload;
static ID3D12InfoQueue* info;
static void ld_flush(ld_flush_reason reason);
static void ld_dump_dred(const char* stage,HRESULT hr) {fprintf(stderr,"failure %s %08lx\n",stage,hr);}
static void ld_drain_info_queue(const char* stage) {(void)stage;}
#if defined(YZ_PERF_PROFILE)
typedef struct {
    u64 gpu_ticks,gpu_lists_attempted,gpu_lists_measured,gpu_lists_unmeasured,gpu_lists_invalid;
} ld_profile_counts;
static struct {ld_profile_counts total,previous;} g_ld_profile;
static ID3D12QueryHeap* g_ld_gpu_queries;
static ID3D12Resource* g_ld_gpu_readback;
static u64 *g_ld_gpu_results,g_ld_gpu_frequency;
static int g_ld_gpu_armed,g_ld_gpu_init_attempted;
#include "gpu_profile_actual.inc"
#endif
#include "haze_native_submission_slots.h"

/* Fixture's synchronous dependency boundary. The actual existing production
 * ld_flush is unchanged by this candidate and has separate failure fixtures. */
static void ld_flush(ld_flush_reason reason)
{
    (void)reason;
#if defined(YZ_PERF_PROFILE)
    ld_gpu_profile_end_list();
#endif
    OK(g.list->lpVtbl->Close(g.list));
    ID3D12CommandList* lists[]={(ID3D12CommandList*)g.list};
    g.queue->lpVtbl->ExecuteCommandLists(g.queue,1,lists);
    OK(g.queue->lpVtbl->Signal(g.queue,g.fence,++g.fence_value));
    assert(ld_slots_wait(g.fence_value));
#if defined(YZ_PERF_PROFILE)
    ld_gpu_profile_completed_list();
#endif
    for(u32 i=0;i<g.n_retired_textures;++i) g.retired_textures[i]->lpVtbl->Release(g.retired_textures[i]);
    g.n_retired_textures=0;
    OK(g.alloc->lpVtbl->Reset(g.alloc));
    OK(g.list->lpVtbl->Reset(g.list,g.alloc,NULL));
#if defined(YZ_PERF_PROFILE)
    ld_gpu_profile_begin_list();
#endif
}
static ID3D12DescriptorHeap* heap(D3D12_DESCRIPTOR_HEAP_TYPE type,UINT count)
{
    D3D12_DESCRIPTOR_HEAP_DESC d={0};d.Type=type;d.NumDescriptors=count;
    d.Flags=type==D3D12_DESCRIPTOR_HEAP_TYPE_RTV?D3D12_DESCRIPTOR_HEAP_FLAG_NONE:D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* result=NULL;
    OK(g.dev->lpVtbl->CreateDescriptorHeap(g.dev,&d,&IID_ID3D12DescriptorHeap,(void**)&result));return result;
}
static ID3D12Resource* readback(u64 size)
{
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;
    d.Height=d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* result=NULL;
    OK(g.dev->lpVtbl->CreateCommittedResource(g.dev,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,NULL,&IID_ID3D12Resource,(void**)&result));return result;
}
static ID3D12Resource* texture(void)
{
    D3D12_HEAP_PROPERTIES hp={0};hp.Type=D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC d={0};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    d.Width=d.Height=32;d.DepthOrArraySize=d.MipLevels=d.SampleDesc.Count=1;
    d.Format=DXGI_FORMAT_R8G8B8A8_UNORM;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    ID3D12Resource* result=NULL;
    OK(g.dev->lpVtbl->CreateCommittedResource(g.dev,&hp,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,NULL,&IID_ID3D12Resource,(void**)&result));return result;
}
static void buffers(ID3D12Resource* output,u32 frame)
{
    ID3D12Resource* resources[]={g.upload,g.vb,g.ib,g.cb,g.ps_cb};
    void* maps[]={g.upload_mapped,g.vb_mapped,g.ib_mapped,g.cb_mapped,g.ps_cb_mapped};
    for(u32 i=0;i<5;++i) {
        memset(maps[i],(int)(frame*16+i+1),64);
        g.list->lpVtbl->CopyBufferRegion(g.list,output,i*64,resources[i],0,64);
    }
}
static void effect(ID3D12Resource* source,ID3D12Resource* output,u32 channel)
{
    ID3D12DescriptorHeap* rtv=heap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,1);
    D3D12_CPU_DESCRIPTOR_HANDLE handle;rtv->lpVtbl->GetCPUDescriptorHandleForHeapStart(rtv,&handle);
    g.dev->lpVtbl->CreateRenderTargetView(g.dev,source,NULL,handle);
    FLOAT color[4]={0,0,0,1};color[channel]=1;
    g.list->lpVtbl->ClearRenderTargetView(g.list,handle,color,0,NULL);
    ID3D12Resource* result=rsx_postprocess_run(&g_ld_postprocess,g.dev,g.list,source,1,0,0);
    D3D12_RESOURCE_BARRIER barrier={0};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource=result;barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_RENDER_TARGET;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;
    g.list->lpVtbl->ResourceBarrier(g.list,1,&barrier);
    D3D12_TEXTURE_COPY_LOCATION src={0},dst={0};src.pResource=result;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.pResource=output;dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.PlacedFootprint.Footprint.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    dst.PlacedFootprint.Footprint.Width=dst.PlacedFootprint.Footprint.Height=32;
    dst.PlacedFootprint.Footprint.Depth=1;dst.PlacedFootprint.Footprint.RowPitch=256;
    g.list->lpVtbl->CopyTextureRegion(g.list,&dst,0,0,0,&src,NULL);
    barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COPY_SOURCE;barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
    g.list->lpVtbl->ResourceBarrier(g.list,1,&barrier);
    rtv->lpVtbl->Release(rtv);
}
static DWORD WINAPI unblock(void* parameter)
{
    Sleep(100);ID3D12Fence* gate=(ID3D12Fence*)parameter;OK(gate->lpVtbl->Signal(gate,1));return 0;
}
static void verify_buffers(ID3D12Resource* resource,u32 frame)
{
    u8* data=NULL;OK(resource->lpVtbl->Map(resource,0,NULL,(void**)&data));
    for(u32 i=0;i<5;++i)for(u32 j=0;j<64;++j)assert(data[i*64+j]==frame*16+i+1);
    resource->lpVtbl->Unmap(resource,0,NULL);
}
static void verify_effect(ID3D12Resource* resource,u32 channel)
{
    u8* data=NULL;OK(resource->lpVtbl->Map(resource,0,NULL,(void**)&data));
    for(u32 y=0;y<32;++y)for(u32 x=0;x<32;++x)for(u32 c=0;c<4;++c)
        assert(data[y*256+x*4+c]==((c==channel||c==3)?255:0));
    resource->lpVtbl->Unmap(resource,0,NULL);
}
static void session(ID3D12Device* device)
{
    assert(!g_ld_slots.initialized);memset(&g,0,sizeof g);g.dev=device;g.ready=1;
    D3D12_COMMAND_QUEUE_DESC q={0};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    OK(device->lpVtbl->CreateCommandQueue(device,&q,&IID_ID3D12CommandQueue,(void**)&g.queue));
    OK(device->lpVtbl->CreateCommandAllocator(device,D3D12_COMMAND_LIST_TYPE_DIRECT,&IID_ID3D12CommandAllocator,(void**)&g.alloc));
    OK(device->lpVtbl->CreateCommandList(device,0,D3D12_COMMAND_LIST_TYPE_DIRECT,g.alloc,NULL,&IID_ID3D12GraphicsCommandList,(void**)&g.list));
    OK(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&g.fence));
    g.fence_event=CreateEventA(NULL,FALSE,FALSE,NULL);assert(g.fence_event);
    assert(ld_slots_buffer(UPLOAD_SIZE,&g.upload,(void**)&g.upload_mapped));
    assert(ld_slots_buffer(VERT_BUFFER_SIZE,&g.vb,(void**)&g.vb_mapped));
    assert(ld_slots_buffer(INDEX_BUFFER_SIZE,&g.ib,(void**)&g.ib_mapped));
    assert(ld_slots_buffer(CB_RING_BYTES,&g.cb,(void**)&g.cb_mapped));
    assert(ld_slots_buffer(PS_CB_RING_BYTES,&g.ps_cb,(void**)&g.ps_cb_mapped));
    g.srv_heap=heap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,SRV_RING_TABLES*SRV_TABLE_SIZE);
    for(u32 i=0;i<LD_SAMPLER_HEAP_PAGES;++i)g.smp_pages[i]=heap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,SMP_RING_TABLES*SMP_TABLE_SIZE);
    g.smp_page_count=LD_SAMPLER_HEAP_PAGES;g.smp_heap=g.smp_pages[0];
    assert(rsx_postprocess_init(&g_ld_postprocess,device,32,32)==0);
    ID3D12Resource *outputs[4],*images[2],*sources[2];
    for(u32 i=0;i<4;++i)outputs[i]=readback(320);
    for(u32 i=0;i<2;++i){images[i]=readback(8192);sources[i]=texture();}
    ID3D12Fence* gate=NULL;OK(device->lpVtbl->CreateFence(device,0,D3D12_FENCE_FLAG_NONE,&IID_ID3D12Fence,(void**)&gate));
    OK(g.queue->lpVtbl->Wait(g.queue,gate,1));
    ID3D12DescriptorHeap* original_heap=g_ld_postprocess.heap;
    ID3D12Resource* original_upload=g.upload;
    buffers(outputs[0],0);effect(sources[0],images[0],0);ld_submit_frame();
    assert(g.ready && g_ld_slots.submitted==1 && g_ld_slots.active==1);
    assert(g.fence->lpVtbl->GetCompletedValue(g.fence)==0);
    assert(g.upload!=original_upload && g_ld_postprocess.heap!=original_heap);
    buffers(outputs[1],1);effect(sources[1],images[1],1);
    HANDLE helper=CreateThread(NULL,0,unblock,gate,0,NULL);assert(helper);
    ld_submit_frame();assert(g.ready && g_ld_slots.submitted==2 && g_ld_slots.active==0);
    assert(WaitForSingleObject(helper,5000)==WAIT_OBJECT_0);CloseHandle(helper);
    ld_flush(LD_FLUSH_PRESENT);
    verify_buffers(outputs[0],0);verify_buffers(outputs[1],1);
    verify_effect(images[0],0);verify_effect(images[1],1);
    /* Full settings boundary followed by off/on while changing slot owner. */
    rsx_postprocess_destroy(&g_ld_postprocess);
    buffers(outputs[2],2);ld_submit_frame();assert(g.ready && !g_ld_postprocess.heap);
    ld_flush(LD_FLUSH_PRESENT);
    assert(rsx_postprocess_init(&g_ld_postprocess,device,32,32)==0);
    ID3D12PipelineState* settings_pipeline=g_ld_postprocess.pipeline;
    buffers(outputs[3],3);ld_submit_frame();assert(g.ready && g_ld_postprocess.pipeline==settings_pipeline);
    ld_flush(LD_FLUSH_PRESENT);
    verify_buffers(outputs[2],2);verify_buffers(outputs[3],3);
    assert(g_ld_slot_observation_count==4);
    assert(g_ld_slot_observations[0].wait_ticks==0);
    assert(g_ld_slot_observations[0].reuse_age_ticks==0);
    for(u32 i=0;i<4;++i) {
        assert(g_ld_slot_observations[i].end>=g_ld_slot_observations[i].begin);
        assert(g_ld_slot_observations[i].wait_ticks<=
            g_ld_slot_observations[i].end-g_ld_slot_observations[i].begin);
    }
    /* Native quit reports without taking over resource cleanup. */
    ld_slots_report();
    assert(g_ld_slots.initialized && g_ld_slots.submitted==4);
#if defined(YZ_PERF_PROFILE)
    assert(g_ld_slots.gpu_lists>=2);
#endif
#if defined(YZ_PERF_PROFILE)
    (void)ld_gpu_profile_frame_valid(); /* Frame attribution is validated by integration. */
#endif
    ld_slots_shutdown();assert(!g_ld_slots.initialized && !g_ld_slots.unavailable);
    assert(!g_ld_slot_observation_count);
#if defined(YZ_PERF_PROFILE)
    ld_gpu_profile_cleanup();
#endif
    ld_frame_storage remaining={0};ld_slots_capture(&remaining);ld_slots_release(&remaining);
    rsx_postprocess_destroy(&g_ld_postprocess);
    g.list->lpVtbl->Release(g.list);g.fence->lpVtbl->Release(g.fence);g.queue->lpVtbl->Release(g.queue);
    gate->lpVtbl->Release(gate);CloseHandle(g.fence_event);
    for(u32 i=0;i<4;++i)outputs[i]->lpVtbl->Release(outputs[i]);
    for(u32 i=0;i<2;++i){images[i]->lpVtbl->Release(images[i]);sources[i]->lpVtbl->Release(sources[i]);}
}
int main(void)
{
    ID3D12Debug* debug=NULL;OK(D3D12GetDebugInterface(&IID_ID3D12Debug,(void**)&debug));
    debug->lpVtbl->EnableDebugLayer(debug);debug->lpVtbl->Release(debug);
    IDXGIFactory4* factory=NULL;IDXGIAdapter* adapter=NULL;ID3D12Device* device=NULL;
    OK(CreateDXGIFactory1(&IID_IDXGIFactory4,(void**)&factory));
    OK(factory->lpVtbl->EnumWarpAdapter(factory,&IID_IDXGIAdapter,(void**)&adapter));
    OK(D3D12CreateDevice((IUnknown*)adapter,D3D_FEATURE_LEVEL_11_0,&IID_ID3D12Device,(void**)&device));
    OK(device->lpVtbl->QueryInterface(device,&IID_ID3D12InfoQueue,(void**)&info));
    session(device);session(device);
    u64 errors=0;
    for(u64 i=0;i<info->lpVtbl->GetNumStoredMessages(info);++i){SIZE_T bytes=0;
        OK(info->lpVtbl->GetMessage(info,i,NULL,&bytes));D3D12_MESSAGE* m=(D3D12_MESSAGE*)malloc(bytes);assert(m);
        OK(info->lpVtbl->GetMessage(info,i,m,&bytes));
        if(m->Severity<=D3D12_MESSAGE_SEVERITY_ERROR){++errors;fprintf(stderr,"D3D12: %s\n",m->pDescription);}free(m);
    }
    assert(!errors);
    info->lpVtbl->Release(info);device->lpVtbl->Release(device);adapter->lpVtbl->Release(adapter);factory->lpVtbl->Release(factory);
    puts("PASS actual two-slot source: blocked GPU ownership, five upload buffers, alternating FXAA descriptors, settings off/on, shutdown/reinit; zero debug errors");return 0;
}
