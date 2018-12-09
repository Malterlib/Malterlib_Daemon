// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon.h"

namespace NMib::NDaemon
{
	CDaemonParams::CDaemonParams
		(
			NStr::CStr const& _DaemonName
			, NStr::CStr const& _DisplayName
			, NStr::CStr const& _DaemonDesc
			, void* _pNativeHandle
			, FImplementationFactory const& _ImplementationFactory
			, FProcessCommand const& _ProcessCommand
			, FErrorReporter const& _ErrorReporter
			, FInformationReporter const& _InformationReporter
			, FErrorReporterYesNo const& _ErrorReporterYesNo
		)
		: mp_DaemonName(_DaemonName)
		, mp_DisplayName(_DisplayName)
		, mp_DaemonDesc(_DaemonDesc)
		, mp_fImplementationFactory(_ImplementationFactory)
		, mp_fProcessCommand(_ProcessCommand)
		, mp_fReportError(_ErrorReporter)
		, mp_fReportInformation(_InformationReporter)
		, mp_fReportErrorYesNo(_ErrorReporterYesNo)
		, mp_pNativeHandle(_pNativeHandle)
		, mp_DaemonMode(EDaemonMode_Global)
		, mp_bInteractive(false)
		, mp_bCustomDaemonName(false)
		, mp_bDisableWriteDaemon(false)
		, mp_bKeepRunning(false)
		, mp_bDetachConsole(false)
		, mp_bDaemonize(false)
	{
	}

	CDaemonParams::~CDaemonParams()
	{

	}

	NMib::NStr::CStr CDaemonParams::fp_CleanDaemonName(NMib::NStr::CStr const &_DaemonName)
	{
		NStr::CStr Result;

		aint iParse = 0;
		ch32 Current = _DaemonName.f_GetAt(iParse);

		while (Current)
		{
			if (NStr::fg_CharIsAlphabetical(Current) || NStr::fg_CharIsNumber(Current) || Current == '_' || Current == '-' || Current == '.')
				Result.f_AddChar(Current);

			Current = _DaemonName.f_GetAt(++iParse);
		}

		return Result;
	}

