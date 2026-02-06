// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Storage/Variant>

namespace NMib::NDaemon
{
	class CDaemon;

	class CDaemonImp
	{
	public:

		CDaemonImp() {}
		virtual ~CDaemonImp() {}

		virtual void f_DaemonPause() {}
		virtual void f_DaemonResume() {}
		virtual bool f_DaemonValidate(CDaemon* _pDaemon) { return true; }
	};

	enum EDaemonAction
	{
		EDaemonAction_Custom,
		EDaemonAction_Add,
		EDaemonAction_Remove,
		EDaemonAction_Start,
		EDaemonAction_Stop,
		EDaemonAction_Run,
		EDaemonAction_RunAsProgram,
		EDaemonAction_RunAsProgramNoDebug,
		EDaemonAction_Exists,
		EDaemonAction_Restart,
	};

	enum EReportError
	{
		EReportError_No,
		EReportError_Yes,
		EReportError_Cancel,
	};

	enum EDaemonMode
	{
		EDaemonMode_Global,
		EDaemonMode_AllUsers,
		EDaemonMode_LocalUser
	};

	enum EActionResult
	{
		EActionResult_Success = 0,
		EActionResult_Failure = 1,
		EActionResult_Test = 25		// For unit test
	};

	using CDaemonActionParam = NStorage::TCVariant<void, bool>;

	class CDaemonParams;

	using FImplementationFactory = NFunction::TCFunction<NStorage::TCUniquePointer<CDaemonImp> ()>;
	using FProcessCommand = NFunction::TCFunction<EActionResult (CDaemonParams&, CDaemon* _pDaemon, bool& _bHandled)>;
	using FErrorReporter = NFunction::TCFunction<void (NStr::CStr const&)>;
	using FInformationReporter = NFunction::TCFunction<void (NStr::CStr const&, NStr::CStr const&)>;
	using FErrorReporterYesNo = NFunction::TCFunction<EReportError (NStr::CStr const&, EReportError)>;
	using CCommandLineVector = NContainer::TCVector<NMib::NStr::CStr>;
	using COptionHandlerMap = NContainer::TCMap<NStr::CStr, NFunction::TCFunction<void (CCommandLineVector &)>>;

	class CDaemonParams
	{
	public:
		CDaemonParams
			(
				NStr::CStr const& _DaemonName
				, NStr::CStr const& _DisplayName
				, NStr::CStr const& _DaemonDesc
				, void* _pNativeHandle
				, FImplementationFactory const& _ImplementationFactory = fg_Default()
				, FProcessCommand const& _ProcessCommand = fg_Default()
				, FErrorReporter const& _ErrorReporter = fg_Default()
				, FInformationReporter const& _InformationReporter = fg_Default()
				, FErrorReporterYesNo const& _ErrorReporterYesNo = fg_Default()
			)
		;

		~CDaemonParams();

		void f_ParseCommandLine(NContainer::TCVector<NMib::NStr::CStr> const &_CommandLine);
		void* f_GetNativeHandle() const;

		NStr::CStr f_GetCustomActionKey() const;
		void f_SetAction(EDaemonAction _Action);
		EDaemonAction f_GetAction() const;
		void f_SetActionParam(CDaemonActionParam const &_Param);
		CDaemonActionParam const& f_GetActionParam() const;

		NStr::CStr f_GetDaemonGroup() const;
		bool f_GetInteractive() const;
		bool f_GetDisableWriteDaemon() const;
		void f_SetDisableWriteDaemon(bool _bDisable);
		bool f_GetKeepRunning() const;
		bool f_GetDaemonize() const;

		void f_SetDetachConsole(bool _bValue);
		bool f_GetDetachConsole() const;

		void f_SetDaemonName(NStr::CStr const &_DaemonName, bool _bCustom = true);
		NStr::CStr f_GetDaemonName() const;
		NStr::CStr f_GetDaemonDisplayName() const;
		NStr::CStr f_GetDaemonDescription() const;
		EDaemonMode f_GetDaemonMode() const;
		void f_SetDaemonMode(EDaemonMode _Mode);

		void f_SetRunAsUser(NStr::CStr const &_User);
		NStr::CStr f_GetRunAsUser() const;
		void f_SetRunAsGroup(NStr::CStr const &_Group);
		NStr::CStr f_GetRunAsGroup() const;

		void f_SetMaxShutdownTime(fp64 const &_Seconds);
		fp64 f_GetMaxShutdownTime() const;

		NContainer::TCVector<NStr::CStr> const& f_GetDaemonDependencies() const;
		void f_SetDaemonDependencies(NContainer::TCVector<NStr::CStr> _Dependencies);

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

		bool f_WriteDaemonNameFile(NMib::NStr::CStr &_Error) const;

		bool f_WriteDaemonModeFile(NMib::NStr::CStr &_Error) const;
		bool f_RemoveDaemonModeFile(NMib::NStr::CStr &_Error) const;

