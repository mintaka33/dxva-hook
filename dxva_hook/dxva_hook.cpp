#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <tchar.h>
#include <memory.h>

#include <iostream>
#include <string>
#include <map>
#include <vector>

#include "dxva_data.h"

#include "wrapper.h"

using namespace std;

#define FREE_RESOURCE(res) \
    if(res) {res->Release(); res = NULL;}

#define CHECK_SUCCESS(hr, msg) \
    if (!SUCCEEDED(hr)) { printf("ERROR: Failed to call %s\n", msg); return -1; }

int dxva_decode()
{
    DXVAData dxvaDecData = g_dxvaDataAVC_Short;
    HRESULT hr = S_OK;
    ID3D11Device* pD3D11Device = NULL;
    ID3D11DeviceContext* pDeviceContext = NULL;
    D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1 };
    D3D_FEATURE_LEVEL fl;
    GUID profile = dxvaDecData.guidDecoder;

    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 1,
        D3D11_SDK_VERSION, &pD3D11Device, &fl, &pDeviceContext);
    CHECK_SUCCESS(hr, "D3D11CreateDevice");

    ID3D11VideoDevice* pD3D11VideoDevice = NULL;
    hr = pD3D11Device->QueryInterface(&pD3D11VideoDevice);
    CHECK_SUCCESS(hr, "QueryInterface");

    ID3D11VideoDecoder* pVideoDecoder = NULL;
    D3D11_VIDEO_DECODER_DESC decoderDesc = { 0 };
    decoderDesc.Guid = profile;
    decoderDesc.SampleWidth = dxvaDecData.picWidth;
    decoderDesc.SampleHeight = dxvaDecData.picHeight;
    decoderDesc.OutputFormat = DXGI_FORMAT_NV12;
    D3D11_VIDEO_DECODER_CONFIG config = { 0 };
    config.ConfigBitstreamRaw = dxvaDecData.isShortFormat; // 0: long format; 1: short format
    hr = pD3D11VideoDevice->CreateVideoDecoder(&decoderDesc, &config, &pVideoDecoder);
    CHECK_SUCCESS(hr, "CreateVideoDecoder");

    ID3D11Texture2D* pSurfaceDecodeNV12 = NULL;
    D3D11_TEXTURE2D_DESC descRT = { 0 };
    descRT.Width = dxvaDecData.picWidth;
    descRT.Height = dxvaDecData.picHeight;
    descRT.MipLevels = 1;
    descRT.ArraySize = 1;
    descRT.Format = DXGI_FORMAT_NV12;
    descRT.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC 
    descRT.Usage = D3D11_USAGE_DEFAULT; // D3D11_USAGE 
    descRT.BindFlags = D3D11_BIND_DECODER;
    descRT.CPUAccessFlags = 0;
    descRT.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    hr = pD3D11Device->CreateTexture2D(&descRT, NULL, &pSurfaceDecodeNV12);
    CHECK_SUCCESS(hr, "CreateTexture2D");

    ID3D11Texture2D* pSurfaceCopyStaging = NULL;
    D3D11_TEXTURE2D_DESC descStaging = { 0 };
    descStaging.Width = dxvaDecData.picWidth;
    descStaging.Height = dxvaDecData.picHeight;
    descStaging.MipLevels = 1;
    descStaging.ArraySize = 1;
    descStaging.Format = DXGI_FORMAT_NV12;
    descStaging.SampleDesc = { 1, 0 }; // DXGI_SAMPLE_DESC 
    descStaging.Usage = D3D11_USAGE_STAGING; // D3D11_USAGE 
    descStaging.BindFlags = 0;
    descStaging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    descStaging.MiscFlags = 0;
    hr = pD3D11Device->CreateTexture2D(&descStaging, NULL, &pSurfaceCopyStaging);
    CHECK_SUCCESS(hr, "CreateTexture2D");

    ID3D11VideoDecoderOutputView* pDecodeOutputView = NULL;
    D3D11_VIDEO_DECODER_OUTPUT_VIEW_DESC viewDesc = { 0 };
    viewDesc.DecodeProfile = profile;
    viewDesc.ViewDimension = D3D11_VDOV_DIMENSION_TEXTURE2D;
    hr = pD3D11VideoDevice->CreateVideoDecoderOutputView(pSurfaceDecodeNV12, &viewDesc, &pDecodeOutputView);
    CHECK_SUCCESS(hr, "CreateVideoDecoderOutputView");

    UINT profileCount = 0;
    GUID decoderGUID = {};
    profileCount = pD3D11VideoDevice->GetVideoDecoderProfileCount();
    printf("INFO: Decoder Profile Count = %d\n", profileCount);

    for (UINT i = 0; i < profileCount; i++)
    {
        hr = pD3D11VideoDevice->GetVideoDecoderProfile(i, &decoderGUID);
        CHECK_SUCCESS(hr, "GetVideoDecoderProfile");
        OLECHAR sGUID[64] = { 0 };
        StringFromGUID2(decoderGUID, sGUID, 64);
        //wprintf(L"INFO: Index %02d - GUID = %s\n", i, sGUID);
    }

    ID3D11VideoContext* pVideoContext = NULL;
    hr = pDeviceContext->QueryInterface(&pVideoContext);
    CHECK_SUCCESS(hr, "QueryInterface");

    // Decode begin frame
    hr = pVideoContext->DecoderBeginFrame(pVideoDecoder, pDecodeOutputView, 0, 0);
    CHECK_SUCCESS(hr, "DecoderBeginFrame");

    // Prepare DXVA buffers for decoding
    UINT sizeDesc = sizeof(D3D11_VIDEO_DECODER_BUFFER_DESC) * dxvaDecData.dxvaBufNum;
    D3D11_VIDEO_DECODER_BUFFER_DESC* descDecBuffers = new D3D11_VIDEO_DECODER_BUFFER_DESC[dxvaDecData.dxvaBufNum];
    memset(descDecBuffers, 0, sizeDesc);
    for (UINT i = 0; i < dxvaDecData.dxvaBufNum; i++)
    {
        BYTE* buffer = 0;
        UINT bufferSize = 0;
        descDecBuffers[i].BufferIndex = i;
        descDecBuffers[i].BufferType = dxvaDecData.dxvaDecBuffers[i].bufType;
        descDecBuffers[i].DataSize = dxvaDecData.dxvaDecBuffers[i].bufSize;

        hr = pVideoContext->GetDecoderBuffer(pVideoDecoder, descDecBuffers[i].BufferType, &bufferSize, reinterpret_cast<void**>(&buffer));
        CHECK_SUCCESS(hr, "GetDecoderBuffer");
        UINT copySize = min(bufferSize, descDecBuffers[i].DataSize);
        memcpy_s(buffer, copySize, dxvaDecData.dxvaDecBuffers[i].pBufData, copySize);
        hr = pVideoContext->ReleaseDecoderBuffer(pVideoDecoder, descDecBuffers[i].BufferType);
        CHECK_SUCCESS(hr, "ReleaseDecoderBuffer");
    }

    // Submit decode workload to GPU
    hr = pVideoContext->SubmitDecoderBuffers(pVideoDecoder, dxvaDecData.dxvaBufNum, descDecBuffers);
    CHECK_SUCCESS(hr, "SubmitDecoderBuffers");
    delete[] descDecBuffers;

    // Decode end frame
    hr = pVideoContext->DecoderEndFrame(pVideoDecoder);
    CHECK_SUCCESS(hr, "DecoderEndFrame");

    // Map decode surface and dump NV12 to file
    if (1)
    {
        D3D11_BOX box;
        box.left = 0,
            box.right = dxvaDecData.picWidth,
            box.top = 0,
            box.bottom = dxvaDecData.picHeight,
            box.front = 0,
            box.back = 1;
        pDeviceContext->CopySubresourceRegion(pSurfaceCopyStaging, 0, 0, 0, 0, pSurfaceDecodeNV12, 0, &box);
        D3D11_MAPPED_SUBRESOURCE subRes;
        ZeroMemory(&subRes, sizeof(subRes));
        hr = pDeviceContext->Map(pSurfaceCopyStaging, 0, D3D11_MAP_READ, 0, &subRes);
        CHECK_SUCCESS(hr, "Map");

        UINT height = dxvaDecData.picHeight;
        BYTE* pData = (BYTE*)malloc(subRes.RowPitch * (height + height / 2));
        if (pData)
        {
            CopyMemory(pData, subRes.pData, subRes.RowPitch * (height + height / 2));
            FILE* fp;
            char fileName[256] = {};
            sprintf_s(fileName, 256, "out_%d_%d_nv12.yuv", subRes.RowPitch, height);
            fopen_s(&fp, fileName, "wb");
            fwrite(pData, subRes.RowPitch * (height + height / 2), 1, fp);
            fclose(fp);
            free(pData);
        }
        pDeviceContext->Unmap(pSurfaceCopyStaging, 0);
    }

    FREE_RESOURCE(pDeviceContext);
    FREE_RESOURCE(pSurfaceDecodeNV12);
    FREE_RESOURCE(pSurfaceCopyStaging);
    FREE_RESOURCE(pD3D11VideoDevice);
    FREE_RESOURCE(pVideoDecoder);
    FREE_RESOURCE(pDecodeOutputView);
    FREE_RESOURCE(pVideoContext);
    FREE_RESOURCE(pD3D11Device);

    printf("INFO: execution done. \n");

    return 0;
}

int main(char argc, char** argv)
{
    DetourOpenAdapter();

    dxva_decode();

    return 0;
}
