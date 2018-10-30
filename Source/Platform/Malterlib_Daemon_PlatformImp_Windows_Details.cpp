// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
	NThread::CMutual CService::CDetails::msp_ServiceControlLock;
	SERVICE_STATUS CService::CDetails::msp_ServiceStatus;
	SERVICE_STATUS_HANDLE CService::CDetails::msp_ServiceStatusHandle;
	CService::CDetails* CService::CDetails::msp_pThis = nullptr;
	bint CService::CDetails::msp_bIsShutdown = false;
	CService::CDetails::CTaskIconCleaner CService::CDetails::msp_TaskIcon;

	CService::CDetails::CDetails(CService *_pOwner)
		: mp_pOwner(_pOwner)
	{
		if (msp_pThis)
			DMibError("You cannot have two services running in the same process at once");
		else
			msp_pThis = this;
	}

	CService::CDetails::~CDetails()
	{
		mp_pStopThread.f_Clear();
		mp_pStopReportThread.f_Clear();
		msp_pThis = nullptr;
	}

	CServiceParams const &CService::CDetails::fp_GetServiceParams() const
	{
		return msp_pThis->mp_pOwner->f_GetServiceParams();
	}

	bint CService::CDetails::f_IsShutdown() const
	{
		return msp_bIsShutdown;
	}

	NStr::CStr CService::CDetails::fp_GetAddCommandLine() const
	{
		NStr::CStr Strings = NFile::NPlatform::fg_ConvertToWindowsPath(NSys::NFile::fg_GetProgramPath(), false);

		if (Strings[0] != '"')
			Strings = NStr::CStr("\"") + Strings + "\"";

		NStr::CStr CommandLine;
		if (mp_pOwner->f_GetServiceParams().f_GetAddCommandLine() != "")
			CommandLine = Strings + " " + mp_pOwner->f_GetServiceParams().f_GetAddCommandLine();
		else
			CommandLine = Strings + " -Service " + mp_pOwner->f_GetServiceParams().f_GetServiceName();

		return CommandLine;
	}


	void CService::CDetails::fp_ServiceResume()
	{
		if (mp_pImp)
			mp_pImp->f_ServiceResume();
	}

	void CService::CDetails::fp_ServicePause()
	{
		if (mp_pImp)
			mp_pImp->f_ServicePause();
	}

	void CService::CDetails::fp_ServiceCreate()
	{
		mp_pImp = mp_pOwner->f_GetServiceParams().f_ImplementationFactory();
	}

	void CService::CDetails::fp_ServiceDestroy()
	{
		if (mp_pImp)
			mp_pImp = nullptr;
	}

	aint CService::CDetails::fp_StopThread(NThread::CThreadObject *)
	{
		msp_pThis->mp_pImp = nullptr;
		return 0;
	}

	aint CService::CDetails::fp_StopReportThread(NThread::CThreadObject *_pThread)
	{
		while (1)
		{
			if (mp_pStopThread->f_GetState() == NThread::EThreadState_Stopped)
			{
				DMibLock(msp_ServiceControlLock);
				msp_ServiceStatus.dwWin32ExitCode = 0; 
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOPPED; 

				DMibDTrace("Service stopped: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
				{ 
					HRESULT status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
				}
				return 0;
			}
			else
			{
				DMibLock(msp_ServiceControlLock);
				msp_ServiceStatus.dwWin32ExitCode = 0; 
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
				++msp_ServiceStatus.dwCheckPoint; 
				msp_ServiceStatus.dwWaitHint      += 1500;
				DMibDTrace("Service stop pending: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
				{ 
					HRESULT status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
				}
			}
			WaitForSingleObject(mp_pStopThread->f_GetThread(), 1000);
		}
		return 0;
	}
}
