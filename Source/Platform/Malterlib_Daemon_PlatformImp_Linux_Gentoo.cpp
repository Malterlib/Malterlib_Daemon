// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Gentoo.h"
#include <Mib/Process/ProcessLaunch>

#define DInitD "/etc/init.d"
#define DRCUpdateBin "/sbin/rc-update"
#define DGentooReleaseFile "/etc/gentoo-release"

namespace NMib::NDaemon
{
	static NStr::CStr fs_GetStartStopDaemonParams(CDaemonParams const &_Params)
	{
		NStr::CStr Result;

		if (!_Params.f_GetRunAsUser().f_IsEmpty())
		{
			Result += NStr::CStr::CFormat("-u {}") << _Params.f_GetRunAsUser();
			if (!_Params.f_GetRunAsGroup().f_IsEmpty())
				Result += NStr::CStr::CFormat(":{} ") << _Params.f_GetRunAsGroup();
			else
				Result += " ";
		}
		else if (!_Params.f_GetRunAsGroup().f_IsEmpty())
		{
			Result += NStr::CStr::CFormat("-g {} ") << _Params.f_GetRunAsGroup();
		}

		Result += NStr::CStr::CFormat("-d {} ") << NFile::CFile::fs_GetPath(_Params.f_GetExecutablePath()).f_EscapeStr();

		return Result;
	}

	static NStr::CStr fs_GetScriptFilename(CDaemonParams const &_Params)
	{
		return _Params.f_GetDaemonName();
	}

	NStr::CStr CGentoo::fp_GetScriptDirectory(EDaemonMode _Mode) const
	{
		return mp_ScriptDirectory;
	}

	NStr::CStr CGentoo::fp_GetScriptPath(CDaemonParams const &_Params) const
	{
		NStr::CStr ScriptFileDirectory = fp_GetScriptDirectory(_Params.f_GetDaemonMode());
		return NStr::CStr::CFormat("{}/{}") << ScriptFileDirectory << fs_GetScriptFilename(_Params);
	}

	bool CGentoo::fp_IsScriptThisExecutable(CDaemonParams const &_Params) const
	{
		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);
		NStr::CStr Contents;

		try
		{
			Contents = NFile::CFile::fs_ReadStringFromFile(NStr::CStr(ScriptFilePath), true);
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

			(NStr::CStr::CParse("DAEMON={}") >> Value).f_Parse(Line, nParsed);
			if (nParsed == 1)
			{
				NStr::CStr ExpectedExec = NMib::NStr::fg_StrEscapeBashDoubleQuotes(_Params.f_GetExecutablePath());
				if (ExpectedExec.f_CmpNoCase(Value) == 0)
				{
					return true;
				}
				else
				{
					mp_pOwner->f_ReportError(NStr::CStr::CFormat("Daemon name is already in use: {} and '{}' != '{}'") << ScriptFilePath << Value << ExpectedExec);
					return false;
				}
			}
		}

		mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to find executable in {}") << ScriptFilePath);
		return false;
	}

	bool CGentoo::fs_IsSupported()
	{
		return NFile::CFile::fs_FileExists(NStr::CStr(DInitD))
			&& NFile::CFile::fs_FileExists(NStr::CStr(DGentooReleaseFile))
			&& NFile::CFile::fs_FileExists(NStr::CStr(DRCUpdateBin));
	}

	CGentoo::CGentoo(CDaemon *_pOwner)
		: CDaemonSystemInterfaceShared(_pOwner, ESupportedFeature_None)
	{
		if (NFile::CFile::fs_FileExists(NStr::CStr(DInitD)))
			mp_ScriptDirectory = DInitD;

		DMibLog(Debug, "Script directory: {}", mp_ScriptDirectory);
	}

	CGentoo::EGentooDaemonStatus CGentoo::fp_GetStatus(NStr::CStr const &_ScriptFilePath)
	{
		NStr::CStr ListResult;
		NStr::CStr ListError;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(_ScriptFilePath, "status", ListResult, ListError, ExitCode))
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error checking running status of daemon {}: {}\n{}") << _ScriptFilePath << ListResult << ListError);
			return EGentooDaemonStatus_InvalidDaemon;
		}
		else
			return (CGentoo::EGentooDaemonStatus)ExitCode;
	}

	bool CGentoo::fp_GetStatusAdded(NStr::CStr const &_DaemonName)
	{
		NStr::CStr ListResult;

		NStr::CStr ListError;
		uint32 ExitCode = 0;

		NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DRCUpdateBin, "show default", ListResult, ListError, ExitCode);

		if (ExitCode)
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error listing services: {}") << ListError);

		NStr::CStr DaemonNeedle = NStr::CStr::CFormat("{} | default") << _DaemonName;
		return ListResult.f_FindNoCase(DaemonNeedle) != -1;
	}

	EActionResult CGentoo::f_Start(CDaemonParams const &_Params)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);
		try
		{
			if (!NFile::CFile::fs_FileExists(ScriptFilePath, NFile::EFileAttrib_File))
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Daemon file does not exist: {}") << ScriptFilePath);
				return EActionResult_Failure;
			}
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing daemon file: {}") << _Exception.f_GetErrorStr());
			return EActionResult_Failure;
		}

		if (fp_GetStatus(ScriptFilePath) == EGentooDaemonStatus_Started)
		{
			mp_pOwner->f_ReportInformation("Start daemon", NStr::CStr::CFormat("Daemon was already running: {}") << ScriptFilePath);
			return EActionResult_Success;
		}

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "start", Result, Error, ExitCode)
			|| ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error starting daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}

		mp_pOwner->f_ReportInformation("Start daemon", NStr::CStr::CFormat("Daemon {} was successfully started") << ScriptFilePath);
		return EActionResult_Success;
	}

	EActionResult CGentoo::f_Stop(CDaemonParams const &_Params, bool _bWait)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		bool bSerivceExists;
		if (f_Exists(_Params, bSerivceExists) == EActionResult_Failure)
			return EActionResult_Failure;
		if (!bSerivceExists)
		{
			mp_pOwner->f_ReportInformation("Stop Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it was not stopped") << ScriptFilePath);
			return EActionResult_Success;
		}


		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (fp_GetStatus(ScriptFilePath) == EGentooDaemonStatus_Stopped)
		{
			mp_pOwner->f_ReportInformation("Stop daemon", NStr::CStr::CFormat("Daemon was not running: {}") << ScriptFilePath);
			return EActionResult_Success;
		}

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "stop", Result, Error, ExitCode)
			|| ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error stopping daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}

		mp_pOwner->f_ReportInformation("Stop daemon", NStr::CStr::CFormat("Daemon {} was successfully stopped") << ScriptFilePath);
		return EActionResult_Success;
	}

	EActionResult CGentoo::f_Restart(CDaemonParams const &_Params, bool _bWait)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		bool bSerivceExists;
		if (f_Exists(_Params, bSerivceExists) == EActionResult_Failure)
			return EActionResult_Failure;
		if (!bSerivceExists)
		{
			mp_pOwner->f_ReportInformation("Stop Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it was not restarted") << ScriptFilePath);
			return EActionResult_Success;
		}

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "restart", Result, Error, ExitCode) || ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error restarting daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}

		mp_pOwner->f_ReportInformation("Restart daemon", NStr::CStr::CFormat("Daemon {} was successfully restarted") << ScriptFilePath);
		return EActionResult_Success;
	}

	NStr::CStr CGentoo::fp_CreateScriptFromParams(CDaemonParams const &_Params) const
	{
		NStr::CStr Script;


		Script += "#!/sbin/runscript\n\n";


		Script += "# Author: " DMibStringize(DProductContact) "\n\n";

		NStr::CStr PidFileDirectory = NStr::CStr::CFormat("/var/run/{}") << _Params.f_GetDaemonName();
		NStr::CStr PidFilePath = NStr::CStr::CFormat("{}/{}") << PidFileDirectory << _Params.f_GetDaemonName();

		Script += NStr::CStr::CFormat("DESC={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonDisplayName());
		Script += NStr::CStr::CFormat("DAEMON={}\n") << NMib::NStr::fg_StrEscapeBashDoubleQuotes(_Params.f_GetExecutablePath());
		Script += NStr::CStr::CFormat("NAME={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonName());
		Script += NStr::CStr::CFormat("DAEMON_ARGS=( '-Service' {} '-Daemonize' {} )\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonName()) << NMib::NStr::fg_StrEscapeBashSingleQuotes(PidFilePath);
		Script += NStr::CStr::CFormat("PIDFILE={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(PidFilePath);
		Script += NStr::CStr::CFormat("STOPTIMEOUT={}\n") << 24*60*60;

		Script += "\n";

		Script += "depend() {\n"
			"	need net\n"
			"	need localmount\n"
			"	need net\n"
			"	after bootmisc\n"
			"}\n\n";

		Script += "checkconfig() {\n";
		Script += NStr::CStr::CFormat("	if [ ! -d {0} ] ; then\n"
			"		mkdir {0}\n") << PidFileDirectory;
		if (!_Params.f_GetRunAsUser().f_IsEmpty() || !_Params.f_GetRunAsGroup().f_IsEmpty())
			Script += NStr::CStr::CFormat("		chown {1}:{2} {0}\n") << PidFileDirectory << _Params.f_GetRunAsUser() << _Params.f_GetRunAsGroup();
		Script += "	fi\n}\n\n";

/*
# Return
#   0 if daemon has been started
#   1 if daemon was already running
#   2 if daemon could not be started
*/
		Script += NStr::CStr::CFormat(	"start() {{\n"
										"	checkconfig\n"
										"	ebegin \"Starting $NAME\"\n"
										"	start-stop-daemon --start --quiet --pidfile \"$PIDFILE\" --exec \"$DAEMON\" {0} --test > /dev/null || return 1\n"
										"	start-stop-daemon --start --quiet --pidfile \"$PIDFILE\" --exec \"$DAEMON\" {0} -- \"${{DAEMON_ARGS[@]:0}\" || return 2\n"
										"	eend $?\n"
										"}\n\n") << fs_GetStartStopDaemonParams(_Params);


/*
# Return
#   0 if daemon has been stopped
#   1 if daemon was already stopped
#   2 if daemon could not be stopped
#   other if a failure occurred
*/
		Script +=	"stop() {\n"
					"	ebegin \"Stopping $NAME\"\n"
					"	start-stop-daemon --stop --quiet --retry=TERM/$STOPTIMEOUT/KILL/5 --pidfile \"$PIDFILE\" --name \"$NAME\"\n"
					"	RETVAL=\"$?\"\n"
					"	[ \"$RETVAL\" = 2 ] && eend 2\n"
					"	start-stop-daemon --stop --quiet --retry=0/$STOPTIMEOUT/KILL/5 --exec \"$DAEMON\"\n"
					"	[ \"$?\" = 2 ] && eend 2\n"
					"	rm -f \"$PIDFILE\"\n"
					"	eend \"$RETVAL\"\n"
					"}\n\n";

		return Script;
	}

	EActionResult CGentoo::f_Add(CDaemonParams const &_Params, bool _bCheckForExisting)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr ScriptFileDirectory = fp_GetScriptDirectory(_Params.f_GetDaemonMode());
		NStr::CStr ScriptFilePath = NStr::CStr::CFormat("{}/{}") << ScriptFileDirectory << fs_GetScriptFilename(_Params);
		NStr::CStr DaemonName = fs_GetScriptFilename(_Params);

		if (ScriptFileDirectory.f_IsEmpty())
		{
			mp_pOwner->f_ReportError("Failed to locate script directory");
			return EActionResult_Failure;
		}

		if (_bCheckForExisting)
		{
			if (NFile::CFile::fs_FileExists(ScriptFilePath, NFile::EFileAttrib_File))
			{
				if (fp_IsScriptThisExecutable(_Params))
					return EActionResult_Success;
				else
					return EActionResult_Failure;
			}
		}

		NStr::CStr Data = fp_CreateScriptFromParams(_Params);

		try
		{
			NStr::CStr Error;
			if (!_Params.f_GetDisableWriteDaemon() && !_Params.f_WriteDaemonModeFile(Error))
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

			if (!NFile::CFile::fs_FileExists(ScriptFileDirectory, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_CreateDirectory(ScriptFileDirectory);

			NFile::CFile::fs_WriteStringToFile(NStr::CStr(ScriptFilePath), Data, false);

			NFile::EFileAttrib Attribs = NFile::CFile::fs_GetAttributes(ScriptFilePath);
			Attribs |= NFile::EFileAttrib_Executable;
			NFile::CFile::fs_SetAttributes(ScriptFilePath, Attribs);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to install daemon at {}: {}") << ScriptFilePath << _Exception.f_GetErrorStr());
			return EActionResult_Failure;
		}

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DRCUpdateBin, NStr::CStr::CFormat("add {} default") << DaemonName, Result, Error, ExitCode)
			|| ExitCode
			|| !Error.f_IsEmpty())
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to add daemon {} to runlevel default: {}") << DaemonName << Error);
			return EActionResult_Failure;
		}

		mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully installed daemon at {}") << ScriptFilePath);
		return EActionResult_Success;
	}

	EActionResult CGentoo::f_Remove(CDaemonParams const &_Params)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (f_Stop(_Params, false) == EActionResult_Failure)
			return EActionResult_Failure;

		NStr::CStr DaemonName = fs_GetScriptFilename(_Params);
		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		// Remove daemon from runlevel
		if (fp_GetStatusAdded(DaemonName))
		{
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;

			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DRCUpdateBin, NStr::CStr::CFormat("del {} default") << DaemonName, Result, Error, ExitCode)
				|| ExitCode
				|| !Error.f_IsEmpty())
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon {} from runlevel default: {}") << DaemonName << Error);
				return EActionResult_Failure;
			}

			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed daemon {} from runlevel default") << DaemonName);
		}
		else
			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Daemon {} did not exist in runlevel default") << DaemonName);

		bool bSerivceExists;
		if (f_Exists(_Params, bSerivceExists) == EActionResult_Failure)
			return EActionResult_Failure;
		if (!bSerivceExists)
		{
			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it was not removed") << ScriptFilePath);
			return EActionResult_Success;
		}

		try
		{
			NFile::CFile::fs_DeleteFile(ScriptFilePath);
			NStr::CStr Error;
			if (!_Params.f_RemoveDaemonModeFile(Error))
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon mode file: {}") << Error);
		}
		catch (NFile::CExceptionFile &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon at {}: {}\nPerhaps you need to use sudo?") << ScriptFilePath << _Exception.f_GetErrorStr());
			return EActionResult_Failure;
		}

		mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed daemon from {}") << ScriptFilePath);
		return EActionResult_Success;
	}

	bool CGentoo::f_SupportsAutoRestart() const
	{
		return false;
	}

	EActionResult CGentoo::f_Exists(CDaemonParams const &_Params, bool &_bExists) const
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		_bExists = false;
		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);
		try
		{
			if (NFile::CFile::fs_FileExists(ScriptFilePath, NFile::EFileAttrib_File))
				_bExists = true;
			return EActionResult_Success;
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to check for existing file {}: {}") << ScriptFilePath << _Exception.f_GetErrorStr());
		}

		return EActionResult_Failure;
	}
}
