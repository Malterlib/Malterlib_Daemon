// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	SC_HANDLE CDaemon::CDetails::fp_OpenSCManager() const
	{
		return OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | DELETE | SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
	}

	EActionResult CDaemon::CDetails::f_Run()
	{
		NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName());
		SERVICE_TABLE_ENTRYW DispatchTable[] = { { (ch16 *)Temp.f_GetStr(), CDaemon::CDetails::fsp_ServiceStart}, { nullptr, nullptr} };

		if (!StartServiceCtrlDispatcherW(DispatchTable))
		{
			f_ReportError(NStr::CStr::CFormat("StartServiceCtrlDispatcher error: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	void CDaemon::CDetails::fs_AbortService()
	{
		if (!msp_pThis->mp_pStopThread)
		{
			msp_ServiceStatus.dwWin32ExitCode = ERROR_SUCCESS_RESTART_REQUIRED;
			msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING;
			msp_ServiceStatus.dwCheckPoint    = 0;
			msp_ServiceStatus.dwWaitHint      = 10000;

			msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CDaemon_Destroy");
			msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CDaemon_DestroyReport");

			if (!SetServiceStatus(msp_ServiceStatusHandle, &msp_ServiceStatus))
				DMibLogWithCategory(Daemon, Error, "SetServiceStatus error: {}", NMib::NPlatform::fg_Win32_GetLastErrorStr());
		}
	}

	void WINAPI CDaemon::CDetails::fsp_ServiceStart(DWORD _nArgs, LPWSTR *_pArgs)
	{
		DWORD status;
		DWORD specificError;

		msp_ServiceStatus.dwServiceType        = SERVICE_WIN32_OWN_PROCESS;
		msp_ServiceStatus.dwCurrentState       = SERVICE_START_PENDING;
		msp_ServiceStatus.dwControlsAccepted   = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN;
		if (NMib::NPlatform::fg_IsVista())
		{
			msp_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_PRESHUTDOWN;

		}
		msp_ServiceStatus.dwWin32ExitCode      = 0;
		msp_ServiceStatus.dwServiceSpecificExitCode = 0;
		msp_ServiceStatus.dwCheckPoint         = 0;
		msp_ServiceStatus.dwWaitHint           = 0;

		msp_ServiceStatusHandle = RegisterServiceCtrlHandlerExW(
			NStr::NPlatform::fg_StrToWindows(msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName()),
			&CDetails::fsp_ServiceCtrlHandler,
			msp_pThis);

		if (msp_ServiceStatusHandle == (SERVICE_STATUS_HANDLE)0)
		{
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] RegisterServiceCtrlHandler failed {}\n", GetLastError());
			return;
		}

		// Initialization code goes here.
		status = fsp_ServiceInitialization(_nArgs,_pArgs, &specificError);

		// Handle error condition
		if (status != NO_ERROR)
		{
			msp_ServiceStatus.dwCurrentState       = SERVICE_STOPPED;
			msp_ServiceStatus.dwCheckPoint         = 0;
			msp_ServiceStatus.dwWaitHint           = 0;
			msp_ServiceStatus.dwWin32ExitCode      = status;
			msp_ServiceStatus.dwServiceSpecificExitCode = specificError;

			SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus);
			return;
		}

		// Initialization complete - report running status.
		msp_ServiceStatus.dwCurrentState       = SERVICE_RUNNING;
		msp_ServiceStatus.dwCheckPoint         = 0;
		msp_ServiceStatus.dwWaitHint           = 0;

		if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
		{
			status = GetLastError();
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] SetServiceStatus error {}\n", status);
		}

		// This is where the service does its work.
		DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] Returning the Main Thread\n", 0);

		return;
	}

	DWORD WINAPI CDaemon::CDetails::fsp_ServiceInitialization(DWORD _nArgs, LPWSTR *_pArgs, DWORD *_pSpecificError)
	{
		msp_pThis->fp_DaemonCreate();
		return msp_pThis->mp_pImp == nullptr;
	}

	DWORD WINAPI CDaemon::CDetails::fsp_ServiceCtrlHandler(DWORD _ControlCode, DWORD _EventType, void *_pEventData, void *_pContext)
	{
		[[maybe_unused]] DWORD status;

		DMibLock(msp_ServiceControlLock);

		switch(_ControlCode)
		{
		case SERVICE_CONTROL_PAUSE:
			// Do whatever it takes to pause here.
			if (msp_ServiceStatus.dwCurrentState == SERVICE_RUNNING)
			{
				msp_pThis->fp_DaemonPause();
				msp_ServiceStatus.dwCurrentState = SERVICE_PAUSED;
			}

			break;

		case SERVICE_CONTROL_CONTINUE:
			// Do whatever it takes to continue here.
			if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
			{
				msp_pThis->fp_DaemonResume();
				msp_ServiceStatus.dwCurrentState = SERVICE_RUNNING;
			}
			break;

		case SERVICE_CONTROL_PRESHUTDOWN:
		case SERVICE_CONTROL_SHUTDOWN:
			{
				NMib::NPlatform::fg_ReportIsShuttingDown();

				if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
				{
					msp_pThis->fp_DaemonResume();
				}

				msp_bIsShutdown = true;

				// Do whatever it takes to stop here.
				if (!msp_pThis->mp_pStopThread)
				{
					msp_ServiceStatus.dwWin32ExitCode = 0;
					msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING;
					msp_ServiceStatus.dwCheckPoint    = 0;
					msp_ServiceStatus.dwWaitHint      = 10000;

					msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CDaemon_Destroy");
					msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CDaemon_DestroyReport");
				}
			}
			break;

		case SERVICE_CONTROL_STOP:
			if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
			{
				msp_pThis->fp_DaemonResume();
			}

			// Do whatever it takes to stop here.
			if (!msp_pThis->mp_pStopThread)
			{
				msp_ServiceStatus.dwWin32ExitCode = 0;
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING;
				msp_ServiceStatus.dwCheckPoint    = 0;
				msp_ServiceStatus.dwWaitHint      = 10000;

				msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CDaemon_Destroy");
				msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CDaemon_DestroyReport");
			}
			break;

		case SERVICE_CONTROL_INTERROGATE:
			// Fall through to send current status.
			break;
		default:
			return ERROR_CALL_NOT_IMPLEMENTED;
		}

		// Send current status.
		if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
		{
			status = GetLastError();
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] SetServiceStatus error {}\n", (status));
		}
		return NO_ERROR;
	}
}
