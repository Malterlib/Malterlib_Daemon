// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	EActionResult CDaemon::CDetails::f_Start()
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;

		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonStart();

		SC_HANDLE ServiceManager = fp_OpenSCManager();

		if (!ServiceManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(ServiceManager);
				}
			)
		;

		SC_HANDLE Service = OpenServiceW(ServiceManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_START | SERVICE_QUERY_STATUS);

		if (Service)
		{
			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(Service);
					}
				)
			;
			SERVICE_STATUS Status;

			if (!QueryServiceStatus(Service, &Status))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			if (Status.dwCurrentState == SERVICE_RUNNING || Status.dwCurrentState == SERVICE_START_PENDING)
				return EActionResult_Success;

			if (!StartService(Service, 0, nullptr))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to start daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			while (1)
			{
				if (!QueryServiceStatus(Service, &Status))
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

	EActionResult CDaemon::CDetails::f_Stop(bool _bWait)
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

		SC_HANDLE ServiceManager = fp_OpenSCManager();
		if (!ServiceManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(ServiceManager);
				}
			)
		;

		SC_HANDLE Service = OpenServiceW(ServiceManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_STOP | SERVICE_QUERY_STATUS);

		if (Service)
		{
			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(Service);
					}
				)
			;

			SERVICE_STATUS Status;

			if (!QueryServiceStatus(Service, &Status))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}

			if (Status.dwCurrentState != SERVICE_STOPPED)
			{
				uint32 Control = SERVICE_CONTROL_STOP;
				SERVICE_STATUS Status;

				if (!ControlService(Service, Control, &Status))
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

			QueryServiceStatusEx(Service, SC_STATUS_PROCESS_INFO, (LPBYTE) &ProcessStatus, sizeof(ProcessStatus), &SizeNeeded);

			HANDLE Process = nullptr;
			if (ProcessStatus.dwProcessId)
				Process = OpenProcess(SYNCHRONIZE, false, ProcessStatus.dwProcessId);

			auto Cleanup = fg_OnScopeExit
				(
					[&]
					{
						if (Process)
							CloseHandle(Process);
					}
				)
			;

			while (1)
			{
				if (!QueryServiceStatus(Service, &Status))
					break;

				if (Status.dwCurrentState == SERVICE_STOPPED)
					break;
				Sleep(10);
			}

			if (Process)
				WaitForSingleObject(Process, 240000);
		}
		else
		{
			f_ReportError(NStr::CStr::CFormat("Unable to stop daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::f_Restart(bool _bWait)
	{
		EActionResult Result = f_Stop(_bWait);
		if (Result != EActionResult_Success)
			return Result;
		return f_Start();
	}
}
