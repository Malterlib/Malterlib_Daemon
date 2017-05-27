// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include <Mib/Core/PlatformSpecific/WindowsString>
#include <Mib/Core/PlatformSpecific/WindowsError>
#include <Mib/Core/PlatformSpecific/Windows>
#include <Mib/Process/Platform>
#include <Windows.h>

namespace NMib
{
	namespace NService
	{
		class CService::CDetails
		{
			bool fp_CheckParamsSupported(CServiceParams const &_Params) const
			{
				bool bRet = true;
				switch (_Params.f_GetServiceMode())
				{
				case EServiceMode_AllUsers:
					{
						mp_pOwner->f_ReportError("User services (-AllUsers) are not supported on this platform");
						bRet = false;
					}
					break;
				case EServiceMode_LocalUser:
					{
						mp_pOwner->f_ReportError("Local user services (-LocalUser) are not supported on this platform");
						bRet = false;
					}
					break;
				}
			
				return bRet;			
			}

			
			CServiceParams const& fp_GetServiceParams() const
			{
				return msp_pThis->mp_pOwner->f_GetServiceParams();
			}

		public:

			CDetails(CService* _pOwner)
				: mp_pOwner(_pOwner)
			{
				if (msp_pThis)
					DMibError("You cannot have two services running in the same process at once");
				else
					msp_pThis = this;
			}

			~CDetails()
			{
				mp_pStopThread.f_Clear();
				mp_pStopReportThread.f_Clear();
				msp_pThis = nullptr;
			}

			EActionResult f_Run()
			{
				NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName());
				SERVICE_TABLE_ENTRYW DispatchTable[] = { { (ch16 *)Temp.f_GetStr(), CService::CDetails::fsp_ServiceStart}, { nullptr, nullptr} }; 

				if (!StartServiceCtrlDispatcherW( DispatchTable)) 
				{ 
					f_ReportError(NStr::CStr::CFormat("StartServiceCtrlDispatcher error: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
				return EActionResult_Success;
			}

			EActionResult f_RunAsProgram(bool _bDebug)
			{
				fp_ServiceCreate();

				if (_bDebug)
				{
					HICON Icon = LoadIcon((HINSTANCE)mp_pOwner->f_GetServiceParams().f_GetNativeHandle(), MAKEINTRESOURCE(101));
					msp_TaskIcon.f_Init(Icon);
				
					// Just spin in eternity
					while (1)
					{
						if (msp_TaskIcon.f_Update())
							break;

						Sleep(50);
					}
				}
				else
				{
					NProcess::NPlatform::fg_Process_WaitForTermination();
				}

				fp_ServiceDestroy();

				return EActionResult_Success;
			}

			SC_HANDLE f_OpenSCManager() const
			{
				return OpenSCManager(nullptr, nullptr, SC_MANAGER_CREATE_SERVICE | DELETE | SERVICE_STOP | SERVICE_START | SERVICE_QUERY_STATUS);
			}

			EActionResult f_Start()
			{
				if (!fp_CheckParamsSupported(fp_GetServiceParams()))
					return EActionResult_Failure;
				SC_HANDLE schSCManager = f_OpenSCManager();
				if (!schSCManager)
				{
					f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
				
				auto CleanupServiceManager = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schSCManager);
						}
					)
				;

				SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), SERVICE_START | SERVICE_QUERY_STATUS);

