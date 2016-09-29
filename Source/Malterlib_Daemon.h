// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

namespace NMib
{
	namespace NService
	{
		class CService;

		class CServiceImp
		{
		public:

			CServiceImp() {}
			virtual ~CServiceImp() {}

			virtual void f_ServicePause() {}
			virtual void f_ServiceResume() {}
			virtual bool f_ServiceValidate(CService* _pService) { return true; }
		};

		enum EServiceAction
		{
			EServiceAction_Custom,
			EServiceAction_Add,
			EServiceAction_Remove,
			EServiceAction_Start,
			EServiceAction_Stop,
			EServiceAction_Run,
			EServiceAction_RunAsProgram,
			EServiceAction_Exists,
			EServiceAction_Restart,
		};

		enum EReportError
		{
			EReportError_No,
			EReportError_Yes,
			EReportError_Cancel,
		};
		
		enum EServiceMode
		{
			EServiceMode_Global,
			EServiceMode_AllUsers,
			EServiceMode_LocalUser
		};
		
		enum EActionResult
		{
			EActionResult_Success = 0,
			EActionResult_Failure = 1,
			EActionResult_Test = 25		// For unit test
		};

		typedef NContainer::TCVariant<void, bool> CServiceActionParam;

		class CServiceParams;
		typedef NFunction::TCFunction<NPtr::TCUniquePointer<CServiceImp> ()> FImplementationFactory;
		typedef NFunction::TCFunction<EActionResult (CServiceParams&, CService* _pService, bint& _bHandled)> FProcessCommand;
		typedef NFunction::TCFunction<void (NStr::CStr const&)> FErrorReporter;
		typedef NFunction::TCFunction<void (NStr::CStr const&, NStr::CStr const&)> FInformationReporter;
		typedef NFunction::TCFunction<EReportError (NStr::CStr const&, EReportError)> FErrorReporterYesNo;

		typedef NContainer::TCVector<NMib::NStr::CStr> CCommandLineVector;
		typedef NContainer::TCMap<NStr::CStr, NFunction::TCFunction<void (CCommandLineVector &)>> COptionHandlerMap;

		class CServiceParams
		{
		public:
			CServiceParams
				(
					NStr::CStr const& _ServiceName
					, NStr::CStr const& _DisplayName
					, NStr::CStr const& _ServiceDesc
					, void* _pNativeHandle
					, FImplementationFactory const& _ImplementationFactory = fg_Default()
					, FProcessCommand const& _ProcessCommand = fg_Default()
					, FErrorReporter const& _ErrorReporter = fg_Default()
					, FInformationReporter const& _InformationReporter = fg_Default()
					, FErrorReporterYesNo const& _ErrorReporterYesNo = fg_Default()
				)
			;

			~CServiceParams();

			void f_ParseCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine);
			void* f_GetNativeHandle() const;

			NStr::CStr f_GetCustomActionKey() const;
			void f_SetAction(EServiceAction _Action);
			EServiceAction f_GetAction() const;
			void f_SetActionParam(CServiceActionParam const &_Param);
			CServiceActionParam const& f_GetActionParam() const;

			NStr::CStr f_GetServiceGroup() const;
			bool f_GetInteractive() const;
			bool f_GetDisableWriteService() const;
			void f_SetDisableWriteService(bool _bDisable);
			bool f_GetKeepRunning() const;
			bool f_GetDaemonize() const;

			void f_SetServiceName(NStr::CStr const &_ServiceName, bool _bCustom = true);
			NStr::CStr f_GetServiceName() const;
			NStr::CStr f_GetServiceDisplayName() const;
			NStr::CStr f_GetServiceDescription() const;
			EServiceMode f_GetServiceMode() const;
			void f_SetServiceMode(EServiceMode _Mode);

			void f_SetRunAsUser(NStr::CStr const &_User);
			NStr::CStr f_GetRunAsUser() const;
			void f_SetRunAsGroup(NStr::CStr const &_Group);
			NStr::CStr f_GetRunAsGroup() const;

			NContainer::TCVector<NStr::CStr> const& f_GetServiceDependencies() const;

			NStr::CStr f_GetExecutablePath() const;
			void f_SetExecutablePath(NStr::CStr const &_Path);
			NStr::CStr f_GetCommandLine() const;
			NStr::CStr f_GetLocalizedStr(NStr::CStr const& _Key) const;

