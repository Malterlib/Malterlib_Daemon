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
	class CDaemon_Tests : public CTest
	{
	public:

		NMib::NStr::CStr m_DaemonName;

		CDaemon_Tests()
		{
			m_DaemonName = "Malterlib_TestDaemon_" + NMib::NCryptography::fg_GetHashedUuidString(NMib::NFile::CFile::fs_GetProgramPath(), NMib::NCryptography::CUniversallyUniqueIdentifier("{469e17ec-9536-44bc-8c8d-ab80a5dcbf13}"));
		}

		static NMib::NStr::CStr fs_GetTestPath(NMib::NStr::CStr const &_TestPath, NMib::NStr::CStr const &_Test)
		{
			if (_Test.f_IsEmpty())
				return NMib::NProcess::CProcessLaunchParams::fs_GetParams({"--test", _TestPath, "--logger", "Registry", "--filter-results", "[\"All\"]", "--process-recursive"});
			else
				return NMib::NProcess::CProcessLaunchParams::fs_GetParams({"--test", _TestPath / _Test, "--logger", "Registry", "--filter-results", "[\"All\"]", "--process-recursive"});
		}

		NMib::NThread::CMutual m_ProxyClientLock;
		NMib::NStorage::TCUniquePointer<NMib::NProcess::CProxiedLaunchClient> m_pProxyClient;

		NMib::NStr::CStr m_DaemonDirectory;

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

			Params.m_Parameters = "--test Malterlib/Daemon/Daemon/ProxyServer --logger Null --process-recursive";
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

		NMib::NStr::CStr f_GetDaemonDir()
		{
			NMib::NStr::CStr ProgramDirectory = NMib::NFile::CFile::fs_GetProgramDirectory();
			NMib::NStr::CStr DaemonDir = ProgramDirectory + "/" + m_DaemonDirectory;

			auto Mapped = m_CreatedDirs(DaemonDir);
			if (Mapped.f_WasCreated())
			{
				NMib::NStr::CStr DaemonFile = NMib::NFile::CFile::fs_GetFile(NMib::NFile::CFile::fs_GetProgramPath()).f_Replace("Test_Malterlib_Daemon", "Test_Malterlib_Helper_Daemon");
				NMib::NStr::CStr SourceFile = ProgramDirectory + "/" + DaemonFile;
				NMib::NStr::CStr DestFile = DaemonDir + "/" + DaemonFile;
				for (mint i = 0; i < 5; ++i)
				{
					try
					{
						if (NMib::NFile::CFile::fs_FileExists(DaemonDir))
							NMib::NFile::CFile::fs_DeleteDirectoryRecursive(DaemonDir);
						break;
					}
					catch (NMib::NFile::CExceptionFile const &)
					{
					}
				}

				NMib::NFile::CFile::fs_CreateDirectory(DaemonDir);
#if defined(DPlatformFamily_macOS) || defined(DPlatformFamily_Linux)
				if (NMib::NFile::CFile::fs_FileExists(ProgramDirectory / "MalterlibHelper"))
					NMib::NFile::CFile::fs_CopyFile(ProgramDirectory / "MalterlibHelper", DaemonDir / "MalterlibHelper");
#endif
#if defined(DMibLLVMSanitizerRuntime)
				if (NMib::NFile::CFile::fs_FileExists(ProgramDirectory / DMibLLVMSanitizerRuntime))
					NMib::NFile::CFile::fs_CopyFile(ProgramDirectory / DMibLLVMSanitizerRuntime, DaemonDir / DMibLLVMSanitizerRuntime);
#endif

#ifdef DPlatformFamily_macOS
				if (NMib::NFile::CFile::fs_FileExists(ProgramDirectory / "MalterlibOverrideMalloc.dylib"))
					NMib::NFile::CFile::fs_CopyFile(ProgramDirectory / "MalterlibOverrideMalloc.dylib", DestFile);
#endif
				NMib::NFile::CFile::fs_CopyFile(SourceFile, DestFile);
				auto Attribs = NMib::NFile::CFile::fs_GetAttributes(DaemonDir);
				NMib::NFile::CFile::fs_SetAttributes
					(
						DaemonDir
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

			return DaemonDir;
		}

		NMib::NStr::CStr f_GetDaemonPath()
		{
			return f_GetDaemonDir() + "/" + NMib::NFile::CFile::fs_GetFile(NMib::NFile::CFile::fs_GetProgramPath()).f_Replace("Test_Malterlib_Daemon", "Test_Malterlib_Helper_Daemon");
		}

		void f_CleanupFiles()
		{
			for (auto &Dir : m_CreatedDirs)
			{
				for (mint i = 0; i < 5; ++i)
				{
					try
					{
						if (NMib::NFile::CFile::fs_FileExists(Dir))
							NMib::NFile::CFile::fs_DeleteDirectoryRecursive(Dir);
						break;
					}
					catch (NMib::NFile::CExceptionFile const &)
					{
					}
				}
			}
		}

		void f_LaunchDaemonProcess
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

			Params.m_Target = f_GetDaemonPath();
			Params.m_Parameters = _CommandLine;

			Params.m_bShowLaunched = false;
			Params.m_Elevation = NMib::NProcess::EProcessLaunchElevation_None;
			Params.m_bStdOutPID = true;
			Params.m_bSeparateStdErr = true;
			Params.m_bEnableStdRedirection = true;
			Params.m_Prompt = "You need to elevate to run the daemon tests.";
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
					case NMib::NProcess::EProcessLaunchState_Launched:
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

		bool m_bUserDaemon = false;
		NMib::NStr::CStr m_RunAsUser;
		NMib::NStr::CStr m_RunAsGroup;

		void f_CheckDaemonIsRunning()
		{
			NMib::NStr::CStr DaemonDir = f_GetDaemonDir();
			NMib::NStr::CStr File = DaemonDir + "/Running";

			bool bExists = false;
			bool bTimedOut = false;

			NMib::NThread::CEventAutoReset FileChangeEvent;
			if (NMib::NFile::CFileChangeNotification::fs_Supported())
			{
				NMib::NFile::CFileChangeNotification FileChangeNotification;

				FileChangeNotification.f_Open(DaemonDir, NMib::NFile::EFileChange_All, &FileChangeEvent);

				NMib::NTime::CClock Clock;
				Clock.f_Start();
				bExists = NMib::NFile::CFile::fs_FileExists(File);
				while (!bExists)
				{
					FileChangeEvent.f_WaitTimeout(0.1f);
					bExists = NMib::NFile::CFile::fs_FileExists(File);
					if (Clock.f_GetTime() > 120.0)
					{
						bTimedOut = true;
						break;
					}
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
					if (Clock.f_GetTime() > 120.0)
					{
						bTimedOut = true;
						break;
					}
				}
			}
			bExists = NMib::NFile::CFile::fs_FileExists(File);

			DMibExpectFalse(bTimedOut);

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
					if (m_bUserDaemon)
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
#ifdef DPlatformFamily_macOS
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
#ifdef DPlatformFamily_macOS
							RootGroup = "wheel";
#endif
							DMibTest(DMibExpr(User) == DMibExpr("root"));
							DMibTest(DMibExpr(Group) == DMibExpr(RootGroup));
						}
					}
#endif
					DMibTest(DMibExpr(ProgramDir) == DMibExpr(DaemonDir));

				}
			}
		}

		void f_CheckDaemonIsNotRunning()
		{
			NMib::NStr::CStr DaemonDir = f_GetDaemonDir();

			NMib::NStr::CStr File = DaemonDir + "/Running";
			bool bExists = NMib::NFile::CFile::fs_FileExists(File);
			DMibTest(DMibExpr(bExists) == DMibExpr(false));
		}

		void f_LaunchDaemon(NMib::NStr::CStr const &_ExtraParams, bool _bElevated, bool _bElevatedRun, bool _bShouldFail)
		{
			for (int iRun = 0; iRun < 2; ++iRun)
			{
				NMib::NStr::CStr DaemonTestPath = "NoDaemonNameSpecified";

				if (iRun == 0)
					DaemonTestPath = "DaemonNameSpecified";

				NMib::NStr::CStr DaemonName = m_DaemonName;

				if (iRun != 0)
					DaemonName = "";

				DMibTestPath(DaemonTestPath);

				{
					DMibTestPath("Cleanup");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-RemoveService {} {}") << DaemonName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				{
					DMibTestPath("Add");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-AddService {} {}") << m_DaemonName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				{
					DMibTestPath("AddDaemonIfNotAdded");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-AddServiceIfNotAdded {} {}") << m_DaemonName << _ExtraParams, _bElevated, 0, true, true, false, _bShouldFail);
				}
				{
					DMibTestPath("DaemonExistsAfterAdd");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-Exists {} {}") << m_DaemonName << _ExtraParams, _bElevatedRun, 0, true, false, true, _bShouldFail);
				}
				{
					DMibTestPath("Start");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-StartService {} {}") << DaemonName << _ExtraParams, _bElevatedRun, 0, true, false, false, _bShouldFail);
					if (!_bShouldFail)
						f_CheckDaemonIsRunning();
				}
				{
					DMibTestPath("Stop");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-StopService {} {}") << DaemonName << _ExtraParams, _bElevatedRun, 0, true, false, false, _bShouldFail);
					f_CheckDaemonIsNotRunning();
				}
				{
					DMibTestPath("Remove");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-RemoveService {} {}") << DaemonName << _ExtraParams, _bElevated, 0, true, false, false, _bShouldFail);
				}
				{
					DMibTestPath("DaemonDoesNotExistAfterRemove");
					f_LaunchDaemonProcess(NMib::NStr::CStr::CFormat("-Exists {} {}") << m_DaemonName << _ExtraParams, _bElevatedRun, 1, true, false, true, _bShouldFail);
				}
			}

		}

		void f_TestLocalUserDaemon(bool _bUnsupported)
		{
			{
				DMibTestPath("LocalUserDaemon");
				m_bUserDaemon = true;
				auto Cleanup
					= NMib::fg_OnScopeExit
					(
						[&]
						{
							m_bUserDaemon = false;
						}
					)
				;
				f_LaunchDaemon("-LocalUser", false, false, !(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & NMib::NDaemon::EDaemonFeature_LocalUserDaemon) || _bUnsupported);
				{
					DMibTestPath("StartDaemonDoesNotExist");
					f_LaunchDaemonProcess("-StartService FakeDaemonDoesNotExist -LocalUser", false, 1, true, true, false, !(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & NMib::NDaemon::EDaemonFeature_LocalUserDaemon) || _bUnsupported);
				}
			};
		}

		void f_TestAllUsersDaemon(NMib::NStr::CStr const &_ExtraParams, bool _bUnsupported)
		{
			if (!NMib::NTest::fg_GroupActive("Manual"))
				return;
			{
				DMibTestPath("AllUsersDaemons");
				m_bUserDaemon = true;
				auto Cleanup
					= NMib::fg_OnScopeExit
					(
						[&]
						{
							m_bUserDaemon = false;
						}
					)
				;
				f_LaunchDaemon("-AllUsers " + _ExtraParams, true, false, !(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & NMib::NDaemon::EDaemonFeature_AllUsersDaemon) || _bUnsupported);
			};
		}

		void f_TestGlobalDaemon(NMib::NStr::CStr const &_ExtraParams, bool _bUnsupported)
		{
			if (!NMib::NTest::fg_GroupActive("Manual"))
				return;
			{
				DMibTestPath("GlobalDaemon");
				f_LaunchDaemon(_ExtraParams, true, true, !(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & NMib::NDaemon::EDaemonFeature_GlobalDaemon) || _bUnsupported);
			}
		}

		void f_TestCustomAction()
		{
			DMibTestPath("CustomAction");
			f_LaunchDaemonProcess("-CustomAction", false, 0, true, true, false, false);

			NMib::NStr::CStr DaemonDir = f_GetDaemonDir();

			NMib::NStr::CStr File = DaemonDir + "/CustomAction";
			bool bCustomActionRan = NMib::NFile::CFile::fs_FileExists(File);
			DMibTest(DMibExpr(bCustomActionRan) == DMibExpr(true))(ETest_FailAndStop);

			NMib::NStr::CStr CustomActionStr = NMib::NFile::CFile::fs_ReadStringFromFile(File);
			DMibTest(DMibExpr(CustomActionStr) == DMibExpr("CustomAction"));

			if (bCustomActionRan)
				NMib::NFile::CFile::fs_DeleteFile(File);
		}

		void f_DoTestsInPath(NMib::NStr::CStr const &_Desc, NMib::NStr::CStr const &_Path, bool _bUnsupported)
		{
			DMibTestPath(_Desc);
			m_DaemonDirectory = _Path;

			{
				DMibTestPath("DefaultUser");
				f_TestLocalUserDaemon(_bUnsupported);
				f_TestAllUsersDaemon("", _bUnsupported);
				f_TestGlobalDaemon("", _bUnsupported);
				f_TestCustomAction();
			}
#if defined(DPlatformFamily_Windows)

	#pragma message ( "TODO: Implement user/group management for Windows and enable this daemon test code." )

#else
			{
				DMibTestPath("MalterlibTestUser");
				if (NMib::NTest::fg_GroupActive("Manual"))
					f_LaunchDaemonProcess("-DeleteUserAndGroup", true, 0, false, true, false, false);
				{
					DMibTestPath("User");
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

					f_TestAllUsersDaemon("-RunAsUser _idstestuser", _bUnsupported);
					f_TestGlobalDaemon("-RunAsUser _idstestuser", _bUnsupported);
				}
				{
					DMibTestPath("Group");
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
					f_TestAllUsersDaemon("-RunAsGroup _idstestgroup", _bUnsupported);
					f_TestGlobalDaemon("-RunAsGroup _idstestgroup", _bUnsupported);
				}
				{
					DMibTestPath("GroupAndUser");
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
					f_TestAllUsersDaemon("-RunAsUser _idstestuser -RunAsGroup _idstestgroup", _bUnsupported);
					f_TestGlobalDaemon("-RunAsUser _idstestuser -RunAsGroup _idstestgroup", _bUnsupported);
				}

				if (NMib::NTest::fg_GroupActive("Manual"))
					f_LaunchDaemonProcess("-DeleteUserAndGroup", true, 0, false, true, false, false);
			};
#endif // DPlatformFamily_Windows
		}

		void f_DoTests()
		{
			f_DoProxyServer();

			DMibTestSuite("Tests")
			{
				f_DoTestsInPath("NormalDir", "NoSpace", false);

				NMib::NStr::CStr EvilDir = NMib::NStr::CWStr(str_utf16("'Evil' Dir 日本語 ÖÖÖ $(Bash) (Paren)"));
	#ifndef DPlatformFamily_Windows
				EvilDir += " \"From hell\"";
	#endif
				f_DoTestsInPath("EvilDir", EvilDir, !!(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & (NMib::NDaemon::EDaemonFeature_EscapedPathBroken|NMib::NDaemon::EDaemonFeature_EscapeCharBroken)));

				NMib::NStr::CStr SlightlyLessEvilDir = NMib::NStr::CWStr(str_utf16("'Evil' Dir 日本語 ÖÖÖ (Paren)"));

				f_DoTestsInPath("SlightlyLessEvilDir", SlightlyLessEvilDir, !!(NMib::NDaemon::CDaemon::fs_SupportedFeatures() & NMib::NDaemon::EDaemonFeature_EscapedPathBroken));

				f_CleanupFiles();
			};
		}
	};

	DMibTestRegister(CDaemon_Tests, Malterlib::Daemon);
}
