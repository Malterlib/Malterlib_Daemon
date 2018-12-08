// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/signal.h>
#if DPlatformVersionMax >= 1060
	#include <ServiceManagement/ServiceManagement.h>
#endif
#include <Mib/Process/ProcessLaunch>

namespace NMib::NDaemon
{
	void fg_RunDaemonStatusApp(NFunction::TCFunction<void ()> const& _fPause, NFunction::TCFunction<void ()> const& _fResume, NStr::CStr const &_DaemonName, NContainer::CByteVector const& _IconData);
	void fg_CancelRunDaemonStatusApp();

	NStr::CStr CDaemon::fs_GetUniquePrefix()
	{
#ifdef DProductCompanyUniqueIdentifier
		return DMibStringize(DProductCompanyUniqueIdentifier);
#else
		return "com.malterlib";
#endif
	}

	class CDaemon::CDetails
	{

		bool fp_CheckParamsSupported() const
		{
			auto &Params = mp_pOwner->mp_Params;
			bool bRet = true;
			if (!Params.f_GetDaemonGroup().f_IsEmpty())
			{
				mp_pOwner->f_ReportError("Daemon groups are not supported on this platform");
				bRet = false;
			}
			if (Params.f_GetInteractive())
			{
				mp_pOwner->f_ReportError("Interactive services are not supported on this platform");
				bRet = false;
			}

			if (!Params.f_GetDaemonDependencies().f_IsEmpty())
			{
				mp_pOwner->f_ReportError("Daemon dependencies are not supported on this platform");
				bRet = false;
			}

			return bRet;
		}

	public:

		CDetails(CDaemon* _pOwner)
			: mp_pOwner(_pOwner)
		{
			if (msp_pThis)
				DMibError("You cannot have two services running in the same process at once");
			else
				msp_pThis = this;
		}

		~CDetails()
		{
			msp_pThis = nullptr;
		}

		static NStr::CStr fs_GetFullDaemonName(CDaemonParams const &_Params)
		{
			return NStr::CStr::CFormat(CDaemon::fs_GetUniquePrefix() + ".{}") << _Params.f_GetDaemonName();
		}


		static NStr::CStr fs_GetPlistFilename(CDaemonParams const &_Params)
		{
			return fs_GetFullDaemonName(_Params) + ".plist";
		}

		static NStr::CStr fs_GetPlistDirectory(EDaemonMode _Mode)
		{
			if (_Mode == EDaemonMode_LocalUser)
				return NStr::CStr::CFormat("{}/Library/LaunchAgents") << NSys::NFile::fg_GetUserHomeDirectory();
			else if (_Mode == EDaemonMode_AllUsers)
				return NStr::CStr("/Library/LaunchAgents");
			else
				return NStr::CStr("/Library/LaunchDaemons");
		}

		static NStr::CStr fs_GetPlistPath(CDaemonParams const &_Params)
		{
			NStr::CStr LaunchFileDirectory = fs_GetPlistDirectory(_Params.f_GetDaemonMode());
			return NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetPlistFilename(_Params);
		}

		static int fs_SystemCall(NStr::CStr const &_Target, NStr::CStr const &_Parameters, NStr::CStr *_pResult = nullptr, NStr::CStr *_pError = nullptr)
		{
			uint32 ExitCode = 66;

			NMib::NProcess::CProcessLaunchParams Params;
			Params.m_Target = _Target;
			Params.m_Parameters = _Parameters;

			Params.m_bShowLaunched = false;
			Params.m_bStdOutPID = true;

			Params.m_bMakeEffectiveUserReal = true;
			Params.m_bMakeEffectiveGroupReal = true;

			Params.m_fOnStateChange
				= [&](NMib::NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceStart)
				{
					switch (_State.f_GetTypeID())
					{
					case NMib::NProcess::EProcessLaunchState_Exited:
						{
							NMib::fg_Volatile(ExitCode) = _State.f_Get<NMib::NProcess::EProcessLaunchState_Exited>();
						}
						break;
					case NMib::NProcess::EProcessLaunchState_LaunchFailed:
						{
							*_pError += _State.f_Get<NMib::NProcess::EProcessLaunchState_LaunchFailed>();
							*_pError += "\n";
						}
						break;
					}
				}
			;

			Params.m_fOnOutput
				= [&](NMib::NProcess::EProcessLaunchOutputType _OutputType, NMib::NStr::CStr const &_Output)
				{
					if (_pResult && _OutputType == NMib::NProcess::EProcessLaunchOutputType_StdOut)
						*_pResult += _Output;
					else
						*_pError += _Output;
				}
			;

			{
				if (_pResult)
					_pResult->f_Clear();
				if (_pError)
					_pError->f_Clear();
				NMib::NProcess::CProcessLaunch Launcher(Params, NMib::NProcess::EProcessLaunchCloseFlag_BlockOnExit);
			}

			return ExitCode;
		}

