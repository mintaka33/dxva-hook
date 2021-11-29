#include <windows.h>
#include <d3d10umddi.h>
#include <stdio.h>

#include <fstream>
#include <map>

using namespace std;

#include "detours.h"

typedef HRESULT(APIENTRY* pfnOpenAdapter)(_Inout_ D3D10DDIARG_OPENADAPTER*);

LPCSTR gfxDriverPath = "C:\\Windows\\System32\\DriverStore\\FileRepository\\iigd_dch.inf_amd64_19812d3db79f7a21\\igd10iumd64.dll";

pfnOpenAdapter OpenAdapter = nullptr;
PFND3D10DDI_CREATEDEVICE DdiCreateDevice = nullptr;
PFND3DDDI_ALLOCATECB DdiAllocateCb = nullptr;
PFND3DDDI_LOCKCB DdiLockCb = nullptr;
PFND3DDDI_LOCK2CB DdiLock2Cb = nullptr;
PFND3DDDI_SUBMITCOMMANDCB DdiSubmitCommandCb = nullptr;
PFND3DDDI_SUBMITCOMMANDTOHWQUEUECB DdiSubmitCommandToHwQueueCb = nullptr;
PFND3DDDI_MAPGPUVIRTUALADDRESSCB DdiMapGpuVirtualAddressCb = nullptr;

map<D3DGPU_VIRTUAL_ADDRESS, D3DKMT_HANDLE> mapHandleGpuVA;
map<D3DKMT_HANDLE, PVOID> mapHandleCpuAddress;

HRESULT WrapperMapGpuVirtualAddressCb(HANDLE hDevice, D3DDDI_MAPGPUVIRTUALADDRESS* pMapGpuVaArg)
{
    HRESULT ret = S_OK;
    ret = DdiMapGpuVirtualAddressCb(hDevice, pMapGpuVaArg);
    mapHandleGpuVA[pMapGpuVaArg->VirtualAddress] = pMapGpuVaArg->hAllocation;
    printf("Enter WrapperMapGpuVirtualAddressCb, hAllocation = 0x%08llx, VirtualAddress = 0x%08llx\n", pMapGpuVaArg->hAllocation, pMapGpuVaArg->VirtualAddress);
    return ret;
}

HRESULT WrapperSubmitCommandCb(HANDLE hDevice, const D3DDDICB_SUBMITCOMMAND* pSubmitArg)
{
    HRESULT ret = S_OK;
    D3DGPU_VIRTUAL_ADDRESS cmdBufGpuVA = pSubmitArg->Commands - pSubmitArg->PrivateDriverDataSize;
    PVOID pCpuAddress = mapHandleCpuAddress[mapHandleGpuVA[cmdBufGpuVA]];
    printf("#### Enter WrapperSubmitCommandCb, CommandVA = 0x%08llx, CommandLength = %d, CpuAddress = 0x%08llx\n", 
        cmdBufGpuVA, pSubmitArg->CommandLength, pCpuAddress);

    if (1 && pCpuAddress)
    {
        ofstream outfile;
        outfile.open("cmdbuf.bin", ios::binary);
        outfile.write((const char*)pCpuAddress, pSubmitArg->CommandLength);
        outfile.close();
    }

    ret = DdiSubmitCommandCb(hDevice, pSubmitArg);
    return ret;
}

HRESULT WrapperSubmitCommandToHwQueueCb(HANDLE hDevice, const D3DDDICB_SUBMITCOMMANDTOHWQUEUE* pSubmitHwQueueArg)
{
    HRESULT ret = S_OK;
    printf("Enter WrapperSubmitCommandToHwQueueCb\n");
    ret = DdiSubmitCommandToHwQueueCb(hDevice, pSubmitHwQueueArg);
    return ret;
}

