// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Concurrency/ThreadSafeQueue>
#include <Mib/Daemon/Daemon>
#include <Mib/Process/ProxiedProcessLaunch>
#include <Mib/Cryptography/UUID>

namespace
{
	using namespace NMib::NTest;
	using namespace NMib::NTime;

	enum EExitResult
	{
		EExitResult_None
		, EExitResult_Exited
		, EExitResult_NotLaunched
	};
	class CService_Tests : public CTest
	{
	public:
		
		NMib::NStr::CStr m_ServiceName;

		CService_Tests()
		{
			m_ServiceName = "Malterlib_TestService_" + NMib::NDataProcessing::fg_GetHashedUuidString(NMib::NFile::CFile::fs_GetProgramPath(), NMib::NDataProcessing::CUniversallyUniqueIdentifier("{469e17ec-9536-44bc-8c8d-ab80a5dcbf13}"));
		}

		static NMib::NStr::CStr fs_GetTestPath(NMib::NStr::CStr const &_TestPath, NMib::NStr::CStr const &_Test)
		{
			if (_Test.f_IsEmpty())
				return NMib::NStr::CStr::CFormat("--Tests \"{}\" --TestLogger Registry --TestResults (All ProcessRecursive)") << _TestPath;
			else
				return NMib::NStr::CStr::CFormat("--Tests \"{}/{}\" --TestLogger Registry --TestResults (All ProcessRecursive)") << _TestPath << _Test;
		}
		
		NMib::NThread::CMutual m_ProxyClientLock;
		NMib::NPtr::TCUniquePointer<NMib::NProcess::CProxiedLaunchClient> m_pProxyClient;
		
		NMib::NStr::CStr m_ServiceDirectory;
		
		NMib::NContainer::TCSet<NMib::NStr::CStr> m_CreatedDirs;
			