		static int fg_GetHighestSecondColumnValue(NStr::CStr &_Data)
		{
			int Value = 0;
			while (!_Data.f_IsEmpty())
			{
				int NewLinePos = _Data.f_FindReverse("\n");
				if (NewLinePos == -1)
					break;
				_Data = _Data.f_Left(NewLinePos);
				int SpacePos = _Data.f_FindReverse(" ");
				NStr::CStr ValueString = _Data.f_Right(_Data.f_GetLen() - SpacePos - 1);
				int NewValue = ValueString.f_ToInt();
				if (NewValue > Value)
					Value = NewValue;
			}
			return Value;
		}

		bool f_PrepareUserAndGroup(CDaemonParams const &_Params)
		{
			NStr::CStr StdOut, StdErr;
			int GID, UID;

			NStr::CStr GroupName = _Params.f_GetRunAsGroup();
			NStr::CStr UserName = _Params.f_GetRunAsUser();

			// Create group if not exists
			if (GroupName.f_IsEmpty())
				GroupName = "daemon";

			if (UserName.f_IsEmpty())
				UserName = "daemon";

			NStr::CStr ReturnGID;

			try
			{
				if (!NSys::fg_UserManagement_GroupExists(GroupName, ReturnGID))
					NSys::fg_UserManagement_CreateGroup(GroupName, ReturnGID);
			}
			catch (NMib::NException::CException &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Exception when creating group named {} error {}") << GroupName << _Exception.f_GetErrorStr());

				return false;
			}

			GID = ReturnGID.f_ToInt(-1);

			if (GID == -1)
			{
				f_ReportError(NStr::CStr::CFormat("Group {} is invalid") << GroupName);
				return false;
			}

			// Create user if not exists

			NStr::CStr ReturnUID;

			try
			{
				if (!NSys::fg_UserManagement_UserExists(UserName, ReturnUID))
					NSys::fg_UserManagement_CreateUser(GroupName, UserName, "", UserName, NFile::CFile::fs_GetProgramDirectory(), ReturnUID, NSys::EUserManagementCreateUserFlag_None);
			}
			catch (NMib::NException::CException &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Unable to create user named {} error {}") << UserName << _Exception.f_GetErrorStr());
				return false;
			}

			UID = ReturnUID.f_ToInt(-1);

			if (UID == -1)
			{
				f_ReportError(NStr::CStr::CFormat("User {} is invalid") << UserName);
				return false;
			}

			try
			{
				NFile::CFile::fs_SetOwner(NFile::CFile::fs_GetProgramDirectory(), UserName);
				NFile::CFile::fs_SetGroup(NFile::CFile::fs_GetProgramDirectory(), GroupName);
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				mp_pOwner->f_ReportError(NStr::CStr::CFormat("Failed to set permissions on daemon directory: {}") << _Exception.f_GetErrorStr());
				if (NMib::NProcess::NPlatform::fg_Process_GetElevation() == NMib::NProcess::EProcessElevation_IsNotElevated)
					mp_pOwner->f_ReportError("Perhaps you need to use sudo?");
				return false;
			}

			return true;
		}

		static void fs_SigTermHandler(int const sigid)
		{
			msp_pThis->mp_InterruptedEvent.f_SetSignaled();
		}

