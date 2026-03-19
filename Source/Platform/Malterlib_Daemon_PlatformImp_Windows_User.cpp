// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

#include <Mib/Core/PlatformSpecific/WindowsRegistry>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NDaemon
{
	using namespace NStr;
	namespace
	{
		NStr::CStr fg_GetName(CDaemonParams const &_Params)
		{
			return "Malterlib_Daemon_{}"_f << _Params.f_GetDaemonName();
		}

		NStr::CStr fg_GetExecutableCommand(CDaemonParams const &_Params)
		{
			NContainer::TCVector<CStr> Params;

			Params.f_Insert(NFile::NPlatform::fg_ConvertToWindowsPath(NSys::NFile::fg_GetProgramPath(), false));

			if (auto AddCommandLine = _Params.f_GetAddCommandLine())
				return NProcess::CProcessLaunchParams::fs_GetParamsWindows(Params) + " " + AddCommandLine;
			else
			{
				Params.f_Insert({"--daemon-run-debug", "--detach-console"});
				return NProcess::CProcessLaunchParams::fs_GetParamsWindows(Params);
			}
		}

		NMib::NPlatform::CWin32_Registry fg_GetRegistry(CDaemonParams const &_Params)
		{
			auto Root = NMib::NPlatform::CWin32_Registry::ERegRoot_CurrentUser;
			if (_Params.f_GetDaemonMode() == EDaemonMode_AllUsers)
				Root = NMib::NPlatform::CWin32_Registry::ERegRoot_LocalMachine;
			return NMib::NPlatform::CWin32_Registry(Root, "Software\\Microsoft\\Windows\\CurrentVersion\\Run");
		}

		bool fg_IsRunning(NStr::CStr const &_RootDirectory)
		{
			CStr DaemonPidPath = _RootDirectory / (NFile::CFile::fs_GetFileNoExt(NFile::CFile::fs_GetProgramPath()) + ".ServicePID");
			return NFile::CFile::fs_FileExists(DaemonPidPath);
		}
	}

	EActionResult CDaemon::CDetails::fp_UserDaemonExists(bool &_bExists) const
	{
		auto &Params = fp_GetDaemonParams();
		try
		{
			auto Registry = fg_GetRegistry(Params);
			_bExists = Registry.f_ValueExists("", fg_GetName(Params));
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to query registry for daemon existance: {}"_f << _Exception);
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::fp_UserDaemonStart()
	{
		bool bDaemonExists;
		if (f_Exists(bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			f_ReportError("Daemon is not installed so it can not be started");
			return EActionResult_Failure;
		}

		CStr RootDirectory = fp_GetDaemonParams().f_GetRootDirectory();

		if (fg_IsRunning(RootDirectory))
		{
			f_ReportInformation("Start Daemon", "Daemon is already running");
			return EActionResult_Success;
		}

		try
		{
			NConcurrency::TCPromise<void> LaunchResult;
			NMib::NProcess::CProcessLaunchParams Params = NMib::NProcess::CProcessLaunchParams::fs_LaunchExecutable
				(
					NSys::NFile::fg_GetProgramPath()
					, NContainer::TCVector<CStr>{"--daemon-run-debug", "--detach-console"}
					, NSys::NFile::fg_GetProgramDirectory()
					, [=](NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
					{
						switch (_State.f_GetTypeID())
						{
						case NMib::NProcess::EProcessLaunchState_LaunchFailed:
							{
								LaunchResult.f_SetException(DMibErrorInstance(_State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>()));
								break;
							}
						case NMib::NProcess::EProcessLaunchState_Launched:
							{
								LaunchResult.f_SetResult();
								break;
							}
						case NMib::NProcess::EProcessLaunchState_Exited:
							break;
						}
					}
				)
			;

			NMib::NProcess::CProcessLaunch ProcessLaunch(Params, NMib::NProcess::EProcessLaunchCloseFlag_None);
			LaunchResult.f_MoveFuture().f_CallSync(60.0);

			NTime::CStopwatch Stopwatch{true};
			while (!fg_IsRunning(RootDirectory))
			{
				if (Stopwatch.f_GetTime() >= 60.0)
					DMibError("Timed out waiting for daemon to start");
				Sleep(10);
			}
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to start daemon: {}"_f << _Exception);
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::fp_UserDaemonStop(bool _bWait)
	{
		CStr RootDirectory = fp_GetDaemonParams().f_GetRootDirectory();

		if (!fg_IsRunning(RootDirectory))
		{
			bool bDaemonExists;
			if (f_Exists(bDaemonExists) == EActionResult_Failure)
				return EActionResult_Failure;

			if (!bDaemonExists)
				f_ReportInformation("Stop Daemon", "Daemon is not installed so it can not be stopped");
			else
				f_ReportInformation("Stop Daemon", "Daemon was not running");

			return EActionResult_Success;
		}

		try
		{
			CStr DaemonPidPath = RootDirectory / (NFile::CFile::fs_GetFileNoExt(NFile::CFile::fs_GetProgramPath()) + ".ServicePID");
			umint PID = NFile::CFile::fs_ReadStringFromFile(DaemonPidPath, true).f_ToInt(0);

			HANDLE hProcess = nullptr;
			if (PID && _bWait)
				hProcess = OpenProcess(SYNCHRONIZE, false, PID);

			auto Cleanup = fg_OnScopeExit
				(
					[&]
					{
						if (hProcess)
							CloseHandle(hProcess);
					}
				)
			;

			CStr DaemonStateFile = RootDirectory / (NFile::CFile::fs_GetFileNoExt(NFile::CFile::fs_GetProgramPath()) + ".ServiceState");
			NFile::CFile::fs_WriteStringToFile(DaemonStateFile, "Stop", false);

			if (hProcess)
			{
				NTime::CStopwatch Stopwatch{true};
				auto WaitResult = WaitForSingleObject(hProcess, 240000);
				if (WaitResult == WAIT_TIMEOUT)
					DMibError("Timed out waiting for process to exit");
			}
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to stop daemon: {}"_f << _Exception);
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::fp_UserDaemonAdd(bool _bCheckForExisting)
	{
		auto &Params = fp_GetDaemonParams();
		CStr Name = fg_GetName(Params);
		try
		{
			auto Registry = fg_GetRegistry(Params);
			Registry.f_Write("", Name, fg_GetExecutableCommand(Params));
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to query registry for daemon existance: {}"_f << _Exception);
			return EActionResult_Failure;
		}

		NStr::CStr Error;
		if (!fp_GetDaemonParams().f_GetDisableWriteDaemon() && !fp_GetDaemonParams().f_WriteDaemonModeFile(Error))
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::fp_UserDaemonRemove()
	{
		bool bDaemonExists;
		if (f_Exists(bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			f_ReportInformation("Remove Daemon", "Daemon is not installed so it has not been removed");
			return EActionResult_Success;
		}

		if (f_Stop(true) == EActionResult_Failure)
			return EActionResult_Failure;

		auto &Params = fp_GetDaemonParams();

		try
		{
			auto Registry = fg_GetRegistry(Params);
			Registry.f_DeleteValue("", fg_GetName(Params));
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to remove daemon from registry: {}"_f << _Exception);
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}
}
