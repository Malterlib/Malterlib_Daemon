// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Systemd.h"
#include <Mib/Process/ProcessLaunch>
#include <Mib/Process/Platform>

#define DPkgConfigExecutable "pkg-config"
#define DSystemctlExecutable "systemctl"
#define DSearchPaths NMib::NContainer::fg_CreateVector<NMib::NStr::CStr>("/usr/bin", "/bin")

#define DSystemdSystemDirectory1 "/usr/lib/systemd/system"
#define DSystemdSystemDirectory2 "/etc/systemd/system"

#define DSystemdUserDirectory1 "/usr/lib/systemd/user"
#define DSystemdUserDirectory2 "/etc/systemd/user"

namespace NMib
{
	namespace NProcess
	{
		namespace NPlatform
		{
			NStr::CStr fg_FindExecutable(NStr::CStr const &_Path, bint _bAllowLocate, NMib::NFile::EFileAttrib _Type, NContainer::TCVector<NStr::CStr> const &_ExtraPaths);
		}
	}
}

namespace NMib
{
	namespace NService
	{
		static NStr::CStr fs_GetSystemctlOptions(CServiceParams const &_Params)
		{
			NStr::CStr Options;
			EServiceMode Mode = _Params.f_GetServiceMode();

			if (Mode == EServiceMode_LocalUser)
				Options += "--user ";
			else if (Mode == EServiceMode_AllUsers)
				Options += "--global ";
			else
				Options += "--system ";

			return Options;
		}

		static NStr::CStr fs_GetUnitConfigFilename(CServiceParams const &_Params)
		{
			return _Params.f_GetServiceName() + ".service";
		}

		static NStr::CStr fs_GetExecutableCommand(CServiceParams const &_Params)
		{
			return NStr::CStr::CFormat("{} -Service") << _Params.f_GetExecutablePath();
		}

		NStr::CStr CSystemd::fp_GetUnitConfigDirectory(EServiceMode _Mode) const
		{
			if (_Mode == EServiceMode_LocalUser)
				return NStr::CStr::CFormat("{}/.config/systemd/user") << NSys::NFile::fg_GetUserHomeDirectory();
			else if (_Mode == EServiceMode_AllUsers)
				return mp_SystemdUserUnitDirectory;
			else
				return mp_SystemdSystemUnitDirectory;
		}
			
		bool CSystemd::fp_IsUnitConfigThisExecutable(CService *pOwner, CServiceParams const &_Params) const
		{
			NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
			NStr::CStr Contents;

			try
			{
				Contents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(LaunchFilePath), true);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				pOwner->f_ReportError(NStr::CStr::CFormat("Failed to read existing file: {}") << _Exception.f_GetErrorStr());
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

				(NStr::CStr::CParse("ExecStart={}") >> Value).f_Parse(Line, nParsed);
				if (nParsed == 1)
				{
					NStr::CStr ExpectedExec = fs_GetExecutableCommand(_Params);
					if (ExpectedExec.f_CmpNoCase(Value) == 0)
					{
						return true;
					}
					else
					{
						pOwner->f_ReportError(NStr::CStr::CFormat("Service name is already in use: {}") << LaunchFilePath);
						return false;
					}
				}
			}

			pOwner->f_ReportError(NStr::CStr::CFormat("Unable to find executable in {}") << LaunchFilePath);
			return false;
		}
	
		static NStr::CStr fs_CreateSystemdConfFromParams(CServiceParams const &_Params)
		{
			NStr::CStr Conf;
			
			Conf += NStr::CStr::CFormat("[Unit]\nDescription={}\n") << _Params.f_GetServiceDisplayName();
			Conf += NStr::CStr::CFormat("After=local-fs.target network.target\n") << 0;
			Conf += "\n";
			
			Conf += NStr::CStr::CFormat("[Service]\nExecStart={}\n") << fs_GetExecutableCommand(_Params);
			Conf += "KillMode=mixed\n";
			Conf += NStr::CStr::CFormat("TimeoutStopSec={}\n") << 24*60*60;
			Conf += NStr::CStr::CFormat("WorkingDirectory={}\n") << NFile::CFile::fs_GetPath(_Params.f_GetExecutablePath());
			if (!_Params.f_GetRunAsUser().f_IsEmpty())
				Conf += NStr::CStr::CFormat("User={}\n") << _Params.f_GetRunAsUser();
			if (!_Params.f_GetRunAsGroup().f_IsEmpty())
				Conf += NStr::CStr::CFormat("Group={}\n") << _Params.f_GetRunAsGroup();
			Conf += "Restart=always\n";
			Conf += "\n";
			
			Conf += NStr::CStr::CFormat("[Install]\nWantedBy=multi-user.target\n") << 0;
			Conf += "\n";
			
			return Conf;
		}
		
