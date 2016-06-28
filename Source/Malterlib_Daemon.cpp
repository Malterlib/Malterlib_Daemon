// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon.h"

namespace NMib
{
	namespace NService
	{

		CServiceParams::CServiceParams
			(
				NStr::CStr const& _ServiceName
				, NStr::CStr const& _DisplayName
				, NStr::CStr const& _ServiceDesc
				, void* _pNativeHandle
				, FImplementationFactory const& _ImplementationFactory
				, FProcessCommand const& _ProcessCommand
				, FErrorReporter const& _ErrorReporter
				, FInformationReporter const& _InformationReporter
				, FErrorReporterYesNo const& _ErrorReporterYesNo
			)
			: mp_ServiceName(_ServiceName)
			, mp_DisplayName(_DisplayName)
			, mp_ServiceDesc(_ServiceDesc)
			, mp_fImplementationFactory(_ImplementationFactory)
			, mp_fProcessCommand(_ProcessCommand)
			, mp_fReportError(_ErrorReporter)
			, mp_fReportInformation(_InformationReporter)
			, mp_fReportErrorYesNo(_ErrorReporterYesNo)
			, mp_pNativeHandle(_pNativeHandle)
			, mp_ServiceMode(EServiceMode_Global)
			, mp_bInteractive(false)
			, mp_bCustomServiceName(false)
			, mp_bDisableWriteService(false)
			, mp_bKeepRunning(false)
			, mp_bDaemonize(false)
		{
		}
		
		CServiceParams::~CServiceParams()
		{

		}
		
		NMib::NStr::CStr CServiceParams::fp_CleanServiceName(NMib::NStr::CStr const &_ServiceName)
		{
			NStr::CStr Result;

			aint iParse = 0;
			ch32 Current = _ServiceName.f_GetAt(iParse);

			while (Current)
			{
				if (NStr::fg_CharIsAlphabetical(Current) || NStr::fg_CharIsNumber(Current) || Current == '_' || Current == '-' || Current == '.')
					Result.f_AddChar(Current);

				Current = _ServiceName.f_GetAt(++iParse);
			}

			return Result;
		}

		void CServiceParams::fp_CopyElementsToCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine)
		{
			for (auto iArg = _CommandLine.f_GetIterator(); iArg; ++iArg)
			{
				if (mp_CommandLine.f_IsEmpty())
					mp_CommandLine += *iArg;
				else
					mp_CommandLine += NStr::CStr::CFormat(" {}") << *iArg;
			}
		}

		void CServiceParams::fs_ParseOptions(COptionHandlerMap &_OptionHandlers, CCommandLineVector &_CommandLine) 
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

		bool CServiceParams::fs_ParseOptionArgument(CCommandLineVector &_CommandLine, NStr::CStr &_Destination)
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

