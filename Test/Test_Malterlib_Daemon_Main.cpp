// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Core/Core>
#include <Mib/Daemon/Daemon>
#include <stdlib.h>

using namespace NMib;
using namespace NMib::NStr;

void fg_ReportError(CStr const& _Title, CStr const& _Error)
{
	DMibConErrOut("{}\n", _Error);
}

void fg_ReportInformation(CStr const& _Title, CStr const& _Error)
{
	DMibConOut("{}\n", _Error);
}

NService::EReportError fg_ReportErrorYesNo(CStr const& _Title, CStr const& _Message, NService::EReportError _Default)
{
	DMibConErrOut("{}\n", _Message);
	return _Default;
}

int fg_ServiceMain(void* _pNativeHandle)
{
	class CServiceImplementation : public NService::CServiceImp
	{
	public:

		CServiceImplementation()
		{
			CStr File = NFile::CFile::fs_GetProgramDirectory() + "/Running";

			try 
			{
				NMib::NStr::CStr Output;
				Output += NSys::fg_UserManagement_GetProcessRealUserName() + "\n";
				Output += NSys::fg_UserManagement_GetProcessRealGroupName() + "\n";
				Output += NFile::CFile::fs_GetProgramDirectory();
				
				NFile::CFile::fs_WriteStringToFile(File, Output, false);
				DMibConOut("Wrote file {}\n", File);
			}
			catch (NMib::NException::CException const &_Error)
			{
				fg_ReportError("Error", NStr::CStr::CFormat("Unable to write file {}: {}") << File << _Error.f_GetErrorStr());
			}
		}

		~CServiceImplementation()
		{
			NTime::CClock Clock;
			Clock.f_Start();
			while (Clock.f_GetTime() < 1.0f)
				NSys::fg_Thread_Sleep(0.1f);
			CStr File = NFile::CFile::fs_GetProgramDirectory() + "/Running";
			try
			{
				NFile::CFile::fs_DeleteFile(File);
				DMibConOut("Deleted file {}\n", File);
			}
			catch (NMib::NException::CException const &_Error)
			{
				fg_ReportError("Error", NStr::CStr::CFormat("Unable to delete file {}: {}") << File << _Error.f_GetErrorStr());
			}
		}

		void f_ServicePause()
		{

		}

		void f_ServiceResume()
		{

		}

	};

	NService::CServiceParams ServiceParams
		(
			"Malterlib_Tests_Service"
			, "Malterlib_Tests_Service"
			, "Malterlib_Tests_Service"
			, _pNativeHandle
			, [&]() -> NPtr::TCUniquePointer<NService::CServiceImp>
			{
				NPtr::TCUniquePointer<NService::CServiceImp> ServerService = fg_Explicit(DMibNew CServiceImplementation());
				return ServerService;
			}
			, [&] (NService::CServiceParams& _Params, NService::CService* _pService, bint& _bHandled) -> NService::EActionResult
			{
				NMib::fg_GetSys()->f_RegisterProgram("Malterlib_Tests_Service", "support@hansoft.se", _Params.f_GetAction() == NService::EServiceAction_Run);

				if (_Params.f_GetAction() == NService::EServiceAction_Custom)
				{
					_bHandled = true;

					if (_Params.f_GetCustomActionKey() == "DeleteUserAndGroup")
					{
#if defined(DPlatformFamily_Windows)
						

	#pragma message ( "TODO: Implement user/group management for Windows and enable this service test code." )

#else
						NMib::NStr::CStr Tmp;
						try 
						{
							if (NMib::NSys::fg_UserManagement_UserExists("_idstestuser", Tmp))
								NMib::NSys::fg_UserManagement_DeleteUser("_idstestuser");
							if (NMib::NSys::fg_UserManagement_GroupExists("_idstestgroup", Tmp))
								NMib::NSys::fg_UserManagement_DeleteGroup("_idstestgroup");
						}
						catch (NMib::NException::CException const &_Exception)
						{
							fg_ReportError("Error", _Exception.f_GetErrorStr());
							return NService::EActionResult_Failure;
						}
#endif // DPlatformFamily_Windows
					}
					else
					{
						CStr File = NFile::CFile::fs_GetProgramDirectory() + "/CustomAction";
						try
						{
							NFile::CFile::fs_WriteStringToFile(File, _Params.f_GetCustomActionKey(), false);
						}
						catch (NMib::NException::CException const &_Exception)
						{
							fg_ReportError("Error", NStr::CStr::CFormat("Unable to write file {}: {}") << File << _Exception.f_GetErrorStr());
						}
					}

					return NService::EActionResult_Success;
				}

				return NService::EActionResult_Success;
			}
			, [&] (CStr const& _Errors)
			{
				fg_ReportError("Error", _Errors);
			}
			, [] (CStr const& _Heading, CStr const& _Information)
			{
				fg_ReportInformation(_Heading, _Information);
			}
			, [&] (CStr const& _Errors, NService::EReportError _Default) -> NMib::NService::EReportError
			{
				return fg_ReportErrorYesNo("Error", _Errors, _Default);
			}
		)
	;

	NContainer::TCVector<CStr> lArgs;
	NSys::fg_Process_GetCommandLineArgs(lArgs);

	ServiceParams.f_ParseCommandLine(lArgs);

	NService::CService Service(ServiceParams);

	return Service.f_ProcessCommand();
}

#ifdef DPlatformFamily_Windows

#include <Windows.h>

int __cdecl wmain(int argc, wchar_t *argv[], wchar_t *envp[]) {}
int __cdecl main(int argc, wchar_t *argv[]){}
int __stdcall WinMain(struct HINSTANCE__ * hInstance, struct HINSTANCE__ * hPrevInstance, char *lpCmdLine,int nShowCmd){;}

int WINAPI wWinMain(IN HINSTANCE hInstance,IN HINSTANCE hPrevInstance,IN wchar_t *lpCmdLine,IN int nShowCmd)
{
	return fg_ServiceMain((void*)hInstance);
}
#else
int main(int argc, char *argv[])
{
	return fg_ServiceMain(nullptr);
}
#endif

DMibAppNoClass;