		EActionResult CSystemd::fp_SetUnitEnable(CServiceParams const &_Params, bint _bEnable) const
		{
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			bint bUnitEnabled = false;
			if (fp_IsUnitEnabled(_Params, bUnitEnabled) != EActionResult_Failure)
			{
				if ((!bUnitEnabled && !_bEnable) || (bUnitEnabled && _bEnable))
					return EActionResult_Success;
			}
					
			NStr::CStr Command = NStr::CStr::CFormat("{}{} {}") << fs_GetSystemctlOptions(_Params) << (_bEnable ? "enable" : "disable") << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode) || ExitCode)
				return EActionResult_Failure;
			
			return EActionResult_Success;
		}
		
		EActionResult CSystemd::fp_IsUnitEnabled(CServiceParams const &_Params, bint& _bIsEnabled) const
		{
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			_bIsEnabled = false;
			
			NStr::CStr Command = NStr::CStr::CFormat(" is-enabled {}") << fs_GetUnitConfigFilename(_Params);
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode))
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to check is-enabled {}: {}") << Command << Error);
				return EActionResult_Failure;
			}
			
			_bIsEnabled = (ExitCode == 0);
			
			return EActionResult_Success;
		}

		bint CSystemd::fs_IsSupported()
		{
			return NFile::CFile::fs_FileExists
				(
					NProcess::NPlatform::fg_FindExecutable(DSystemctlExecutable, true, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable, DSearchPaths)
					, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable
				)
			;
		}

		CSystemd::CSystemd(CService *_pOwner)
			: CServiceSystemInterfaceShared(_pOwner)
		{
			{
				NStr::CStr Executable = NProcess::NPlatform::fg_FindExecutable(DPkgConfigExecutable, true, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable, DSearchPaths);
				if (NFile::CFile::fs_FileExists(Executable, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable))
					mp_PkgConfigExecutable = Executable;
			}
			

			{
				NStr::CStr Executable = NProcess::NPlatform::fg_FindExecutable(DSystemctlExecutable, true, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable, DSearchPaths);
				if (NFile::CFile::fs_FileExists(Executable, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable))
					mp_SystemCtlExecutable = Executable;
				else
					DMibError("Cloud not find systemctl executable");
			}
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;

				if 
					(
						!mp_PkgConfigExecutable.f_IsEmpty() 
						&& NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_PkgConfigExecutable, "systemd --variable=systemdsystemunitdir", Result, Error, ExitCode) 
						&& !ExitCode 
						&& !Result.f_IsEmpty()
					)
				{
					mp_SystemdSystemUnitDirectory = Result.f_Replace("\n", "");
				}
				else if (NFile::CFile::fs_FileExists(NStr::CStr(DSystemdSystemDirectory1)))
					mp_SystemdSystemUnitDirectory = DSystemdSystemDirectory1;
				else if (NFile::CFile::fs_FileExists(NStr::CStr(DSystemdSystemDirectory2)))
					mp_SystemdSystemUnitDirectory = DSystemdSystemDirectory2;
			}
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;

				if 
					(
						!mp_PkgConfigExecutable.f_IsEmpty() 
						&& NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_PkgConfigExecutable, "systemd --variable=systemduserunitdir", Result, Error, ExitCode) 
						&& !ExitCode 
						&& !Result.f_IsEmpty()
					)
				{
					mp_SystemdUserUnitDirectory = Result.f_Replace("\n", "");
				}
				else if (NFile::CFile::fs_FileExists(NStr::CStr(DSystemdUserDirectory1)))
					mp_SystemdUserUnitDirectory = DSystemdUserDirectory1;
				else if (NFile::CFile::fs_FileExists(NStr::CStr(DSystemdUserDirectory2)))
					mp_SystemdUserUnitDirectory = DSystemdUserDirectory2;
			}
			
			DMibLog(Debug, "System unit directory: {}", mp_SystemdSystemUnitDirectory);
			DMibLog(Debug, "User unit directory: {}", mp_SystemdUserUnitDirectory);
		}
		
		CSystemd::~CSystemd()
		{
		}

		EActionResult CSystemd::f_Start(CServiceParams const &_Params)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			NStr::CStr UnitFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
			NStr::CStr UnitFilePath = NStr::CStr::CFormat("{}/{}") << UnitFileDirectory << fs_GetUnitConfigFilename(_Params);
			
			try
			{
				if (!NFile::CFile::fs_FileExists(UnitFilePath, NFile::EFileAttrib_File))
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Service file does not exist: {}") << UnitFilePath);
					return EActionResult_Failure;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing service file {}: {}") << UnitFilePath << _Exception.f_GetErrorStr());
				return EActionResult_Failure;
			}
			
			if (fp_SetUnitEnable(_Params, true) == EActionResult_Failure)
				return EActionResult_Failure;
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;
				
				NStr::CStr Command = NStr::CStr::CFormat("{}show {} --property=MainPID") << fs_GetSystemctlOptions(_Params) << _Params.f_GetServiceName();

				if (NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode))
				{
					NStr::CStr MainPID;
					(NStr::CStr::CParse("MainPID={}") >> MainPID).f_Parse(Result);
							
					if (MainPID.f_ToInt(0) != 0)
					{
						mp_pOwner->f_ReportInformation("Start service", NStr::CStr::CFormat("Service {} already running") << _Params.f_GetServiceName());
						return EActionResult_Success;
					}
				}
			}
							  
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;

			NStr::CStr Command = NStr::CStr::CFormat("{}start {}") << fs_GetSystemctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error starting service {}\nCommand {} returned with code {}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}
		   
			mp_pOwner->f_ReportInformation("Start service", NStr::CStr::CFormat("Successfully started service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}

		EActionResult CSystemd::f_Stop(CServiceParams const &_Params, bint _bWait)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			if (_Params.f_GetKeepRunning())
				return EActionResult_Success;
			
			bool bServiceExists;
			if (f_Exists(_Params, bServiceExists) == EActionResult_Failure)
				return EActionResult_Failure;
			

			if (!bServiceExists)
			{
				NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
				NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
				
				mp_pOwner->f_ReportInformation("Stop Service", NStr::CStr::CFormat("Service is not installed at '{}' so it has not been stopped") << LaunchFilePath);
				return EActionResult_Success;
			}
			
			{
				NStr::CStr Result;
				NStr::CStr Error;
				uint32 ExitCode = 0;
				
				NStr::CStr Command = NStr::CStr::CFormat("{}show {} --property=MainPID") << fs_GetSystemctlOptions(_Params) << _Params.f_GetServiceName();
				NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode);

				NStr::CStr MainPID;
				(NStr::CStr::CParse("MainPID={}") >> MainPID).f_Parse(Result);
					
				if (!ExitCode && MainPID.f_ToInt(0) == 0)
				{
					mp_pOwner->f_ReportInformation("Stop service", NStr::CStr::CFormat("Service {} was not running so it was not stopped") << _Params.f_GetServiceName());
					return EActionResult_Success;
				}
			}
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			NStr::CStr Command = NStr::CStr::CFormat("{}stop {}") << fs_GetSystemctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error stopping service {}\nCommand {} returned with code {}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}

				
			mp_pOwner->f_ReportInformation("Stop service", NStr::CStr::CFormat("Successfully stopped service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}

		EActionResult CSystemd::f_Restart(CServiceParams const &_Params, bint _bWait)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			if (_Params.f_GetKeepRunning())
				return EActionResult_Success;
			
			bool bServiceExists;
			if (f_Exists(_Params, bServiceExists) == EActionResult_Failure)
				return EActionResult_Failure;
			
			if (!bServiceExists)
			{
				NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
				NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
				
				mp_pOwner->f_ReportInformation("Restart Service", NStr::CStr::CFormat("Service is not installed at '{}' so it has not been restarted") << LaunchFilePath);
				return EActionResult_Success;
			}
			
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;
			
			NStr::CStr Command = NStr::CStr::CFormat("{}restart {}") << fs_GetSystemctlOptions(_Params) << _Params.f_GetServiceName();
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error restarting service {}\nCommand {} returned with code {}: {}") << _Params.f_GetServiceName() << Command << ExitCode << Error);
				return EActionResult_Failure;
			}

			mp_pOwner->f_ReportInformation("Restart service", NStr::CStr::CFormat("Successfully restarted service {}") << _Params.f_GetServiceName());
			return EActionResult_Success;
		}

		EActionResult CSystemd::f_Add(CServiceParams const &_Params, bint _bCheckForExisting)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
					
			NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
			
			if (LaunchFileDirectory.f_IsEmpty())
			{
				mp_pOwner->f_ReportError("Failed to locate service unit directory");
				return EActionResult_Failure;
			}
			
			if (_bCheckForExisting)
			{
				try
				{
					if (NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
					{
						if (fp_IsUnitConfigThisExecutable(mp_pOwner, _Params))
						{
							fp_SetUnitEnable(_Params, true);
							return EActionResult_Success;
						}
						else
							return EActionResult_Failure;
					}
				}
				catch (NFile::CExceptionFile const& _Exception)
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing launch file path: {}. The error was: {}") << LaunchFilePath << _Exception.f_GetErrorStr());
					return EActionResult_Failure;
				}
			}
					
			NStr::CStr Data = fs_CreateSystemdConfFromParams(_Params);

			try
			{
				NStr::CStr Error;
				if (!_Params.f_GetDisableWriteService() && !_Params.f_WriteServiceModeFile(Error))
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write service mode file: {}") << Error);
					
				if (!NFile::CFile::fs_FileExists(LaunchFileDirectory, NFile::EFileAttrib_Directory))
					NFile::CFile::fs_CreateDirectory(LaunchFileDirectory);
				
				NFile::CFile UnitFile(LaunchFilePath, NFile::EFileOpen_Write);
				UnitFile.f_Write(Data, Data.f_GetLen());
				UnitFile.f_Close();
			
				if (fp_SetUnitEnable(_Params, true) == EActionResult_Failure)
					return EActionResult_Failure;
				
				mp_pOwner->f_ReportInformation("Add Service", NStr::CStr::CFormat("Successfully installed service at {}") << LaunchFilePath);
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to install service at {}: {}") << LaunchFilePath << _Exception.f_GetErrorStr());
			}
					
			return EActionResult_Failure;
		}

		EActionResult CSystemd::f_Remove(CServiceParams const &_Params)
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
			
			bool bServiceExists;
			if (f_Exists(_Params, bServiceExists) == EActionResult_Failure)
				return EActionResult_Failure;
				
			if (!bServiceExists)
			{
				mp_pOwner->f_ReportInformation("Remove Service", NStr::CStr::CFormat("Service is not installed at '{}' so it has not been removed") << LaunchFilePath);
				return EActionResult_Success;
			}

			if (f_Stop(_Params, false) == EActionResult_Failure)
				return EActionResult_Failure;
			
			fp_SetUnitEnable(_Params, false);
			
			try
			{
				NFile::CFile::fs_DeleteFile(LaunchFilePath);
				
				NStr::CStr Error;
				if (!_Params.f_RemoveServiceModeFile(Error))
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove service mode file: {}") << Error);
				
				mp_pOwner->f_ReportInformation("Remove Service", NStr::CStr::CFormat("Successfully removed service from {}") << LaunchFilePath);
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove service at {}: {}\nPerhaps you need to use sudo?") << LaunchFilePath << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

		bool CSystemd::f_SupportsAutoRestart() const
		{
			return true;
		}
		
		EActionResult CSystemd::f_Exists(CServiceParams const &_Params, bool &_bExists) const
		{
			if (!fp_CheckParamsSupported(_Params))
				return EActionResult_Failure;
			
			_bExists = false;
			NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params.f_GetServiceMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);
			
			try
			{
				if (NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
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
