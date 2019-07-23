// ***********************************************************************
// Assembly         :
// Author           : aerden
// Created          : 07-18-2019
//
// Last Modified By : aerden
// Last Modified On : 07-18-2019
// ***********************************************************************
// <copyright file="driver.c" company="">
//     Copyright (c) . All rights reserved.
// </copyright>
// <summary></summary>
// ***********************************************************************
#include <ndis.h>
#include <ntddk.h>
#include <wdf.h>

#include <fwpmk.h>

#pragma warning(push)
#pragma warning(disable:4201)
#include <fwpsk.h>
#pragma warning(pop)

#include "ioctl.h"
#include <stddef.h>

#define WPP_CONTROL_GUIDS \
    WPP_DEFINE_CONTROL_GUID(delaydrv,(e7db16bb, 41be, 4c05, b73e, 5feca06f8207),  \
        WPP_DEFINE_BIT(TRACE_INIT)               \
        WPP_DEFINE_BIT(TRACE_SHUTDOWN) \
        WPP_DEFINE_BIT(TRACE_STATE_CHANGE)      \
        WPP_DEFINE_BIT(TRACE_LAYER_NOTIFY) \
        WPP_DEFINE_BIT(TRACE_FLOW_ESTABLISHED)      )

#include "driver.tmh"
#include <fwpvi.h>
#include <fwptypes.h>
#include <sal.h>
#include "intsafe.h"

#include <storport.h>

#define INITGUID
#include <guiddef.h>

// {473B37A0-844A-4822-8BFB-CB8BDE41C548}
DEFINE_GUID(DELAY_SUBLAYER,
    0x473b37a0, 0x844a, 0x4822, 0x8b, 0xfb, 0xcb, 0x8b, 0xde, 0x41, 0xc5, 0x48);

// {EAFB1F6F-60FB-4272-8A7A-91269E8D93A8}
DEFINE_GUID(DELAY_OUTBOUND_CALLOUT_V6,
    0xeafb1f6f, 0x60fb, 0x4272, 0x8a, 0x7a, 0x91, 0x26, 0x9e, 0x8d, 0x93, 0xa8);

// {76367278-18BE-4117-9990-8C2FA6CF5BAC}
DEFINE_GUID(DELAY_OUTBOUND_CALLOUT_V4,
    0x76367278, 0x18be, 0x4117, 0x99, 0x90, 0x8c, 0x2f, 0xa6, 0xcf, 0x5b, 0xac);

// {94554E73-6E41-4150-AC5D-E8796C5FFE6F}
DEFINE_GUID(DELAY_INBOUND_CALLOUT_V6,
    0x94554e73, 0x6e41, 0x4150, 0xac, 0x5d, 0xe8, 0x79, 0x6c, 0x5f, 0xfe, 0x6f);

// {01F18EA0-21D3-457E-8D72-DDD41FA6803A}
DEFINE_GUID(DELAY_INBOUND_CALLOUT_V4,
    0x1f18ea0, 0x21d3, 0x457e, 0x8d, 0x72, 0xdd, 0xd4, 0x1f, 0xa6, 0x80, 0x3a);

// {BA90F67C-9CF6-48F3-80D7-0EC4EB54E66C}
DEFINE_GUID(DELAY_CONNECT_CALLOUT_V6,
    0xba90f67c, 0x9cf6, 0x48f3, 0x80, 0xd7, 0xe, 0xc4, 0xeb, 0x54, 0xe6, 0x6c);

// {526DFE8D-4BE3-48BE-831D-4E84B75ECF98}
DEFINE_GUID(DELAY_CONNECT_CALLOUT_V4,
    0x526dfe8d, 0x4be3, 0x48be, 0x83, 0x1d, 0x4e, 0x84, 0xb7, 0x5e, 0xcf, 0x98);

// {6F787CB4-60C5-4E7C-9E4B-4DBCEA8BE6C6}
DEFINE_GUID(DELAY_RECVACCEPT_CALLOUT_V6,
    0x6f787cb4, 0x60c5, 0x4e7c, 0x9e, 0x4b, 0x4d, 0xbc, 0xea, 0x8b, 0xe6, 0xc6);

// {01D2AA6C-9537-4ECA-B720-C8CE13BDAF55}
DEFINE_GUID(DELAY_RECVACCEPT_CALLOUT_V4,
    0x1d2aa6c, 0x9537, 0x4eca, 0xb7, 0x20, 0xc8, 0xce, 0x13, 0xbd, 0xaf, 0x55);

