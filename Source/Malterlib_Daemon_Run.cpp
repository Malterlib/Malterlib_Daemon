// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/File/ExeFS>

#include "Malterlib_Daemon.h"

#ifdef DPlatformFamily_Windows
	void * g_hDllInstance = 0;
#endif

namespace NMib
{
	namespace NService
	{
		namespace
		{
			void fg_ReportError(NStr::CStr const& _Title, NStr::CStr const& _Error)
			{
				DMibConErrOut("{}{\n}", _Error);
			}

			NService::EReportError fg_ReportErrorYesNo(NStr::CStr const& _Title, NStr::CStr const& _Message, NService::EReportError _Default)
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
	#		elif defined(DPlatformFamily_OSX)
				NContainer::TCVector<uint8> IconData;
				if (NFile::fg_ReadExeFSFile("ServiceIcon.png", IconData))
					pIconData = &IconData;
	#		endif
			
			NService::CServiceParams ServiceParams
				(
					DaemonIdentifier
					, _Name
					, _Name
					, pIconData
					, [&]() -> NPtr::TCUniquePointer<NService::CServiceImp>
					{
						return _fImpFactory();
					}
					, [&] (NService::CServiceParams &_Params, NService::CService *_pService, bint &_bHandled) -> EActionResult 
					{
						NMib::fg_GetSys()->f_RegisterProgram(_Name, _SupportEmail, false);

						if (_Params.f_GetAction() == NService::EServiceAction_Run)
						{
							{
								NFile::CFile File(NFile::CFile::fs_GetProgramDirectory() + "/ServiceName", NFile::EFileOpen_Write);
								NStr::CStr ServiceName = _Params.f_GetServiceName();
								File.f_Write(ServiceName.f_GetStr(), ServiceName.f_GetLen());
							}
							NMib::fg_GetSys()->f_RegisterProgram(_Name, _SupportEmail, true);
						}
						else if (_Params.f_GetAction() == NService::EServiceAction_Custom)
						{
							_pService->f_ReportInformation
								(
									"Command Line Options"
									, NStr::CStr::CFormat
									(
										"Command line options:{\n}"
										"{\n}"
										"-AddService [Service Name]            Adds the current executable to the system as a service.{\n}"
										"-AddServiceIfNotAdded [Service Name]  Adds the current executable to the system as a service if{\n}"
										"                                      a service with the same name does not exist.{\n}"
										"-RemoveService [Service Name]         Removes the program from the system service list.{\n}"
										"-StartService [Service Name]          Attempts to start the service.{\n}"
										"-StopService [Service Name]           Attempts to stop the service.{\n}"
										"-RunAsProgram                         Runs the daemon on the command line.{\n}"
										"-Service [Service Name]               Starts the program as a daemon. Only used by the system.{\n}"
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
					, [&] (NStr::CStr const& _Errors, NService::EReportError _Default) -> NMib::NService::EReportError
					{
						return fg_ReportErrorYesNo("Error", _Errors, _Default);
					}
				)
			;

			NContainer::TCVector<NStr::CStr> lArgs;
			NSys::fg_Process_GetCommandLineArgs(lArgs);

			ServiceParams.f_ParseCommandLine(lArgs);

			NService::CService Service(ServiceParams);

			aint ReturnValue = Service.f_ProcessCommand();

			if (ReturnValue)
			{
				if (ServiceParams.f_GetAction() == NService::EServiceAction_Add)
				{
					fg_ReportError("Error", "Failed to add service.");
					return 1;
				}
				else if (ServiceParams.f_GetAction() == NService::EServiceAction_Remove)
				{
					fg_ReportError("Error", "Failed to delete service.");
					return 1;
				}
				else if (ServiceParams.f_GetAction() == NService::EServiceAction_Start)
				{
					fg_ReportError("Error", "Failed to start service");
					return 1;
				}
				else if (ServiceParams.f_GetAction() == NService::EServiceAction_Restart)
				{
					fg_ReportError("Error", "Failed to restart service");
					return 1;
				}
				else if (ServiceParams.f_GetAction() == NService::EServiceAction_Stop)
				{
					fg_ReportError("Error", "Failed to stop service");
					return 1;
				}
				else
					return 1;
			}
			return 0;
		}		
	}
}
