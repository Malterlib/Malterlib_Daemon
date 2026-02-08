// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include <signal.h>
#include "Malterlib_Daemon_PlatformImp_Linux_Upstart.h"
#include "Malterlib_Daemon_PlatformImp_Linux_Systemd.h"
#include "Malterlib_Daemon_PlatformImp_Linux_Gentoo.h"
#include "Malterlib_Daemon_PlatformImp_Linux_Script.h"

#include <errno.h>

extern "C"
{
	#include <unistd.h>
}

#include <Mib/Core/PlatformSpecific/PosixErrNo>
#include <Mib/Process/ProcessLaunch>

namespace NMib::NDaemon
{
	class CDaemon::CDetails
	{
	public:
		CDetails(CDaemon* _pOwner)
			: mp_pOwner(_pOwner)
			, mp_InterruptedReason(EDaemonInterrupt_None)
		{
			if (msp_pThis)
				DMibError("You cannot have two daemons running in the same process at once");
			else
				msp_pThis = this;

			if (CSystemd::fs_IsSupported())
			{
				DMibLog(DebugVerbose1, "Systemd supported.", 0);
				mp_pDaemonIntegration = fg_Construct<CSystemd>(_pOwner);
			}
			else if (CUpstart::fs_IsSupported())
			{
				DMibLog(DebugVerbose1, "Upstart supported.", 0);
				mp_pDaemonIntegration = fg_Construct<CUpstart>(_pOwner);
			}
			else if (CGentoo::fs_IsSupported())
			{
				DMibLog(DebugVerbose1, "Gentoo supported.", 0);
				mp_pDaemonIntegration = fg_Construct<CGentoo>(_pOwner);
			}
			else if (CScript::fs_IsSupported())
			{
				DMibLog(DebugVerbose1, "Script supported.", 0);
				mp_pDaemonIntegration = fg_Construct<CScript>(_pOwner);
			}
			else
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("No supported daemon system detected."));
			}
		}

		~CDetails()
		{
			msp_pThis = nullptr;
		}

		static bool fs_SupportsAutoRestart()
		{
			if (!msp_pThis)
				return false;
			return msp_pThis->f_SupportsAutoRestart();
		}

		bool f_SupportsAutoRestart() const
		{
			if (!mp_pDaemonIntegration)
				return false;
			return mp_pDaemonIntegration->f_SupportsAutoRestart();
		}

		bool f_PrepareUserAndGroup(CDaemonParams const &_Params)
		{
			NStr::CStr StdOut, StdErr;
			int GID, UID;

			NStr::CStr GroupName = _Params.f_GetRunAsGroup();
			NStr::CStr UserName = _Params.f_GetRunAsUser();

			// Create group if not exists
			if (GroupName.f_IsEmpty())
				GroupName = "root";

			if (UserName.f_IsEmpty())
				UserName = "root";

			NStr::CStr ReturnGID;

			try
			{
				if (!NSys::fg_UserManagement_GroupExists(GroupName, ReturnGID))
					NSys::fg_UserManagement_CreateGroup(GroupName, ReturnGID);
			}
			catch (NMib::NException::CException &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Exception when creating group named {}\n{}") << GroupName << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}

			GID = ReturnGID.f_ToInt(-1);

			if (GID == -1)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Group {} is invalid") << GroupName);
				return false;
			}

			// Create user if not exists

			NStr::CStr ReturnUID;

			try
			{
				if (!NSys::fg_UserManagement_UserExists(UserName, ReturnUID))
				{
					NSys::fg_UserManagement_CreateUser
						(
							GroupName
							, UserName
							, ""
							, _Params.f_GetDaemonDescription()
							, _Params.f_GetRootDirectory()
							, ReturnUID
							, NSys::EUserManagementCreateUserFlag_None
						)
					;
				}
			}
			catch (NMib::NException::CException &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to create user named {}\n{}") << UserName << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}

			UID = ReturnUID.f_ToInt(-1);

			if (UID == -1)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("User {} is invalid") << UserName);
				return false;
			}

			// Create PID directory
			NStr::CStr PIDDirectory = NStr::CStr::CFormat("/var/run/{}") << _Params.f_GetDaemonName();
			try
			{
				if (!NFile::CFile::fs_FileExists(PIDDirectory, NFile::EFileAttrib_Directory))
				{
					try
					{
						NFile::CFile::fs_CreateDirectory(PIDDirectory);
					}
					catch (NFile::CExceptionFile const &_Exception)
					{
						mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to create PID directory: {}") << _Exception.f_GetErrorStr());
						if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
							mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
						return false;
					}
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing PID directory: {}") << _Exception.f_GetErrorStr());
				return false;
			}

			try
			{
				NFile::CFile::fs_SetOwnerAndGroupRecursive(PIDDirectory, UserName, GroupName, false);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to set permissions on PID directory: {}") << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}

			try
			{
				NFile::CFile::fs_SetOwner(_Params.f_GetRootDirectory(), UserName);
				NFile::CFile::fs_SetGroup(_Params.f_GetRootDirectory(), GroupName);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to set permissions on daemon directory: {}") << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}

			return true;
		}

		EActionResult f_Start()
		{
			return mp_pDaemonIntegration->f_Start(mp_pOwner->mp_Params);
		}

		EActionResult f_Stop(bool _bWait)
		{
			return mp_pDaemonIntegration->f_Stop(mp_pOwner->mp_Params, _bWait);
		}

		EActionResult f_Restart(bool _bWait)
		{
			return mp_pDaemonIntegration->f_Restart(mp_pOwner->mp_Params, _bWait);
		}

		EActionResult f_Exists(bool &_bExists) const
		{
			return mp_pDaemonIntegration->f_Exists(mp_pOwner->mp_Params, _bExists);
		}

		EActionResult f_Add(bool _bCheckForExisting)
		{
			if ((!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty() || !mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty()) && !f_PrepareUserAndGroup(mp_pOwner->mp_Params))
				return EActionResult_Failure;

			return mp_pDaemonIntegration->f_Add(mp_pOwner->mp_Params, _bCheckForExisting);
		}

		EActionResult f_Remove()
		{
			return mp_pDaemonIntegration->f_Remove(mp_pOwner->mp_Params);
		}

		static void fs_SigHandler(int const _sigid)
		{
			sigset_t WaitSet;
			sigset_t OldSet;
			sigemptyset(&WaitSet);
			sigaddset(&WaitSet, SIGTERM);
			sigaddset(&WaitSet, SIGINT);
			sigaddset(&WaitSet, SIGTSTP);
			sigaddset(&WaitSet, SIGCONT);
			pthread_sigmask(SIG_BLOCK, &WaitSet, &OldSet);

			auto Cleanup = g_OnScopeExit / [&]
				{
					pthread_sigmask(SIG_SETMASK, &OldSet, nullptr);
				}
			;

			switch (_sigid)
			{
				case SIGTSTP:
					msp_pThis->mp_InterruptedReason.f_FetchOr(EDaemonInterrupt_Pause);
					break;
				case SIGCONT:
					msp_pThis->mp_InterruptedReason.f_FetchOr(EDaemonInterrupt_Resume);
					break;
				default:
					msp_pThis->mp_InterruptedReason.f_FetchOr(EDaemonInterrupt_Exit);
					break;
			}
			msp_pThis->mp_InterruptedEvent.f_Signal();
		}

		int fp_DoStart(NStr::CStr const &_PidFilePath)
		{
			mint PidOfProcess = 0;
			try
			{
				if (NFile::CFile::fs_FileExists(_PidFilePath))
				{
					NStr::CStr PidContents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(_PidFilePath));
					PidOfProcess = PidContents.f_ToInt(mint(0));
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to read pid file {}: {}") << _PidFilePath << _Exception.f_GetErrorStr());
				return 2;
			}

			if (PidOfProcess)
			{
				if (NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
					return 1; // Already running
			}

			return 0;
		}

		int fp_DoStop(NStr::CStr const &_PidFilePath)
		{
			mint PidOfProcess = 0;
			try
			{
				if (NFile::CFile::fs_FileExists(_PidFilePath))
				{
					NStr::CStr PidContents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(_PidFilePath));
					PidOfProcess = PidContents.f_ToInt(mint(0));
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to read pid file {}: {}") << _PidFilePath << _Exception.f_GetErrorStr());
				return 2;
			}

			auto fl_DeletePidFile
				= [&]
				{
					try
					{
						if (NFile::CFile::fs_FileExists(_PidFilePath))
							NFile::CFile::fs_DeleteFile(_PidFilePath);
					}
					catch (NFile::CExceptionFile const &)
					{
					}
				}
			;

			if (PidOfProcess)
			{
				if (!NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
				{
					fl_DeletePidFile();
					return 1; // Already stopped
				}

			}
			else
				return 1; // Already stopped

			fp64 Timeout = mp_pOwner->mp_Params.f_GetValueForKey("-DoStop").f_ToFloat(fp64(1200.0));

			NTime::CClock Clock;
			Clock.f_Start();
			if (kill(PidOfProcess, SIGTERM))
			{
				int ErrNo = errno;
				if (!NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
				{
					fl_DeletePidFile();
					return 0;
				}
				mp_pOwner->f_ReportError(NMib::NPlatform::fg_FormatErrno(NMib::NStr::CStr::CFormat("kill({}, SIGTERM) when stopping daemon") << PidOfProcess, ErrNo));
				return 2;
			}

			bool bTriedKill = false;
			while (true)
			{
				if (Clock.f_GetTime() > Timeout && !bTriedKill)
				{
					bTriedKill = true;
					if (kill(PidOfProcess, SIGKILL))
					{
						int ErrNo = errno;
						if (!NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
						{
							fl_DeletePidFile();
							return 0;
						}
						mp_pOwner->f_ReportError(NMib::NPlatform::fg_FormatErrno(NMib::NStr::CStr::CFormat("kill({}, SIGTERM) when stopping daemon") << PidOfProcess, ErrNo));
						return 2;
					}
				}
				if (!NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
				{
					fl_DeletePidFile();
					return 0;
				}
				NSys::fg_Thread_Sleep(0.1);
			}

			return 2;
		}

		int fp_DoStatus(NStr::CStr const &_PidFilePath)
		{
			mint PidOfProcess = 0;
			try
			{
				if (NFile::CFile::fs_FileExists(_PidFilePath))
				{
					NStr::CStr PidContents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(_PidFilePath));
					PidOfProcess = PidContents.f_ToInt(mint(0));
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to read pid file {}: {}") << _PidFilePath << _Exception.f_GetErrorStr());
				return 4;
			}

			if (PidOfProcess)
			{
				if (NMib::NProcess::NPlatform::fg_Process_IsRunning(PidOfProcess))
				{
					mp_pOwner->f_ReportInformation("Daemon Status", NStr::CStr::CFormat("{} ({}) is running...") << mp_pOwner->mp_Params.f_GetExecutablePath() << PidOfProcess);
					return 0;
				}
				mp_pOwner->f_ReportInformation("Daemon Status", NStr::CStr::CFormat("{} is stopped (pid file still exists)") << mp_pOwner->mp_Params.f_GetExecutablePath());
				return 1;
			}

			mp_pOwner->f_ReportInformation("Daemon Status", NStr::CStr::CFormat("{} is stopped") << mp_pOwner->mp_Params.f_GetExecutablePath());

			return 3;

			return 0;
#if 0
					0	program is running or service is OK
					1	program is dead and /var/run pid file exists
					2	program is dead and /var/lock lock file exists
					3	program is not running
					4	program or service status is unknown
					5-99	reserved for future LSB use
					100-149	reserved for distribution use
					150-199	reserved for application use
					200-254	reserved
#endif
		}


		EActionResult f_Run()
		{
			NFunction::TCFunction<EActionResult (EActionResult _Result)> fConvertResult
				= [](EActionResult _Result) -> EActionResult
				{
					return _Result;
				}
			;

			if (mp_pOwner->mp_Params.f_GetDaemonize())
			{
				if (mp_pOwner->mp_Params.f_IsKeySet("-DoStart") || mp_pOwner->mp_Params.f_IsKeySet("-DoStop") || mp_pOwner->mp_Params.f_IsKeySet("-DoStatus"))
				{
					fConvertResult
						= [](EActionResult _Result) -> EActionResult
						{
							if (_Result == EActionResult_Failure)
								return (EActionResult)2;
							if (_Result == 2)
								return (EActionResult)1;
							return _Result;
						}
					;
				}

				NStr::CStr PidFilePath = mp_pOwner->mp_Params.f_GetValueForKey("-Daemonize");

				if (PidFilePath.f_IsEmpty())
				{
					mp_pOwner->f_ReportError("Pid file should be specified after the '-Daemonize' argument");
					return fConvertResult(EActionResult_Failure);
				}

				if (mp_pOwner->mp_Params.f_IsKeySet("-DoStart"))
				{
					int Ret = fp_DoStart(PidFilePath);
					if (Ret)
						return (EActionResult)Ret;
				}
				else if (mp_pOwner->mp_Params.f_IsKeySet("-DoStop"))
				{
					return (EActionResult)fp_DoStop(PidFilePath);
				}
				else if (mp_pOwner->mp_Params.f_IsKeySet("-DoStatus"))
				{
					return (EActionResult)fp_DoStatus(PidFilePath);
				}


				NMib::NProcess::CProcessLaunchParams Params;
				Params.m_Target = mp_pOwner->mp_Params.f_GetExecutablePath();
				Params.m_Parameters = NStr::CStr::CFormat("-Service {} --OutputPID") << mp_pOwner->mp_Params.f_GetDaemonName();

				if (!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty())
					Params.m_RunAsUser = mp_pOwner->mp_Params.f_GetRunAsUser();
				else
					Params.m_bMakeEffectiveUserReal = true;

				if (!mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty())
					Params.m_RunAsGroup = mp_pOwner->mp_Params.f_GetRunAsGroup();
				else
					Params.m_bMakeEffectiveGroupReal = true;

				Params.m_bShowLaunched = false;
				Params.m_bEnableStdRedirection = true;
				Params.m_bSeparateStdErr = true;
				Params.m_bStdOutPID = true;

				NStr::CStr Error;
				NThread::CEvent PIDReceivedEvent;
				NStr::CStr QueuedStdOut;
				uint32 ExitCode = 66;

				bool bLaunchFailed = false;
				bool bPIDReceived = false;

				Params.m_fOnStateChange
					= [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
					{
						switch (_State.f_GetTypeID())
						{
						case NMib::NProcess::EProcessLaunchState_Exited:
							{
								NMib::fg_Volatile(ExitCode) = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();
								PIDReceivedEvent.f_SetSignaled();
							}
							break;
						case NMib::NProcess::EProcessLaunchState_LaunchFailed:
							{
								bLaunchFailed = true;
								Error += _State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>();
								PIDReceivedEvent.f_SetSignaled();
							}
							break;
						case NMib::NProcess::EProcessLaunchState_Launched:
							{
							}
							break;
						}
					}
				;

				Params.m_fOnOutput
					= [&](NMib::NProcess::EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
					{
						if (_OutputType == NMib::NProcess::EProcessLaunchOutputType_StdOut)
						{
							QueuedStdOut += _Output;

							while (true)
							{
								aint iLine = QueuedStdOut.f_FindChar('\n');
								if (iLine < 0)
									return;

								NStr::CStr Line = QueuedStdOut.f_Left(iLine);
								QueuedStdOut = QueuedStdOut.f_Extract(iLine + 1);

								if (Line.f_StartsWith("bdda0079-b6eb-41ac-88d0-01b50e8be939 "))
								{
									fg_GetStrSep(Line, " ");
									fg_GetStrSep(Line, " ");
									NStr::CStr LaunchError = Line.f_ReplaceChar('\r', '\n').f_Trim();
									if (!LaunchError.f_IsEmpty())
										Error += LaunchError;
									bPIDReceived = true;
									PIDReceivedEvent.f_SetSignaled();
								}
							}
						}
						else
							Error += _Output;
					}
				;

				NMib::NProcess::CProcessLaunch Launcher(Params, NMib::NProcess::EProcessLaunchCloseFlag_None);

				if (PIDReceivedEvent.f_WaitTimeout(30.0f))
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Timed out waiting for process to launch: {} {}") << Params.m_Target << Params.m_Parameters);
					Launcher.f_Close(NMib::NProcess::EProcessLaunchCloseFlag_TerminateProcess);
					return fConvertResult(EActionResult_Failure);
				}

				if (bLaunchFailed)
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to launch process: {}") << Error);
					Launcher.f_Close(NMib::NProcess::EProcessLaunchCloseFlag_TerminateProcess);
					return fConvertResult(EActionResult_Failure);
				}

				if (!bPIDReceived)
				{
					if (!Error.f_IsEmpty())
						mp_pOwner->f_ReportError(Error.f_Trim());
					else
						mp_pOwner->f_ReportError(NMib::NStr::fg_Format("Expected PID output not found. Exit code: {}", ExitCode));
					return fConvertResult(EActionResult_Failure);
				}

				mint ProcessPID = Launcher.f_GetProcessID();

				if (!ProcessPID)
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Process did not output a pid: {} {}") << Params.m_Target << Params.m_Parameters);
					Launcher.f_Close(NMib::NProcess::EProcessLaunchCloseFlag_TerminateProcess);
					return fConvertResult(EActionResult_Failure);
				}

				DMibLog(Debug, "Forked into child process {}", ProcessPID);

				if (PidFilePath != "?")
				{
					DMibLog(Debug, "Pid file path {}", PidFilePath);
					if (!PidFilePath.f_IsEmpty() && PidFilePath.f_Left(1) != "-")
					{
						try
						{
							NStr::CStr PID = NStr::CStr::fs_ToStr(ProcessPID);
							NFile::CFile::fs_CreateDirectory(NFile::CFile::fs_GetPath(PidFilePath));
							NFile::CFile PidFile(PidFilePath, NFile::EFileOpen_Write);
							PidFile.f_Write(PID, PID.f_GetLen());
							PidFile.f_Close();
						}
						catch (NFile::CExceptionFile const &_Exception)
						{
							mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write pid file {}: {}") << PidFilePath << _Exception.f_GetErrorStr());
							Launcher.f_StopProcess();
							mp_pOwner->f_ReportInformation("Daemonize", "Waiting for process to shut down gracefully");
							Launcher.f_Close(NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);
							return fConvertResult(EActionResult_Failure);
						}

					}
				}

				return fConvertResult(EActionResult_Success);
			}
			else
			{
				{
					if (!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty() || !mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty())
					{
						NMib::NProcess::CProcessLaunchParams Params;
						Params.m_Target = mp_pOwner->mp_Params.f_GetExecutablePath();
						Params.m_Parameters = NStr::CStr::CFormat("-Service {}") << mp_pOwner->mp_Params.f_GetDaemonName();

						if (!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty())
							Params.m_RunAsUser = mp_pOwner->mp_Params.f_GetRunAsUser();
						else
							Params.m_bMakeEffectiveUserReal = true;

						if (!mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty())
							Params.m_RunAsGroup = mp_pOwner->mp_Params.f_GetRunAsGroup();
						else
							Params.m_bMakeEffectiveGroupReal = true;

						Params.m_bShowLaunched = false;
						Params.m_bEnableStdRedirection = true;
						Params.m_bSeparateStdErr = true;

						uint32 ExitCode = 1;
						bool bExit = false;

						Params.m_fOnStateChange
							= [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
							{
								switch (_State.f_GetTypeID())
								{
								case NMib::NProcess::EProcessLaunchState_Exited:
									{
										NMib::fg_Volatile(ExitCode) = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();
										bExit = true;
										mp_InterruptedEvent.f_Signal();
									}
									break;
								case NMib::NProcess::EProcessLaunchState_LaunchFailed:
									{
										DMibConErrOut("Launch failed: {}{\n}", _State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>());
										bExit = true;
										mp_InterruptedEvent.f_Signal();
									}
									break;
								case NMib::NProcess::EProcessLaunchState_Launched:
									{
									}
									break;
								}
							}
						;

						Params.m_fOnOutput
							= [&](NMib::NProcess::EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
							{
								if (_OutputType == NMib::NProcess::EProcessLaunchOutputType_StdOut)
								{
									DMibConOutRaw(_Output);
								}
								else
								{
									DMibConErrOutRaw(_Output);
								}
							}
						;

						NMib::NProcess::CProcessLaunch Launcher(Params, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);

						signal(SIGTERM, (sig_t) fs_SigHandler);
						signal(SIGINT, (sig_t) fs_SigHandler);
						signal(SIGTSTP, (sig_t) fs_SigHandler);
						signal(SIGCONT, (sig_t) fs_SigHandler);

						while(!bExit)
						{
							auto Interrupts = mp_InterruptedReason.f_Exchange(EDaemonInterrupt_None);
							if (Interrupts & EDaemonInterrupt_Pause)
								kill(Launcher.f_GetProcessID(), SIGTSTP);
							if (Interrupts & EDaemonInterrupt_Resume)
								kill(Launcher.f_GetProcessID(), SIGCONT);
							if (Interrupts & EDaemonInterrupt_Exit)
								kill(Launcher.f_GetProcessID(), SIGTERM);
							if (!bExit)
								mp_InterruptedEvent.f_Wait();
						}

						return (EActionResult)ExitCode;

					}

				}



				return fp_RunDaemon();
			}
		}

	private:
		EActionResult fp_RunDaemon()
		{
			auto pSigterm = signal(SIGTERM, (sig_t) fs_SigHandler);
			auto pSigint = signal(SIGINT, (sig_t) fs_SigHandler);
			auto pSigstp = signal(SIGTSTP, (sig_t) fs_SigHandler);
			auto pSigcont = signal(SIGCONT, (sig_t) fs_SigHandler);

			auto Cleanup
				= g_OnScopeExit / [&]
				{
					signal(SIGTERM, pSigterm);
					signal(SIGINT, pSigint);
					signal(SIGTSTP, pSigstp);
					signal(SIGCONT, pSigcont);
				}
			;

			fp_DaemonCreate();

			if (!mp_pImp->f_DaemonValidate(mp_pOwner))
				return EActionResult_Failure;

			bool bExit = false;
			while(!bExit)
			{
				auto Interrupts = mp_InterruptedReason.f_Exchange(EDaemonInterrupt_None);
				if (Interrupts & EDaemonInterrupt_Exit)
				{
					bExit = true;
					break;
				}

				if (Interrupts & EDaemonInterrupt_Pause)
					fp_DaemonPause();

				if (Interrupts & EDaemonInterrupt_Resume)
					fp_DaemonResume();

				mp_InterruptedEvent.f_Wait();
			}

			fp_DaemonDestroy();

			return EActionResult_Success;
		}

		void fp_DaemonResume()
		{
			if (mp_pImp)
				mp_pImp->f_DaemonResume();
		}

		void fp_DaemonPause()
		{
			if (mp_pImp)
				mp_pImp->f_DaemonPause();
		}

		void fp_DaemonCreate()
		{
			mp_pImp = mp_pOwner->f_GetDaemonParams().f_ImplementationFactory();
		}

		void fp_DaemonDestroy()
		{
			if (mp_pImp)
				mp_pImp = nullptr;
		}

		static CDetails*			   msp_pThis;

		NStorage::TCUniquePointer<CDaemonSystemInterface> mp_pDaemonIntegration;
		bool mp_bDaemonize;

		enum EDaemonInterrupt : uint32
		{
			EDaemonInterrupt_None = 0,
			EDaemonInterrupt_Exit = DMibBit(0),
			EDaemonInterrupt_Pause = DMibBit(1),
			EDaemonInterrupt_Resume = DMibBit(2),
		};
		NAtomic::TCAtomic<uint32> mp_InterruptedReason;
		NThread::CEventAutoReset mp_InterruptedEvent;
		CDaemon*					   mp_pOwner;
		NStorage::TCUniquePointer<CDaemonImp>   mp_pImp;
	};

	CDaemon::CDetails* CDaemon::CDetails::msp_pThis = nullptr;

	CDaemon::CDaemon(CDaemonParams const& _Params)
		: mp_pD(fg_Construct(this))
		, mp_Params(_Params)
	{

	}

	CDaemon::~CDaemon()
	{

	}

	namespace
	{
		struct CCalcFeatures
		{
			EDaemonFeature m_SupportedFeatures;
			CCalcFeatures()
			{
				m_SupportedFeatures = EDaemonFeature_GlobalDaemon;
				try
				{
					if (NMib::NFile::CFile::fs_FileExists(NStr::CStr("/etc/redhat-release")))
					{
						NStr::CStr Contents = NMib::NFile::CFile::fs_ReadStringFromFile(NStr::CStr("/etc/redhat-release"));

						if (Contents.f_StartsWith("Fedora release 18 ")
							|| Contents.f_StartsWith("Fedora release 19 "))
							m_SupportedFeatures |= EDaemonFeature_EscapedPathBroken;
						else if (Contents.f_StartsWith("CentOS release 6.")
							|| Contents.f_StartsWith("Red Hat Enterprise Linux Server release 6."))
							m_SupportedFeatures |= EDaemonFeature_EscapeCharBroken;	// Upstart escape character is broken
					}
				}
				catch (NMib::NException::CException const &)
				{
				}

				try
				{
					if (NMib::NFile::CFile::fs_FileExists(NStr::CStr("/etc/SuSE-release")))
					{
						NStr::CStr Contents = NMib::NFile::CFile::fs_ReadStringFromFile(NStr::CStr("/etc/SuSE-release"));

						if (Contents.f_StartsWith("openSUSE"))
							m_SupportedFeatures |= EDaemonFeature_EscapedPathBroken;
					}
				}
				catch (NMib::NException::CException const &)
				{
				}

				try
				{
					if (NMib::NFile::CFile::fs_FileExists(NStr::CStr("/etc/lsb-release")))
					{
						NStr::CStr Contents = NMib::NFile::CFile::fs_ReadStringFromFile(NStr::CStr("/etc/lsb-release"));

						if (Contents.f_Find("DISTRIB_ID=Ubuntu") != -1)
							m_SupportedFeatures |= EDaemonFeature_EscapeCharBroken;	// Upstart escape character is broken
					}
				}
				catch (NMib::NException::CException const &)
				{
				}

				if (CSystemd::fs_IsSupported())
				{
					m_SupportedFeatures |= EDaemonFeature_LocalUserDaemon | EDaemonFeature_AllUsersDaemon;
				}
				else if (CUpstart::fs_IsSupported())
				{
					m_SupportedFeatures |= EDaemonFeature_LocalUserDaemon | EDaemonFeature_AllUsersDaemon;
				}
				else if (CGentoo::fs_IsSupported())
				{
				}
				else if (CScript::fs_IsSupported())
				{
				}
			}
		};
		constinit NStorage::TCAggregate<CCalcFeatures> g_SupportedFeatures = {DAggregateInit};
	}

	EDaemonFeature CDaemon::fs_SupportedFeatures()
	{
		return (*g_SupportedFeatures).m_SupportedFeatures;
	}


	NStr::CStr CDaemon::fs_GetUniquePrefix()
	{
#ifdef DProductCompanyUniqueIdentifier
		return DMibStringize(DProductCompanyUniqueIdentifier);
#else
		return "com.malterlib";
#endif
	}

	EActionResult CDaemon::f_Start()
	{
		return mp_pD->f_Start();
	}

	EActionResult CDaemon::f_Stop(bool _bWait)
	{
		return mp_pD->f_Stop(_bWait);
	}

	EActionResult CDaemon::f_Restart(bool _bWait)
	{
		return mp_pD->f_Restart(_bWait);
	}

	EActionResult CDaemon::f_Exists(bool &_bExists) const
	{
		return mp_pD->f_Exists(_bExists);
	}

	EActionResult CDaemon::f_Add(bool _bCheckForExisting)
	{
		return mp_pD->f_Add(_bCheckForExisting);
	}

	EActionResult CDaemon::f_Remove()
	{
		return mp_pD->f_Remove();
	}

	EActionResult CDaemon::f_Run()
	{
		return mp_pD->f_Run();
	}

	EActionResult CDaemon::f_RunAsProgram(bool _bDebug)
	{
		return mp_pD->f_Run();
	}

	bool CDaemon::f_IsShutdown() const
	{
		return false;
	}

	void CDaemon::f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Message)
	{
		f_GetDaemonParams().f_ReportInformation(_Heading, _Message);
	}

	void CDaemon::f_ReportError(NStr::CStr const& _Message)
	{
		f_GetDaemonParams().f_ReportError(_Message);
	}

	EReportError CDaemon::f_ReportErrorYesNo(NStr::CStr const& _Message, EReportError _Default)
	{
		return f_GetDaemonParams().f_ReportErrorYesNo(_Message, _Default);
	}

	bool CDaemon::fs_SupportsAutoRestart()
	{
		return CDaemon::CDetails::fs_SupportsAutoRestart();
	}

	void CDaemon::fs_QuitDaemon()
	{
		CDetails::fs_SigHandler(SIGTERM);
	}
}