	void CDaemonParams::fp_CopyElementsToCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine)
	{
		for (auto iArg = _CommandLine.f_GetIterator(); iArg; ++iArg)
		{
			if (mp_CommandLine.f_IsEmpty())
				mp_CommandLine += *iArg;
			else
				mp_CommandLine += NStr::CStr::CFormat(" {}") << *iArg;
		}
	}

	void CDaemonParams::fs_ParseOptions(COptionHandlerMap &_OptionHandlers, CCommandLineVector &_CommandLine)
	{
		while (!_CommandLine.f_IsEmpty())
		{
			NStr::CStr Keyname = _CommandLine.f_Pop();
			if (!Keyname.f_IsEmpty() && Keyname.f_Left(1) == "-")
			{
				Keyname = Keyname.f_Delete(0,1);
				if (_OptionHandlers.f_Exists(Keyname))
					_OptionHandlers[Keyname](_CommandLine);
				else
				{
					_CommandLine.f_InsertFirst(Keyname);
					_OptionHandlers["Custom"](_CommandLine);
				}
			}
		}
	}

	bool CDaemonParams::fs_ParseOptionArgument(CCommandLineVector &_CommandLine, NStr::CStr &_Destination)
	{
		if (!_CommandLine.f_IsEmpty() && _CommandLine[0].f_Left(1) != "-")
		{
			NStr::CStr Arg = _CommandLine.f_Pop();
			if (Arg.f_IsEmpty())
				return false;

			_Destination = Arg;
			return true;
		}
		return false;
	}

	void CDaemonParams::f_ParseCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine)
	{
		bool bWriteDaemonName = false;
		mp_CommandLine.f_Clear();

		mp_Action = EDaemonAction_Custom;
		mp_ActionParam = true;
		mp_lRawArguments = _CommandLine;

		NContainer::TCVector<NMib::NStr::CStr> Cmd(_CommandLine);

		if (!Cmd.f_IsEmpty())
			Cmd.f_Pop();		// Executable name

		if (mp_ExecutablePath.f_IsEmpty())
			mp_ExecutablePath = NFile::CFile::fs_GetProgramPath();

		NStr::CStr DaemonNamePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceName";
		try
		{
			if (NFile::CFile::fs_FileExists(DaemonNamePath))
			{
				NStr::CStr DaemonName = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(DaemonNamePath), true);
				if (!DaemonName.f_IsEmpty() && DaemonName != mp_DaemonName)
				{
					mp_DaemonName = DaemonName;
					mp_bCustomDaemonName = true;
				}
			}
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			f_ReportError(NStr::CStr::CFormat("Failed to read daemon name file {}. The error was: {}") << DaemonNamePath << _Exception.f_GetErrorStr());
		}

		NStr::CStr DaemonModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";
		try
		{
			if (NFile::CFile::fs_FileExists(DaemonModePath))
			{
				NStr::CStr DaemonMode = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(DaemonModePath), true);
				if (DaemonMode.f_CmpNoCase("AllUsers") == 0)
					mp_DaemonMode = EDaemonMode_AllUsers;
				else if (DaemonMode.f_CmpNoCase("LocalUser") == 0)
					mp_DaemonMode = EDaemonMode_LocalUser;
				else
					mp_DaemonMode = EDaemonMode_Global;
			}
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			f_ReportError(NStr::CStr::CFormat("Failed to read daemon mode file {}. The error was: {}") << DaemonModePath << _Exception.f_GetErrorStr());
		}

		NContainer::TCVector<NMib::NStr::CStr> lArgsWithoutExecutable = Cmd;
		fp_CopyElementsToCommandLine(lArgsWithoutExecutable);

		if (!Cmd.f_IsEmpty())
		{

			COptionHandlerMap OptionHandlers;

			OptionHandlers["-daemon-add"] = OptionHandlers["AddService"] = [&](CCommandLineVector &_Cmd)
				{
					NStr::CStr DaemonName;
					mp_Action = EDaemonAction_Add;
					mp_ActionParam = false;
					fs_ParseOptionArgument(Cmd, DaemonName);
					if (!DaemonName.f_IsEmpty() && DaemonName != mp_DaemonName)
					{
						mp_DaemonName = DaemonName;
						mp_bCustomDaemonName = true;
					}

					bWriteDaemonName = true;
				}
			;

			OptionHandlers["AddServiceIfNotAdded"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Add;
					mp_ActionParam = true;
					NStr::CStr DaemonName;
					fs_ParseOptionArgument(Cmd, DaemonName);
					if (!DaemonName.f_IsEmpty() && DaemonName != mp_DaemonName)
					{
						mp_DaemonName = DaemonName;
						mp_bCustomDaemonName = true;
					}

					bWriteDaemonName = true;
				}
			;
			OptionHandlers["-daemon-remove"] = OptionHandlers["RemoveService"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Remove;
					fs_ParseOptionArgument(Cmd, mp_DaemonName);
				}
			;
			OptionHandlers["-daemon-start"] = OptionHandlers["StartService"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Start;
					fs_ParseOptionArgument(Cmd, mp_DaemonName);
				}
			;
			OptionHandlers["-daemon-restart"] = OptionHandlers["RestartService"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Restart;
					fs_ParseOptionArgument(Cmd, mp_DaemonName);
				}
			;
			OptionHandlers["-daemon-stop"] = OptionHandlers["StopService"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Stop;
					fs_ParseOptionArgument(Cmd, mp_DaemonName);
				}
			;
			OptionHandlers["-daemon-run-debug"] = OptionHandlers["RunAsProgram"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_RunAsProgram;
				}
			;
			OptionHandlers["-daemon-exists"] = OptionHandlers["Exists"] = [&](CCommandLineVector &_Cmd)
			{
				mp_Action = EDaemonAction_Exists;
				fs_ParseOptionArgument(Cmd, mp_DaemonName);
			}
			;
			OptionHandlers["-daemon-run"] = OptionHandlers["Service"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EDaemonAction_Run;
					fs_ParseOptionArgument(Cmd, mp_DaemonName);
				}
			;

			OptionHandlers["-mode"] = [&](CCommandLineVector &_Cmd)
				{
					NStr::CStr Mode;
					fs_ParseOptionArgument(Cmd, Mode);
					if (Mode == "global")
						mp_DaemonMode = EDaemonMode_Global;
					else if (Mode == "user")
						mp_DaemonMode = EDaemonMode_LocalUser;
					else if (Mode == "all-users")
						mp_DaemonMode = EDaemonMode_AllUsers;
				}
			;

			OptionHandlers["LocalUser"] = [&](CCommandLineVector &_Cmd)
				{
					mp_DaemonMode = EDaemonMode_LocalUser;
				}
			;
			OptionHandlers["AllUsers"] = [&](CCommandLineVector &_Cmd)
				{
					mp_DaemonMode = EDaemonMode_AllUsers;
				}
			;

			OptionHandlers["-run-as-user"] = OptionHandlers["RunAsUser"] = [&](CCommandLineVector &_Cmd)
				{
					fs_ParseOptionArgument(_Cmd, mp_RunAsUser);
				}
			;

			OptionHandlers["-run-as-group"] = OptionHandlers["RunAsGroup"] = [&](CCommandLineVector &_Cmd)
				{
					fs_ParseOptionArgument(_Cmd, mp_RunAsGroup);
				}
			;

			OptionHandlers["DisableWriteService"] = [&](CCommandLineVector &_Cmd)
				{
					mp_bDisableWriteDaemon  = true;
				}
			;

			OptionHandlers["KeepRunning"] = [&](CCommandLineVector &_Cmd)
				{
					mp_bKeepRunning  = true;
				}
			;

			OptionHandlers["-detach-console"] = OptionHandlers["DetachConsole"] = [&](CCommandLineVector &_Cmd)
				{
					mp_bDetachConsole = true;
				}
			;

			OptionHandlers["-daemonize"] = OptionHandlers["Daemonize"] = [&](CCommandLineVector &_Cmd)
				{
					mp_bDaemonize  = true;
				}
			;


			OptionHandlers["Custom"] = [&](CCommandLineVector &_Cmd)
				{
					if (mp_CustomAction.f_IsEmpty() && mp_Action == EDaemonAction_Custom)
					{
						mp_Action = EDaemonAction_Custom;
						fs_ParseOptionArgument(Cmd, mp_CustomAction);
					}
				}
			;

			fs_ParseOptions(OptionHandlers, Cmd);
		}

		mp_DaemonName = fp_CleanDaemonName(mp_DaemonName);
		if (mp_bCustomDaemonName)
			mp_DisplayName += NStr::CStr::CFormat(" ({})") << mp_DaemonName;

		if (bWriteDaemonName && !mp_bDisableWriteDaemon)
		{
			NStr::CStr Error;
			if (!f_WriteDaemonNameFile(Error))
				f_ReportError(NStr::CStr::CFormat("Failed to write daemon name file: {}") << Error);
		}

		//fp_Trace();
	}

	bool CDaemonParams::f_WriteDaemonNameFile(NStr::CStr &_Error) const
	{
		NStr::CStr DaemonNamePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceName";

		try
		{
			NFile::CFile::fs_WriteStringToFile(NStr::CStr(DaemonNamePath), mp_DaemonName, false);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			_Error = _Exception.f_GetErrorStr();
			return false;
		}

		return true;
	}

	bool CDaemonParams::fp_Trace(NStr::CStr &_Error)
	{
		NContainer::CRegistry_CStr TraceRegistry;
		TraceRegistry.f_SetValue("ServiceName", mp_DaemonName);
		TraceRegistry.f_SetValue("Params", NSys::fg_Process_GetCommandLine());

		NStr::CStr ServiceTracePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceTrace";

		try
		{
			NStr::CStr OutputStr = TraceRegistry.f_GenerateStr();
			NFile::CFile::fs_WriteStringToFile(NStr::CStr(ServiceTracePath), OutputStr, false);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			_Error = _Exception.f_GetErrorStr();
			return false;
		}
		return true;
	}

	bool CDaemonParams::f_WriteDaemonModeFile(NStr::CStr &_Error) const
	{
		NStr::CStr ModeString;
		switch (mp_DaemonMode)
		{
		case EDaemonMode_AllUsers:
			ModeString = "AllUsers";
				break;
		case EDaemonMode_LocalUser:
			ModeString = "LocalUser";
				break;
		default:
			ModeString = "Global";
			break;
		}

		NStr::CStr DaemonModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";

		try
		{
			NFile::CFile::fs_WriteStringToFile(NStr::CStr(DaemonModePath), ModeString, false);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			_Error = _Exception.f_GetErrorStr();
			return false;
		}

		return true;
	}

	bool CDaemonParams::f_RemoveDaemonModeFile(NStr::CStr &_Error) const
	{
		NStr::CStr DaemonModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";
		try
		{
			if (NFile::CFile::fs_FileExists(DaemonModePath))
				NFile::CFile::fs_DeleteFile(DaemonModePath);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			_Error = _Exception.f_GetErrorStr();
			return false;
		}

		return true;
	}

	NStr::CStr CDaemonParams::f_GetCustomActionKey() const
	{
		return mp_CustomAction;
	}

	void CDaemonParams::f_SetAction(EDaemonAction _Action)
	{
		mp_Action = _Action;
	}

	EDaemonAction CDaemonParams::f_GetAction() const
	{
		return mp_Action;
	}

	void* CDaemonParams::f_GetNativeHandle() const
	{
		return mp_pNativeHandle;
	}

	CDaemonActionParam const &CDaemonParams::f_GetActionParam() const
	{
		return mp_ActionParam;
	}

	void CDaemonParams::f_SetActionParam(CDaemonActionParam const &_Param)
	{
		mp_ActionParam = _Param;
	}

	NStr::CStr CDaemonParams::f_GetDaemonGroup() const
	{
		return mp_DaemonGroup;
	}

	bool CDaemonParams::f_GetInteractive() const
	{
		return mp_bInteractive;
	}

	bool CDaemonParams::f_GetDisableWriteDaemon() const
	{
		return mp_bDisableWriteDaemon;
	}

	void CDaemonParams::f_SetDisableWriteDaemon(bool _bDisable)
	{
		mp_bDisableWriteDaemon = _bDisable;
	}

	bool CDaemonParams::f_GetKeepRunning() const
	{
		return mp_bKeepRunning;
	}

	void CDaemonParams::f_SetDetachConsole(bool _bValue)
	{
		mp_bDetachConsole = _bValue;
	}

	bool CDaemonParams::f_GetDetachConsole() const
	{
		return mp_bDetachConsole;
	}

	bool CDaemonParams::f_GetDaemonize() const
	{
		return mp_bDaemonize;
	}

	EDaemonMode CDaemonParams::f_GetDaemonMode() const
	{
		return mp_DaemonMode;
	}

	void CDaemonParams::f_SetDaemonMode(EDaemonMode _Mode)
	{
		mp_DaemonMode = _Mode;
	}

	NStr::CStr CDaemonParams::f_GetDaemonName() const
	{
		return mp_DaemonName;
	}

	void CDaemonParams::f_SetDaemonName(NStr::CStr const &_DaemonName, bool _bCustom)
	{
		mp_DaemonName = _DaemonName;
		mp_bCustomDaemonName = _bCustom;
	}

	NStr::CStr CDaemonParams::f_GetDaemonDisplayName() const
	{
		return mp_DisplayName;
	}

	NStr::CStr CDaemonParams::f_GetDaemonDescription() const
	{
		return mp_DaemonDesc;
	}

	NContainer::TCVector<NStr::CStr> const& CDaemonParams::f_GetDaemonDependencies() const
	{
		return mp_DaemonDependencies;
	}

	NStr::CStr CDaemonParams::f_GetExecutablePath() const
	{
		return mp_ExecutablePath;
	}

	void CDaemonParams::f_SetExecutablePath(NStr::CStr const &_Path)
	{
		mp_ExecutablePath = _Path;
	}

	NStr::CStr CDaemonParams::f_GetCommandLine() const
	{
		return mp_CommandLine;
	}

	void CDaemonParams::f_SetRunAsUser(NStr::CStr const &_User)
	{
		mp_RunAsUser = _User;
	}

	NStr::CStr CDaemonParams::f_GetRunAsUser() const
	{
		return mp_RunAsUser;
	}

	void CDaemonParams::f_SetRunAsGroup(NStr::CStr const &_Group)
	{
		mp_RunAsGroup = _Group;
	}

	NStr::CStr CDaemonParams::f_GetRunAsGroup() const
	{
		return mp_RunAsGroup;
	}

	NStr::CStr CDaemonParams::f_GetLocalizedStr(NStr::CStr const& _Key) const
	{
		NStr::CStr LocalizedString;
		mp_LocalizedStrings.f_Lookup(_Key, LocalizedString);
		return LocalizedString;
	}

	void CDaemonParams::f_SetAddCommandLine(NStr::CStr const& _CommandLine)
	{
		mp_AddCommandLine = _CommandLine;
	}

	NStr::CStr CDaemonParams::f_GetAddCommandLine() const
	{
		return mp_AddCommandLine;
	}

	bool CDaemonParams::f_IsKeySet(NStr::CStr const &_Key) const
	{
		for (auto iArg = mp_lRawArguments.f_GetIterator(); iArg; ++iArg)
		{
			if (*iArg == _Key)
				return true;
		}
		return false;
	}

	void CDaemonParams::f_SetKey(NStr::CStr const &_Key, bool _bKeySet)
	{
		for (auto iArg = mp_lRawArguments.f_GetIterator(); iArg; ++iArg)
		{
			if (*iArg == _Key)
			{
				if (!_bKeySet)
					mp_lRawArguments.f_Remove(&*iArg - mp_lRawArguments.f_GetArray());
				return;
			}
		}
		if (_bKeySet)
			mp_lRawArguments.f_Insert(_Key);
	}

	void CDaemonParams::f_SetValueForKey(NStr::CStr const &_Key, NStr::CStr const &_Value)
	{
		for (auto iArg = mp_lRawArguments.f_GetIterator(); iArg; ++iArg)
		{
			if (*iArg == _Key)
			{
				++iArg;
				if (iArg)
				{
					*iArg = _Value;
					return;
				}
				mp_lRawArguments.f_Insert(_Value);
				return ;
			}
		}

		mp_lRawArguments.f_Insert(_Key);
		mp_lRawArguments.f_Insert(_Value);
	}

	NStr::CStr CDaemonParams::f_GetValueForKey(NStr::CStr const& _Key) const
	{
		for (auto iArg = mp_lRawArguments.f_GetIterator(); iArg; ++iArg)
		{
			if (*iArg == _Key)
			{
				++iArg;
				if (iArg)
					return *iArg;

				return NStr::CStr();
			}
		}

		return NStr::CStr();
	}

	NStorage::TCUniquePointer<CDaemonImp> CDaemonParams::f_ImplementationFactory() const
	{
		if (mp_fImplementationFactory.f_IsEmpty())
			return nullptr;

		return mp_fImplementationFactory();
	}

	EActionResult CDaemonParams::f_ProcessCommand(CDaemon* _pDaemon, bint& _bHandled)
	{
		if (mp_fProcessCommand.f_IsEmpty())
			return EActionResult_Failure;

		return mp_fProcessCommand(*this, _pDaemon, _bHandled);
	}

	void CDaemonParams::f_ReportError(NStr::CStr const& _Error) const
	{
		if (!mp_fReportError.f_IsEmpty())
			mp_fReportError(_Error);
	}

	void CDaemonParams::f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information) const
	{
		if (!mp_fReportInformation.f_IsEmpty())
			mp_fReportInformation(_Heading, _Information);
	}

	EReportError CDaemonParams::f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default) const
	{
		if (!mp_fReportErrorYesNo.f_IsEmpty())
			return mp_fReportErrorYesNo(_Error, _Default);

		return EReportError_No;
	}

	CDaemonParams const& CDaemon::f_GetDaemonParams() const
	{
		return mp_Params;
	}

	EActionResult CDaemon::f_ProcessCommand()
	{
		bint bHandled = false;
		EActionResult CommandResult = mp_Params.f_ProcessCommand(this, bHandled);

		if (bHandled)
			return CommandResult;

		switch (mp_Params.f_GetAction())
		{
			case EDaemonAction_Add:
			{
				bint bCheckForExisting = mp_Params.f_GetActionParam().f_GetAsType<bool>();
				CommandResult = f_Add(bCheckForExisting);
			}
			break;
			case EDaemonAction_Remove:
			{
				CommandResult = f_Remove();
			}
			break;
			case EDaemonAction_Start:
			{
				CommandResult = f_Start();
			}
			break;
			case EDaemonAction_Restart:
			{
				bint bWait = mp_Params.f_GetActionParam().f_GetAsType<bool>();
				CommandResult = f_Restart(bWait);
			}
			break;
			case EDaemonAction_Stop:
			{
				bint bWait = mp_Params.f_GetActionParam().f_GetAsType<bool>();
				CommandResult = f_Stop(bWait);
			}
			break;
			case EDaemonAction_Run:
			{
				NMib::fg_GetSys()->f_SetRunningAsDaemon(true);
				CommandResult = f_Run();
			}
			break;
			case EDaemonAction_RunAsProgram:
			{
				CommandResult = f_RunAsProgram(true);
			}
			break;
			case EDaemonAction_RunAsProgramNoDebug:
			{
				CommandResult = f_RunAsProgram(false);
			}
			break;
			case EDaemonAction_Exists:
			{
				bool bExists;
				if (f_Exists(bExists) == EActionResult_Failure)
					CommandResult = EActionResult_Failure;
				else
					CommandResult = bExists ? EActionResult_Success : EActionResult_Failure;
			}
			break;
		}

		return CommandResult;
	}
}
