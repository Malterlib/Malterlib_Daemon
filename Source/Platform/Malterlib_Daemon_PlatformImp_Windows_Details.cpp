// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	NThread::CMutual CDaemon::CDetails::msp_ServiceControlLock;
	SERVICE_STATUS CDaemon::CDetails::msp_ServiceStatus;
	SERVICE_STATUS_HANDLE CDaemon::CDetails::msp_ServiceStatusHandle;
	CDaemon::CDetails* CDaemon::CDetails::msp_pThis = nullptr;
	bool CDaemon::CDetails::msp_bIsShutdown = false;
	CDaemon::CDetails::CTaskIconCleaner CDaemon::CDetails::msp_TaskIcon;

	CDaemon::CDetails::CDetails(CDaemon *_pOwner)
		: mp_pOwner(_pOwner)
	{
		if (msp_pThis)
			DMibError("You cannot have two services running in the same process at once");
		else
			msp_pThis = this;
	}

	CDaemon::CDetails::~CDetails()
	{
		mp_pStopThread.f_Clear();
		mp_pStopReportThread.f_Clear();
		msp_pThis = nullptr;
	}

	CDaemonParams const &CDaemon::CDetails::fp_GetDaemonParams() const
	{
		return msp_pThis->mp_pOwner->f_GetDaemonParams();
	}

	bool CDaemon::CDetails::f_IsShutdown() const
	{
		return msp_bIsShutdown;
	}

	NStr::CStr CDaemon::CDetails::fp_GetAddCommandLine() const
	{
		NStr::CStr Strings = NFile::NPlatform::fg_ConvertToWindowsPath(NSys::NFile::fg_GetProgramPath(), false);

		if (Strings[0] != '"')
			Strings = NStr::CStr("\"") + Strings + "\"";

		NStr::CStr CommandLine;
		if (mp_pOwner->f_GetDaemonParams().f_GetAddCommandLine() != "")
			CommandLine = Strings + " " + mp_pOwner->f_GetDaemonParams().f_GetAddCommandLine();
		else
			CommandLine = Strings + " -Service " + mp_pOwner->f_GetDaemonParams().f_GetDaemonName();

		return CommandLine;
	}


	void CDaemon::CDetails::fp_DaemonResume()
	{
		if (mp_pImp)
			mp_pImp->f_DaemonResume();
	}

	void CDaemon::CDetails::fp_DaemonPause()
	{
		if (mp_pImp)
			mp_pImp->f_DaemonPause();
	}

	void CDaemon::CDetails::fp_DaemonCreate()
	{
		mp_pImp = mp_pOwner->f_GetDaemonParams().f_ImplementationFactory();
	}

	void CDaemon::CDetails::fp_DaemonDestroy()
	{
		if (mp_pImp)
			mp_pImp = nullptr;
	}

	aint CDaemon::CDetails::fp_StopThread(NThread::CThreadObject *)
	{
		msp_pThis->mp_pImp = nullptr;
		return 0;
	}

	aint CDaemon::CDetails::fp_StopReportThread(NThread::CThreadObject *_pThread)
	{
		while (1)
		{
			if (mp_pStopThread->f_GetState() == NThread::EThreadState_Stopped)
			{
				DMibLock(msp_ServiceControlLock);
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOPPED; 

				DMibDTrace("Daemon stopped: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
				{ 
					[[maybe_unused]] HRESULT status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] SetServiceStatus error {}\n", status); 
				}
				return 0;
			}
			else
			{
				DMibLock(msp_ServiceControlLock);
				msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
				++msp_ServiceStatus.dwCheckPoint; 
				msp_ServiceStatus.dwWaitHint      += 1500;
				DMibDTrace("Daemon stop pending: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
				{ 
					[[maybe_unused]] HRESULT status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetDaemonParams().f_GetDaemonName() + "] SetServiceStatus error {}\n", status); 
				}
			}
			WaitForSingleObject(mp_pStopThread->f_GetThread(), 1000);
		}
		return 0;
	}
}