		NMib::NProcess::CProxiedLaunchClient &f_GetClient()
		{
			DMibLock(m_ProxyClientLock);
			if (m_pProxyClient)
				return *m_pProxyClient;
			
			NMib::NProcess::CProcessLaunchParams Params;
			Params.m_Target = NMib::NFile::CFile::fs_GetProgramPath();
			if (NMib::NProcess::CProcessLaunch::fs_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
				Params.m_Elevation = NMib::NProcess::EProcessLaunchElevation_Elevate;
			
			Params.m_Parameters = "--Tests Malterlib/Daemon/Service/ProxyServer --TestLogger Null --TestResults (ProcessRecursive)";	
			Params.m_bStdOutPID = 1;
			
			m_pProxyClient = NMib::fg_Construct(Params, NMib::fg_Default());
			
			return *m_pProxyClient;
		}
		
		void f_DoProxyServer()
		{
			if (fg_TestReportFlags() & ETestReportFlag_ProcessRecursive)
			{
				DMibTestSuite("ProxyServer")
				{
					NMib::NProcess::CProxiedLaunchServer Server;
				};
			}
		}

		NMib::NStr::CStr f_GetServiceDir()
		{
			NMib::NStr::CStr ServiceDir = NMib::NFile::CFile::fs_GetProgramDirectory() + "/" + m_ServiceDirectory;

			auto Mapped = m_CreatedDirs(ServiceDir);
			if (Mapped.f_WasCreated())
			{
				NMib::NStr::CStr ServiceFile = NMib::NFile::CFile::fs_GetFile(NMib::NFile::CFile::fs_GetProgramPath()).f_Replace("Test_Malterlib_Daemon", "Test_Malterlib_Helper_Daemon");
				NMib::NStr::CStr SourceFile = NMib::NFile::CFile::fs_GetProgramDirectory() + "/" + ServiceFile;
				NMib::NStr::CStr DestFile = ServiceDir + "/" + ServiceFile;
				if (NMib::NFile::CFile::fs_FileExists(ServiceDir))
				{
					try
					{
						NMib::NFile::CFile::fs_DeleteDirectoryRecursive(ServiceDir);
					}
					catch (NMib::NFile::CExceptionFile const &)
					{
					}
				}
				NMib::NFile::CFile::fs_CreateDirectory(ServiceDir);
				NMib::NFile::CFile::fs_CopyFile(SourceFile, DestFile);
				auto Attribs = NMib::NFile::CFile::fs_GetAttributes(ServiceDir);
				NMib::NFile::CFile::fs_SetAttributes
					(
						ServiceDir
						, Attribs 
			 			| NMib::NFile::EFileAttrib_UserExecute
						| NMib::NFile::EFileAttrib_UserRead
						| NMib::NFile::EFileAttrib_UserWrite
						| NMib::NFile::EFileAttrib_GroupExecute
						| NMib::NFile::EFileAttrib_GroupRead
						| NMib::NFile::EFileAttrib_GroupWrite
						| NMib::NFile::EFileAttrib_EveryoneExecute
						| NMib::NFile::EFileAttrib_EveryoneRead
						| NMib::NFile::EFileAttrib_EveryoneWrite
						| NMib::NFile::EFileAttrib_UnixAttributesValid
					)
				;
			}
			
			return ServiceDir;
		}

		NMib::NStr::CStr f_GetServicePath()
		{
																																						 
			return f_GetServiceDir() + "/" + NMib::NFile::CFile::fs_GetFile(NMib::NFile::CFile::fs_GetProgramPath()).f_Replace("Test_Malterlib_Daemon", "Test_Malterlib_Helper_Daemon");
		}
		
		void f_CleanupFiles()
		{
			for (auto &Dir : m_CreatedDirs)
			{
				try
				{
					NMib::NFile::CFile::fs_DeleteDirectoryRecursive(Dir);
				}
				catch (NMib::NFile::CExceptionFile const &)
				{
				}
				
			}
		}
		
		void f_LaunchServiceProcess
			(
				NMib::NStr::CStr const &_CommandLine
				, bool _bElevate
				, uint32 _ExpectedExitCode
				, bool _bRunTest
				, bool _bFailAndStop
				, bool _bExpectNoError
				, bool _bExpectFail
			)
		{
			
			EExitResult Exited = EExitResult_None;
			uint32 ExitCode = 66;
			NMib::NProcess::CProcessLaunchParams Params;
		
			Params.m_Target = f_GetServicePath();
			Params.m_Parameters = _CommandLine;

			Params.m_bShowLaunched = false;
			Params.m_Elevation = NMib::NProcess::EProcessLaunchElevation_None;
			Params.m_bStdOutPID = true;
			Params.m_bSeparateStdErr = true;
			Params.m_bEnableStdRedirection = true;
			Params.m_Prompt = "You need to elevate to run the service tests.";
			Params.m_Environment["MalterlibDisableStdErrLog"] = "true";

			NMib::NStr::CStr Errors;
			
			Params.m_fOnStateChange
				= [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
				{
					switch (_State.f_GetTypeID())
					{
					case NMib::NProcess::EProcessLaunchState_Exited:
						{
							NMib::fg_Volatile(Exited) = EExitResult_Exited;
							NMib::fg_Volatile(ExitCode) = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();
						}
						break;
					case NMib::NProcess::EProcessLaunchState_LaunchFailed:
						{
							NMib::fg_Volatile(Exited) = EExitResult_NotLaunched;
							Errors += _State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>();
							//DMibDTrace("Error: {}\r\n", _State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>());
						}
						break;
					}
				}
			;
			NMib::NStr::CStr Output;
			Params.m_fOnOutput
				= [&](NMib::NProcess::EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
				{
					if (_OutputType == NMib::NProcess::EProcessLaunchOutputType_StdOut)
					{
						//DMibDTrace("{}\n", _Output);
						Output += _Output;
					}
					else
					{
						//DMibDTrace("Error: {}\n", _Output);
						Errors += _Output;
					}
				}
			;

			{
				if (_bElevate)
				{
					auto pLaunch = f_GetClient().f_GetFactory()(Params, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);
				}
				else
				{
					NMib::NProcess::CProcessLaunch Launcher(Params, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);
				}
			}
			if (_bRunTest)
			{
				if (_bExpectFail)
				{
					DMibTest(DMibExpr(Errors) == DMibExpr(""))(ETest_ExpectFail);
					DMibTest(DMibExpr(Exited) == DMibExpr(EExitResult_Exited))(_bFailAndStop ? ETest_FailAndStop : ETest_Fail);
					if (_ExpectedExitCode == 0)
						DMibTest(DMibExpr(ExitCode) == DMibExpr(_ExpectedExitCode))(ETest_ExpectFail);
					else
						DMibTest(DMibExpr(ExitCode) == DMibExpr(_ExpectedExitCode))(_bFailAndStop ? ETest_FailAndStop : ETest_Fail);
				}
				else if (_bFailAndStop)
				{
					if (_ExpectedExitCode == 0 || _bExpectNoError)
						DMibTest(DMibExpr(Errors) == DMibExpr(""))(ETest_FailAndStop);
					else
						DMibTest(DMibExpr(Errors) != DMibExpr(""))(ETest_FailAndStop);
					DMibTest(DMibExpr(Exited) == DMibExpr(EExitResult_Exited))(ETest_FailAndStop);
					DMibTest(DMibExpr(ExitCode) == DMibExpr(_ExpectedExitCode))(ETest_FailAndStop);
				}
				else
				{
					if (_ExpectedExitCode == 0 || _bExpectNoError)
						DMibTest(DMibExpr(Errors) == DMibExpr(""));
					else
						DMibTest(DMibExpr(Errors) != DMibExpr(""));
					DMibTest(DMibExpr(Exited) == DMibExpr(EExitResult_Exited));
					DMibTest(DMibExpr(ExitCode) == DMibExpr(_ExpectedExitCode));
				}
			}
			else
			{
				if (Exited != EExitResult_Exited)
				{
					DMibError("Failed to launch process");
				}
				if (ExitCode != _ExpectedExitCode)
				{
					DMibError("Unexpected exit code");
				}
				if (_ExpectedExitCode == 0 || _bExpectNoError)
				{
					if (!Errors.f_IsEmpty())
						DMibError(NMib::NStr::CStr::CFormat("Unexpected error output: ") << Errors);
				}
				else
				{
					if (Errors.f_IsEmpty())
						DMibError(NMib::NStr::CStr::CFormat("Expected error output: ") << Errors);
				}
			}
		}

		zbool m_bUserService;
		NMib::NStr::CStr m_RunAsUser;
		NMib::NStr::CStr m_RunAsGroup;

		void f_CheckServiceIsRunning()
		{
			NMib::NStr::CStr ServiceDir = f_GetServiceDir();
			NMib::NStr::CStr File = ServiceDir + "/Running";
			
			bool bExists = false;
			
			NMib::NThread::CEventAutoResetReportable FileChangeEvent;
			if (NMib::NFile::CFileChangeNotification::fs_Supported())
			{
				NMib::NFile::CFileChangeNotification FileChangeNotification;
				
				FileChangeNotification.f_Open(ServiceDir, NMib::NFile::EFileChange_All, &FileChangeEvent);
				
				NMib::NTime::CClock Clock;
				Clock.f_Start();
				bExists = NMib::NFile::CFile::fs_FileExists(File);
				while (!bExists)
				{
					FileChangeEvent.f_WaitTimeout(0.1f);
					bExists = NMib::NFile::CFile::fs_FileExists(File);
					if (Clock.f_GetTime() > 10.0)
						break;
				}
				
				FileChangeNotification.f_Close();
			}
			else
			{
				NMib::NTime::CClock Clock;
				Clock.f_Start();
				bool bExists = NMib::NFile::CFile::fs_FileExists(File);
				while (!bExists)
				{
					NMib::NSys::fg_Thread_Sleep(0.1f);
					bExists = NMib::NFile::CFile::fs_FileExists(File);
					if (Clock.f_GetTime() > 10.0)
						break;
				}
			}
			bExists = NMib::NFile::CFile::fs_FileExists(File);
						
			DMibTest(DMibExpr(bExists) == DMibExpr(true));
			
			if (bExists)
			{
				auto Output = NMib::NFile::CFile::fs_ReadStringFromFile(File, true);
				if (!Output.f_IsEmpty() && Output != "\n")
				{
					NMib::NStr::CStr User = NMib::NStr::fg_GetStrLineSep(Output);
					NMib::NStr::CStr Group = NMib::NStr::fg_GetStrLineSep(Output);
					NMib::NStr::CStr ProgramDir = NMib::NStr::fg_GetStrLineSep(Output);
#ifndef DPlatformFamily_Windows
					if (m_bUserService)
					{
						DMibTest(DMibExpr(User) != DMibExpr("root"));
						DMibTest(DMibExpr(Group) != DMibExpr("wheel"));
					}
					else
					{
						NMib::NStr::CStr RootGroup = "root";
						if (!m_RunAsUser.f_IsEmpty() && !m_RunAsGroup.f_IsEmpty())
						{
							DMibTest(DMibExpr(User) == DMibExpr(m_RunAsUser));
							DMibTest(DMibExpr(Group) == DMibExpr(m_RunAsGroup));
						}
						else if (!m_RunAsUser.f_IsEmpty())
						{
#ifdef DPlatformFamily_OSX
							RootGroup = "daemon";
#endif
							DMibTest(DMibExpr(User) == DMibExpr(m_RunAsUser));
							DMibTest(DMibExpr(Group) == DMibExpr(RootGroup));
						}
						else if (!m_RunAsGroup.f_IsEmpty())
						{
							DMibTest(DMibExpr(User) == DMibExpr("root"));
							DMibTest(DMibExpr(Group) == DMibExpr(m_RunAsGroup));
						}
						else
						{
#ifdef DPlatformFamily_OSX
							RootGroup = "wheel";
#endif
							DMibTest(DMibExpr(User) == DMibExpr("root"));
							DMibTest(DMibExpr(Group) == DMibExpr(RootGroup));
						}
					}
#endif					
					DMibTest(DMibExpr(ProgramDir) == DMibExpr(ServiceDir));
					
				}
			}			
		}

		void f_CheckServiceIsNotRunning()
		{
			NMib::NStr::CStr ServiceDir = f_GetServiceDir();
			
			NMib::NStr::CStr File = ServiceDir + "/Running";
			bool bExists = NMib::NFile::CFile::fs_FileExists(File);
			DMibTest(DMibExpr(bExists) == DMibExpr(false));
		}
		
		void f_LaunchService(NMib::NStr::CStr const &_ExtraParams, bint _bElevated, bool _bElevatedRun, bool _bShouldFail)
		{		
			for (int iRun = 0; iRun < 2; ++iRun)
			{
				NMib::NStr::CStr ServiceTestPath = "NoServiceNameSpecified";
				
				if (iRun == 0)
					ServiceTestPath = "ServiceNameSpecified";
				
				NMib::NStr::CStr ServiceName = m_ServiceName;
				
				if (iRun != 0)
					ServiceName = "";
				
				DMibTestPath(ServiceTestPath);
				
				{
					DMibTestPath("Cleanup");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-RemoveService {} {}") << ServiceName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				
				{
					DMibTestPath("Add");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-AddService {} {}") << m_ServiceName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				{
					DMibTestPath("AddServiceIfNotAdded");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-AddServiceIfNotAdded {} {}") << m_ServiceName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				{
					DMibTestPath("ServiceExistsAfterAdd");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-Exists {} {}") << m_ServiceName << _ExtraParams, _bElevatedRun, 0, true, false, true, _bShouldFail);
				}
				{
					DMibTestPath("Start");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-StartService {} {}") << ServiceName << _ExtraParams, _bElevatedRun, 0, true, false, false, _bShouldFail);
					if (!_bShouldFail)
						f_CheckServiceIsRunning();
				}
				{
					DMibTestPath("Stop");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-StopService {} {}") << ServiceName << _ExtraParams, _bElevatedRun, 0, true, false, false, _bShouldFail);
					f_CheckServiceIsNotRunning();
				}
				{
					DMibTestPath("Remove");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-RemoveService {} {}") << ServiceName << _ExtraParams, _bElevated, 0, true, false, false, _bShouldFail);
				}
				{
					DMibTestPath("ServiceDoesNotExistAfterRemove");
					f_LaunchServiceProcess(NMib::NStr::CStr::CFormat("-Exists {} {}") << m_ServiceName << _ExtraParams, _bElevatedRun, 1, true, false, true, _bShouldFail);
				}
			}
			
		}

		void f_TestLocalUserService(bool _bUnsupported)
		{
			DMibTestSuite("LocalUserService")
			{
				m_bUserService = true;
				auto Cleanup 
					= NMib::fg_OnScopeExit
					(
						[&]
						{
							m_bUserService = false;
						}
					)
				;
				f_LaunchService("-LocalUser", false, false, !(NMib::NService::CService::fs_SupportedFeatures() & NMib::NService::EServiceFeature_LocalUserService) || _bUnsupported);
				{
					DMibTestPath("StartServiceDoesNotExist");
					f_LaunchServiceProcess("-StartService FakeServiceDoesNotExist -LocalUser", false, 1, true, true, false, !(NMib::NService::CService::fs_SupportedFeatures() & NMib::NService::EServiceFeature_LocalUserService) || _bUnsupported);
				}
			};
		}

		void f_TestAllUsersService(NMib::NStr::CStr const &_ExtraParams, bool _bUnsupported)
		{
			DMibTestSuite(CTestCategory("AllUsersServices") << CTestGroup("Manual"))
			{
				m_bUserService = true;
				auto Cleanup 
					= NMib::fg_OnScopeExit
					(
						[&]
						{
							m_bUserService = false;
						}
					)
				;
				f_LaunchService("-AllUsers " + _ExtraParams, true, false, !(NMib::NService::CService::fs_SupportedFeatures() & NMib::NService::EServiceFeature_AllUsersService) || _bUnsupported);
			};
		}
		
		void f_TestGlobalService(NMib::NStr::CStr const &_ExtraParams, bool _bUnsupported)
		{
			DMibTestSuite(CTestCategory("GlobalService") << CTestGroup("Manual"))
			{
				f_LaunchService(_ExtraParams, true, true, !(NMib::NService::CService::fs_SupportedFeatures() & NMib::NService::EServiceFeature_GlobalService) || _bUnsupported);
			};
		}

		void f_TestCustomAction()
		{
			DMibTestSuite("CustomAction")
			{
				{
					DMibTestPath("RunCustomAction");
					f_LaunchServiceProcess("-CustomAction", false, 0, true, true, false, false);

					NMib::NStr::CStr ServiceDir = f_GetServiceDir();
					
					NMib::NStr::CStr File = ServiceDir + "/CustomAction";
					bool bCustomActionRan = NMib::NFile::CFile::fs_FileExists(File);
					DMibTest(DMibExpr(bCustomActionRan) == DMibExpr(true))(ETest_FailAndStop);

					NMib::NStr::CStr CustomActionStr = NMib::NFile::CFile::fs_ReadStringFromFile(File);
					DMibTest(DMibExpr(CustomActionStr) == DMibExpr("CustomAction"));

					if (bCustomActionRan)
						NMib::NFile::CFile::fs_DeleteFile(File);
				}
			};
		}
		
		void f_DoTestsInPath(NMib::NStr::CStr const &_Desc, NMib::NStr::CStr const &_Path, bool _bUnsupported)
		{
			DMibTestCategory(_Desc)
			{
				m_ServiceDirectory = _Path;
				
				DMibTestCategory("DefaultUser")
				{
					f_TestLocalUserService(_bUnsupported);
					f_TestAllUsersService("", _bUnsupported);
					f_TestGlobalService("", _bUnsupported);
					f_TestCustomAction();
				};
				
	#if defined(DPlatformFamily_Windows)

		#pragma message ( "TODO: Implement user/group management for Windows and enable this service test code." )

	#else
				DMibTestCategory("MalterlibTestUser")
				{
					if (NMib::NTest::fg_GroupActive("Manual"))
						f_LaunchServiceProcess("-DeleteUserAndGroup", true, 0, false, true, false, false);
					
					DMibTestCategory("User")
					{
						m_RunAsUser = "_idstestuser";
						auto Cleanup 
							= NMib::fg_OnScopeExit
							(
								[&]
								{
									m_RunAsUser.f_Clear();
								}
							)
						;
						
						f_TestAllUsersService("-RunAsUser _idstestuser", _bUnsupported);
						f_TestGlobalService("-RunAsUser _idstestuser", _bUnsupported);
					};
					DMibTestCategory("Group")
					{
						m_RunAsGroup = "_idstestgroup";
						auto Cleanup 
							= NMib::fg_OnScopeExit
							(
								[&]
								{
									m_RunAsGroup.f_Clear();
								}
							)
						;
						f_TestAllUsersService("-RunAsGroup _idstestgroup", _bUnsupported);
						f_TestGlobalService("-RunAsGroup _idstestgroup", _bUnsupported);
					};
					DMibTestCategory("GroupAndUser")
					{
						m_RunAsUser = "_idstestuser";
						m_RunAsGroup = "_idstestgroup";
						auto Cleanup 
							= NMib::fg_OnScopeExit
							(
								[&]
								{
									m_RunAsUser.f_Clear();
									m_RunAsGroup.f_Clear();
								}
							)
						;
						f_TestAllUsersService("-RunAsUser _idstestuser -RunAsGroup _idstestgroup", _bUnsupported);
						f_TestGlobalService("-RunAsUser _idstestuser -RunAsGroup _idstestgroup", _bUnsupported);
					};

					if (NMib::NTest::fg_GroupActive("Manual"))
						f_LaunchServiceProcess("-DeleteUserAndGroup", true, 0, false, true, false, false);
				};
	#endif // DPlatformFamily_Windows
			};
		}
	
		void f_DoTests()
		{
			f_DoProxyServer();
			
			f_DoTestsInPath("NormalDir", "NoSpace", false);

			NMib::NStr::CStr EvilDir = NMib::NStr::CWStr(str_utf16("'Evil' Dir 日本語 ÖÖÖ $(Bash) (Paren)"));
#ifndef DPlatformFamily_Windows
			EvilDir += " \"From hell\"";
#endif
			f_DoTestsInPath("EvilDir", EvilDir, !!(NMib::NService::CService::fs_SupportedFeatures() & (NMib::NService::EServiceFeature_EscapedPathBroken|NMib::NService::EServiceFeature_EscapeCharBroken)));
			
			NMib::NStr::CStr SlightlyLessEvilDir = NMib::NStr::CWStr(str_utf16("'Evil' Dir 日本語 ÖÖÖ (Paren)"));

			f_DoTestsInPath("SlightlyLessEvilDir", SlightlyLessEvilDir, !!(NMib::NService::CService::fs_SupportedFeatures() & NMib::NService::EServiceFeature_EscapedPathBroken));
			
			f_CleanupFiles();
		}

	};
	
	DMibTestRegister(CService_Tests, Malterlib::Daemon);
}