#define TAG_NAME_CALLOUT 'CnoM'

/// <summary>
/// The g WDM device
/// </summary>
DEVICE_OBJECT* gWdmDevice;
/// <summary>
/// The driver entry
/// </summary>
DRIVER_INITIALIZE DriverEntry;
/// <summary>
/// The evt driver unload
/// </summary>
EVT_WDF_DRIVER_UNLOAD EvtDriverUnload;

NTSTATUS
EvtDeviceAdd(
    _In_ PWDFDEVICE_INIT pInit
);

/// <summary>
/// The g delay enabled
/// </summary>
long g_delay_enabled = 0;

UINT32 g_ConnectCalloutIdV4 = 0;
UINT32 g_ConnectCalloutIdV6 = 0;
UINT32 g_OutboundCalloutIdV4 = 0;
UINT32 g_OutboundCalloutIdV6 = 0;
UINT32 g_RecvAcceptCalloutIdV4 = 0;
UINT32 g_RecvAcceptCalloutIdV6 = 0;
UINT32 g_InboundCalloutIdV4 = 0;
UINT32 g_InboundCalloutIdV6 = 0;

HANDLE gEngineHandle = NULL;

/// <summary>
/// Streams the callout v4.
/// </summary>
/// <param name="inFixedValues">The in fixed values.</param>
/// <param name="inMetaValues">The in meta values.</param>
/// <param name="packet">The packet.</param>
/// <param name="classifyContext">The classify context.</param>
/// <param name="filter">The filter.</param>
/// <param name="flowContext">The flow context.</param>
/// <param name="classifyOut">The classify out.</param>
/// <returns>NTSTATUS.</returns>
NTSTATUS Callout(
    _In_ const FWPS_INCOMING_VALUES* inFixedValues,
    _In_ const FWPS_INCOMING_METADATA_VALUES* inMetaValues,
    _Inout_opt_ void* packet,
    _In_opt_ const void* classifyContext,
    _In_ const FWPS_FILTER* filter,
    _In_ UINT64 flowContext,
    _Inout_ FWPS_CLASSIFY_OUT* classifyOut)
{
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(inFixedValues);
    UNREFERENCED_PARAMETER(inMetaValues);
    UNREFERENCED_PARAMETER(classifyContext);
    UNREFERENCED_PARAMETER(filter);
    UNREFERENCED_PARAMETER(flowContext);
    UNREFERENCED_PARAMETER(packet);

    if (g_delay_enabled)
    {
        // Generally a bad idea to induce any kind of wait in callout function.
        // It would be better to queue items and reinject after timeout in background thread.
        // KeDelayExecutionThread would be better if possible (aside from being bad here to wait)
        // but it causes BSOD because for good reason... windows does not like 
        // delays in DPCs. This solution is a hack and is not very maintainable
        // as the same "ATTEMPTED SWITCH FROM DPC" check could be added at 
        // some point to StorPortStallExecution.

        // Packet injection solution, implemented optimally...
        // would need processing threads x logical cores
        // and connect/packet queues per flow with locks
        // to prevent out of band data injection.

        /*LARGE_INTEGER intervalTime;

        intervalTime.QuadPart = -1000000;

        KeDelayExecutionThread(
            KernelMode,
            FALSE,
            &intervalTime
        );*/

        // StorPortStallExecution
        // Documentation for this function notes..
        // 'The given value must be less than a full millisecond'
        // This generally works, however may contemplate also adding a 
        // time computation with KeQueryTickCount/KeQueryTimeIncrement.
        // Note* CPU Usage on test VM is roughly 14% for this. Room for improvement by 
        // moving to packet injection.
        UINT32 waittotal = 100 * 1000;
        UINT wait;

        while (waittotal > 0)
        {
            wait = min(999, waittotal);
            StorPortStallExecution(wait);
            waittotal -= wait;
        }
    }

    classifyOut->actionType = FWP_ACTION_CONTINUE;

    return status;
}

