// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	EActionResult CDaemon::CDetails::f_Add(bool _bCheckForExisting)
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;

		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonAdd(_bCheckForExisting);

		SC_HANDLE schSCManager = fp_OpenSCManager();
		if (!schSCManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schSCManager);
				}
			)
		;

		NMib::NStr::CWStr RunAsUser;
		NMib::NStr::CWStrSecure RunAsUserPasssword;

		if ((!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty() || !mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty()) && !fp_PrepareUserAndGroup(mp_pOwner->mp_Params, RunAsUser, RunAsUserPasssword))
			return EActionResult_Failure;

		if (_bCheckForExisting)
		{
			SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_ALL_ACCESS);

			if (schService)
			{
				auto CleanupService = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schService);
						}
					)
				;

				QUERY_SERVICE_CONFIG *pQueryConfig;
				uint32 NeededSize = 0;
				QueryServiceConfig(schService, nullptr, 0, &NeededSize);

				{
					NContainer::CByteVector Vector;
					Vector.f_SetLen(NeededSize);
					pQueryConfig = (QUERY_SERVICE_CONFIG *)Vector.f_GetArray();

					if (QueryServiceConfig(schService, pQueryConfig, NeededSize, &NeededSize))
					{
						NContainer::TCVector<NStr::CStr> const &lDependencies = mp_pOwner->f_GetDaemonParams().f_GetDaemonDependencies();
						NContainer::TCVector<ch16> Deps;
						umint nDeps = lDependencies.f_GetLen();
						if (nDeps)
						{
							for (umint i = 0; i < nDeps; ++i)
							{
								NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(lDependencies[i]);
								Deps.f_Insert(Temp.f_GetStr(), Temp.f_GetLen() + 1);
							}
							Deps.f_Insert(ch16(0));
						}

						{
							if
								(
									!ChangeServiceConfigW
									(
										schService
										, SERVICE_NO_CHANGE
										, SERVICE_NO_CHANGE
										, SERVICE_NO_CHANGE
										, NStr::NPlatform::fg_StrToWindows(fp_GetAddCommandLine())
										, nullptr
										, nullptr
										, !Deps.f_IsEmpty() ? Deps.f_GetArray() : nullptr
										, !RunAsUser.f_IsEmpty() ? RunAsUser.f_GetStr() : nullptr
										, !RunAsUserPasssword.f_IsEmpty() ? RunAsUserPasssword.f_GetStr() : nullptr
										, nullptr
									)
								)
							{
								DMibTrace("Could not change daemon config\n", 0);
							}
						}
					}
				}

				fp_UpdateService(schService);

				NStr::CStr Error;
				if (!fp_GetDaemonParams().f_GetDisableWriteDaemon() && !fp_GetDaemonParams().f_WriteDaemonModeFile(Error))
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

				return EActionResult_Success;
			}
		}

		bool bAlreadyExists = false;
		EActionResult Result = f_Exists(bAlreadyExists);
		if (Result != EActionResult_Success)
			return Result;

		if (bAlreadyExists)
		{
			mp_pOwner->f_ReportError("Daemon already exists, remove it manually with --daemon-remove before trying to add again.");
			return EActionResult_Failure;
		}

		{
			SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), DELETE);

			if (schService)
			{
				if (!DeleteService(schService))
				{
					f_ReportError(NStr::CStr::CFormat("Unable to delete daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
			}
		}

		NContainer::TCVector<NStr::CStr> const& lDependencies = mp_pOwner->f_GetDaemonParams().f_GetDaemonDependencies();
		NContainer::TCVector<ch16> Deps;
		umint nDeps = lDependencies.f_GetLen();
		if (nDeps)
		{
			for (umint i = 0; i < nDeps; ++i)
			{
				NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(lDependencies[i]);
				Deps.f_Insert(Temp.f_GetStr(), Temp.f_GetLen() + 1);
			}
			Deps.f_Insert(ch16(0));
		}

		NStr::CWStr ServiceGroup = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonGroup());
		SC_HANDLE schService = CreateServiceW
			(
				schSCManager              // SCManager database
				, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName())              // name of daemon
				, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonDisplayName())           // daemon name to display
				, SERVICE_ALL_ACCESS        // desired access
				, SERVICE_WIN32_OWN_PROCESS | (mp_pOwner->f_GetDaemonParams().f_GetInteractive() ? SERVICE_INTERACTIVE_PROCESS : 0) // daemon type
				, SERVICE_AUTO_START      // start type
				, SERVICE_ERROR_NORMAL      // error control type
				, NStr::NPlatform::fg_StrToWindows(fp_GetAddCommandLine())        // service's binary
				, !mp_pOwner->f_GetDaemonParams().f_GetDaemonGroup().f_IsEmpty() ? ServiceGroup.f_GetStr() : nullptr          // no load ordering group
				, nullptr                      // no tag identifier
				, !Deps.f_IsEmpty() ? Deps.f_GetArray() : nullptr                      // no dependencies
				, !RunAsUser.f_IsEmpty() ? RunAsUser.f_GetStr() : nullptr
				, !RunAsUserPasssword.f_IsEmpty() ? RunAsUserPasssword.f_GetStr() : nullptr
			)
		;

		if (schService == nullptr)
		{
			f_ReportError(NStr::CStr::CFormat("Error returned when creating daemon {}\r\n{}") << fp_GetAddCommandLine() << NMib::NPlatform::fg_Win32_GetLastErrorStr(0) );
			return EActionResult_Failure;
		}
		else
			DMibTrace("Creation of daemon successful", 0);

		auto CleanupService = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schService);
				}
			)
		;

		NStr::CStr Error;
		if (!fp_GetDaemonParams().f_GetDisableWriteDaemon() && !fp_GetDaemonParams().f_WriteDaemonModeFile(Error))
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

		fp_UpdateService(schService);
		return EActionResult_Success;
	}

	EActionResult CDaemon::CDetails::f_Remove()
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;

		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonRemove();

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

		SC_HANDLE schSCManager = fp_OpenSCManager();
		if (!schSCManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}

		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schSCManager);
				}
			)
		;

		SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), DELETE);
		if (schService)
		{
			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(schService);
					}
				)
			;
			if (!DeleteService(schService))
			{
				f_ReportError(NStr::CStr::CFormat("Unable to delete daemon: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
				return EActionResult_Failure;
			}
		}

		return EActionResult_Success;
	}

	void CDaemon::CDetails::fp_UpdateService(SC_HANDLE _Service)
	{
		SERVICE_DESCRIPTIONW Description;
		NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonDescription());
		Description.lpDescription = (ch16 *)Temp.f_GetStr();
		ChangeServiceConfig2W(_Service, SERVICE_CONFIG_DESCRIPTION, &Description);

		SERVICE_FAILURE_ACTIONSW RestartActions;

		RestartActions.dwResetPeriod = 60;
		Temp = NStr::NPlatform::fg_StrToWindows(NStr::CStr("Rebooting the server in response to crash of ") + mp_pOwner->f_GetDaemonParams().f_GetDaemonDisplayName() + " crash.");
		RestartActions.lpRebootMsg = (ch16 *)Temp.f_GetStr();
		RestartActions.lpCommand = fg_AutoConstCast(str_utf16(""));
		RestartActions.cActions = 3;
		SC_ACTION Actions[3];
		Actions[0].Delay = 1000;
		Actions[0].Type = SC_ACTION_RESTART;
		Actions[1].Delay = 10 * 1000;
		Actions[1].Type = SC_ACTION_RESTART;
		Actions[2].Delay = 60 * 1000;
		Actions[2].Type = SC_ACTION_RESTART;
		RestartActions.lpsaActions = Actions;

		if (!ChangeServiceConfig2W(_Service, SERVICE_CONFIG_FAILURE_ACTIONS, &RestartActions))
			f_ReportError(NStr::CStr::CFormat("Failed to change service config: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));

		SERVICE_FAILURE_ACTIONS_FLAG FailureActionFlags = {0};
		FailureActionFlags.fFailureActionsOnNonCrashFailures = true;
		if (!ChangeServiceConfig2W(_Service, SERVICE_CONFIG_FAILURE_ACTIONS_FLAG, &FailureActionFlags))
			f_ReportError(NStr::CStr::CFormat("Failed to change service config: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));

		if (NMib::NPlatform::fg_IsVista())
		{
			SERVICE_PRESHUTDOWN_INFO PreShutDown;
			PreShutDown.dwPreshutdownTimeout = (fp_GetDaemonParams().f_GetMaxShutdownTime() * 1000.0).f_ToInt();
			ChangeServiceConfig2W(_Service, SERVICE_CONFIG_PRESHUTDOWN_INFO, &PreShutDown);
		}

		SERVICE_SID_INFO ServiceSidInfo = {0};
		ServiceSidInfo.dwServiceSidType = SERVICE_SID_TYPE_UNRESTRICTED;
		if (!ChangeServiceConfig2W(_Service, SERVICE_CONFIG_SERVICE_SID_INFO, &ServiceSidInfo))
			f_ReportError(NStr::CStr::CFormat("Failed to set service SID type: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
	}

	EActionResult CDaemon::CDetails::f_Exists(bool &_bExists) const
	{
		if (!fp_CheckParamsSupported(fp_GetDaemonParams()))
			return EActionResult_Failure;

		if (fp_GetDaemonParams().f_GetDaemonMode() != EDaemonMode_Global)
			return fp_UserDaemonExists(_bExists);

		_bExists = false;
		SC_HANDLE schSCManager = fp_OpenSCManager();
		if (!schSCManager)
		{
			f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
			return EActionResult_Failure;
		}
		auto CleanupServiceManager = fg_OnScopeExit
			(
				[&]
				{
					CloseServiceHandle(schSCManager);
				}
			)
		;

		SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetDaemonParams().f_GetDaemonName()), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);

		if (schService)
		{
			auto CleanupService = fg_OnScopeExit
				(
					[&]
					{
						CloseServiceHandle(schService);
					}
				)
			;
			_bExists = true;;
		}
		return EActionResult_Success;
	}
}