				if (schService)
				{
					auto CleanupService = fg_OnScopeExit
						(
							[&]
							{
								CloseServiceHandle(schService);
							}
						)
					;
					SERVICE_STATUS Status;

					if (!QueryServiceStatus(schService, &Status))
					{
						f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
						return EActionResult_Failure;
					}

					if (Status.dwCurrentState == SERVICE_RUNNING || Status.dwCurrentState == SERVICE_START_PENDING)
					{
						return EActionResult_Success;
					}

					if (!StartService(schService, 0, nullptr))
					{
						f_ReportError(NStr::CStr::CFormat("Unable to start service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
						return EActionResult_Failure;
					}

					while (1)
					{
						if (!QueryServiceStatus(schService, &Status))
							break;

						if (Status.dwCurrentState != SERVICE_START_PENDING)
							break;
						Sleep(10);
					}
				}
				else
				{	
					f_ReportError(NStr::CStr::CFormat("Unable to start service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}

				return EActionResult_Success;
			}

			EActionResult f_Stop(bint _bWait)
			{
				if (!fp_CheckParamsSupported(fp_GetServiceParams()))
					return EActionResult_Failure;
				
				bool bServiceExists;
				if (f_Exists(bServiceExists) == EActionResult_Failure)
					return EActionResult_Failure;
				
				if (!bServiceExists)
				{
					f_ReportInformation("Stop Service", "Service is not installed so it has not been stopped");
					return EActionResult_Success;
				}
				
				SC_HANDLE schSCManager = f_OpenSCManager();
				if (!schSCManager)
				{
					f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}

				auto CleanupServiceManager = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schSCManager);
						}
					)
				;

				SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), SERVICE_STOP | SERVICE_QUERY_STATUS);