/// <summary>
/// Streams the notify v4.
/// </summary>
/// <param name="notifyType">Type of the notify.</param>
/// <param name="filterKey">The filter key.</param>
/// <param name="filter">The filter.</param>
/// <returns>NTSTATUS.</returns>
NTSTATUS Notify(
    _In_ FWPS_CALLOUT_NOTIFY_TYPE notifyType,
    _In_ const GUID* filterKey,
    _Inout_ const FWPS_FILTER* filter)
{
    UNREFERENCED_PARAMETER(notifyType);
    UNREFERENCED_PARAMETER(filterKey);
    UNREFERENCED_PARAMETER(filter);

    return STATUS_SUCCESS;
}

/// <summary>
/// The evt device control
/// </summary>
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL EvtDeviceControl;

/// <summary>
/// Controls the driver initialize.
/// </summary>
/// <param name="pDevice">The p device.</param>
/// <returns>NTSTATUS.</returns>
NTSTATUS
CtlDriverInit(
    _In_ WDFDEVICE* pDevice
)
{
    WDF_IO_QUEUE_CONFIG queueConfig;

    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,
        WdfIoQueueDispatchSequential
    );

    queueConfig.EvtIoDeviceControl = EvtDeviceControl;

    return WdfIoQueueCreate(
        *pDevice,
        &queueConfig,
        WDF_NO_OBJECT_ATTRIBUTES,
        NULL
    );
}

/// <summary>
/// Evts the device control.
/// </summary>
/// <param name="Queue">The queue.</param>
/// <param name="Request">The request.</param>
/// <param name="OutputBufferLength">Length of the output buffer.</param>
/// <param name="InputBufferLength">Length of the input buffer.</param>
/// <param name="IoControlCode">The io control code.</param>
VOID
EvtDeviceControl(
    _In_ WDFQUEUE Queue,
    _In_ WDFREQUEST Request,
    _In_ size_t OutputBufferLength,
    _In_ size_t InputBufferLength,
    _In_ ULONG IoControlCode
)
{
    NTSTATUS status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Queue);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(InputBufferLength);

    switch (IoControlCode)
    {
    case IOCTL_ENABLE_DELAY:
    {
        status = STATUS_SUCCESS;
        g_delay_enabled = 1;

        break;
    }

    case IOCTL_DISABLE_DELAY:
    {
        status = STATUS_SUCCESS;
        g_delay_enabled = 0;

        break;
    }

    default:
    {
        status = STATUS_INVALID_PARAMETER;
    }
    }

    WdfRequestComplete(Request, status);
}

