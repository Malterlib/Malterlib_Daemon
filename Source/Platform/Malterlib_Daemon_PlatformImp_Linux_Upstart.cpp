// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Upstart.h"
#include <Mib/Process/ProcessLaunch>

#define DInitctlExecutable "/sbin/initctl"

namespace NMib
{
	namespace NService
	{
		static NStr::CStr fs_GetConfFilename(CServiceParams const &_Params)
		{
			return _Params.f_GetServiceName() + ".conf";
		}

		static NStr::CStr fs_GetExecutableCommand(CServiceParams const &_Params)
		{
			NStr::CStr ExecCommand = NMib::NStr::fg_StrEscapeBashDoubleQuotes(_Params.f_GetExecutablePath());
			ExecCommand += " -Service ";
			ExecCommand += _Params.f_GetServiceName();
			
			if (!_Params.f_GetRunAsUser().f_IsEmpty())
				ExecCommand += NStr::CStr::CFormat(" -RunAsUser {}") << _Params.f_GetRunAsUser().f_EscapeStr();
			if (!_Params.f_GetRunAsGroup().f_IsEmpty())
				ExecCommand += NStr::CStr::CFormat(" -RunAsGroup {}") << _Params.f_GetRunAsGroup().f_EscapeStr();
			
			return ExecCommand;
		}

		bint CUpstart::fs_IsSupported()
		{
			return NFile::CFile::fs_FileExists(NStr::CStr("/etc/init")) && NFile::CFile::fs_FileExists(NStr::CStr(DInitctlExecutable));
		}
				
