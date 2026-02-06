// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Systemd.h"
#include <Mib/Process/ProcessLaunch>
#include <Mib/Process/Platform>

NMib::NStr::CStr gc_SystemctlExecutable = NMib::NStr::gc_Str<"systemctl">;

NMib::NStr::CStr gc_SearchPaths[] =
	{
		NMib::NStr::gc_Str<"/usr/bin">
		, NMib::NStr::gc_Str<"/bin">
	}
;

NMib::NStr::CStr gc_SystemDSystemDirectories[] =
	{
		NMib::NStr::gc_Str<"/usr/lib/systemd/system">
		, NMib::NStr::gc_Str<"/etc/systemd/system">
	}
;

NMib::NStr::CStr gc_SystemDUserDirectories[] =
	{
		NMib::NStr::gc_Str<"/usr/lib/systemd/user">
		, NMib::NStr::gc_Str<"/etc/systemd/user">
	}
;

namespace NMib::NProcess::NPlatform
{
	NStr::CStr fg_FindExecutable(NStr::CStr const &_Path, bool _bAllowLocate, NMib::NFile::EFileAttrib _Type, NContainer::TCVector<NStr::CStr> const &_ExtraPaths, NStr::CStr const &_LocalPaths = {});
}

namespace NMib::NDaemon
{
	// _bEnableDisable: true for enable/disable operations where --global is valid
	//                  false for start/stop/restart/show where --global is NOT valid
	static NStr::CStr fs_GetSystemctlOptions(CDaemonParams const &_Params, bool _bEnableDisable = false)
	{
		NStr::CStr Options;
		EDaemonMode Mode = _Params.f_GetDaemonMode();

		if (Mode == EDaemonMode_LocalUser)
			Options += "--user ";
		else if (Mode == EDaemonMode_AllUsers)
		{
			// --global is only valid for enable/disable operations
			// For start/stop/restart, user services must use --user
			if (_bEnableDisable)
				Options += "--global ";
			else
				Options += "--user ";
		}
		else
			Options += "--system ";

		return Options;
	}

	static NStr::CStr fs_GetUnitConfigFilename(CDaemonParams const &_Params)
	{
		return _Params.f_GetDaemonName() + ".service";
	}

	static NStr::CStr fs_EscapePath(NStr::CStr const &_Path)
	{
		using namespace NStr;
		CStr Return;
		for (uch8 const *pParse = (uch8 const *)_Path.f_GetStr(); *pParse; ++pParse)
		{
			if (NStr::fg_CharIsAnsiAlphabetical(*pParse) || NStr::fg_CharIsNumber(*pParse) || *pParse == '/' || *pParse == '_')
				Return.f_AddChar(*pParse);
			else
				Return += "\\x{nfh,sf0,sj2}"_f << *pParse;
		}
		return Return;
	}

	static NStr::CStr fs_GetExecutableCommand(CDaemonParams const &_Params)
	{
		NStr::CStr ExecutablePath = _Params.f_GetExecutablePath();
		NStr::CStr EscapedExecutablePath = fs_EscapePath(ExecutablePath);

		if (EscapedExecutablePath != ExecutablePath)
			return NStr::CStr::CFormat("/usr/bin/env {} -Service") << EscapedExecutablePath;

		return NStr::CStr::CFormat("{} -Service") << ExecutablePath;
	}

	NStr::CStr CSystemd::fp_GetUnitConfigDirectory(CDaemonParams const &_Params) const
	{
		auto Mode = _Params.f_GetDaemonMode();

		if (Mode == EDaemonMode_LocalUser)
			return NStr::CStr::CFormat("{}/.config/systemd/user") << NSys::NFile::fg_GetUserHomeDirectory();

		auto UnitFileName = fs_GetUnitConfigFilename(_Params);

		if (Mode == EDaemonMode_AllUsers)
		{
			for (auto &Directory : mp_SystemdUserUnitDirectories)
			{
				if (NFile::CFile::fs_FileExists(Directory / UnitFileName))
					return Directory;
			}

			return mp_SystemdUserUnitDirectories[0];
		}
		else
		{
			for (auto &Directory : mp_SystemdSystemUnitDirectories)
			{
				if (NFile::CFile::fs_FileExists(Directory / UnitFileName))
					return Directory;
			}

			return mp_SystemdSystemUnitDirectories[0];
		}
	}

