// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include <Mib/Core/PlatformSpecific/WindowsString>
#include <Mib/Core/PlatformSpecific/WindowsError>
#include <Mib/Core/PlatformSpecific/Windows>
#include <Mib/Core/PlatformSpecific/WindowsFilePath>
#include <Mib/Process/Platform>
#include <Mib/Cryptography/RandomID>
#include <Windows.h>
#include <LsaLookup.h>
#include <Ntsecapi.h>

namespace NMib::NDaemon
{
	class CDaemon::CDetails
	{
	public:

		CDetails(CDaemon* _pOwner);
		~CDetails();

		EActionResult f_Run();
		static void fs_AbortDebug();

		EActionResult f_RunAsProgram(bool _bDebug);

		EActionResult f_Start();
		EActionResult f_Stop(bool _bWait);
		EActionResult f_Restart(bool _bWait);
		EActionResult f_Add(bool _bCheckForExisting);
		EActionResult f_Remove();

		EActionResult f_Exists(bool &_bExists) const;

		bool f_IsShutdown() const;

		void f_ReportInformation(NStr::CStr const &_Heading, NStr::CStr const &_Message) const;
		void f_ReportError(NStr::CStr const &_Message) const;
		EReportError f_ReportErrorYesNo(NStr::CStr const &_Message, EReportError _Default) const;

	protected:
		struct CTaskIconCleaner
		{
			CTaskIconCleaner();
			~CTaskIconCleaner();

			static LRESULT CALLBACK fs_ReportWindowProc(HWND _hWnd, UINT _Message, WPARAM _WParam, LPARAM _LParam);

			void f_Init(HICON _hIcon);
			bool f_Update();

			NOTIFYICONDATA m_NotifyIconData;
			HWND m_hReportWnd;
			bool m_bInit = false;
			bool m_bAbortDebug = false;
		};

		bool fp_CheckParamsSupported(CDaemonParams const &_Params) const;
		CDaemonParams const &fp_GetDaemonParams() const;

		SC_HANDLE fp_OpenSCManager() const;

		EActionResult fp_UserDaemonExists(bool &_bExists) const;
		EActionResult fp_UserDaemonStart();
		EActionResult fp_UserDaemonStop(bool _bWait);
		EActionResult fp_UserDaemonAdd(bool _bCheckForExisting);
		EActionResult fp_UserDaemonRemove();

		void fp_UpdateService(SC_HANDLE _Service);
		bool fp_PrepareUserAndGroup(CDaemonParams const &_Params, NMib::NStr::CWStr &o_RunAsUser, NMib::NStr::CWStrSecure &o_RunAsUserPassword);
		NStr::CStr fp_GetAddCommandLine() const;
		static void WINAPI fsp_ServiceStart (DWORD _nArgs, LPWSTR *_pArgs);
		static DWORD WINAPI fsp_ServiceInitialization(DWORD _nArgs, LPWSTR *_pArgs, DWORD *_pSpecificError);
		static DWORD WINAPI fsp_ServiceCtrlHandler (DWORD _ControlCode, DWORD _EventType, void *_pEventData, void *_pContext);
		void fp_DaemonResume();
		void fp_DaemonPause();
		void fp_DaemonCreate();
		void fp_DaemonDestroy();
		aint fp_StopThread(NThread::CThreadObject *);
		aint fp_StopReportThread(NThread::CThreadObject *_pThread);

		NStorage::TCUniquePointer<NThread::CThreadObject> mp_pStopThread;
		NStorage::TCUniquePointer<NThread::CThreadObject> mp_pStopReportThread;

		static NThread::CMutual        msp_ServiceControlLock; 
		static SERVICE_STATUS          msp_ServiceStatus; 
		static SERVICE_STATUS_HANDLE   msp_ServiceStatusHandle;
		static CDetails*			   msp_pThis;
		static bool					   msp_bIsShutdown;
		static CTaskIconCleaner		   msp_TaskIcon;

		CDaemon*					   mp_pOwner;
		NStorage::TCUniquePointer<CDaemonImp>   mp_pImp;
	};
}