		void CServiceParams::f_ParseCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine)
		{
			bool bWriteServiceName = false;
			mp_CommandLine.f_Clear();
			
			mp_Action = EServiceAction_Custom;
			mp_ActionParam = false;
			mp_lRawArguments = _CommandLine;

			NContainer::TCVector<NMib::NStr::CStr> Cmd(_CommandLine);

			if (!Cmd.f_IsEmpty())
				Cmd.f_Pop();		// Executable name

			if (mp_ExecutablePath.f_IsEmpty())
				mp_ExecutablePath = NFile::CFile::fs_GetProgramPath();
						 
			NStr::CStr ServiceNamePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceName";
			try
			{
				if (NFile::CFile::fs_FileExists(ServiceNamePath))
				{
					NStr::CStr ServiceName = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(ServiceNamePath), true);
					if (!ServiceName.f_IsEmpty() && ServiceName != mp_ServiceName)
					{
						mp_ServiceName = ServiceName;
						mp_bCustomServiceName = true;
					}
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to read service name file {}. The error was: {}") << ServiceNamePath << _Exception.f_GetErrorStr());
			}
			
			NStr::CStr ServiceModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";
			try
			{
				if (NFile::CFile::fs_FileExists(ServiceModePath))
				{
					NStr::CStr ServiceMode = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(ServiceModePath), true);
					if (ServiceMode.f_CmpNoCase("AllUsers") == 0)
						mp_ServiceMode = EServiceMode_AllUsers;
					else if (ServiceMode.f_CmpNoCase("LocalUser") == 0)
						mp_ServiceMode = EServiceMode_LocalUser;
					else
						mp_ServiceMode = EServiceMode_Global;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to read service mode file {}. The error was: {}") << ServiceModePath << _Exception.f_GetErrorStr());
			}

			NContainer::TCVector<NMib::NStr::CStr> lArgsWithoutExecutable = Cmd;
			fp_CopyElementsToCommandLine(lArgsWithoutExecutable);

			if (!Cmd.f_IsEmpty())
			{

				COptionHandlerMap OptionHandlers;

				OptionHandlers["AddService"] = [&](CCommandLineVector &_Cmd)
					{
						NStr::CStr ServiceName;
						mp_Action = EServiceAction_Add;
						fs_ParseOptionArgument(Cmd, ServiceName);
						if (!ServiceName.f_IsEmpty() && ServiceName != mp_ServiceName)
						{
							mp_ServiceName = ServiceName;
							mp_bCustomServiceName = true;
						}

						bWriteServiceName = true;
					}
				;

				OptionHandlers["AddServiceIfNotAdded"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Add;
						mp_ActionParam = true;
						NStr::CStr ServiceName;
						fs_ParseOptionArgument(Cmd, ServiceName);
						if (!ServiceName.f_IsEmpty() && ServiceName != mp_ServiceName)
						{
							mp_ServiceName = ServiceName;
							mp_bCustomServiceName = true;
						}

						bWriteServiceName = true;
					}
				;
				OptionHandlers["RemoveService"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Remove;
						fs_ParseOptionArgument(Cmd, mp_ServiceName);
					}
				;
				OptionHandlers["StartService"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Start;
						fs_ParseOptionArgument(Cmd, mp_ServiceName);
					}
				;
				OptionHandlers["RestartService"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Restart;
						fs_ParseOptionArgument(Cmd, mp_ServiceName);
					}
				;
				OptionHandlers["StopService"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Stop;
						fs_ParseOptionArgument(Cmd, mp_ServiceName);
					}
				;
				OptionHandlers["RunAsProgram"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_RunAsProgram;
					}
				;
				OptionHandlers["Exists"] = [&](CCommandLineVector &_Cmd)
				{
					mp_Action = EServiceAction_Exists;
					fs_ParseOptionArgument(Cmd, mp_ServiceName);
				}
				;
				OptionHandlers["Service"] = [&](CCommandLineVector &_Cmd)
					{
						mp_Action = EServiceAction_Run;
						fs_ParseOptionArgument(Cmd, mp_ServiceName);
					}
				;
			   
				OptionHandlers["LocalUser"] = [&](CCommandLineVector &_Cmd)
					{
						mp_ServiceMode = EServiceMode_LocalUser;
					}
				;
				OptionHandlers["AllUsers"] = [&](CCommandLineVector &_Cmd)
					{
						mp_ServiceMode = EServiceMode_AllUsers;
					}
				;

				OptionHandlers["RunAsUser"] = [&](CCommandLineVector &_Cmd)
					{
						fs_ParseOptionArgument(_Cmd, mp_RunAsUser);
					}
				;

				OptionHandlers["RunAsGroup"] = [&](CCommandLineVector &_Cmd)
					{
						fs_ParseOptionArgument(_Cmd, mp_RunAsGroup);
					}
				;
				
				OptionHandlers["DisableWriteService"] = [&](CCommandLineVector &_Cmd)
					{
						mp_bDisableWriteService  = true;
					}
				;
				
				OptionHandlers["KeepRunning"] = [&](CCommandLineVector &_Cmd)
					{
						mp_bKeepRunning  = true;
					}
				;
				
				OptionHandlers["Daemonize"] = [&](CCommandLineVector &_Cmd)
					{
						mp_bDaemonize  = true;
					}
				;
				
				
				OptionHandlers["Custom"] = [&](CCommandLineVector &_Cmd)
					{
						if (mp_CustomAction.f_IsEmpty() && mp_Action == EServiceAction_Custom)
						{
							mp_Action = EServiceAction_Custom;
							fs_ParseOptionArgument(Cmd, mp_CustomAction);
						}
					}
				;

				fs_ParseOptions(OptionHandlers, Cmd);
			}

			mp_ServiceName = fp_CleanServiceName(mp_ServiceName);
			if (mp_bCustomServiceName)
				mp_DisplayName += NStr::CStr::CFormat(" ({})") << mp_ServiceName;

			if (bWriteServiceName && !mp_bDisableWriteService)
			{
				NStr::CStr Error;
				if (!f_WriteServiceNameFile(Error))
					f_ReportError(NStr::CStr::CFormat("Failed to write service name file: {}") << Error);
			}

			//fp_Trace();
		}
		
		bool CServiceParams::f_WriteServiceNameFile(NStr::CStr &_Error) const
		{	
			NStr::CStr ServiceNamePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceName";
			
			try
			{
				NFile::CFile::fs_WriteStringToFile(NStr::CStr(ServiceNamePath), mp_ServiceName, false);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				_Error = _Exception.f_GetErrorStr();
				return false;
			}
			
			return true;
		}
		
		bool CServiceParams::fp_Trace(NStr::CStr &_Error)
		{
			NRegistry::CRegistry_CStr TraceRegistry;
			TraceRegistry.f_SetValue("ServiceName", mp_ServiceName);
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

		bool CServiceParams::f_WriteServiceModeFile(NStr::CStr &_Error) const
		{
			NStr::CStr ModeString;
			switch (mp_ServiceMode)
			{
			case EServiceMode_AllUsers:
				ModeString = "AllUsers";
					break;
			case EServiceMode_LocalUser:
				ModeString = "LocalUser";
					break;
			default:
				ModeString = "Global";
				break;
			}

			NStr::CStr ServiceModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";

			try
			{
				NFile::CFile::fs_WriteStringToFile(NStr::CStr(ServiceModePath), ModeString, false);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				_Error = _Exception.f_GetErrorStr();
				return false;
			}

			return true;
		}

		bool CServiceParams::f_RemoveServiceModeFile(NStr::CStr &_Error) const
		{
			NStr::CStr ServiceModePath = NFile::CFile::fs_GetProgramDirectory() + "/ServiceMode";
			try
			{
				if (NFile::CFile::fs_FileExists(ServiceModePath))
					NFile::CFile::fs_DeleteFile(ServiceModePath);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				_Error = _Exception.f_GetErrorStr();
				return false;
			}

			return true;
		}

		NStr::CStr CServiceParams::f_GetCustomActionKey() const
		{
			return mp_CustomAction;
		}

		void CServiceParams::f_SetAction(EServiceAction _Action)
		{
			mp_Action = _Action;
		}

		EServiceAction CServiceParams::f_GetAction() const
		{
			return mp_Action;
		}

		void* CServiceParams::f_GetNativeHandle() const
		{
			return mp_pNativeHandle;
		}

		CServiceActionParam const &CServiceParams::f_GetActionParam() const
		{
			return mp_ActionParam;
		}
		
		void CServiceParams::f_SetActionParam(CServiceActionParam const &_Param)
		{
			mp_ActionParam = _Param;
		}

		NStr::CStr CServiceParams::f_GetServiceGroup() const
		{
			return mp_ServiceGroup;
		}

		bool CServiceParams::f_GetInteractive() const
		{
			return mp_bInteractive;
		}
		
		bool CServiceParams::f_GetDisableWriteService() const
		{
			return mp_bDisableWriteService;
		}
		
		void CServiceParams::f_SetDisableWriteService(bool _bDisable)
		{
			mp_bDisableWriteService = _bDisable;
		}
		
		bool CServiceParams::f_GetKeepRunning() const
		{
			return mp_bKeepRunning;
		}
		
		bool CServiceParams::f_GetDaemonize() const
		{
			return mp_bDaemonize;
		}

		EServiceMode CServiceParams::f_GetServiceMode() const
		{
			return mp_ServiceMode;
		}
		
		void CServiceParams::f_SetServiceMode(EServiceMode _Mode)
		{
			mp_ServiceMode = _Mode;
		}
		
		NStr::CStr CServiceParams::f_GetServiceName() const
		{
			return mp_ServiceName;
		}

		void CServiceParams::f_SetServiceName(NStr::CStr const &_ServiceName, bool _bCustom)
		{
			mp_ServiceName = _ServiceName;
			mp_bCustomServiceName = _bCustom;
		}

		NStr::CStr CServiceParams::f_GetServiceDisplayName() const
		{
			return mp_DisplayName;
		}

		NStr::CStr CServiceParams::f_GetServiceDescription() const
		{
			return mp_ServiceDesc;
		}

		NContainer::TCVector<NStr::CStr> const& CServiceParams::f_GetServiceDependencies() const
		{
			return mp_ServiceDependencies;
		}

		NStr::CStr CServiceParams::f_GetExecutablePath() const
		{
			return mp_ExecutablePath;
		}

		void CServiceParams::f_SetExecutablePath(NStr::CStr const &_Path)
		{
			mp_ExecutablePath = _Path;
		}

		NStr::CStr CServiceParams::f_GetCommandLine() const
		{
			return mp_CommandLine;
		}

		void CServiceParams::f_SetRunAsUser(NStr::CStr const &_User)
		{
			mp_RunAsUser = _User;
		}
		
		NStr::CStr CServiceParams::f_GetRunAsUser() const
		{
			return mp_RunAsUser;
		}
		
		void CServiceParams::f_SetRunAsGroup(NStr::CStr const &_Group)
		{
			mp_RunAsGroup = _Group;
		}

		NStr::CStr CServiceParams::f_GetRunAsGroup() const
		{
			return mp_RunAsGroup;
		}

		NStr::CStr CServiceParams::f_GetLocalizedStr(NStr::CStr const& _Key) const
		{
			NStr::CStr LocalizedString;
			mp_LocalizedStrings.f_Lookup(_Key, LocalizedString);
			return LocalizedString;
		}

		void CServiceParams::f_SetAddCommandLine(NStr::CStr const& _CommandLine)
		{
			mp_AddCommandLine = _CommandLine;
		}

		NStr::CStr CServiceParams::f_GetAddCommandLine() const
		{
			return mp_AddCommandLine;
		}

		bool CServiceParams::f_IsKeySet(NStr::CStr const &_Key) const
		{
			for (auto iArg = mp_lRawArguments.f_GetIterator(); iArg; ++iArg)
			{
				if (*iArg == _Key)
					return true;
			}
			return false;
		}
		
		void CServiceParams::f_SetKey(NStr::CStr const &_Key, bool _bKeySet)
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

		void CServiceParams::f_SetValueForKey(NStr::CStr const &_Key, NStr::CStr const &_Value)
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
		
		NStr::CStr CServiceParams::f_GetValueForKey(NStr::CStr const& _Key) const
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

		NPtr::TCUniquePointer<CServiceImp> CServiceParams::f_ImplementationFactory() const
		{
			if (mp_fImplementationFactory.f_IsEmpty())
				return nullptr;

			return mp_fImplementationFactory();
		}

		EActionResult CServiceParams::f_ProcessCommand(CService* _pService, bint& _bHandled)
		{
			if (mp_fProcessCommand.f_IsEmpty())
				return EActionResult_Failure;

			return mp_fProcessCommand(*this, _pService, _bHandled);
		}

		void CServiceParams::f_ReportError(NStr::CStr const& _Error) const
		{
			if (!mp_fReportError.f_IsEmpty())
				mp_fReportError(_Error);
		}

		void CServiceParams::f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information) const
		{
			if (!mp_fReportInformation.f_IsEmpty())
				mp_fReportInformation(_Heading, _Information);
		}

		EReportError CServiceParams::f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default) const
		{
			if (!mp_fReportErrorYesNo.f_IsEmpty())
				return mp_fReportErrorYesNo(_Error, _Default);

			return EReportError_No;
		}
		
		CServiceParams const& CService::f_GetServiceParams() const
		{
			return mp_Params;
		}
		
		EActionResult CService::f_ProcessCommand()
		{
			bint bHandled = false;
			EActionResult CommandResult = mp_Params.f_ProcessCommand(this, bHandled);
			
			if (bHandled)
				return CommandResult;
			
			switch (mp_Params.f_GetAction())
			{
				case EServiceAction_Add:
				{
					bint bCheckForExisting = mp_Params.f_GetActionParam().f_GetAsType<bool>();
					CommandResult = f_Add(bCheckForExisting);
				}
				break;
				case EServiceAction_Remove:
				{
					CommandResult = f_Remove();
				}
				break;
				case EServiceAction_Start:
				{
					CommandResult = f_Start();
				}
				break;
				case EServiceAction_Restart:
				{
					bint bWait = mp_Params.f_GetActionParam().f_GetAsType<bool>();
					CommandResult = f_Stop(bWait);
					if (CommandResult == EActionResult_Success)
						CommandResult = f_Start();
				}
				break;
				case EServiceAction_Stop:
				{
					bint bWait = mp_Params.f_GetActionParam().f_GetAsType<bool>();
					CommandResult = f_Stop(bWait);
				}
				break;
				case EServiceAction_Run:
				{
					NMib::fg_GetSys()->f_SetRunningAsService(true);
					CommandResult = f_Run();
				}
				break;
				case EServiceAction_RunAsProgram:
				{
					CommandResult = f_RunAsProgram();
				}
				break;
				case EServiceAction_Exists:
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
}
