// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	using namespace NStr;

	void CDaemon::CDetails::fs_AbortDebug()
	{
		msp_TaskIcon.m_bAbortDebug = true;
		PostMessage(msp_TaskIcon.m_hReportWnd, WM_NULL, 0, 0);
	}

	EActionResult CDaemon::CDetails::f_RunAsProgram(bool _bDebug)
	{
		if (fp_GetDaemonParams().f_GetDetachConsole())
			FreeConsole();

		CStr RootDirectory = fp_GetDaemonParams().f_GetRootDirectory();
		CStr DaemonStateFile = RootDirectory / (NFile::CFile::fs_GetFileNoExt(NFile::CFile::fs_GetProgramPath()) + ".ServiceState");
		NFile::CFile StateFile;
		{
			StateFile.f_Open(DaemonStateFile, NFile::EFileOpen_Write | NFile::EFileOpen_Temporary | NFile::EFileOpen_NoLocalCache | NFile::EFileOpen_ShareAll);
			CStr State = "Run";
			StateFile.f_Write(State.f_GetStr(), State.f_GetLen());
		}

		if (_bDebug)
		{
			HICON Icon = LoadIcon((HINSTANCE)mp_pOwner->f_GetDaemonParams().f_GetNativeHandle(), IDI_APPLICATION);
			if (!Icon)
				Icon = LoadIcon(nullptr, IDI_APPLICATION);
			msp_TaskIcon.f_Init(Icon);
		}

		NStorage::TCSharedPointer<NThread::CEvent> pAbortStarted = fg_Construct();

		NStorage::TCUniquePointer<NThread::CThreadObject> pAbortThread = NThread::CThreadObject::fs_StartThread
			(
				[=](NThread::CThreadObject *_pThread) -> aint
				{
					NFile::CFileChangeNotification FileChangeNotification;
					try
					{
						FileChangeNotification.f_Open(RootDirectory, NFile::EFileChange_Write, &_pThread->m_EventWantQuit);
					}
					catch (NFile::CExceptionFile const &_Exception)
					{
						(void)_Exception;
						DMibLog(Error, "Failed to register for daemon state change: {}", _Exception);
						pAbortStarted->f_SetSignaled();
						return 1;
					}

					auto ServiceStateFileName = NFile::CFile::fs_GetFile(DaemonStateFile);

					pAbortStarted->f_SetSignaled();

					while (_pThread->f_GetState() != NThread::EThreadState_EventWantQuit)
					{
						try
						{
							NFile::CFileChangeNotification::CNotification Notification;
							while (FileChangeNotification.f_GetNotification(Notification))
							{
								if (Notification.m_Path == ServiceStateFileName && NFile::CFile::fs_ReadStringFromFile(DaemonStateFile, true) == "Stop")
								{
									CDaemon::CDetails::fs_AbortDebug();
									return 0;
								}
							}
						}
						catch (NFile::CExceptionFile const &_Exception)
						{
							(void)_Exception;
							DMibLog(Error, "Failed to check state of daemon file: {}", _Exception);
						}
						_pThread->m_EventWantQuit.f_Wait();
					}
					return 0;
				}
				, "Daemon Abort"
			)
		;

		pAbortStarted->f_Wait();

		fp_DaemonCreate();

		NFile::CFile PIDFile;
		{
			CStr DaemonPidPath = RootDirectory / (NFile::CFile::fs_GetFileNoExt(NFile::CFile::fs_GetProgramPath()) + ".ServicePID");
			PIDFile.f_Open(DaemonPidPath, NFile::EFileOpen_Write | NFile::EFileOpen_Temporary | NFile::EFileOpen_NoLocalCache | NFile::EFileOpen_ShareRead);
			CStr Pid = "{}"_f << NProcess::NPlatform::fg_Process_GetCurrentUID();
			PIDFile.f_Write(Pid.f_GetStr(), Pid.f_GetLen());
		}

		if (_bDebug)
		{
			auto Subscription = NProcess::NPlatform::fg_Process_WaitForTermination
				(
					[&]
					{
						fs_AbortDebug();
					}
				)
			;

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

		fp_DaemonDestroy();

		return EActionResult_Success;
	}

}
