// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	EActionResult CDaemon::CDetails::f_Start()
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;

		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonStart();

		SC_HANDLE schSCManager = fp_OpenSCManager();

		if (!schSCManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}
				
		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schSCManager);
				}
			)
		;

		SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_START | SERVICE_QUERY_STATUS);

		if (schService)
		{
			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(schService);
					}
				)
			;
			SERVICE_STATUS Status;

			if (!QueryServiceStatus(schService, &Status))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			if (Status.dwCurrentState == SERVICE_RUNNING || Status.dwCurrentState == SERVICE_START_PENDING)
				return EActionResult_Success;

			if (!StartService(schService, 0, nullptr))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to start daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			while (1)
			{
				if (!QueryServiceStatus(schService, &Status))
					break;

				if (Status.dwCurrentState != SERVICE_START_PENDING)
					break;
				Sleep(10);
			}
		}
		else
		{	
			f_ReportError(NStr::CStr::CFormat("Unable to start daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::f_Stop(bint _bWait)
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;
				
		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonStop(_bWait);

		bool bDaemonExists;
		if (f_Exists(bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;
				
		if (!bDaemonExists)
		{
			f_ReportInformation("Stop Daemon", "Daemon is not installed so it has not been stopped");
			return EActionResult_Success;
		}
				
		SC_HANDLE schSCManager = fp_OpenSCManager();
		if (!schSCManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schSCManager);
				}
			)
		;

		SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_STOP | SERVICE_QUERY_STATUS);

		if (schService)
		{

			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(schService);
					}
				)
			;

			SERVICE_STATUS Status;

			if (!QueryServiceStatus(schService, &Status))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			if (Status.dwCurrentState != SERVICE_STOPPED)
			{
				uint32 Control = SERVICE_CONTROL_STOP;
				SERVICE_STATUS Status;

				if (!ControlService(schService, Control, &Status))
				{
					uint32 Error = GetLastError();

					if (Error != ERROR_SERVICE_NOT_ACTIVE)
					{
						f_ReportError(NStr::CStr::CFormat("Unable to stop daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
						return EActionResult_Failure;
					}
					else if (!_bWait)
						return EActionResult_Success;
				}
			}
			else if (!_bWait)
				return EActionResult_Success;

			SERVICE_STATUS_PROCESS ProcessStatus;
			NMemory::fg_MemClear(ProcessStatus);

			DWORD SizeNeeded = 0;

			QueryServiceStatusEx(
				schService,
				SC_STATUS_PROCESS_INFO,
				(LPBYTE) &ProcessStatus,
				sizeof(ProcessStatus),
				&SizeNeeded
				);

			HANDLE hProcess = nullptr;
			if (ProcessStatus.dwProcessId)
				hProcess = OpenProcess(SYNCHRONIZE, false, ProcessStatus.dwProcessId);

			auto Cleanup = fg_OnScopeExit
				(
					[&]
					{
						if (hProcess)
							CloseHandle(hProcess);
					}
				)
			;

			while (1)
			{
				if (!QueryServiceStatus(schService, &Status))
					break;

				if (Status.dwCurrentState == SERVICE_STOPPED)
					break;
				Sleep(10);
			}

			if (hProcess)
			{
				WaitForSingleObject(hProcess, 240000);
			}
		}
		else
		{	
			f_ReportError(NStr::CStr::CFormat("Unable to stop daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::f_Restart(bint _bWait)
	{
		EActionResult Result = f_Stop(_bWait);
		if (Result != EActionResult_Success)
			return Result;
		return f_Start();
	}
}