				if (schService)
				{

					auto CleanupService = fg_OnScopeExit
						(
							[&]
							{
								CloseServiceHandle(schService);
							}
						)
					;

					SERVICE_STATUS Status;

					if (!QueryServiceStatus(schService, &Status))
					{
						f_ReportError(NStr::CStr::CFormat("Unable to query service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
						return EActionResult_Failure;
					}

					if (Status.dwCurrentState != SERVICE_STOPPED)
					{
						uint32 Control = SERVICE_CONTROL_STOP;
						SERVICE_STATUS Status;

						if (!ControlService(schService, Control, &Status))
						{
							uint32 Error = GetLastError();

							if (Error != ERROR_SERVICE_NOT_ACTIVE)
							{
								f_ReportError(NStr::CStr::CFormat("Unable to stop service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
								return EActionResult_Failure;
							}
							else if (!_bWait)
								return EActionResult_Success;
						}
					}
					else if (!_bWait)
						return EActionResult_Success;

					SERVICE_STATUS_PROCESS ProcessStatus;
					NMem::fg_MemClear(ProcessStatus);

					DWORD SizeNeeded = 0;

					QueryServiceStatusEx(
						schService,
						SC_STATUS_PROCESS_INFO,
						(LPBYTE) &ProcessStatus,
						sizeof(ProcessStatus),
						&SizeNeeded
						);

					HANDLE hProcess = nullptr;
					if (ProcessStatus.dwProcessId)
						hProcess = OpenProcess(SYNCHRONIZE, false, ProcessStatus.dwProcessId);

					auto Cleanup = fg_OnScopeExit
						(
							[&]
							{
								if (hProcess)
									CloseHandle(hProcess);
							}
						)
					;

					while (1)
					{
						if (!QueryServiceStatus(schService, &Status))
							break;

						if (Status.dwCurrentState == SERVICE_STOPPED)
							break;
						Sleep(10);
					}

					if (hProcess)
					{
						WaitForSingleObject(hProcess, 240000);
					}
				}
				else
				{	
					f_ReportError(NStr::CStr::CFormat("Unable to stop service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
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

			EActionResult f_Add(bint _bCheckForExisting)
			{
				if (!fp_CheckParamsSupported(fp_GetServiceParams()))
					return EActionResult_Failure;
				SC_HANDLE schSCManager = f_OpenSCManager();
				if (!schSCManager)
				{
					f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
				auto CleanupServiceManager = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schSCManager);
						}
					)
				;

				if (_bCheckForExisting)
				{
					SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);

					if (schService)
					{
						auto CleanupService = fg_OnScopeExit
							(
								[&]
								{
									CloseServiceHandle(schService);
								}
							)
						;
						QUERY_SERVICE_CONFIG *pQueryConfig;
						uint32 NeededSize = 0;
						QueryServiceConfig(schService, nullptr, 0, &NeededSize);
						{
							NContainer::TCVector<uint8> Vector;
							Vector.f_SetLen(NeededSize);
							pQueryConfig = (QUERY_SERVICE_CONFIG *)Vector.f_GetArray();

							if (QueryServiceConfig(schService, pQueryConfig, NeededSize, &NeededSize))
							{
								//					CStr BinaryPath = pQueryConfig->lpBinaryPathName;
								//					if (BinaryPath != lpszBinaryPathName)

								NContainer::TCVector<NStr::CStr> const& lDependencies = mp_pOwner->f_GetServiceParams().f_GetServiceDependencies();
								NContainer::TCVector<ch16> Deps;
								mint nDeps = lDependencies.f_GetLen();
								if (nDeps)
								{
									for (mint i = 0; i < nDeps; ++i)
									{
										NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(lDependencies[i]);
										Deps.f_Insert(Temp.f_GetStr(), Temp.f_GetLen() + 1);
									}
									Deps.f_Insert(ch16(0));
								}


								{
									if (!ChangeServiceConfigW(schService, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, SERVICE_NO_CHANGE, NStr::NPlatform::fg_StrToWindows(fp_GetAddCommandLine()), nullptr, nullptr, !Deps.f_IsEmpty() ? Deps.f_GetArray() : nullptr, nullptr, nullptr, nullptr))
									{
										DMibTrace("Could not change service config\n", 0);
									}
								}

							}
						}
						f_UpdateService(schService);
						return EActionResult_Success;
					}
				}

				{
					SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), DELETE);

					if (schService)
					{
						if (!DeleteService(schService))
						{
							f_ReportError(NStr::CStr::CFormat("Unable to delete service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
							return EActionResult_Failure;
						}
					}
				}

				NContainer::TCVector<NStr::CStr> const& lDependencies = mp_pOwner->f_GetServiceParams().f_GetServiceDependencies();
				NContainer::TCVector<ch16> Deps;
				mint nDeps = lDependencies.f_GetLen();
				if (nDeps)
				{
					for (mint i = 0; i < nDeps; ++i)
					{
						NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(lDependencies[i]);
						Deps.f_Insert(Temp.f_GetStr(), Temp.f_GetLen() + 1);
					}
					Deps.f_Insert(ch16(0));
				}

				NStr::CWStr ServiceGroup = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceGroup());
				SC_HANDLE schService = CreateServiceW( 
					schSCManager,              // SCManager database 
					NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()),              // name of service 
					NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceDisplayName()),           // service name to display 
					SERVICE_ALL_ACCESS,        // desired access 
					SERVICE_WIN32_OWN_PROCESS | (mp_pOwner->f_GetServiceParams().f_GetInteractive() ? SERVICE_INTERACTIVE_PROCESS : 0), // service type 
					SERVICE_AUTO_START,      // start type 
					SERVICE_ERROR_NORMAL,      // error control type 
					NStr::NPlatform::fg_StrToWindows(fp_GetAddCommandLine()),        // service's binary 
					!mp_pOwner->f_GetServiceParams().f_GetServiceGroup().f_IsEmpty() ? ServiceGroup.f_GetStr() : nullptr,          // no load ordering group 
					nullptr,                      // no tag identifier 
					!Deps.f_IsEmpty() ? Deps.f_GetArray() : nullptr,                      // no dependencies 
					nullptr,                      // LocalSystem account 
					nullptr);                     // no password 

				if (schService == nullptr) 
				{
					f_ReportError(NStr::CStr::CFormat("Error returned when creating service {}\r\n{}") << fp_GetAddCommandLine() << NMib::NPlatform::fg_Win32_GetLastErrorStr(0) );
					return EActionResult_Failure;
				}
				else 
					DMibTrace("Creation of service successful", 0);
				auto CleanupService = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schService);
						}
					)
				;