	bool CSystemd::fp_IsUnitConfigThisExecutable(CDaemon *pOwner, CDaemonParams const &_Params) const
	{
		NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
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
					pOwner->f_ReportError(NStr::CStr::CFormat("Daemon name is already in use: {}") << LaunchFilePath);
					return false;
				}
			}
		}

		pOwner->f_ReportError(NStr::CStr::CFormat("Unable to find executable in {}") << LaunchFilePath);
		return false;
	}

	static NStr::CStr fs_CreateSystemdConfFromParams(CDaemonParams const &_Params)
	{
		NStr::CStr Conf;

		Conf += NStr::CStr::CFormat("[Unit]\nDescription={}\n") << _Params.f_GetDaemonDisplayName();
		if (_Params.f_GetRequiresGraphicalSessionInUserMode() && _Params.f_GetDaemonMode() != EDaemonMode_Global)
		{
			Conf += "After=graphical-session.target\n";
			Conf += "PartOf=graphical-session.target\n";
		}
		else
			Conf += NStr::CStr::CFormat("After=local-fs.target network.target\n") << 0;
		Conf += "\n";

		Conf += NStr::CStr::CFormat("[Service]\nExecStart={}\n") << fs_GetExecutableCommand(_Params);
		Conf += "KillMode=mixed\n";
		Conf += NStr::CStr::CFormat("TimeoutStopSec={}\n") <<  _Params.f_GetMaxShutdownTime().f_ToInt();
		Conf += NStr::CStr::CFormat("WorkingDirectory={}\n") << NFile::CFile::fs_GetPath(_Params.f_GetExecutablePath());
		if (!_Params.f_GetRunAsUser().f_IsEmpty())
			Conf += NStr::CStr::CFormat("User={}\n") << _Params.f_GetRunAsUser();
		if (!_Params.f_GetRunAsGroup().f_IsEmpty())
			Conf += NStr::CStr::CFormat("Group={}\n") << _Params.f_GetRunAsGroup();
		Conf += "Restart=always\n";
		Conf += "\n";

		if (_Params.f_GetDaemonMode() == EDaemonMode_Global)
			Conf += NStr::CStr::CFormat("[Install]\nWantedBy=multi-user.target\n") << 0;
		else if (_Params.f_GetRequiresGraphicalSessionInUserMode())
			Conf += "[Install]\nWantedBy=graphical-session.target\n";
		else
			Conf += NStr::CStr::CFormat("[Install]\nWantedBy=default.target\n") << 0;
		Conf += "\n";

		return Conf;
	}

	EActionResult CSystemd::fp_SetUnitEnable(CDaemonParams const &_Params, bool _bEnable) const
	{
		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		bool bUnitEnabled = false;
		if (fp_IsUnitEnabled(_Params, bUnitEnabled) != EActionResult_Failure)
		{
			if ((!bUnitEnabled && !_bEnable) || (bUnitEnabled && _bEnable))
				return EActionResult_Success;
		}

		NStr::CStr Command = NStr::CStr::CFormat("{}{} {}") << fs_GetSystemctlOptions(_Params, true) << (_bEnable ? "enable" : "disable") << _Params.f_GetDaemonName();
		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode) || ExitCode)
			return EActionResult_Failure;

		return EActionResult_Success;
	}

	EActionResult CSystemd::fp_IsUnitEnabled(CDaemonParams const &_Params, bool& _bIsEnabled) const
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

	NStr::CStr CSystemd::fsp_FindExecutable(NStr::CStr const &_Executable)
	{
		return NProcess::NPlatform::fg_FindExecutable
			(
				_Executable
				, true
				, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable
				, {gc_SearchPaths, fg_ArraySize(gc_SearchPaths)}
			)
		;
	}

	bool CSystemd::fs_IsSupported()
	{
		return NFile::CFile::fs_FileExists
			(
				fsp_FindExecutable(gc_SystemctlExecutable)
				, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable
			)
		;
	}

	CSystemd::CSystemd(CDaemon *_pOwner)
		: CDaemonSystemInterfaceShared(_pOwner, ESupportedFeature_LocalUser | ESupportedFeature_AllUsers)
	{
		using namespace NStr;

		{

			CStr Executable = fsp_FindExecutable(gc_Str<"pkg-config">);
			if (NFile::CFile::fs_FileExists(Executable, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable))
				mp_PkgConfigExecutable = Executable;
		}
		{
			CStr Executable = fsp_FindExecutable(gc_SystemctlExecutable);
			if (NFile::CFile::fs_FileExists(Executable, NMib::NFile::EFileAttrib_File | NMib::NFile::EFileAttrib_Executable))
				mp_SystemCtlExecutable = Executable;
			else
				DMibError("Cloud not find systemctl executable");
		}

		{
			CStr Result;
			CStr Error;
			uint32 ExitCode = 0;

			if
				(
					!mp_PkgConfigExecutable.f_IsEmpty()
					&& NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_PkgConfigExecutable, "systemd --variable=systemdsystemunitdir", Result, Error, ExitCode)
					&& !ExitCode
					&& !Result.f_IsEmpty()
				)
			{
				mp_SystemdSystemUnitDirectories.f_Insert(Result.f_Replace("\n", ""));
			}

			for (auto &Directory : gc_SystemDSystemDirectories)
			{
				if (mp_SystemdSystemUnitDirectories.f_Contains(Directory) < 0 && NFile::CFile::fs_FileExists(Directory))
					mp_SystemdSystemUnitDirectories.f_Insert(Directory);
			}
		}

		{
			CStr Result;
			CStr Error;
			uint32 ExitCode = 0;

			if
				(
					!mp_PkgConfigExecutable.f_IsEmpty()
					&& NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_PkgConfigExecutable, "systemd --variable=systemduserunitdir", Result, Error, ExitCode)
					&& !ExitCode
					&& !Result.f_IsEmpty()
				)
			{
				mp_SystemdUserUnitDirectories.f_Insert(Result.f_Replace("\n", ""));
			}

			for (auto &Directory : gc_SystemDUserDirectories)
			{
				if (mp_SystemdUserUnitDirectories.f_Contains(Directory) < 0 && NFile::CFile::fs_FileExists(Directory))
					mp_SystemdUserUnitDirectories.f_Insert(Directory);
			}
		}

		if (mp_SystemdSystemUnitDirectories.f_IsEmpty())
			DMibError("Could not find systemd unit directory");

		if (mp_SystemdUserUnitDirectories.f_IsEmpty())
			DMibError("Could not find systemd unit directory for users");
	}

	CSystemd::~CSystemd()
	{
	}

	EActionResult CSystemd::f_Start(CDaemonParams const &_Params)
	{
		using namespace NStr;

		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		CStr UnitFileDirectory = fp_GetUnitConfigDirectory(_Params);
		CStr UnitFilePath = "{}/{}"_f << UnitFileDirectory << fs_GetUnitConfigFilename(_Params);

		try
		{
			if (!NFile::CFile::fs_FileExists(UnitFilePath, NFile::EFileAttrib_File))
			{
				mp_pOwner->f_ReportError("Daemon file does not exist: {}"_f << UnitFilePath);
				return EActionResult_Failure;
			}
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError("Failed to check for existing daemon file {}: {}"_f << UnitFilePath << _Exception.f_GetErrorStr());
			return EActionResult_Failure;
		}

		if (fp_SetUnitEnable(_Params, true) == EActionResult_Failure)
			return EActionResult_Failure;

		{
			CStr Result;
			CStr Error;
			uint32 ExitCode = 0;

			CStr Command = "{}show {} --property=MainPID"_f << fs_GetSystemctlOptions(_Params) << _Params.f_GetDaemonName();

			if (NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode))
			{
				CStr MainPID;
				(CStr::CParse("MainPID={}") >> MainPID).f_Parse(Result);

				if (MainPID.f_ToInt(0) != 0)
				{
					mp_pOwner->f_ReportInformation("Start daemon", "Daemon {} already running"_f << _Params.f_GetDaemonName());
					return EActionResult_Success;
				}
			}
		}

		CStr Result;
		CStr Error;
		uint32 ExitCode = 0;

		CStr Command = "{}start {}"_f << fs_GetSystemctlOptions(_Params) << _Params.f_GetDaemonName();
		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode) || ExitCode)
		{
			mp_pOwner->f_ReportError("Error starting daemon {}\nCommand {} returned with code {}: {}"_f << _Params.f_GetDaemonName() << Command << ExitCode << Error);
			return EActionResult_Failure;
		}

		if (!Error.f_IsEmpty())
			mp_pOwner->f_ReportInformation("Start daemon", "Starting daemon {} reported: {}"_f << _Params.f_GetDaemonName() << Error);

		mp_pOwner->f_ReportInformation("Start daemon", "Successfully started daemon {}"_f << _Params.f_GetDaemonName());
		return EActionResult_Success;
	}

	EActionResult CSystemd::f_Stop(CDaemonParams const &_Params, bool _bWait)
	{
		using namespace NStr;

		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		bool bDaemonExists;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
			CStr LaunchFilePath = "{}/{}"_f << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);

			mp_pOwner->f_ReportInformation("Stop Daemon", "Daemon is not installed at '{}' so it has not been stopped"_f << LaunchFilePath);
			return EActionResult_Success;
		}

		{
			CStr Result;
			CStr Error;
			uint32 ExitCode = 0;

			CStr Command = "{}show {} --property=MainPID"_f << fs_GetSystemctlOptions(_Params) << _Params.f_GetDaemonName();
			NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode);

			CStr MainPID;
			(CStr::CParse("MainPID={}") >> MainPID).f_Parse(Result);

			if (!ExitCode && MainPID.f_ToInt(0) == 0)
			{
				mp_pOwner->f_ReportInformation("Stop daemon", "Daemon {} was not running so it was not stopped"_f << _Params.f_GetDaemonName());
				return EActionResult_Success;
			}
		}
		CStr Result;
		CStr Error;
		uint32 ExitCode = 0;

		CStr Command = "{}stop {}"_f << fs_GetSystemctlOptions(_Params) << _Params.f_GetDaemonName();
		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode) || ExitCode)
		{
			mp_pOwner->f_ReportError("Error stopping daemon {}\nCommand {} returned with code {}: {}"_f << _Params.f_GetDaemonName() << Command << ExitCode << Error);
			return EActionResult_Failure;
		}

		if (!Error.f_IsEmpty())
			mp_pOwner->f_ReportInformation("Stop daemon", "Stopping daemon {} reported: {}"_f << _Params.f_GetDaemonName() << Error);

		mp_pOwner->f_ReportInformation("Stop daemon", "Successfully stopped daemon {}"_f << _Params.f_GetDaemonName());
		return EActionResult_Success;
	}

	EActionResult CSystemd::f_Restart(CDaemonParams const &_Params, bool _bWait)
	{
		using namespace NStr;

		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		bool bDaemonExists;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
			CStr LaunchFilePath = "{}/{}"_f << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);

			mp_pOwner->f_ReportInformation("Restart Daemon", "Daemon is not installed at '{}' so it has not been restarted"_f << LaunchFilePath);
			return EActionResult_Success;
		}

		CStr Result;
		CStr Error;
		uint32 ExitCode = 0;

		CStr Command = "{}restart {}"_f << fs_GetSystemctlOptions(_Params) << _Params.f_GetDaemonName();
		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(mp_SystemCtlExecutable, Command, Result, Error, ExitCode) || ExitCode)
		{
			mp_pOwner->f_ReportError("Error restarting daemon {}\nCommand {} returned with code {}: {}"_f << _Params.f_GetDaemonName() << Command << ExitCode << Error);
			return EActionResult_Failure;
		}

		if (!Error.f_IsEmpty())
			mp_pOwner->f_ReportInformation("Restart daemon", "Restarting daemon {} reported: {}"_f << _Params.f_GetDaemonName() << Error);

		mp_pOwner->f_ReportInformation("Restart daemon", "Successfully restarted daemon {}"_f << _Params.f_GetDaemonName());
		return EActionResult_Success;
	}

	EActionResult CSystemd::f_Add(CDaemonParams const &_Params, bool _bCheckForExisting)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
		NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);

		if (LaunchFileDirectory.f_IsEmpty())
		{
			mp_pOwner->f_ReportError("Failed to locate daemon unit directory");
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

		{
			bool bAlreadyExists = false;
			EActionResult Result = f_Exists(_Params, bAlreadyExists);
			if (Result != EActionResult_Success)
				return Result;

			if (bAlreadyExists)
			{
				mp_pOwner->f_ReportError("Daemon already exists, remove it manually with --daemon-remove before trying to add again.");
				return EActionResult_Failure;
			}
		}

		NStr::CStr Data = fs_CreateSystemdConfFromParams(_Params);

		try
		{
			NStr::CStr Error;
			if (!_Params.f_GetDisableWriteDaemon() && !_Params.f_WriteDaemonModeFile(Error))
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

			if (!NFile::CFile::fs_FileExists(LaunchFileDirectory, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_CreateDirectory(LaunchFileDirectory);

			NFile::CFile UnitFile(LaunchFilePath, NFile::EFileOpen_Write);
			UnitFile.f_Write(Data, Data.f_GetLen());
			UnitFile.f_Close();

			if (fp_SetUnitEnable(_Params, true) == EActionResult_Failure)
				return EActionResult_Failure;

			mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully installed daemon at {}") << LaunchFilePath);
			return EActionResult_Success;
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to install daemon at {}: {}") << LaunchFilePath << _Exception.f_GetErrorStr());
		}

		return EActionResult_Failure;
	}

	EActionResult CSystemd::f_Remove(CDaemonParams const &_Params)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
		NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetUnitConfigFilename(_Params);

		bool bDaemonExists;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it has not been removed") << LaunchFilePath);
			return EActionResult_Success;
		}

		// For AllUsers user services, skip stop when running as root - no user session D-Bus available
		if (_Params.f_GetDaemonMode() != EDaemonMode_AllUsers || NProcess::NPlatform::fg_Process_GetElevation() < NProcess::EProcessElevation_IsElevated)
		{
			if (f_Stop(_Params, false) == EActionResult_Failure)
				return EActionResult_Failure;
		}

		fp_SetUnitEnable(_Params, false);

		// Manually remove any remaining .wants symlinks, as systemctl disable
		// has known bugs where it silently fails to remove symlinks in certain cases
		// (see https://github.com/systemd/systemd/issues/10578)
		fp_RemoveWantsSymlinks(_Params);

		try
		{
			NFile::CFile::fs_DeleteFile(LaunchFilePath);

			NStr::CStr Error;
			if (!_Params.f_RemoveDaemonModeFile(Error))
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon mode file: {}") << Error);

			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed daemon from {}") << LaunchFilePath);
			return EActionResult_Success;
		}
		catch (NFile::CExceptionFile &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon at {}: {}\nPerhaps you need to use sudo?") << LaunchFilePath << _Exception.f_GetErrorStr());
		}

		return EActionResult_Failure;
	}

	void CSystemd::fp_RemoveWantsSymlinks(CDaemonParams const &_Params) const
	{
		NStr::CStr UnitFileName = fs_GetUnitConfigFilename(_Params);
		auto Mode = _Params.f_GetDaemonMode();

		NContainer::TCVector<NStr::CStr> Directories;

		if (Mode == EDaemonMode_Global)
			Directories = mp_SystemdSystemUnitDirectories;
		else
		{
			if (Mode == EDaemonMode_LocalUser)
				Directories.f_Insert(NStr::CStr::CFormat("{}/.config/systemd/user") << NSys::NFile::fg_GetUserHomeDirectory());
			else
				Directories = mp_SystemdUserUnitDirectories;
		}

		for (auto &BaseDir : Directories)
		{
			try
			{
				auto SubDirs = NFile::CFile::fs_FindFiles(BaseDir + "/*.wants", NFile::EFileAttrib_Directory, false);
				for (auto &WantsDir : SubDirs)
				{
					NStr::CStr SymlinkPath = WantsDir + "/" + UnitFileName;
					try
					{
						if (NFile::CFile::fs_FileExists(SymlinkPath))
							NFile::CFile::fs_DeleteFile(SymlinkPath);
					}
					catch (NFile::CExceptionFile const &)
					{
					}
				}
			}
			catch (NFile::CExceptionFile const &)
			{
			}
		}
	}

	bool CSystemd::f_SupportsAutoRestart() const
	{
		return true;
	}

	EActionResult CSystemd::f_Exists(CDaemonParams const &_Params, bool &_bExists) const
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		_bExists = false;
		NStr::CStr LaunchFileDirectory = fp_GetUnitConfigDirectory(_Params);
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
}