			void f_SetValueForKey(NStr::CStr const &_Key, NStr::CStr const &_Value);
			NStr::CStr f_GetValueForKey(NStr::CStr const& _Key) const;
			void f_SetKey(NStr::CStr const& _Key, bool _bKeySet);
			bool f_IsKeySet(NStr::CStr const& _Key) const;

			void f_SetAddCommandLine(NStr::CStr const& _CommandLine);
			NStr::CStr f_GetAddCommandLine() const;

			bool f_WriteServiceNameFile(NMib::NStr::CStr &_Error) const;
			
			bool f_WriteServiceModeFile(NMib::NStr::CStr &_Error) const;
			bool f_RemoveServiceModeFile(NMib::NStr::CStr &_Error) const;

			NPtr::TCUniquePointer<CServiceImp> f_ImplementationFactory() const;
			EActionResult f_ProcessCommand(CService* _pService, bint& _bHandled);
			
			void f_ReportError(NStr::CStr const& _Error) const;
			void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information) const;
			EReportError f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default) const;

			static void fs_ParseOptions(COptionHandlerMap &, CCommandLineVector &_CommandLine);
			static bool fs_ParseOptionArgument(CCommandLineVector &_CommandLine, NStr::CStr & _Destination);

		protected:
			NStr::CStr mp_ServiceName;
			NStr::CStr mp_DisplayName;
			NStr::CStr mp_ServiceDesc;
			bint mp_bInteractive;
			bint mp_bCustomServiceName;
			bint mp_bDisableWriteService;
			bint mp_bKeepRunning;
			bint mp_bDaemonize;
			EServiceMode mp_ServiceMode;
			NStr::CStr mp_ServiceGroup;
			NContainer::TCVector<NStr::CStr> mp_ServiceDependencies;

			NStr::CStr mp_ExecutablePath;
			NStr::CStr mp_CommandLine;
			NContainer::TCVector<NMib::NStr::CStr> mp_lRawArguments;

			NStr::CStr mp_RunAsUser;
			NStr::CStr mp_RunAsGroup;

			EServiceAction mp_Action;
			NStr::CStr mp_CustomAction;
			CServiceActionParam mp_ActionParam;

			NContainer::TCMap<NStr::CStr, NStr::CStr> mp_LocalizedStrings;
			
			FImplementationFactory mp_fImplementationFactory;
			FProcessCommand mp_fProcessCommand;

			FErrorReporter mp_fReportError;
			FErrorReporterYesNo mp_fReportErrorYesNo;
			FInformationReporter mp_fReportInformation;

			NStr::CStr mp_AddCommandLine;
			
			void* mp_pNativeHandle;	
				
			NMib::NStr::CStr fp_CleanServiceName(NMib::NStr::CStr const &_ServiceName);
			void fp_CopyElementsToCommandLine(CCommandLineVector const &_CommandLine);
			
			bool fp_Trace(NStr::CStr &_Error);

		};
		
		enum EServiceFeature
		{
			EServiceFeature_GlobalService = DMibBit(0)
			, EServiceFeature_AllUsersService = DMibBit(1)
			, EServiceFeature_LocalUserService = DMibBit(2)
			, EServiceFeature_EscapedPathBroken = DMibBit(3)
			, EServiceFeature_EscapeCharBroken = DMibBit(4)
		};

		class CService
		{
		public:

			class CDetails;

			CService(CServiceParams const& _Params);
			~CService();

			CServiceParams const& f_GetServiceParams() const;

			EActionResult f_ProcessCommand();

			EActionResult f_Start();
			EActionResult f_Stop(bool _bWait = false);
			EActionResult f_Restart(bool _bWait = false);

			EActionResult f_Add(bool _bCheckForExisting = false);
			EActionResult f_Remove();

			bool f_IsShutdown() const;

			EActionResult f_Run();

			EActionResult f_RunAsProgram();

			EActionResult f_Exists(bool &_bExists) const;

			void f_ReportError(NStr::CStr const& _Error);
			EReportError f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default);
			void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information);
			
			static NStr::CStr fs_GetUniquePrefix();
			static EServiceFeature fs_SupportedFeatures();
			static void fs_QuitDaemon();
			static bool fs_SupportsAutoRestart();

		protected:

			CServiceParams mp_Params;
			NPtr::TCUniquePointer<CDetails> mp_pD;

		};
		
		aint fg_RunDaemon(NStr::CStr const &_DaemonIdentifier, NStr::CStr const &_Name, NStr::CStr const &_SupportEmail, FImplementationFactory const &_fImpFactory);

	} // namespace NService

} // namespace NMib

#ifndef DMibPNoShortCuts
using namespace NMib::NService;
#endif