		EActionResult f_Run()
		{
			mp_InterruptedEvent.f_ResetSignaled();

			auto pSigterm = signal(SIGTERM, (sig_t)fs_SigTermHandler);
			auto pSigint = signal(SIGINT, (sig_t)fs_SigTermHandler);

			auto Cleanup
				= g_OnScopeExit > [&]
				{
					signal(SIGTERM, pSigterm);
					signal(SIGINT, pSigint);
				}
			;

			fp_DaemonCreate();

			mp_InterruptedEvent.f_Wait();

			fp_DaemonDestroy();

			return EActionResult_Success;
		}

		static void fs_CancelDaemonStatusHandler(int const sigid)
		{
			fg_CancelRunDaemonStatusApp();
		}

		EActionResult f_RunAsProgram(bool _bDebug)
		{
			if (!_bDebug)
				return f_Run();

			auto pSigterm = signal(SIGTERM, (sig_t)fs_CancelDaemonStatusHandler);
			auto pSigint = signal(SIGINT, (sig_t)fs_CancelDaemonStatusHandler);

			auto Cleanup
				= g_OnScopeExit > [&]
				{
					signal(SIGTERM, pSigterm);
					signal(SIGINT, pSigint);
				}
			;

			fp_DaemonCreate();

			NContainer::CByteVector IconData;
			if (mp_pOwner->mp_Params.f_GetNativeHandle())
			{
				NContainer::CByteVector* pIconData = static_cast<NContainer::CByteVector*>(mp_pOwner->mp_Params.f_GetNativeHandle());
				if (pIconData)
					IconData = *pIconData;
			}

			fg_RunDaemonStatusApp
				(
					[&]()
					{
						fp_DaemonPause();
					}
					, [&]()
					{
						fp_DaemonResume();
					}
					, mp_pOwner->mp_Params.f_GetDaemonDisplayName()
					, IconData
				 )
			;

			fp_DaemonDestroy();

			return EActionResult_Success;
		}

