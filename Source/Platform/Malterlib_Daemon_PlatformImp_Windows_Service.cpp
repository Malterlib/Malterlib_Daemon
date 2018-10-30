// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
	SC_HANDLE CService::CDetails::fp_OpenSCManager() const
	{
		return OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | DELETE | SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
	}

	EActionResult CService::CDetails::f_Run()
	{
		NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName());
		SERVICE_TABLE_ENTRYW DispatchTable[] = { { (ch16 *)Temp.f_GetStr(), CService::CDetails::fsp_ServiceStart}, { nullptr, nullptr} }; 

		if (!StartServiceCtrlDispatcherW( DispatchTable)) 
		{ 
			f_ReportError(NStr::CStr::CFormat("StartServiceCtrlDispatcher error: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	void WINAPI CService::CDetails::fsp_ServiceStart(DWORD _nArgs, LPWSTR *_pArgs) 
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
			NStr::NPlatform::fg_StrToWindows(msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName()), 
			&CDetails::fsp_ServiceCtrlHandler,
			msp_pThis); 

		if (msp_ServiceStatusHandle == (SERVICE_STATUS_HANDLE)0) 
		{ 
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] RegisterServiceCtrlHandler failed {}\n", GetLastError()); 
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
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
		} 

		// This is where the service does its work. 
		DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] Returning the Main Thread\n", 0); 

		return; 
	} 

	DWORD WINAPI CService::CDetails::fsp_ServiceInitialization(DWORD _nArgs, LPWSTR *_pArgs, DWORD *_pSpecificError) 
	{ 
		msp_pThis->fp_ServiceCreate();
		return msp_pThis->mp_pImp == nullptr;
	}

	DWORD WINAPI CService::CDetails::fsp_ServiceCtrlHandler(DWORD _ControlCode, DWORD _EventType, void *_pEventData, void *_pContext)
	{ 
		DWORD status; 

		DMibLock(msp_ServiceControlLock);

		switch(_ControlCode) 
		{ 
		case SERVICE_CONTROL_PAUSE: 
			// Do whatever it takes to pause here. 
			if (msp_ServiceStatus.dwCurrentState == SERVICE_RUNNING)
			{
				msp_pThis->fp_ServicePause();
				msp_ServiceStatus.dwCurrentState = SERVICE_PAUSED; 
			}

			break; 

		case SERVICE_CONTROL_CONTINUE: 
			// Do whatever it takes to continue here. 
			if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
			{
				msp_pThis->fp_ServiceResume();
				msp_ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
			}
			break; 

		case SERVICE_CONTROL_PRESHUTDOWN:

		case SERVICE_CONTROL_SHUTDOWN:
			{
				if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
				{
					msp_pThis->fp_ServiceResume();
				}

				msp_bIsShutdown = true;

				// Do whatever it takes to stop here. 
				if (!msp_pThis->mp_pStopThread)
				{
					msp_ServiceStatus.dwWin32ExitCode = 0; 
					msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
					msp_ServiceStatus.dwCheckPoint    = 0; 
					msp_ServiceStatus.dwWaitHint      = 10000;

					msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CService_Destroy");
					msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CService_DestroyReport");
				}
			}
			break;

		case SERVICE_CONTROL_STOP:
			if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
			{
				msp_pThis->fp_ServiceResume();
			}

			// Do whatever it takes to stop here. 
			if (!msp_pThis->mp_pStopThread)
			{
				msp_ServiceStatus.dwWin32ExitCode = 0; 
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
				msp_ServiceStatus.dwCheckPoint    = 0; 
				msp_ServiceStatus.dwWaitHint      = 10000;

				msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CService_Destroy");
				msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CService_DestroyReport");
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
			DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", (status)); 
		}
		return NO_ERROR;
	}
}
