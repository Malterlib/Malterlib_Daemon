// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/File/ExeFS>

#include "Malterlib_Daemon.h"

#ifdef DPlatformFamily_Windows
	void * g_hDllInstance = 0;
#endif

namespace NMib::NDaemon
{
	namespace
	{
		void fg_ReportError(NStr::CStr const& _Title, NStr::CStr const& _Error)
		{
			DMibConErrOut("{}{\n}", _Error);
		}

		NDaemon::EReportError fg_ReportErrorYesNo(NStr::CStr const& _Title, NStr::CStr const& _Message, NDaemon::EReportError _Default)
		{
			DMibConOut("{}{\n}", _Message);
			return _Default;
		}

	}
	aint fg_RunDaemon(NStr::CStr const &_DaemonIdentifier, NStr::CStr const &_Name, NStr::CStr const &_SupportEmail, FImplementationFactory const &_fImpFactory)
	{
		NStr::CStr DaemonIdentifier;
		if (_DaemonIdentifier.f_IsEmpty())
			DaemonIdentifier = _DaemonIdentifier.f_Replace(" ", "");
		else
			DaemonIdentifier = _DaemonIdentifier;

		void *pIconData = nullptr;
#		ifdef DPlatformFamily_Windows
			pIconData = g_hDllInstance;
#		elif defined(DPlatformFamily_macOS)
			NContainer::CByteVector IconData;
			if (NFile::fg_ReadExeFSFile("ServiceIcon.png", IconData))
				pIconData = &IconData;
#		endif

		NDaemon::CDaemonParams DaemonParams
			(
				DaemonIdentifier
				, _Name
				, _Name
				, pIconData
				, [&]() -> NStorage::TCUniquePointer<NDaemon::CDaemonImp>
				{
					return _fImpFactory();
				}
				, [&] (NDaemon::CDaemonParams &_Params, NDaemon::CDaemon *_pDaemon, bool &_bHandled) -> EActionResult
				{
					NMib::fg_GetSys()->f_RegisterProgram(_Name, _SupportEmail, false);

					if (_Params.f_GetAction() == NDaemon::EDaemonAction_Run)
					{
						{
							NFile::CFile File(_Params.f_GetRootDirectory() + "/ServiceName", NFile::EFileOpen_Write);
							NStr::CStr DaemonName = _Params.f_GetDaemonName();
							File.f_Write(DaemonName.f_GetStr(), DaemonName.f_GetLen());
						}
						NMib::fg_GetSys()->f_RegisterProgram(_Name, _SupportEmail, true);
					}
					else if (_Params.f_GetAction() == NDaemon::EDaemonAction_Custom)
					{
						_pDaemon->f_ReportInformation
							(
								"Command Line Options"
								, NStr::CStr::CFormat
								(
									"Command line options:{\n}"
									"{\n}"
									"-AddService [Daemon Name]             Adds the current executable to the system as a daemon.{\n}"
									"-AddServiceIfNotAdded [Daemon Name]   Adds the current executable to the system as a daemon if{\n}"
									"                                      a daemon with the same name does not exist.{\n}"
									"-RemoveService [Daemon Name]          Removes the program from the system daemon list.{\n}"
									"-StartService [Daemon Name]           Attempts to start the daemon.{\n}"
									"-StopService [Daemon Name]            Attempts to stop the daemon.{\n}"
									"-RunAsProgram                         Runs the daemon on the command line.{\n}"
									"-Service [Daemon Name]                Starts the program as a daemon. Only used by the system.{\n}"
									"{\n}"
								)
							)
						;
						return EActionResult_Failure;
					}

					return EActionResult_Success;
				}
				, [&] (NStr::CStr const& _Errors)
				{
					fg_ReportError("Error", _Errors);
				}
				, [] (NStr::CStr const& _Heading, NStr::CStr const& _Information)
				{
					fg_ReportError(_Heading, _Information);
				}
				, [&] (NStr::CStr const& _Errors, NDaemon::EReportError _Default) -> NMib::NDaemon::EReportError
				{
					return fg_ReportErrorYesNo("Error", _Errors, _Default);
				}
			)
		;

		NContainer::TCVector<NStr::CStr> Args;
		NSys::fg_Process_GetCommandLineArgs(Args);

		DaemonParams.f_ParseCommandLine(Args);

		NDaemon::CDaemon Daemon(DaemonParams);

		aint ReturnValue = Daemon.f_ProcessCommand();

		if (ReturnValue)
		{
			if (DaemonParams.f_GetAction() == NDaemon::EDaemonAction_Add)
			{
				fg_ReportError("Error", "Failed to add daemon.");
				return 1;
			}
			else if (DaemonParams.f_GetAction() == NDaemon::EDaemonAction_Remove)
			{
				fg_ReportError("Error", "Failed to delete daemon.");
				return 1;
			}
			else if (DaemonParams.f_GetAction() == NDaemon::EDaemonAction_Start)
			{
				fg_ReportError("Error", "Failed to start daemon");
				return 1;
			}
			else if (DaemonParams.f_GetAction() == NDaemon::EDaemonAction_Restart)
			{
				fg_ReportError("Error", "Failed to restart daemon");
				return 1;
			}
			else if (DaemonParams.f_GetAction() == NDaemon::EDaemonAction_Stop)
			{
				fg_ReportError("Error", "Failed to stop daemon");
				return 1;
			}
			else
				return 1;
		}
		return 0;
	}
}
