// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

#include <Mib/Core/PlatformSpecific/WindowsRegistry>
#include <Mib/Process/ProcessLaunch>
#include <Mib/Concurrency/ConcurrencyManager>

namespace NMib::NService
{
	using namespace NStr;
	namespace
	{
		NStr::CStr fg_GetName(CServiceParams const &_Params)
		{
			return "Malterlib_Daemon_{}"_f << _Params.f_GetServiceName();
		}

		NStr::CStr fg_GetExecutableCommand(CServiceParams const &_Params)
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

		NMib::NPlatform::CWin32_Registry fg_GetRegistry(CServiceParams const &_Params)
		{
			auto Root = NMib::NPlatform::CWin32_Registry::ERegRoot_CurrentUser;
			if (_Params.f_GetServiceMode() == EServiceMode_AllUsers)
				Root = NMib::NPlatform::CWin32_Registry::ERegRoot_LocalMachine;
			return NMib::NPlatform::CWin32_Registry(Root, "Software\\Microsoft\\Windows\\CurrentVersion\\Run");
		}

		bool fg_IsRunning()
		{
			CStr ServicePidPath = NFile::CFile::fs_GetProgramDirectory() + "/ServicePID";
			return NFile::CFile::fs_FileExists(ServicePidPath);
		}
	}

	EActionResult CService::CDetails::fp_UserDaemonExists(bool &_bExists) const
	{
		auto &Params = fp_GetServiceParams();
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

	EActionResult CService::CDetails::fp_UserDaemonStart()
	{
		auto &Params = fp_GetServiceParams();

		bool bServiceExists;
		if (f_Exists(bServiceExists) == EActionResult_Failure)
			return EActionResult_Failure;
				
		if (!bServiceExists)
		{
			f_ReportInformation("Start Service", "Service is not installed so it can not be started");
			return EActionResult_Success;
		}

		if (fg_IsRunning())
		{
			f_ReportInformation("Start Service", "Service is already running");
			return EActionResult_Success;
		}

		try
		{
			NConcurrency::TCContinuation<void> LaunchResult;
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
						}
					}
				)
			;

			NMib::NProcess::CProcessLaunch ProcessLaunch(Params, NMib::NProcess::EProcessLaunchCloseFlag_None);
			LaunchResult.f_CallSync(60.0);
		}
		catch (NException::CException const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to start daemon: {}"_f << _Exception);
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	EActionResult CService::CDetails::fp_UserDaemonStop(bint _bWait)
	{
		auto &Params = fp_GetServiceParams();

		bool bServiceExists;
		if (f_Exists(bServiceExists) == EActionResult_Failure)
			return EActionResult_Failure;
				
		if (!bServiceExists)
		{
			f_ReportInformation("Stop Service", "Service is not installed so it can not be stopped");
			return EActionResult_Success;
		}

		if (!fg_IsRunning())
		{
			f_ReportInformation("Stop Service", "Service was not running");
			return EActionResult_Success;
		}

		try
		{
			CStr ServicePidPath = NFile::CFile::fs_GetProgramDirectory() + "/ServicePID";
			mint PID = NFile::CFile::fs_ReadStringFromFile(ServicePidPath, true).f_ToInt(0);

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

			CStr ServiceStateFile = NFile::CFile::fs_GetProgramDirectory() + "/ServiceState";
			NFile::CFile::fs_WriteStringToFile(ServiceStateFile, "Stop", false);

			if (hProcess)
			{
				if (WaitForSingleObject(hProcess, 240000) == WAIT_TIMEOUT)
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

	EActionResult CService::CDetails::fp_UserDaemonAdd(bint _bCheckForExisting)
	{
		auto &Params = fp_GetServiceParams();
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

		return EActionResult_Success;
	}

	EActionResult CService::CDetails::fp_UserDaemonRemove()
	{
		bool bServiceExists;
		if (f_Exists(bServiceExists) == EActionResult_Failure)
			return EActionResult_Failure;
				
		if (!bServiceExists)
		{
			f_ReportInformation("Remove Service", "Service is not installed so it has not been removed");
			return EActionResult_Success;
		}

		if (f_Stop(true) == EActionResult_Failure)
			return EActionResult_Failure;

		auto &Params = fp_GetServiceParams();

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