		EActionResult f_Start()
		{
			if (!fp_CheckParamsSupported())
				return EActionResult_Failure;
			bool bResult = false;
			NStr::CStr Result;
			NStr::CStr Error;

			NStr::CStr LaunchFilePath = fs_GetPlistPath(mp_pOwner->mp_Params);
			try
			{
				if (!NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
				{
					f_ReportError(NStr::CStr::CFormat("Daemon file does not exist: {}") << fs_GetPlistPath(mp_pOwner->mp_Params));
					return EActionResult_Failure;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to check for existing daemon file: {}") << _Exception.f_GetErrorStr());
				return EActionResult_Failure;
			}

			{
				NStr::CStr ListResult;
				NStr::CStr ListError;

				NStr::CStr Command = NStr::CStr::CFormat("list {}") << fs_GetFullDaemonName(mp_pOwner->mp_Params);
				fs_SystemCall("/bin/launchctl", Command, &ListResult, &ListError);

				if (ListResult.f_Find("PID") != -1)
				{
					// The daemon is already loaded
					return EActionResult_Success;
				}
			}

			// .plist is set to automatically run when loaded
			NStr::CStr Command = NStr::CStr::CFormat("load {}") << LaunchFilePath;
			bResult = fs_SystemCall("/bin/launchctl", Command, &Result, &Error) == 0;

			if (!Error.f_IsEmpty())
			{
				f_ReportError(Error.f_Trim());
				return EActionResult_Failure;
			}
			else if (!bResult)
			{
				f_ReportError("Unknown error starting daemon");
				return EActionResult_Failure;
			}

			return EActionResult_Success;
		}

		EActionResult f_Stop(bint _bWait)
		{
			if (!fp_CheckParamsSupported())
				return EActionResult_Failure;
			if (mp_pOwner->mp_Params.f_GetKeepRunning())
				return EActionResult_Success;

			bool bResult = false;
			NStr::CStr Result;
			NStr::CStr Error;

			uint32 Pid = 0;

			{
				NStr::CStr ListResult;
				NStr::CStr ListError;

				NStr::CStr DaemonName = fs_GetFullDaemonName(mp_pOwner->mp_Params);

				NStr::CStr Command = NStr::CStr::CFormat("list {}") << DaemonName;
				fs_SystemCall("/bin/launchctl", Command, &ListResult, &ListError);

				NStr::CStr LoadedLabel = NStr::CStr::CFormat("\"Label\" = \"{}\";") << DaemonName;

				bool bFoundLabel = false;

				for (auto &Line : ListResult.f_SplitLine())
				{
					auto TrimmedLine = Line.f_Trim();
					if (TrimmedLine == LoadedLabel)
						bFoundLabel = true;

					(NStr::CStr::CParse("\"PID\" = {};") >> Pid).f_Parse(TrimmedLine);
				}

				if (Pid == 0 && !bFoundLabel)
				{
					// The daemon is not loaded
					return EActionResult_Success;
				}
			}

			// unload command automatically stops the daemon
			NStr::CStr Command = NStr::CStr::CFormat("unload {}") << fs_GetPlistPath(mp_pOwner->mp_Params);
			bResult = fs_SystemCall("/bin/launchctl", Command, &Result, &Error) == 0;

			if (!Error.f_IsEmpty())
			{
				f_ReportError(Error.f_Trim());
				return EActionResult_Failure;
			}

			return EActionResult_Success;
		}

		EActionResult f_Restart(bint _bWait)
		{
			EActionResult Result = f_Stop(_bWait);
			if (Result != EActionResult_Success)
				return Result;
			return f_Start();
		}

		void fg_SetDictionaryValue(CFMutableDictionaryRef &_Dict, NStr::CStr const &_Key, int _Value)
		{
			CFStringRef KeyStringRef = CFStringCreateWithCString(NULL, _Key.f_GetStr(), kCFStringEncodingUTF8);
			CFNumberRef ValueIntegerRef = CFNumberCreate(NULL, kCFNumberIntType, &_Value);
			CFDictionarySetValue(_Dict, KeyStringRef, ValueIntegerRef);
			CFRelease(KeyStringRef);
			CFRelease(ValueIntegerRef);
		}

		void fg_SetDictionaryValue(CFMutableDictionaryRef &_Dict, NStr::CStr const &_Key, NStr::CStr const &_Value)
		{
			CFStringRef KeyStringRef = CFStringCreateWithCString(NULL, _Key.f_GetStr(), kCFStringEncodingUTF8);
			CFStringRef ValueStringRef = CFStringCreateWithCString(NULL, _Value.f_GetStr(), kCFStringEncodingUTF8);
			CFDictionarySetValue(_Dict, KeyStringRef, ValueStringRef);
			CFRelease(KeyStringRef);
			CFRelease(ValueStringRef);
		}

		void fg_SetDictionaryValue(CFMutableDictionaryRef &_Dict, NStr::CStr const &_Key, NContainer::TCVector<NStr::CStr> const &_Values)
		{
			CFStringRef KeyStringRef = CFStringCreateWithCString(NULL, _Key.f_GetStr(), kCFStringEncodingUTF8);

			CFMutableArrayRef ArrayRef = CFArrayCreateMutable(NULL, _Values.f_GetLen(), &kCFTypeArrayCallBacks);

			for (NStr::CStr const &Value : _Values)
			{
				CFStringRef ValueStringRef = CFStringCreateWithCString(NULL, Value.f_GetStr(), kCFStringEncodingUTF8);
				CFArrayAppendValue(ArrayRef, ValueStringRef);
				CFRelease(ValueStringRef);
			}

			CFDictionarySetValue(_Dict, KeyStringRef, ArrayRef);
			CFRelease(KeyStringRef);
			CFRelease(ArrayRef);
		}

		CFDictionaryRef fg_CreateDictionaryFromParams(CDaemonParams const &_Params, bint _bDisabled = false)
		{
			CFMutableDictionaryRef Dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0, &kCFTypeDictionaryKeyCallBacks,	&kCFTypeDictionaryValueCallBacks);


			fg_SetDictionaryValue(Dict, "Label", NStr::CStr::CFormat(CDaemon::fs_GetUniquePrefix() + ".{}") << _Params.f_GetDaemonName());
			NContainer::TCVector<NStr::CStr> ProgramArguments;
			ProgramArguments.f_Insert(_Params.f_GetExecutablePath());		// We write executable as arguments as Launchd seems to be broken in this regard, it has to have executable as argument or none will be sent
			ProgramArguments.f_Insert("-Service");
			if (_Params.f_GetDisableWriteDaemon())
				ProgramArguments.f_Insert(_Params.f_GetDaemonName());
			fg_SetDictionaryValue(Dict, "ProgramArguments", ProgramArguments);
			CFDictionarySetValue(Dict, CFSTR("KeepAlive"), kCFBooleanTrue);
			CFDictionarySetValue(Dict, CFSTR("RunAtLoad"), kCFBooleanTrue);
//				fg_SetDictionaryValue(Dict, "ExitTimeOut", 0);	// Disable timeout, this seems to have broken on Yosemite
			fg_SetDictionaryValue(Dict, "ExitTimeOut", 72*3600);	// Three day timeout

			if (!_Params.f_GetRunAsUser().f_IsEmpty())
				fg_SetDictionaryValue(Dict, "UserName", _Params.f_GetRunAsUser());

			if (!_Params.f_GetRunAsGroup().f_IsEmpty())
				fg_SetDictionaryValue(Dict, "GroupName", _Params.f_GetRunAsGroup());

			if (_bDisabled)
				CFDictionarySetValue(Dict, CFSTR("Disabled"), kCFBooleanTrue);

			return Dict;
		}



		EActionResult f_Add(bint _bCheckForExisting)
		{
			if (!fp_CheckParamsSupported())
				return EActionResult_Failure;
			NStr::CStr LaunchFileDirectory = fs_GetPlistDirectory(mp_pOwner->mp_Params.f_GetDaemonMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetPlistFilename(mp_pOwner->mp_Params);

			if (_bCheckForExisting)
			{
				try
				{
					if (NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
					{
						EActionResult DaemonNameInUse = EActionResult_Failure;

						NContainer::CByteVector lData = NFile::CFile::fs_ReadFile(LaunchFilePath);
						CFDataRef XmlData = CFDataCreate(NULL, lData.f_GetArray(), lData.f_GetLen());
						DMibDeprecatedSupressStart
						CFPropertyListRef PropertyList = CFPropertyListCreateFromXMLData(NULL, XmlData, kCFPropertyListImmutable, NULL);
						DMibDeprecatedSupressStop
						CFRelease(XmlData);

						if (PropertyList)
						{
							CFArrayRef pArguments = (CFArrayRef)CFDictionaryGetValue((CFDictionaryRef)PropertyList, CFSTR("ProgramArguments"));
							if (pArguments)
							{
								CFStringRef pExec = (CFStringRef)CFArrayGetValueAtIndex(pArguments, 0);
								if (pExec)
								{
									CFStringRef pExecPath = CFStringCreateWithCString(NULL, mp_pOwner->mp_Params.f_GetExecutablePath().f_GetStr(), kCFStringEncodingUTF8);

									if (CFStringCompare(pExecPath, pExec, kCFCompareCaseInsensitive) == kCFCompareEqualTo)
									{
										DaemonNameInUse = EActionResult_Success;
									}
									else
									{
										f_ReportError(NStr::CStr::CFormat("Daemon name is already in use: {}") << LaunchFilePath);
									}

									CFRelease(pExecPath);
								}
								else
								{
									f_ReportError(NStr::CStr::CFormat("Invalid daemon file: {}") << LaunchFilePath);
								}
							}
							else
							{
								f_ReportError(NStr::CStr::CFormat("Invalid daemon file: {}") << LaunchFilePath);
							}

							CFRelease(PropertyList);
						}
						else
						{
							f_ReportError(NStr::CStr::CFormat("Invalid daemon file: {}") << LaunchFilePath);
						}

						if (DaemonNameInUse == EActionResult_Failure)
							return EActionResult_Failure;

					}
				}
				catch (NFile::CExceptionFile const &_Exception)
				{
					f_ReportError(NStr::CStr::CFormat("Failed to check for existing file: {}") << _Exception.f_GetErrorStr());
					return EActionResult_Failure;
				}
			}

			CFPropertyListRef PropertyList = fg_CreateDictionaryFromParams(mp_pOwner->mp_Params);
			DMibDeprecatedSupressStart
			CFDataRef XmlData = CFPropertyListCreateXMLData(NULL, PropertyList);
			DMibDeprecatedSupressStop
			NContainer::CByteVector Data;
			Data.f_Insert((uint8 const *)CFDataGetBytePtr(XmlData), CFDataGetLength(XmlData));

			CFRelease(PropertyList);
			CFRelease(XmlData);

			if ((!mp_pOwner->mp_Params.f_GetRunAsUser().f_IsEmpty() || !mp_pOwner->mp_Params.f_GetRunAsGroup().f_IsEmpty()) && !f_PrepareUserAndGroup(mp_pOwner->mp_Params))
				return EActionResult_Failure;

			try
			{
				NStr::CStr Error;
				if (!mp_pOwner->mp_Params.f_GetDisableWriteDaemon() && !mp_pOwner->mp_Params.f_WriteDaemonModeFile(Error))
					f_ReportError(NStr::CStr::CFormat("Failed to write daemon mode file: {}") << Error);

				if (!NFile::CFile::fs_FileExists(LaunchFileDirectory, NFile::EFileAttrib_Directory))
					NFile::CFile::fs_CreateDirectory(LaunchFileDirectory);

				NFile::CFile PlistFile(LaunchFilePath, NFile::EFileOpen_Write);
				PlistFile.f_Write(Data.f_GetArray(), Data.f_GetLen());
				PlistFile.f_Close();

				f_ReportInformation("Add Daemon", NStr::CStr::CFormat("Successfully installed daemon at {}") << LaunchFilePath);
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to install daemon at {}: {}") << LaunchFilePath << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

		EActionResult f_Remove()
		{
			if (!fp_CheckParamsSupported())
				return EActionResult_Failure;

			{
				try
				{
					// This will set disabled to true, to prevent a situation where the daemon is unloaded
					// but the plist is not deleted (eg with the helper config tool). This prevents a console warning when
					// the user restarts their machine.

					NStr::CStr LaunchFileDirectory = fs_GetPlistDirectory(mp_pOwner->mp_Params.f_GetDaemonMode());
					NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetPlistFilename(mp_pOwner->mp_Params);

					if (NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
					{
						CFPropertyListRef PropertyList = fg_CreateDictionaryFromParams(mp_pOwner->mp_Params, true);
						DMibDeprecatedSupressStart
						CFDataRef XmlData = CFPropertyListCreateXMLData(NULL, PropertyList);
						DMibDeprecatedSupressStop
						NContainer::CByteVector Data;
						Data.f_Insert((uint8 const *)CFDataGetBytePtr(XmlData), CFDataGetLength(XmlData));

						CFRelease(PropertyList);
						CFRelease(XmlData);

						NFile::CFile PlistFile(LaunchFilePath, NFile::EFileOpen_Write);
						PlistFile.f_Write(Data.f_GetArray(), Data.f_GetLen());
						PlistFile.f_Close();
					}
				}
				catch (NMib::NFile::CExceptionFile const&)
				{
					// Failure here doesn't matter, just continue.
				}
			}

			if (f_Stop(true) == EActionResult_Failure)
				return EActionResult_Failure;

			NStr::CStr LaunchFileDirectory = fs_GetPlistDirectory(mp_pOwner->mp_Params.f_GetDaemonMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetPlistFilename(mp_pOwner->mp_Params);

			try
			{
				if (!NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
				{
					f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Daemon is not installed at '{}' so it has not been removed") << LaunchFilePath);
					return EActionResult_Success;
				}
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to check for existing file: {}") << _Exception.f_GetErrorStr());
			}

			try
			{
				NFile::CFile::fs_DeleteFile(LaunchFilePath);
				NStr::CStr Error;
				if (!mp_pOwner->mp_Params.f_RemoveDaemonModeFile(Error))
					f_ReportError(NStr::CStr::CFormat("Failed to remove daemon mode file: {}") << Error);

				f_ReportInformation("Remove Daemon", NStr::CStr::CFormat("Successfully removed daemon from {}") << LaunchFilePath);
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to remove daemon at {}: {}\nPerhaps you need to use sudo?") << LaunchFilePath << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

		EActionResult f_Exists(bool &_bExists) const
		{
			if (!fp_CheckParamsSupported())
				return EActionResult_Failure;
			_bExists = false;
			NStr::CStr LaunchFileDirectory = fs_GetPlistDirectory(mp_pOwner->mp_Params.f_GetDaemonMode());
			NStr::CStr LaunchFilePath = NStr::CStr::CFormat("{}/{}") << LaunchFileDirectory << fs_GetPlistFilename(mp_pOwner->mp_Params);

			try
			{
				if (NFile::CFile::fs_FileExists(LaunchFilePath, NFile::EFileAttrib_File))
					_bExists = true;
				return EActionResult_Success;
			}
			catch (NFile::CExceptionFile const &_Exception)
			{
				f_ReportError(NStr::CStr::CFormat("Failed to check for existing file: {}") << _Exception.f_GetErrorStr());
			}

			return EActionResult_Failure;
		}

		bint f_IsShutdown() const
		{
			return false;
		}

		void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Message) const
		{
			mp_pOwner->f_GetDaemonParams().f_ReportInformation(_Heading, _Message);
		}

		void f_ReportError(NStr::CStr const& _Message) const
		{
			mp_pOwner->f_GetDaemonParams().f_ReportError(_Message);
		}

		EReportError f_ReportErrorYesNo(NStr::CStr const& _Message, EReportError _Default) const
		{
			return mp_pOwner->f_GetDaemonParams().f_ReportErrorYesNo(_Message, _Default);
		}

	protected:

		void fp_DaemonResume()
		{
			if (mp_pImp)
				mp_pImp->f_DaemonResume();
		}

		void fp_DaemonPause()
		{
			if (mp_pImp)
				mp_pImp->f_DaemonPause();
		}

		void fp_DaemonCreate()
		{
			mp_pImp = mp_pOwner->f_GetDaemonParams().f_ImplementationFactory();
		}

		void fp_DaemonDestroy()
		{
			if (mp_pImp)
				mp_pImp = nullptr;
		}

		static CDetails*			   msp_pThis;


		NThread::CEvent mp_InterruptedEvent;
		CDaemon*					   mp_pOwner;
		NStorage::TCUniquePointer<CDaemonImp>   mp_pImp;
	};

	CDaemon::CDetails* CDaemon::CDetails::msp_pThis = nullptr;

	CDaemon::CDaemon(CDaemonParams const& _Params)
		: mp_pD(fg_Construct(this))
		, mp_Params(_Params)
	{

	}

	CDaemon::~CDaemon()
	{

	}

	EDaemonFeature CDaemon::fs_SupportedFeatures()
	{
		return EDaemonFeature_GlobalDaemon | EDaemonFeature_AllUsersDaemon | EDaemonFeature_LocalUserDaemon;
	}

	EActionResult CDaemon::f_Start()
	{
		return mp_pD->f_Start();
	}

	EActionResult CDaemon::f_Stop(bool _bWait)
	{
		return mp_pD->f_Stop(_bWait);
	}

	EActionResult CDaemon::f_Restart(bool _bWait)
	{
		return mp_pD->f_Restart(_bWait);
	}

	EActionResult CDaemon::f_Exists(bool &_bExists) const
	{
		return mp_pD->f_Exists(_bExists);
	}

	EActionResult CDaemon::f_Add(bool _bCheckForExisting)
	{
		return mp_pD->f_Add(_bCheckForExisting);
	}

	EActionResult CDaemon::f_Remove()
	{
		return mp_pD->f_Remove();
	}

	EActionResult CDaemon::f_Run()
	{
		return mp_pD->f_Run();
	}

	EActionResult CDaemon::f_RunAsProgram(bool _bDebug)
	{
		return mp_pD->f_RunAsProgram(_bDebug);
	}

	bool CDaemon::f_IsShutdown() const
	{
		return mp_pD->f_IsShutdown();
	}

	void CDaemon::f_ReportError(NStr::CStr const& _Error)
	{
		mp_pD->f_ReportError(_Error);
	}

	void CDaemon::f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information)
	{
		mp_pD->f_ReportInformation(_Heading, _Information);
	}

	EReportError CDaemon::f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default)
	{
		return mp_pD->f_ReportErrorYesNo(_Error, _Default);
	}

	void CDaemon::fs_QuitDaemon()
	{
		CDetails::fs_SigTermHandler(SIGTERM);
		fg_CancelRunDaemonStatusApp();
	}
	bool CDaemon::fs_SupportsAutoRestart()
	{
		return true;
	}
}