		CUpstart::CUpstart(CService *_pOwner)
			: CServiceSystemInterfaceShared(_pOwner)
			, mp_bSupportsUserFlag(false)
		{
			
			// Get Upstart version
			if (NFile::CFile::fs_FileExists(NStr::CStr(DInitctlExecutable)))
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;
				
				if (NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, "--version", Result, Error, ExitCode) && !ExitCode)
				{
					aint nParsed;

					aint NewLinePos = Result.f_Find("\n");
					if (NewLinePos != -1)
					{
						int VersionMajor;
						int VersionMinor;
						NStr::CStr VersionExtra;
						(NStr::CStr::CParse("initctl (upstart {nfn}.{nfn}{})") >> VersionMajor >> VersionMinor >> VersionExtra).f_Parse(Result.f_Left(NewLinePos), nParsed);
						if (nParsed == 3)
						{
							// Version 1.7 and greater
							if (VersionMajor > 1 || (VersionMajor == 1 && VersionMinor >= 7))
								mp_bSupportsUserFlag = true;
						}
					}
				}
				else
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to execute {}: {}") << NStr::CStr(DInitctlExecutable) << Error);
				}
			}
		}
		
		CUpstart::~CUpstart()
		{
		}

		NStr::CStr CUpstart::fp_GetInitctlOptions(CServiceParams const &_Params) const
		{
			NStr::CStr Options;
			EServiceMode Mode = _Params.f_GetServiceMode();

			if (mp_bSupportsUserFlag && (Mode == EServiceMode_LocalUser || Mode == EServiceMode_AllUsers))
				Options += "--user ";

			return Options;
		}
		
		NStr::CStr CUpstart::fp_GetConfDirectory(CServiceParams const &_Params) const
		{
			EServiceMode Mode = _Params.f_GetServiceMode();

			if (mp_bSupportsUserFlag)
			{
				if (Mode == EServiceMode_LocalUser)
					return NStr::CStr::CFormat("{}/.config/upstart") << NSys::NFile::fg_GetUserHomeDirectory();
				else if (Mode == EServiceMode_AllUsers)
					return NStr::CStr("/usr/share/upstart/sessions");
			}
			else if (Mode != EServiceMode_Global)
			{
				return NStr::CStr::CFormat("{}/.init/") << NSys::NFile::fg_GetUserHomeDirectory();
			}

			return NStr::CStr("/etc/init");
		}

		bool CUpstart::fp_IsConfThisExecutable(CServiceParams const &_Params) const
		{
			NStr::CStr LaunchFileDirectory = fp_GetConfDirectory(_Params);
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetConfFilename(_Params);
			NStr::CStr Contents;

			try
			{
				Contents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(LaunchFilePath), true);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to read existing file: {}") << _Exception.f_GetErrorStr());
				return false;
			}

			while (!Contents.f_IsEmpty())
			{
				aint NewLinePos = Contents.f_Find("\n");
				if (NewLinePos == -1)
					break;

				NStr::CStr Line = Contents.f_Left(NewLinePos);
				Contents = Contents.f_Delete(0, NewLinePos + 1);

				NStr::CStr Value;
				aint nParsed;

				(NStr::CStr::CParse("exec {}") >> Value).f_Parse(Line, nParsed);
				if (nParsed == 1)
				{
					NStr::CStr ExpectedExec = fs_GetExecutableCommand(_Params);
					if (ExpectedExec.f_CmpNoCase(Value) == 0)
					{
						return true;
					}
					else
					{
						mp_pOwner->f_ReportError(NStr::CStr::CFormat("Service name is already in use: {}") << LaunchFilePath);
						return false;
					}
				}
			}

			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to find executable in {}") << LaunchFilePath);
			return false;
		}

		EActionResult CUpstart::f_Start(CServiceParams const &_Params)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			NStr::CStr ConfFileDirectory = fp_GetConfDirectory(_Params);
			NStr::CStr ConfFilePath = NStr::CStr::CFormat("{}/{}") << ConfFileDirectory << fs_GetConfFilename(_Params);

			try
			{
				if (!NFile::CFile::fs_FileExists(ConfFilePath, NFile::EFileAttrib_File))
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Service file does not exist: {}") << ConfFilePath);
					return EActionResult_Failure;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing service file {}: {}") << ConfFilePath << _Exception.f_GetErrorStr());
				return EActionResult_Failure;
			}
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;
				
				// Check for already running service
				NStr::CStr Command = NStr::CStr::CFormat("status {}{}") << fp_GetInitctlOptions(_Params) << _Params.f_GetServiceName();
				if (NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, Command, Result, Error, ExitCode)
					&& !ExitCode
					&& Result.f_FindNoCase("start/running") != -1)
				{
					mp_pOwner->f_ReportInformation("Start service", NStr::CStr::CFormat("Service {} already running") << _Params.f_GetServiceName());
					return EActionResult_Success;
				}
			}
			
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
						  
			NStr::CStr Command = NStr::CStr::CFormat("start {}{}") << fp_GetInitctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty()
				|| Result.f_FindNoCase("stop/waiting") >= 0)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error starting service {}\nCommand {} returned with code {}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}
			
			mp_pOwner->f_ReportInformation("Start service", NStr::CStr::CFormat("Successfully started service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}

		EActionResult CUpstart::f_Stop(CServiceParams const &_Params, bint _bWait)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			if (_Params.f_GetKeepRunning())
				return EActionResult_Success;
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;
			
				// Check for running service
				NStr::CStr Command = NStr::CStr::CFormat("status {}{}") << fp_GetInitctlOptions(_Params) << _Params.f_GetServiceName();
				if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, Command, Result, Error, ExitCode))
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error checking status on service {}\nCommand {} returned with code {}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
					return EActionResult_Failure;
				}

				if (Result.f_FindNoCase("start/running") == -1)
				{
					mp_pOwner->f_ReportInformation("Stop service", NStr::CStr::CFormat("Service {} was not running") << _Params.f_GetServiceName());
					return EActionResult_Success;
				}
			}
			
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			NStr::CStr Command = NStr::CStr::CFormat("stop {}{}") << fp_GetInitctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error stopping service {}\nCommand {} returned with code {0}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}

			mp_pOwner->f_ReportInformation("Stop service", NStr::CStr::CFormat("Successfully stopped service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}
		
		EActionResult CUpstart::f_Restart(CServiceParams const &_Params, bint _bWait)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			if (_Params.f_GetKeepRunning())
				return EActionResult_Success;
			
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			NStr::CStr Command = NStr::CStr::CFormat("restart {}{}") << fp_GetInitctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DInitctlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error restarting service {}\nCommand {} returned with code {0}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}

			mp_pOwner->f_ReportInformation("Restart service", NStr::CStr::CFormat("Successfully restarted service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}

		static NStr::CStr fs_CreateUpstartConfFromParams(CServiceParams const &_Params)
		{
			NStr::CStr Conf;

			Conf += NStr::CStr::CFormat("description \"{}\"\n") << _Params.f_GetServiceDescription();
			Conf += NStr::CStr::CFormat("start on runlevel [2345]\n") << 0;
			
			Conf += NStr::CStr::CFormat("stop on runlevel [016]\n") << 0;
			Conf += "\n";
			
			Conf += NStr::CStr::CFormat("chdir {}\n") << NMib::NStr::fg_StrEscapeBashDoubleQuotes(NFile::CFile::fs_GetPath(_Params.f_GetExecutablePath()));
			Conf += NStr::CStr::CFormat("exec {}\n") << fs_GetExecutableCommand(_Params);
			
			Conf += NStr::CStr::CFormat("kill timeout {}\n") << 24*60*60;

			Conf += NStr::CStr::CFormat("respawn\n");

			return Conf;
		}

		EActionResult CUpstart::f_Add(CServiceParams const &_Params, bint _bCheckForExisting)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			NStr::CStr ConfFileDirectory = fp_GetConfDirectory(_Params);
			NStr::CStr ConfFilePath = NStr::CStr::CFormat("{}/{}") << ConfFileDirectory << fs_GetConfFilename(_Params);
			
			if (_bCheckForExisting)
			{
				if (NFile::CFile::fs_FileExists(ConfFilePath, NFile::EFileAttrib_File))
				{
					if (fp_IsConfThisExecutable(_Params))
						return EActionResult_Success;
					else
						return EActionResult_Failure;
				}
			}
			
			NStr::CStr Data = fs_CreateUpstartConfFromParams(_Params);

			try
			{
				NStr::CStr Error;
				if (!_Params.f_GetDisableWriteService() && !_Params.f_WriteServiceModeFile(Error))
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write service mode file: {}") << Error);
					
				if (!NFile::CFile::fs_FileExists(ConfFileDirectory, NFile::EFileAttrib_Directory))
					NFile::CFile::fs_CreateDirectory(ConfFileDirectory);
				
				NFile::CFile ConfFile(ConfFilePath, NFile::EFileOpen_Write);
				ConfFile.f_Write(Data, Data.f_GetLen());
				ConfFile.f_Close();
				
				mp_pOwner->f_ReportInformation("Add Service", NStr::CStr::CFormat("Successfully installed service at {}") << ConfFilePath);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to install service at {}: {}") << ConfFilePath << _Exception.f_GetErrorStr());
				return EActionResult_Failure;
			}
					
			return EActionResult_Success;
		}

		EActionResult CUpstart::f_Remove(CServiceParams const &_Params)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			if (f_Stop(_Params, false) == EActionResult_Failure)
				return EActionResult_Failure;
			
			NStr::CStr ConfFileDirectory = fp_GetConfDirectory(_Params);
			NStr::CStr ConfFilePath = NStr::CStr::CFormat("{}/{}") << ConfFileDirectory << fs_GetConfFilename(_Params);
			
			try
			{
				if (!NFile::CFile::fs_FileExists(ConfFilePath, NFile::EFileAttrib_File))
				{
					mp_pOwner->f_ReportInformation("Remove Service", NStr::CStr::CFormat("Service is not installed at '{}' so it has not been removed") << ConfFilePath);
					return EActionResult_Success;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing file: {}") << _Exception.f_GetErrorStr());
			}
			
			try
			{
				NFile::CFile::fs_DeleteFile(ConfFilePath);
				NStr::CStr Error;
				if (!_Params.f_RemoveServiceModeFile(Error))
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove service mode file: {}") << Error);
				
				mp_pOwner->f_ReportInformation("Remove Service", NStr::CStr::CFormat("Successfully removed service from {}") << ConfFilePath);
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove service at {}: {}\nPerhaps you need to use sudo?") << ConfFilePath << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

		bool CUpstart::f_SupportsAutoRestart() const
		{
			return true;
		}
		
		EActionResult CUpstart::f_Exists(CServiceParams const &_Params, bool &_bExists) const
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			_bExists = false;
			NStr::CStr ConfFileDirectory = fp_GetConfDirectory(_Params);
			NStr::CStr ConfFilePath = NStr::CStr::CFormat("{}/{}") << ConfFileDirectory << fs_GetConfFilename(_Params);
			
			try
			{
				if (NFile::CFile::fs_FileExists(ConfFilePath, NFile::EFileAttrib_File))
					_bExists = true;
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing file: {}") << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

	} // namespace NService

} // namespace NMib
