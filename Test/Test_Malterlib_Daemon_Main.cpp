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

NDaemon::EReportError fg_ReportErrorYesNo(CStr const& _Title, CStr const& _Message, NDaemon::EReportError _Default)
{
	DMibConErrOut("{}\n", _Message);
	return _Default;
}

int fg_DaemonMain(void* _pNativeHandle)
{
	class CDaemonImplementation : public NDaemon::CDaemonImp
	{
	public:

		CDaemonImplementation()
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

		~CDaemonImplementation()
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

		void f_DaemonPause()
		{

		}

		void f_DaemonResume()
		{

		}

	};

	NDaemon::CDaemonParams DaemonParams
		(
			"Malterlib_Tests_Daemon"
			, "Malterlib_Tests_Daemon"
			, "Malterlib_Tests_Daemon"
			, _pNativeHandle
			, [&]() -> NStorage::TCUniquePointer<NDaemon::CDaemonImp>
			{
				NStorage::TCUniquePointer<NDaemon::CDaemonImp> ServerDaemon = fg_Explicit(DMibNew CDaemonImplementation());
				return ServerDaemon;
			}
			, [&] (NDaemon::CDaemonParams& _Params, NDaemon::CDaemon* _pDaemon, bool& _bHandled) -> NDaemon::EActionResult
			{
				NMib::fg_GetSys()->f_RegisterProgram("Malterlib_Tests_Daemon", "support@malterlib.com", _Params.f_GetAction() == NDaemon::EDaemonAction_Run);

				if (_Params.f_GetAction() == NDaemon::EDaemonAction_Custom)
				{
					_bHandled = true;

					if (_Params.f_GetCustomActionKey() == "DeleteUserAndGroup")
					{
#if defined(DPlatformFamily_Windows)


	#pragma message ( "TODO: Implement user/group management for Windows and enable this daemon test code." )

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
							return NDaemon::EActionResult_Failure;
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

					return NDaemon::EActionResult_Success;
				}

				return NDaemon::EActionResult_Success;
			}
			, [&] (CStr const& _Errors)
			{
				fg_ReportError("Error", _Errors);
			}
			, [] (CStr const& _Heading, CStr const& _Information)
			{
				fg_ReportInformation(_Heading, _Information);
			}
			, [&] (CStr const& _Errors, NDaemon::EReportError _Default) -> NMib::NDaemon::EReportError
			{
				return fg_ReportErrorYesNo("Error", _Errors, _Default);
			}
		)
	;

	NContainer::TCVector<CStr> lArgs;
	NSys::fg_Process_GetCommandLineArgs(lArgs);

	DaemonParams.f_ParseCommandLine(lArgs);

	NDaemon::CDaemon Daemon(DaemonParams);

	return Daemon.f_ProcessCommand();
}

#ifdef DPlatformFamily_Windows

#include <Windows.h>

int __cdecl wmain(int argc, wchar_t *argv[], wchar_t *envp[]) {}
int __cdecl main(int argc, wchar_t *argv[]){}
int __stdcall WinMain(struct HINSTANCE__ * hInstance, struct HINSTANCE__ * hPrevInstance, char *lpCmdLine,int nShowCmd){;}

int WINAPI wWinMain(IN HINSTANCE hInstance,IN HINSTANCE hPrevInstance,IN wchar_t *lpCmdLine,IN int nShowCmd)
{
	return fg_DaemonMain((void*)hInstance);
}
#else
int main(int argc, char *argv[])
{
	return fg_DaemonMain(nullptr);
}
#endif

DMibAppNoClass;