HRESULT WrapperLock2Cb(HANDLE hDevice, D3DDDICB_LOCK2* pLock2Arg)
{
    HRESULT ret = S_OK;
    ret = DdiLock2Cb(hDevice, pLock2Arg);
    mapHandleCpuAddress[pLock2Arg->hAllocation] = pLock2Arg->pData;
    printf("Enter WrapperLock2Cb hAllocation = 0x%08x, Flags = 0x%08x\n", pLock2Arg->hAllocation, pLock2Arg->Flags.Value);
    return ret;
}

HRESULT WrapperLockCb(HANDLE hDevice, D3DDDICB_LOCK* pLockArg)
{
    HRESULT ret = S_OK;
    printf("Enter WrapperLockCb \n");
    ret = DdiLockCb(hDevice, pLockArg);
    return ret;
}

HRESULT WrapperAllocateCb(HANDLE hDevice, D3DDDICB_ALLOCATE* pAllocArg)
{
    HRESULT ret = S_OK;
    ret = DdiAllocateCb(hDevice, pAllocArg);
    printf("Leave WrapperAllocateCb: Input[0x%08x, %d], hAllocation = 0x%08x\n", pAllocArg->hResource, pAllocArg->NumAllocations, pAllocArg->pAllocationInfo->hAllocation);
    return ret;
}

HRESULT APIENTRY WarpDdiCreateDevice(
    D3D10DDI_HADAPTER hAdapter,
    D3D10DDIARG_CREATEDEVICE* pDeviceData)
{
    HRESULT ret = S_OK;
    D3DDDI_DEVICECALLBACKS* tmpCallback = const_cast<D3DDDI_DEVICECALLBACKS*> (pDeviceData->pKTCallbacks);
    DdiAllocateCb = pDeviceData->pKTCallbacks->pfnAllocateCb;
    DdiLockCb = pDeviceData->pKTCallbacks->pfnLockCb;
    DdiLock2Cb = pDeviceData->pKTCallbacks->pfnLock2Cb;
    DdiSubmitCommandCb = pDeviceData->pKTCallbacks->pfnSubmitCommandCb;
    DdiSubmitCommandToHwQueueCb = pDeviceData->pKTCallbacks->pfnSubmitCommandToHwQueueCb;
    DdiMapGpuVirtualAddressCb = pDeviceData->pKTCallbacks->pfnMapGpuVirtualAddressCb;
    tmpCallback->pfnAllocateCb = WrapperAllocateCb;
    tmpCallback->pfnLockCb = WrapperLockCb;
    tmpCallback->pfnLock2Cb = WrapperLock2Cb;
    tmpCallback->pfnSubmitCommandCb = WrapperSubmitCommandCb;
    tmpCallback->pfnSubmitCommandToHwQueueCb = WrapperSubmitCommandToHwQueueCb;
    tmpCallback->pfnMapGpuVirtualAddressCb = WrapperMapGpuVirtualAddressCb;

    ret = DdiCreateDevice(hAdapter, pDeviceData);

    return ret;
}

HRESULT APIENTRY WarpOpenAdapter(D3D10DDIARG_OPENADAPTER* pAdapterData)
{
    printf("Enter WarpOpenAdapter\n");
    HRESULT ret = OpenAdapter(pAdapterData);
    printf("Leave WarpOpenAdapter\n");

    if (ret == S_OK && pAdapterData->pAdapterFuncs->pfnCreateDevice)
    {
        DdiCreateDevice = pAdapterData->pAdapterFuncs->pfnCreateDevice;
        pAdapterData->pAdapterFuncs->pfnCreateDevice = WarpDdiCreateDevice;
    }

    return ret;
}

BOOL DetourOpenAdapter()
{
    HMODULE hLib = LoadLibraryExA(gfxDriverPath, nullptr, 0);
    OpenAdapter = (pfnOpenAdapter)GetProcAddress(hLib, "OpenAdapter10_2");

    if (OpenAdapter)
    {
        DetourRestoreAfterWith();
        DetourTransactionBegin();
        DetourUpdateThread(GetCurrentThread());
        DetourAttach((void**)&OpenAdapter, WarpOpenAdapter);
        DetourTransactionCommit();
    }

    return OpenAdapter != NULL;
}