/// <summary>
/// Drivers the entry.
/// </summary>
/// <param name="driverObject">The driver object.</param>
/// <param name="registryPath">The registry path.</param>
/// <returns>NTSTATUS.</returns>
NTSTATUS
DriverEntry(
    _In_ DRIVER_OBJECT* driverObject,
    _In_ UNICODE_STRING* registryPath
)
{
    NTSTATUS status;
    WDF_DRIVER_CONFIG config;
    WDFDRIVER driver;
    PWDFDEVICE_INIT pInit = NULL;

    ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

    WPP_INIT_TRACING(driverObject, registryPath);

    WDF_DRIVER_CONFIG_INIT(&config, WDF_NO_EVENT_CALLBACK);
    config.DriverInitFlags |= WdfDriverInitNonPnpDriver;
    config.EvtDriverUnload = EvtDriverUnload;

    if (NT_SUCCESS(status = WdfDriverCreate(
        driverObject,
        registryPath,
        WDF_NO_OBJECT_ATTRIBUTES,
        &config,
        &driver
    )))
    {
        pInit = WdfControlDeviceInitAllocate(driver, &SDDL_DEVOBJ_SYS_ALL_ADM_ALL);

        if (pInit)
        {
            status = EvtDeviceAdd(pInit);
        }
        else
        {
            status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(status))
    {
        WPP_CLEANUP(driverObject);
    }

    return status;
}

NTSTATUS
AddFilter(
    _In_ const wchar_t* filterName,
    _In_ const wchar_t* filterDesc,
    _In_ UINT64 context,
    _In_ const GUID* layerKey,
    _In_ const GUID* calloutKey,
    _In_ const GUID* sublayerKey
)
{
    NTSTATUS status = STATUS_SUCCESS;

    FWPM_FILTER filter = { 0 };
    FWPM_FILTER_CONDITION filterConditions[1] = { 0 };

    filter.layerKey = *layerKey;
    filter.displayData.name = (wchar_t*)filterName;
    filter.displayData.description = (wchar_t*)filterDesc;

    filter.action.type = FWP_ACTION_CALLOUT_INSPECTION;
    filter.action.calloutKey = *calloutKey;
    filter.filterCondition = filterConditions;
    filter.subLayerKey = *sublayerKey;
    filter.weight.type = FWP_EMPTY;
    filter.rawContext = context;
    filter.numFilterConditions = 0;

    status = FwpmFilterAdd(
        gEngineHandle,
        &filter,
        NULL,
        NULL);

    return status;
}

NTSTATUS
RegisterCallout(
    _In_ const GUID* layerKey,
    _In_ const GUID* calloutKey,
    _Inout_ void* deviceObject,
    _Out_ UINT32* calloutId
)
{
    NTSTATUS status = STATUS_SUCCESS;

    FWPS_CALLOUT sCallout = { 0 };
    FWPM_CALLOUT mCallout = { 0 };

    FWPM_DISPLAY_DATA displayData = { 0 };

    displayData.name = L"Delay Classify Callout";
    displayData.description = L"delay callouts";

    mCallout.calloutKey = *calloutKey;
    mCallout.displayData = displayData;
    mCallout.applicableLayer = *layerKey;

    sCallout.calloutKey = *calloutKey;
    sCallout.classifyFn = Callout;
    sCallout.notifyFn = Notify;

    status = FwpsCalloutRegister(
        deviceObject,
        &sCallout,
        calloutId
    );

    if (NT_SUCCESS(status))
    {
        status = FwpmCalloutAdd(
            gEngineHandle,
            &mCallout,
            NULL,
            NULL
        );

        if (NT_SUCCESS(status))
        {
            status = AddFilter(
                L"Delay Classify",
                L"Delay filter",
                0,
                layerKey,
                calloutKey,
                &DELAY_SUBLAYER
            );
        }
        else
        {
            FwpsCalloutUnregisterById(*calloutId);
            *calloutId = 0;
        }
    }

    return status;
}

void
UnregisterCallouts(void)
{
    if (0 != g_ConnectCalloutIdV4) FwpsCalloutUnregisterById(g_ConnectCalloutIdV4);
    if (0 != g_ConnectCalloutIdV6) FwpsCalloutUnregisterById(g_ConnectCalloutIdV6);
    if (0 != g_OutboundCalloutIdV4) FwpsCalloutUnregisterById(g_OutboundCalloutIdV4);
    if (0 != g_OutboundCalloutIdV6) FwpsCalloutUnregisterById(g_OutboundCalloutIdV6);
    if (0 != g_RecvAcceptCalloutIdV4) FwpsCalloutUnregisterById(g_RecvAcceptCalloutIdV4);
    if (0 != g_RecvAcceptCalloutIdV6) FwpsCalloutUnregisterById(g_RecvAcceptCalloutIdV6);
    if (0 != g_InboundCalloutIdV4) FwpsCalloutUnregisterById(g_InboundCalloutIdV4);
    if (0 != g_InboundCalloutIdV6) FwpsCalloutUnregisterById(g_InboundCalloutIdV6);
}

NTSTATUS
RegisterCallouts(
    _Inout_ void* deviceObject
)
{
    NTSTATUS status = STATUS_SUCCESS;
    FWPM_SUBLAYER sub_layer;
    FWPM_SESSION session = { 0 };
    session.flags = FWPM_SESSION_FLAG_DYNAMIC;

    status = FwpmEngineOpen(
        NULL,
        RPC_C_AUTHN_WINNT,
        NULL,
        &session,
        &gEngineHandle
    );

    if (NT_SUCCESS(status))
    {
        status = FwpmTransactionBegin(gEngineHandle, 0);

        if (NT_SUCCESS(status))
        {
            RtlZeroMemory(&sub_layer, sizeof(FWPM_SUBLAYER));

            sub_layer.subLayerKey = DELAY_SUBLAYER;
            sub_layer.displayData.name = L"Delay Sub layer";
            sub_layer.displayData.description = L"Delay Sub layer";
            sub_layer.flags = 0;
            sub_layer.weight = 0;

            status = FwpmSubLayerAdd(gEngineHandle, &sub_layer, NULL);

            if (NT_SUCCESS(status))
            {
                RegisterCallout(&FWPM_LAYER_ALE_AUTH_CONNECT_V4, &DELAY_CONNECT_CALLOUT_V4, deviceObject, &g_ConnectCalloutIdV4);
                RegisterCallout(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V4, &DELAY_RECVACCEPT_CALLOUT_V4, deviceObject, &g_RecvAcceptCalloutIdV4);
                RegisterCallout(&FWPM_LAYER_OUTBOUND_TRANSPORT_V4, &DELAY_OUTBOUND_CALLOUT_V4, deviceObject, &g_OutboundCalloutIdV4);
                RegisterCallout(&FWPM_LAYER_INBOUND_TRANSPORT_V4, &DELAY_INBOUND_CALLOUT_V4, deviceObject, &g_InboundCalloutIdV4);
                RegisterCallout(&FWPM_LAYER_ALE_AUTH_CONNECT_V6, &DELAY_CONNECT_CALLOUT_V6, deviceObject, &g_ConnectCalloutIdV6);
                RegisterCallout(&FWPM_LAYER_ALE_AUTH_RECV_ACCEPT_V6, &DELAY_RECVACCEPT_CALLOUT_V6, deviceObject, &g_RecvAcceptCalloutIdV6);
                RegisterCallout(&FWPM_LAYER_OUTBOUND_TRANSPORT_V6, &DELAY_OUTBOUND_CALLOUT_V6, deviceObject, &g_OutboundCalloutIdV6);
                RegisterCallout(&FWPM_LAYER_INBOUND_TRANSPORT_V6, &DELAY_INBOUND_CALLOUT_V6, deviceObject, &g_InboundCalloutIdV6);

                if (0 == (
                    g_ConnectCalloutIdV4 |
                    g_ConnectCalloutIdV6 |
                    g_OutboundCalloutIdV4 |
                    g_OutboundCalloutIdV6 |
                    g_RecvAcceptCalloutIdV4 |
                    g_RecvAcceptCalloutIdV6 |
                    g_InboundCalloutIdV4 |
                    g_InboundCalloutIdV6))
                {
                    UnregisterCallouts();
                    status = STATUS_INVALID_PARAMETER;
                }
            }
        }
    }

    if (!NT_SUCCESS(status))
    {
        if (NULL != gEngineHandle)
        {
            FwpmTransactionAbort(gEngineHandle);
            FwpmEngineClose(gEngineHandle);
            gEngineHandle = NULL;
        }
    }
    
    return status;
}

/// <summary>
/// Evts the device add.
/// </summary>
/// <param name="pInit">The p initialize.</param>
/// <returns>NTSTATUS.</returns>
NTSTATUS
EvtDeviceAdd(
    _In_ PWDFDEVICE_INIT pInit
)
{
    NTSTATUS status;
    WDFDEVICE device;
    DECLARE_CONST_UNICODE_STRING(ntDeviceName, DELAY_DEVICE_NAME);
    DECLARE_CONST_UNICODE_STRING(symbolicName, DELAY_SYMBOLIC_NAME);

    WdfDeviceInitSetDeviceType(pInit, FILE_DEVICE_NETWORK);
    WdfDeviceInitSetCharacteristics(pInit, FILE_DEVICE_SECURE_OPEN, FALSE);

    if (NT_SUCCESS(status = WdfDeviceInitAssignName(pInit, &ntDeviceName)) &&
        NT_SUCCESS(status = WdfDeviceCreate(&pInit, WDF_NO_OBJECT_ATTRIBUTES, &device)) &&
        NT_SUCCESS(status = WdfDeviceCreateSymbolicLink(device, &symbolicName)) &&
        NT_SUCCESS(status = CtlDriverInit(&device)))
    {
        gWdmDevice = WdfDeviceWdmGetDeviceObject(device);

        if (NULL != gWdmDevice)
        {
            status = RegisterCallouts(gWdmDevice);

            WdfControlFinishInitializing(device);
        }
    }

    if (pInit)
    {
        WdfDeviceInitFree(pInit);
    }

    return status;
}

/// <summary>
/// Evts the driver unload.
/// </summary>
/// <param name="Driver">The driver.</param>
void
EvtDriverUnload(
    _In_ WDFDRIVER Driver
)
{
    DRIVER_OBJECT* driverObject;
    g_delay_enabled = 0;
    FwpmEngineClose(gEngineHandle);
    UnregisterCallouts();
    driverObject = WdfDriverWdmGetDriverObject(Driver);
    WPP_CLEANUP(driverObject);
}