				f_UpdateService(schService);
				return EActionResult_Success;
			}

			EActionResult f_Remove()
			{
				if (!fp_CheckParamsSupported(fp_GetServiceParams()))
					return EActionResult_Failure;
				

				bool bServiceExists;
				if (f_Exists(bServiceExists) == EActionResult_Failure)
					return EActionResult_Failure;
				
				if (!bServiceExists)
				{
					f_ReportInformation("Remove Service", "Service is not installed so it has not been removed");
					return EActionResult_Success;
				}

				if (f_Stop(true) == EActionResult_Failure)
					return EActionResult_Failure;
				
				SC_HANDLE schSCManager = f_OpenSCManager();
				if (!schSCManager)
				{
					f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
				auto CleanupServiceManager = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schSCManager);
						}
					)
				;

				SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), DELETE);
				if (schService)
				{
					auto CleanupService = fg_OnScopeExit
						(
							[&]
							{
								CloseServiceHandle(schService);
							}
						)
					;
					if (!DeleteService(schService))
					{
						f_ReportError(NStr::CStr::CFormat("Unable to delete service: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
						return EActionResult_Failure;
					}
				}

				return EActionResult_Success;
			}

			EActionResult f_Exists(bool &_bExists) const
			{
				if (!fp_CheckParamsSupported(fp_GetServiceParams()))
					return EActionResult_Failure;
				_bExists = false;
				SC_HANDLE schSCManager = f_OpenSCManager();
				if (!schSCManager)
				{
					f_ReportError(NStr::CStr::CFormat("Unable to open service manager: {}") << NMib::NPlatform::fg_Win32_GetLastErrorStr(0));
					return EActionResult_Failure;
				}
				auto CleanupServiceManager = fg_OnScopeExit
					(
						[&]
						{
							CloseServiceHandle(schSCManager);
						}
					)
				;

				SC_HANDLE schService = OpenServiceW(schSCManager, NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceName()), SERVICE_QUERY_CONFIG | SERVICE_CHANGE_CONFIG);

				if (schService)
				{
					auto CleanupService = fg_OnScopeExit
						(
							[&]
							{
								CloseServiceHandle(schService);
							}
						)
					;
					_bExists = true;;
				}
				return EActionResult_Success;
			}

			bint f_IsShutdown() const
			{
				return msp_bIsShutdown;
			}

			void f_UpdateService(SC_HANDLE _Service)
			{
				SERVICE_DESCRIPTIONW Description;
				NStr::CWStr Temp = NStr::NPlatform::fg_StrToWindows(mp_pOwner->f_GetServiceParams().f_GetServiceDescription());
				Description.lpDescription = (ch16 *)Temp.f_GetStr();
				ChangeServiceConfig2W(_Service, SERVICE_CONFIG_DESCRIPTION, &Description);

				SERVICE_FAILURE_ACTIONSW RestartActions;

				RestartActions.dwResetPeriod = 60*60*24;
				Temp = NStr::NPlatform::fg_StrToWindows(NStr::CStr("Rebooting the server in response to crash of ") + mp_pOwner->f_GetServiceParams().f_GetServiceDisplayName() + " crash.");
				RestartActions.lpRebootMsg = (ch16 *)Temp.f_GetStr();
				RestartActions.lpCommand = str_utf16("");
				RestartActions.cActions = 3;
				SC_ACTION Actions[3];
				Actions[0].Delay = 0;
				Actions[0].Type = SC_ACTION_RESTART;
				Actions[1].Delay = 0;
				Actions[1].Type = SC_ACTION_RESTART;
				Actions[2].Delay = 0;
				Actions[2].Type = SC_ACTION_NONE;
				RestartActions.lpsaActions = Actions;	

				ChangeServiceConfig2W(_Service, SERVICE_CONFIG_FAILURE_ACTIONS, &RestartActions);

				if (NMib::NPlatform::fg_IsVista())
				{
					SERVICE_PRESHUTDOWN_INFO PreShutDown;
					PreShutDown.dwPreshutdownTimeout = 12*60*60*1000;
					ChangeServiceConfig2W(_Service, SERVICE_CONFIG_PRESHUTDOWN_INFO, &PreShutDown);
				}
			}

			void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Message) const
			{
				mp_pOwner->f_GetServiceParams().f_ReportInformation(_Heading, _Message);
			}

			void f_ReportError(NStr::CStr const& _Message) const
			{
				mp_pOwner->f_GetServiceParams().f_ReportError(_Message);
			}

			EReportError f_ReportErrorYesNo(NStr::CStr const& _Message, EReportError _Default) const
			{
				return mp_pOwner->f_GetServiceParams().f_ReportErrorYesNo(_Message, _Default);
			}

			class CTaskIconCleaner
			{
			public:

				NOTIFYICONDATA m_NotifyIconData;
				bint m_bInit;
				CTaskIconCleaner()
				{
					m_bInit = false;

				}

				HWND m_hReportWnd;

				static LRESULT CALLBACK fs_ReportWindowProc(HWND _hWnd, UINT _Message, WPARAM _WParam, LPARAM _LParam)
				{
					if (_Message == WM_COMMAND)
					{
						if (_WParam == 123)
						{
							msp_pThis->fp_ServicePause();
							return true;
						}
						else if (_WParam == 124)
						{
							msp_pThis->fp_ServiceResume();
							return true;
						}
						else if (_WParam == 125)
						{
							PostQuitMessage(0);
							return true;
						}
					}
					if (_Message == WM_USER + 20 && _LParam == WM_LBUTTONDBLCLK)
					{
						PostQuitMessage(0);

						return true;
					}

					if (_Message == WM_USER + 20 && _LParam == WM_RBUTTONUP)
					{
						SetForegroundWindow(_hWnd);
						HMENU hMenu = CreatePopupMenu();
						AppendMenuA(hMenu, MF_STRING, 123, "Pause");
						AppendMenuA(hMenu, MF_STRING, 124, "Resume");
						AppendMenuA(hMenu, MF_STRING, 125, "Quit");
						AppendMenuA(hMenu, MF_SEPARATOR, 126, "Separator");
						AppendMenuA(hMenu, MF_STRING, 127, "Cancel");
						POINT Pos;
						GetCursorPos(&Pos);
						TrackPopupMenu(hMenu, TPM_BOTTOMALIGN | TPM_LEFTALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, Pos.x, Pos.y, 0, _hWnd, nullptr);
						PostMessage(_hWnd, WM_NULL, 0, 0);
						DestroyMenu(hMenu);

						return true;
					}

					return DefWindowProc(_hWnd, _Message, _WParam, _LParam);
				}

				void f_Init(HICON _hIcon)
				{

					if (!m_bInit)
					{
						m_bInit = true;

						WNDCLASSA WndClass;
						memset(&WndClass, 0, sizeof(WndClass));
						WndClass.lpszClassName = "CService_ReportWindow" ;
						WndClass.lpfnWndProc = fs_ReportWindowProc;
						WndClass.hInstance = 0;
						RegisterClassA(&WndClass);
						m_hReportWnd = CreateWindowA("CService_ReportWindow", "CService_ReportWindow", 0, 0, 0, 0, 0, HWND_MESSAGE, 0, 0, 0);

						NMem::fg_MemClear(m_NotifyIconData);
						if (_hIcon)
						{
							m_NotifyIconData.cbSize = sizeof(m_NotifyIconData);
							m_NotifyIconData.hWnd = m_hReportWnd;
							m_NotifyIconData.hIcon = _hIcon;
							m_NotifyIconData.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
							m_NotifyIconData.uID = 0x0101;
							m_NotifyIconData.uCallbackMessage = WM_USER+20;
							m_NotifyIconData.dwState = 0;
							m_NotifyIconData.dwStateMask = 0;
							NStr::fg_StrCopy(m_NotifyIconData.szTip, "Double click to quit " + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName());

							Shell_NotifyIcon(NIM_ADD, &m_NotifyIconData);
						}
					}
				}

				bint f_Update()
				{	
					MSG Message;

					int32 Ret = GetMessage( &Message, nullptr, 0, 0 );
					if (Ret == -1 || Ret == 0 || Message.message == WM_QUIT)
					{
						return true;
					}

					TranslateMessage(&Message);
					DispatchMessage(&Message);

					return false;
				}

				~CTaskIconCleaner()
				{
					if (m_bInit)
					{
						if (m_NotifyIconData.hIcon)
							Shell_NotifyIcon(NIM_DELETE, &m_NotifyIconData);
						DestroyWindow(m_hReportWnd);
						UnregisterClassA("AOService_ReportWindow", 0);
					}
				}
			};

		protected:

			NStr::CStr fp_GetAddCommandLine() const
			{
				NStr::CStr Strings = NSys::NFile::fg_GetProgramPath();

				if (Strings[0] != '"')
					Strings = NStr::CStr("\"") + Strings + "\"";

				NStr::CStr CommandLine;
				if (mp_pOwner->f_GetServiceParams().f_GetAddCommandLine() != "")
					CommandLine = Strings + " " + mp_pOwner->f_GetServiceParams().f_GetAddCommandLine();
				else
					CommandLine = Strings + " -Service " + mp_pOwner->f_GetServiceParams().f_GetServiceName();

				return CommandLine;
			}

			static void WINAPI fsp_ServiceStart (DWORD _nArgs, LPWSTR *_pArgs) 
			{ 
				DWORD status; 
				DWORD specificError; 

				msp_ServiceStatus.dwServiceType        = SERVICE_WIN32_OWN_PROCESS; 
				msp_ServiceStatus.dwCurrentState       = SERVICE_START_PENDING; 
				msp_ServiceStatus.dwControlsAccepted   = SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_PAUSE_CONTINUE | SERVICE_ACCEPT_SHUTDOWN;
				if (NMib::NPlatform::fg_IsVista())
				{
					msp_ServiceStatus.dwControlsAccepted |= SERVICE_ACCEPT_PRESHUTDOWN;

				}
				msp_ServiceStatus.dwWin32ExitCode      = 0; 
				msp_ServiceStatus.dwServiceSpecificExitCode = 0; 
				msp_ServiceStatus.dwCheckPoint         = 0; 
				msp_ServiceStatus.dwWaitHint           = 0; 

				msp_ServiceStatusHandle = RegisterServiceCtrlHandlerExW( 
					NStr::NPlatform::fg_StrToWindows(msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName()), 
					&CDetails::fsp_ServiceCtrlHandler,
					msp_pThis); 

				if (msp_ServiceStatusHandle == (SERVICE_STATUS_HANDLE)0) 
				{ 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] RegisterServiceCtrlHandler failed {}\n", GetLastError()); 
					return; 
				} 

				// Initialization code goes here. 
				status = fsp_ServiceInitialization(_nArgs,_pArgs, &specificError); 

				// Handle error condition 
				if (status != NO_ERROR) 
				{ 
					msp_ServiceStatus.dwCurrentState       = SERVICE_STOPPED; 
					msp_ServiceStatus.dwCheckPoint         = 0; 
					msp_ServiceStatus.dwWaitHint           = 0; 
					msp_ServiceStatus.dwWin32ExitCode      = status; 
					msp_ServiceStatus.dwServiceSpecificExitCode = specificError; 

					SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus); 
					return; 
				} 

				// Initialization complete - report running status. 
				msp_ServiceStatus.dwCurrentState       = SERVICE_RUNNING; 
				msp_ServiceStatus.dwCheckPoint         = 0; 
				msp_ServiceStatus.dwWaitHint           = 0; 

				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus)) 
				{ 
					status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
				} 

				// This is where the service does its work. 
				DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] Returning the Main Thread\n", 0); 

				return; 
			} 

			static DWORD WINAPI fsp_ServiceInitialization(DWORD _nArgs, LPWSTR *_pArgs, DWORD *_pSpecificError) 
			{ 
				msp_pThis->fp_ServiceCreate();
				return msp_pThis->mp_pImp == nullptr;
			}

			static DWORD WINAPI fsp_ServiceCtrlHandler (DWORD _ControlCode, DWORD _EventType, void *_pEventData, void *_pContext)
			{ 
				DWORD status; 

				DMibLock(msp_ServiceControlLock);

				switch(_ControlCode) 
				{ 
				case SERVICE_CONTROL_PAUSE: 
					// Do whatever it takes to pause here. 
					if (msp_ServiceStatus.dwCurrentState == SERVICE_RUNNING)
					{
						msp_pThis->fp_ServicePause();
						msp_ServiceStatus.dwCurrentState = SERVICE_PAUSED; 
					}

					break; 

				case SERVICE_CONTROL_CONTINUE: 
					// Do whatever it takes to continue here. 
					if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
					{
						msp_pThis->fp_ServiceResume();
						msp_ServiceStatus.dwCurrentState = SERVICE_RUNNING; 
					}
					break; 

				case SERVICE_CONTROL_PRESHUTDOWN:

				case SERVICE_CONTROL_SHUTDOWN:
					{
						if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
						{
							msp_pThis->fp_ServiceResume();
						}

						msp_bIsShutdown = true;

						// Do whatever it takes to stop here. 
						if (!msp_pThis->mp_pStopThread)
						{
							msp_ServiceStatus.dwWin32ExitCode = 0; 
							msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
							msp_ServiceStatus.dwCheckPoint    = 0; 
							msp_ServiceStatus.dwWaitHint      = 10000;

							msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CService_Destroy");
							msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CService_DestroyReport");
						}
					}
					break;

				case SERVICE_CONTROL_STOP:
					if (msp_ServiceStatus.dwCurrentState == SERVICE_PAUSED)
					{
						msp_pThis->fp_ServiceResume();
					}

					// Do whatever it takes to stop here. 
					if (!msp_pThis->mp_pStopThread)
					{
						msp_ServiceStatus.dwWin32ExitCode = 0; 
						msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
						msp_ServiceStatus.dwCheckPoint    = 0; 
						msp_ServiceStatus.dwWaitHint      = 10000;

						msp_pThis->mp_pStopThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopThread(_pThread);}, "CService_Destroy");
						msp_pThis->mp_pStopReportThread = NThread::CThreadObject::fs_StartThread([](NThread::CThreadObject *_pThread){return msp_pThis->fp_StopReportThread(_pThread);}, "CService_DestroyReport");
					}
					break; 

				case SERVICE_CONTROL_INTERROGATE: 
					// Fall through to send current status. 
					break; 
				default:
					return ERROR_CALL_NOT_IMPLEMENTED;
				} 

				// Send current status. 
				if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus)) 
				{ 
					status = GetLastError(); 
					DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", (status)); 
				}
				return NO_ERROR;
			}

			void fp_ServiceResume()
			{
				if (mp_pImp)
					mp_pImp->f_ServiceResume();
			}

			void fp_ServicePause()
			{
				if (mp_pImp)
					mp_pImp->f_ServicePause();
			}

			void fp_ServiceCreate()
			{
				mp_pImp = mp_pOwner->f_GetServiceParams().f_ImplementationFactory();
			}

			void fp_ServiceDestroy()
			{
				if (mp_pImp)
					mp_pImp = nullptr;
			}

			aint fp_StopThread(NThread::CThreadObject *)
			{
				msp_pThis->mp_pImp = nullptr;
				return 0;
			}

			aint fp_StopReportThread(NThread::CThreadObject *_pThread)
			{
				while (1)
				{
					if (mp_pStopThread->f_GetState() == NThread::EThreadState_Stopped)
					{
						DMibLock(msp_ServiceControlLock);
						msp_ServiceStatus.dwWin32ExitCode = 0; 
						msp_ServiceStatus.dwCurrentState  = SERVICE_STOPPED; 

						DMibDTrace("Service stopped: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

						if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
						{ 
							HRESULT status = GetLastError(); 
							DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
						}
						return 0;
					}
					else
					{
						DMibLock(msp_ServiceControlLock);
						msp_ServiceStatus.dwWin32ExitCode = 0; 
						msp_ServiceStatus.dwCurrentState  = SERVICE_STOP_PENDING; 
						++msp_ServiceStatus.dwCheckPoint; 
						msp_ServiceStatus.dwWaitHint      += 1500;
						DMibDTrace("Service stop pending: {}" DMibNewLine, msp_ServiceStatus.dwCheckPoint);

						if (!SetServiceStatus (msp_ServiceStatusHandle, &msp_ServiceStatus))
						{ 
							HRESULT status = GetLastError(); 
							DMibDTrace(" [" + msp_pThis->mp_pOwner->f_GetServiceParams().f_GetServiceName() + "] SetServiceStatus error {}\n", status); 
						}
					}
					WaitForSingleObject(mp_pStopThread->f_GetThread(), 1000);
				}
				return 0;
			}

			NPtr::TCUniquePointer<NThread::CThreadObject> mp_pStopThread;
			NPtr::TCUniquePointer<NThread::CThreadObject> mp_pStopReportThread;

			static NThread::CMutual        msp_ServiceControlLock; 
			static SERVICE_STATUS          msp_ServiceStatus; 
			static SERVICE_STATUS_HANDLE   msp_ServiceStatusHandle;
			static CDetails*			   msp_pThis;
			static bint					   msp_bIsShutdown;
			static CTaskIconCleaner		   msp_TaskIcon;

			CService*					   mp_pOwner;
			NPtr::TCUniquePointer<CServiceImp>   mp_pImp;

		};

		NThread::CMutual CService::CDetails::msp_ServiceControlLock;
		SERVICE_STATUS CService::CDetails::msp_ServiceStatus;
		SERVICE_STATUS_HANDLE CService::CDetails::msp_ServiceStatusHandle;
		CService::CDetails* CService::CDetails::msp_pThis = nullptr;
		bint CService::CDetails::msp_bIsShutdown = false;
		CService::CDetails::CTaskIconCleaner CService::CDetails::msp_TaskIcon;

		CService::CService(CServiceParams const& _Params)
			: mp_pD(fg_Construct(this))
			, mp_Params(_Params)
		{

		}

		CService::~CService()
		{

		}

		EServiceFeature CService::fs_SupportedFeatures()
		{
			return EServiceFeature_GlobalService;
		}
		
		EActionResult CService::f_Start()
		{
			return mp_pD->f_Start();
		}

		EActionResult CService::f_Stop(bool _bWait)
		{
			return mp_pD->f_Stop(_bWait);
		}

		EActionResult CService::f_Restart(bool _bWait)
		{
			return mp_pD->f_Restart(_bWait);
		}

		EActionResult CService::f_Exists(bool &_bExists) const
		{
			return mp_pD->f_Exists(_bExists);
		}

		EActionResult CService::f_Add(bool _bCheckForExisting)
		{
			return mp_pD->f_Add(_bCheckForExisting);
		}

		EActionResult CService::f_Remove()
		{
			return mp_pD->f_Remove();
		}

		EActionResult CService::f_Run()
		{
			return mp_pD->f_Run();
		}

		EActionResult CService::f_RunAsProgram(bool _bDebug)
		{
			return mp_pD->f_RunAsProgram(_bDebug);
		}

		bool CService::f_IsShutdown() const
		{
			return mp_pD->f_IsShutdown();
		}

		void CService::f_ReportError(NStr::CStr const& _Error)
		{
			mp_pD->f_ReportError(_Error);
		}

		void CService::f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information)
		{
			mp_pD->f_ReportInformation(_Heading, _Information);
		}

		EReportError CService::f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default)
		{
			return mp_pD->f_ReportErrorYesNo(_Error, _Default);
		}

		void CService::fs_QuitDaemon()
		{
			DMibError("Not implemented");
		}

		bool CService::fs_SupportsAutoRestart()
		{
			return false;
		}
	} // namespace NService

} // namespace NMib
