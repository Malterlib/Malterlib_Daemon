// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Script.h"
#include <Mib/Process/ProcessLaunch>

#define DInitD "/etc/init.d"
#define DRcD "/etc/rc.d"
#define DChkConfig "/sbin/chkconfig"
#define DUpdateRcD "/usr/sbin/update-rc.d"
#define DInitRunLevels 345
#define DInitStartPrio 60
#define DInitStopPrio 20

namespace NMib::NDaemon
{
	static NStr::CStr fs_GetScriptFilename(CDaemonParams const &_Params)
	{
		return _Params.f_GetDaemonName();
	}

	NStr::CStr CScript::fp_GetScriptDirectory(EDaemonMode _Mode) const
	{
			return mp_ScriptDirectory;
	}

	NStr::CStr CScript::fp_GetScriptPath(CDaemonParams const &_Params) const
	{
		NStr::CStr ScriptFileDirectory = fp_GetScriptDirectory(_Params.f_GetDaemonMode());
		return NStr::CStr::CFormat("{}/{}") << ScriptFileDirectory << fs_GetScriptFilename(_Params);
	}

	bool CScript::fp_IsScriptThisExecutable(CDaemonParams const &_Params) const
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


	bool CScript::fs_IsSupported()
	{
		return (NFile::CFile::fs_FileExists(NStr::CStr(DInitD)) || NFile::CFile::fs_FileExists(NStr::CStr(DRcD)));
	}

	CScript::CScript(CDaemon *_pOwner)
		: CDaemonSystemInterfaceShared(_pOwner, ESupportedFeature_None)
		, mp_ScriptRegistrationMethod(EScriptRegistrationMethod_None)
	{
		try
		{
			if (NFile::CFile::fs_FileExists(NStr::CStr(DInitD)))
				mp_ScriptDirectory = DInitD;
			else if (NFile::CFile::fs_FileExists(NStr::CStr(DRcD)))
				mp_ScriptDirectory = DRcD;

			if (NFile::CFile::fs_FileExists(NStr::CStr(DChkConfig)))
				mp_ScriptRegistrationMethod = EScriptRegistrationMethod_ChkConfig;
			else if (NFile::CFile::fs_FileExists(NStr::CStr(DUpdateRcD)))
				mp_ScriptRegistrationMethod = EScriptRegistrationMethod_UpdateRcD;
		}
		catch (NFile::CExceptionFile const & _Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Unable to check for script support: {}") << _Exception.f_GetErrorStr());
		}

		DMibLog(Debug, "Script directory: {}", mp_ScriptDirectory);

		switch (mp_ScriptRegistrationMethod)
		{
		case EScriptRegistrationMethod_ChkConfig:
			DMibLog(Debug, "Using {} for script management.", NStr::CStr(DChkConfig));
			break;
		case EScriptRegistrationMethod_UpdateRcD:
			DMibLog(Debug, "Using {} for script management.", NStr::CStr(DUpdateRcD));
			break;
		default:
			DMibLog(Debug, "No script management tool found.", 0);
			break;
		}

	}

	CScript::~CScript()
	{
	}

	EActionResult CScript::f_Start(CDaemonParams const &_Params)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

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

		{
			NStr::CStr Result;
			NStr::CStr Error;
			uint32 ExitCode = 0;

			NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "status", Result, Error, ExitCode);

