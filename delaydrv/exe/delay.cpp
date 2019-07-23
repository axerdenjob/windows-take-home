// ***********************************************************************
// Assembly         :
// Author           : aerden
// Created          : 07-18-2019
//
// Last Modified By : aerden
// Last Modified On : 07-18-2019
// ***********************************************************************
// <copyright file="delay.cpp" company="">
//     Copyright (c) . All rights reserved.
// </copyright>
// <summary></summary>
// ***********************************************************************
#include "windows.h"
#include "winioctl.h"
#include "strsafe.h"

#ifndef _CTYPE_DISABLE_MACROS
#define _CTYPE_DISABLE_MACROS
#endif

#include "winsock2.h"
#include "ws2def.h"
#include <conio.h>
#include <stdio.h>
#include "ioctl.h"
#include <iostream>

/// <summary>
/// Opens the device.
/// </summary>
/// <param name="delayDevice">The delay device.</param>
/// <returns>DWORD.</returns>
DWORD
OpenDevice(
    _Out_ HANDLE* delayDevice)
{
    DWORD result = NO_ERROR;

    if (nullptr != delayDevice)
    {
        *delayDevice = CreateFileW(DELAY_DOS_NAME,
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);

        if (INVALID_HANDLE_VALUE == *delayDevice)
        {
            result = GetLastError();
        }
    }
    else
    {
        result = ERROR_INVALID_PARAMETER;
    }

    return ERROR_SUCCESS;
}

/// <summary>
/// Closes the device.
/// </summary>
/// <param name="delayDevice">The delay device.</param>
/// <returns>BOOL.</returns>
BOOL
CloseDevice(
    _In_ HANDLE delayDevice)
{
    return CloseHandle(delayDevice);
}

/// <summary>
/// Enables the device.
/// </summary>
/// <returns>DWORD.</returns>
DWORD
EnableDevice()
{
    HANDLE delayDevice = nullptr;
    DWORD result;
    DWORD bytesReturned;

    result = OpenDevice(&delayDevice);
    if (SUCCEEDED(result))
    {
        if (!DeviceIoControl(delayDevice,
            IOCTL_ENABLE_DELAY,
            nullptr,
            0,
            nullptr,
            0,
            &bytesReturned,
            nullptr))
        {
            result = GetLastError();
            std::cout << result;
        }
    }

    if (FAILED(result))
    {
        std::cout << L"Error " << result << " occurred" << std::endl;
    }

    if (delayDevice)
    {
        CloseDevice(delayDevice);
    }

    return result;
}

/// <summary>
/// Disables the device.
/// </summary>
/// <returns>DWORD.</returns>
DWORD
DisableDevice()
{
    HANDLE delayDevice = nullptr;
    DWORD result;
    DWORD bytesReturned;

    result = OpenDevice(&delayDevice);
    if (SUCCEEDED(result))
    {
        if (delayDevice)
        {
            if (!DeviceIoControl(delayDevice,
                IOCTL_DISABLE_DELAY,
                nullptr,
                0,
                nullptr,
                0,
                &bytesReturned,
                nullptr))
            {
                result = GetLastError();
                std::cout << result;
            }

            CloseDevice(delayDevice);
        }
    }

    return result;
}

/// <summary>
/// Prints the usage.
/// </summary>
void
PrintUsage()
{
    wprintf(L"Usage: delayapp [ on | off ]\n");
}

/// <summary>
/// Processes the arguments.
/// </summary>
/// <param name="argc">The argc.</param>
/// <param name="argv">The argv.</param>
/// <returns>DWORD.</returns>
DWORD
ProcessArguments(_In_ int argc, _In_reads_(argc) PCWSTR argv[])
{
    if (argc == 2)
    {
        if (_wcsicmp(argv[1], L"on") == 0)
        {
            return EnableDevice();
        }
        else if (_wcsicmp(argv[1], L"off") == 0)
        {
            return DisableDevice();
        }
    }

    PrintUsage();

    return ERROR_INVALID_PARAMETER;
}

/// <summary>
/// Wmains the specified argc.
/// </summary>
/// <param name="argc">The argc.</param>
/// <param name="argv">The argv.</param>
/// <returns>int .</returns>
int
__cdecl wmain(
    _In_ int argc,
    _In_reads_(argc) PCWSTR argv[])
{
    return (int)ProcessArguments(argc, argv);
}
