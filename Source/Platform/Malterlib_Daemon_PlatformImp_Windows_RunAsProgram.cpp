// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
	using namespace NStr;

	void CService::CDetails::fs_AbortDebug()
	{
		msp_TaskIcon.m_bAbortDebug = true;
		PostMessage(msp_TaskIcon.m_hReportWnd, WM_NULL, 0, 0);
	}

	EActionResult CService::CDetails::f_RunAsProgram(bool _bDebug)
	{
		if (fp_GetServiceParams().f_GetDetachConsole())
			FreeConsole();

		NFile::CFile PIDFile;
		{
			CStr ServicePidPath = NFile::CFile::fs_GetProgramDirectory() / "ServicePID";
			PIDFile.f_Open(ServicePidPath, NFile::EFileOpen_Write | NFile::EFileOpen_Temporary | NFile::EFileOpen_NoLocalCache | NFile::EFileOpen_ShareRead);
			CStr Pid = "{}"_f << NProcess::NPlatform::fg_Process_GetCurrentUID();
			PIDFile.f_Write(Pid.f_GetStr(), Pid.f_GetLen());
		}

		CStr ServiceStateFile = NFile::CFile::fs_GetProgramDirectory() / "ServiceState";
		NFile::CFile StateFile;
		{
			StateFile.f_Open(ServiceStateFile, NFile::EFileOpen_Write | NFile::EFileOpen_Temporary | NFile::EFileOpen_NoLocalCache | NFile::EFileOpen_ShareAll);
			CStr State = "Run";
			StateFile.f_Write(State.f_GetStr(), State.f_GetLen());
		}

		NPtr::TCUniquePointer<NThread::CThreadObject> pAbortThread = NThread::CThreadObject::fs_StartThread
			(
				[=](NThread::CThreadObject *_pThread) -> aint
				{
					NFile::CFileChangeNotification FileChangeNotification;
					try
					{
						FileChangeNotification.f_Open(NFile::CFile::fs_GetProgramDirectory(), NFile::EFileChange_Write, &_pThread->m_EventWantQuit);
					}
					catch (NFile::CExceptionFile const &_Exception)
					{
						DMibLog(Error, "Failed to register for service state change: {}", _Exception);
						return 1;
					}

					while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
					{
						try
						{
							NFile::CFileChangeNotification::CNotification Notification;
							while (FileChangeNotification.f_GetNotification(Notification))
							{
								if (Notification.m_Path == "ServiceState" && NFile::CFile::fs_ReadStringFromFile(ServiceStateFile, true) == "Stop")
								{
									CService::CDetails::fs_AbortDebug();
									return 0;
								}
							}
						}
						catch (NFile::CExceptionFile const &_Exception)
						{
							DMibLog(Error, "Failed to check state of service file: {}", _Exception);
						}
						_pThread->m_EventWantQuit.f_Wait();
					}
					return 0;
				}
				, "Daemon Abort"
			)
		;

		fp_ServiceCreate();

		if (_bDebug)
		{
			HICON Icon = LoadIcon((HINSTANCE)mp_pOwner->f_GetServiceParams().f_GetNativeHandle(), IDI_APPLICATION);
			msp_TaskIcon.f_Init(Icon);
				
			// Just spin in eternity
			while (1)
			{
				if (msp_TaskIcon.f_Update())
					break;

				Sleep(50);
			}
		}
		else
			NProcess::NPlatform::fg_Process_WaitForTermination();

		fp_ServiceDestroy();

		return EActionResult_Success;
	}

}