			if (!ExitCode)
			{
				mp_pOwner->f_ReportInformation("Start daemon", "Daemon already running");
				return EActionResult_Success;
			}
		}

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "start", Result, Error, ExitCode)
			|| ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error starting daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}
		return EActionResult_Success;
	}

	EActionResult CScript::f_Stop(CDaemonParams const &_Params, bool _bWait)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		bool bDaemonExists = false;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			mp_pOwner->f_ReportInformation("Stop Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it has not been stopped") << ScriptFilePath);
			return EActionResult_Success;
		}

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "stop", Result, Error, ExitCode)
			|| ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error stopping daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}

	EActionResult CScript::f_Restart(CDaemonParams const &_Params, bool _bWait)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		if (_Params.f_GetKeepRunning())
			return EActionResult_Success;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		bool bDaemonExists = false;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			mp_pOwner->f_ReportInformation("Restart Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it has not been restarted") << ScriptFilePath);
			return EActionResult_Success;
		}

		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(ScriptFilePath, "restart", Result, Error, ExitCode)
			|| ExitCode)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Error restarting daemon {}\nReturned with code {}: {}") << ScriptFilePath << ExitCode << Error);
			return EActionResult_Failure;
		}

		return EActionResult_Success;
	}

	NStr::CStr CScript::fp_CreateScriptFromParams(CDaemonParams const &_Params) const
	{
		NStr::CStr Script;


		Script += "#!/bin/bash\n#\n";

		Script +=	"# chkconfig: " DMibStringize(DInitRunLevels) " " DMibStringize(DInitStartPrio) " " DMibStringize(DInitStopPrio) "\n";
		Script +=	NStr::CStr::CFormat("# description: {}\n") << _Params.f_GetDaemonDescription();

		Script += "# Author: " DMibStringize(DProductContact) "\n\n";

		NStr::CStr PidFilePath = NStr::CStr::CFormat("/var/run/{0}/{0}.pid") << _Params.f_GetDaemonName();

		NStr::CStr DaemonizeUserParams;

		if (!_Params.f_GetRunAsUser().f_IsEmpty())
		{
			NStr::fg_AddStrSep(DaemonizeUserParams, "'-RunAsUser'", " ");
			NStr::fg_AddStrSep(DaemonizeUserParams, NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetRunAsUser()), " ");
		}
		if (!_Params.f_GetRunAsGroup().f_IsEmpty())
		{
			NStr::fg_AddStrSep(DaemonizeUserParams, "'-RunAsGroup'", " ");
			NStr::fg_AddStrSep(DaemonizeUserParams, NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetRunAsGroup()), " ");
		}
		if (!DaemonizeUserParams.f_IsEmpty())
		{
			DaemonizeUserParams = "( " + DaemonizeUserParams + " )";
		}


		Script += "PATH=/sbin:/usr/sbin:/bin:/usr/bin\n";
		Script += NStr::CStr::CFormat("DESC={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonDisplayName());
		Script += NStr::CStr::CFormat("DAEMON={}\n") << NMib::NStr::fg_StrEscapeBashDoubleQuotes(_Params.f_GetExecutablePath());
		Script += NStr::CStr::CFormat("NAME={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonName());
		Script += NStr::CStr::CFormat("DAEMON_ARGS=( '-Service' {} '-Daemonize' {} )\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetDaemonName()) << NMib::NStr::fg_StrEscapeBashSingleQuotes(PidFilePath);
		Script += NStr::CStr::CFormat("DAEMON_USER_ARGS={}\n") << DaemonizeUserParams;
		Script += NStr::CStr::CFormat("SCRIPTNAME={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(fp_GetScriptPath(_Params));
		Script += NStr::CStr::CFormat("PIDFILE={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(PidFilePath);
		Script += NStr::CStr::CFormat("PIDDIRECTORY={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(NFile::CFile::fs_GetPath(PidFilePath));
		Script += NStr::CStr::CFormat("STOPTIMEOUT={}\n") << 24*60*60; // 24 hour timeout
		Script += NStr::CStr::CFormat("DAEMON_USER={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetRunAsUser());
		Script += NStr::CStr::CFormat("DAEMON_GROUP={}\n") << NMib::NStr::fg_StrEscapeBashSingleQuotes(_Params.f_GetRunAsGroup());

		Script +=	"\n";

		Script +=	"# Exit if the package is not installed\n"
					"[ -x \"$DAEMON\" ] || exit 0\n\n";


		Script +=	"# Load the VERBOSE setting and other rcS variables\n"
					"[ -x \"/lib/init/vars.sh\" ] && . /lib/init/vars.sh\n\n";


		Script +=	"# Define LSB log_* functions.\n"
					"if [ -x \"/lib/lsb/init-functions\" ] ; then\n"
					"	. /lib/lsb/init-functions\n"
					"else\n"
					"	log_success_msg()\n"
					"	{\n"
					"		echo \"$@\"\n"
					"	}\n"
					"	log_failure_msg()\n"
					"	{\n"
					"		echo \"$@\"\n"
					"	}\n"
					"fi\n"
					"\n"
		;
/*
# Return
#   0 if daemon has been started
#   1 if daemon was already running
#   2 if daemon could not be started
*/
		Script +=	"do_start()\n"
					"\{\n"
					"	\"$DAEMON\" \"${DAEMON_ARGS[@]:0}\" -DoStart \"${DAEMON_USER_ARGS[@]:0}\"\n"
					"	return $?\n"
					"}\n\n"
		;

		Script +=	"do_status()\n"
					"\{\n"
					"	\"$DAEMON\" \"${DAEMON_ARGS[@]:0}\" -DoStatus\n"
					"	return $?\n"
					"}\n\n"
		;

/*
# Return
#   0 if daemon has been stopped
#	1 if daemon was already stopped
#   2 if daemon could not be stopped
#   other if a failure occurred
*/
		Script +=	"do_stop()\n"
					"{\n"
					"	\"$DAEMON\" \"${DAEMON_ARGS[@]:0}\" -DoStop $STOPTIMEOUT\n"
					"}\n\n"
		;


		Script +=	"case \"$1\" in\n"
					"	start)\n"
					"		[ \"$VERBOSE\" != no ] && echo \"Starting $DESC\"\n"
					"		do_start\n"
					"		case \"$?\" in\n"
					"			0) [ \"$VERBOSE\" != no ] && log_success_msg \"$DESC started\" ;;\n"
					"			1) [ \"$VERBOSE\" != no ] && log_success_msg \"$DESC already running\" ;;\n"
					"			2) [ \"$VERBOSE\" != no ] && log_failure_msg \"$DESC failed to start\" && exit 1 || exit 1 ;;\n"
					"		esac\n"
					"		;;\n"
					"	stop)\n"
					"		[ \"$VERBOSE\" != no ] && echo \"Stopping $DESC\"\n"
					"		do_stop\n"
					"		case \"$?\" in\n"
					"			0) [ \"$VERBOSE\" != no ] && log_success_msg \"$DESC stopped\" ;;\n"
					"			1) [ \"$VERBOSE\" != no ] && log_success_msg \"$DESC already stopped\" ;;\n"
					"			2) [ \"$VERBOSE\" != no ] && log_failure_msg \"$DESC failed to stop\" && exit 1 || exit 1 ;;\n"
					"		esac\n"
					"		;;\n"
					"	status)\n"
					"		do_status && exit 0 || exit $?\n"
					"		;;\n"
					"	restart|force-reload)\n"
					"		[ \"$VERBOSE\" != no ] && echo \"Restarting $DESC\"\n"
					"		do_stop\n"
					"		case \"$?\" in\n"
					"			0|1)\n"
					"				do_start\n"
					"				case \"$?\" in\n"
					"					0) log_success_msg \"$DESC restarted\";;\n"
					"					1) log_failure_msg \"$DESC failed to restart. Old process still running\";; # Old process is still running\n"
					"					*) log_failure_msg \"$DESC failed to start after stop. \";; # Failed to start\n"
					"				esac\n"
					"				;;\n"
					"			*)\n"
					"				# Failed to stop\n"
					"				log_failure_msg \"$DESC failed to stop\"\n"
					"				;;\n"
					"		esac\n"
					"		;; \n"
					"	*)\n"
					"		echo \"Usage: $SCRIPTNAME {start|stop|status|restart|force-reload}\" >&2\n"
					"		exit 3\n"
					"		;;\n"
					"esac\n\n"
					":\n"
		;


		return Script;
	}

	EActionResult CScript::fp_AddToRunlevels(CDaemonParams const &_Params) const
	{
		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (mp_ScriptRegistrationMethod == EScriptRegistrationMethod_ChkConfig)
		{
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DChkConfig, NStr::CStr::CFormat("--add {}") << fs_GetScriptFilename(_Params), Result, Error, ExitCode)
				|| ExitCode)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to add {} to runlevels {}: {}") << fs_GetScriptFilename(_Params)  << DInitRunLevels << Error);
				return EActionResult_Failure;
			}
			else
				mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully added {} to runlevels {}") << fs_GetScriptFilename(_Params)  << DInitRunLevels);
		}
		else if (mp_ScriptRegistrationMethod == EScriptRegistrationMethod_UpdateRcD)
		{
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DUpdateRcD, NStr::CStr::CFormat("{} defaults {}") << fs_GetScriptFilename(_Params) << DInitStartPrio, Result, Error, ExitCode)
				|| ExitCode)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to add {} to default runlevels: {}") << fs_GetScriptFilename(_Params)  << Error);
				return EActionResult_Failure;
			}
			else
				mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully added {} to default runlevels") << fs_GetScriptFilename(_Params));
		}

		return EActionResult_Success;
	}

	EActionResult CScript::fp_RemoveFromRunlevels(CDaemonParams const &_Params) const
	{
		NStr::CStr Result;
		NStr::CStr Error;
		uint32 ExitCode = 0;

		if (mp_ScriptRegistrationMethod == EScriptRegistrationMethod_ChkConfig)
		{
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DChkConfig, NStr::CStr::CFormat("--del {}") << fs_GetScriptFilename(_Params), Result, Error, ExitCode)
				|| ExitCode)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove {} from runlevels {}: {}") << fs_GetScriptFilename(_Params) << DInitRunLevels << Error);
				return EActionResult_Failure;
			}
			else
				mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed {} from runlevels {}") << fs_GetScriptFilename(_Params)  << DInitRunLevels);
		}
		else if (mp_ScriptRegistrationMethod == EScriptRegistrationMethod_UpdateRcD)
		{
			if (!NMib::NProcess::CProcessLaunch::fs_LaunchBlock(DUpdateRcD, NStr::CStr::CFormat("-f {} remove") << fs_GetScriptFilename(_Params), Result, Error, ExitCode)
				|| ExitCode)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove {} from default runlevels: {}") << fs_GetScriptFilename(_Params) << Error);
				return EActionResult_Failure;
			}
			else
				mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed {} from default runlevels") << fs_GetScriptFilename(_Params));
		}

		return EActionResult_Success;
	}

	EActionResult CScript::f_Add(CDaemonParams const &_Params, bool _bCheckForExisting)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr ScriptFileDirectory = fp_GetScriptDirectory(_Params.f_GetDaemonMode());
		NStr::CStr ScriptFilePath = NStr::CStr::CFormat("{}/{}") << ScriptFileDirectory << fs_GetScriptFilename(_Params);

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
			mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully installed daemon at {}") << ScriptFilePath);
		}
		catch (NFile::CExceptionFile const &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to install daemon at {}: {}") << ScriptFilePath << _Exception.f_GetErrorStr());
			return EActionResult_Failure;
		}

		if (mp_ScriptRegistrationMethod != EScriptRegistrationMethod_None)
		{
			if (fp_AddToRunlevels(_Params) == EActionResult_Failure)
				return EActionResult_Failure;
		}
		else
				mp_pOwner->f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Please add {} to any desired runlevel init scripts manually.") << ScriptFilePath);

		return EActionResult_Success;
	}

	EActionResult CScript::f_Remove(CDaemonParams const &_Params)
	{
		if (!fp_CheckParamsSupported(_Params))
			return EActionResult_Failure;

		NStr::CStr ScriptFilePath = fp_GetScriptPath(_Params);

		bool bDaemonExists;
		if (f_Exists(_Params, bDaemonExists) == EActionResult_Failure)
			return EActionResult_Failure;

		if (!bDaemonExists)
		{
			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it has not been removed") << ScriptFilePath);
			return EActionResult_Success;
		}

		if (f_Stop(_Params, true) == EActionResult_Failure)
			return EActionResult_Failure;

		if (mp_ScriptRegistrationMethod != EScriptRegistrationMethod_None)
		{
			if (fp_RemoveFromRunlevels(_Params) == EActionResult_Failure)
				return EActionResult_Failure;
		}
		else
			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Please remove {} from all runlevel init scripts manually.") << ScriptFilePath);

		try
		{
			NFile::CFile::fs_DeleteFile(ScriptFilePath);
			NStr::CStr Error;
			if (!_Params.f_RemoveDaemonModeFile(Error))
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon mode file: {}") << Error);

			mp_pOwner->f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed daemon from {}") << ScriptFilePath);
			return EActionResult_Success;
		}
		catch (NFile::CExceptionFile &_Exception)
		{
			mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to remove daemon at {}: {}\nPerhaps you need to use sudo?") << ScriptFilePath << _Exception.f_GetErrorStr());
		}

		return EActionResult_Failure;
	}

	bool CScript::f_SupportsAutoRestart() const
	{
		return false;
	}

	EActionResult CScript::f_Exists(CDaemonParams const &_Params, bool &_bExists) const
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