		NStorage::TCUniquePointer<CDaemonImp> f_ImplementationFactory() const;
		EActionResult f_ProcessCommand(CDaemon* _pDaemon, bool& _bHandled);

		void f_ReportError(NStr::CStr const& _Error) const;
		void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information) const;
		EReportError f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default) const;

		static void fs_ParseOptions(COptionHandlerMap &, CCommandLineVector &_CommandLine);
		static bool fs_ParseOptionArgument(CCommandLineVector &_CommandLine, NStr::CStr & _Destination);

		bool f_GetAlwaysRunStatusApp() const;
		void f_SetAlwaysRunStatusApp(bool _bValue);

		bool f_GetCanPause() const;
		void f_SetCanPause(bool _bValue);

		EExecutionPriority f_GetExecutionPriority() const;
		void f_SetExecutionPriority(EExecutionPriority _Priority);

		bool f_GetRequiresGraphicalSessionInUserMode() const;
		void f_SetRequiresGraphicalSessionInUserMode(bool _bValue);

	protected:
		NStr::CStr mp_DaemonName;
		NStr::CStr mp_DisplayName;
		NStr::CStr mp_DaemonDesc;
		EDaemonMode mp_DaemonMode;
		NStr::CStr mp_DaemonGroup;
		NContainer::TCVector<NStr::CStr> mp_DaemonDependencies;

		NStr::CStr mp_ExecutablePath;
		NStr::CStr mp_CommandLine;
		NContainer::TCVector<NMib::NStr::CStr> mp_lRawArguments;

		NStr::CStr mp_RunAsUser;
		NStr::CStr mp_RunAsGroup;

		EDaemonAction mp_Action;
		NStr::CStr mp_CustomAction;
		CDaemonActionParam mp_ActionParam;

		fp64 mp_MaxShutdownTime = 12_hours;

		NContainer::TCMap<NStr::CStr, NStr::CStr> mp_LocalizedStrings;

		FImplementationFactory mp_fImplementationFactory;
		FProcessCommand mp_fProcessCommand;

		FErrorReporter mp_fReportError;
		FErrorReporterYesNo mp_fReportErrorYesNo;
		FInformationReporter mp_fReportInformation;

		EExecutionPriority mp_ExecutionPriority = EExecutionPriority_Normal;

		NStr::CStr mp_AddCommandLine;

		void* mp_pNativeHandle;

		bool mp_bInteractive;
		bool mp_bCustomDaemonName;
		bool mp_bDisableWriteDaemon;
		bool mp_bKeepRunning;
		bool mp_bDetachConsole;
		bool mp_bDaemonize;
		bool mp_bAlwaysRunStatusApp = false;
		bool mp_bCanPause = true;
		bool mp_bRequiresGraphicalSessionInUserMode = true;

		NMib::NStr::CStr fp_CleanDaemonName(NMib::NStr::CStr const &_DaemonName);
		void fp_CopyElementsToCommandLine(CCommandLineVector const &_CommandLine);
	};

	enum EDaemonFeature
	{
		EDaemonFeature_GlobalDaemon = DMibBit(0)
		, EDaemonFeature_AllUsersDaemon = DMibBit(1)
		, EDaemonFeature_LocalUserDaemon = DMibBit(2)
		, EDaemonFeature_EscapedPathBroken = DMibBit(3)
		, EDaemonFeature_EscapeCharBroken = DMibBit(4)
	};

	class CDaemon
	{
	public:

		class CDetails;

		CDaemon(CDaemonParams const& _Params);
		~CDaemon();

		CDaemonParams const& f_GetDaemonParams() const;

		EActionResult f_ProcessCommand();

		EActionResult f_Start();
		EActionResult f_Stop(bool _bWait = false);
		EActionResult f_Restart(bool _bWait = false);

		EActionResult f_Add(bool _bCheckForExisting = false);
		EActionResult f_Remove();

		bool f_IsShutdown() const;

		EActionResult f_Run();

		EActionResult f_RunAsProgram(bool _bDebug);

		EActionResult f_Exists(bool &_bExists) const;

		void f_ReportError(NStr::CStr const& _Error);
		EReportError f_ReportErrorYesNo(NStr::CStr const& _Error, EReportError _Default);
		void f_ReportInformation(NStr::CStr const& _Heading, NStr::CStr const& _Information);

		static NStr::CStr fs_GetUniquePrefix();
		static EDaemonFeature fs_SupportedFeatures();
		static void fs_QuitDaemon();
		static bool fs_SupportsAutoRestart();

	protected:

		CDaemonParams mp_Params;
		NStorage::TCUniquePointer<CDetails> mp_pD;

	};

	aint fg_RunDaemon(NStr::CStr const &_DaemonIdentifier, NStr::CStr const &_Name, NStr::CStr const &_SupportEmail, FImplementationFactory const &_fImpFactory);
}

#ifndef DMibPNoShortCuts
		using namespace NMib::NDaemon;
#endif
