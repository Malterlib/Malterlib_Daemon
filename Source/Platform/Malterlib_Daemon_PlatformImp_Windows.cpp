// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NService
{
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

	void CService::f_ReportError(NStr::CStr const &_Error)
	{
		mp_pD->f_ReportError(_Error);
	}

	void CService::f_ReportInformation(NStr::CStr const &_Heading, NStr::CStr const &_Information)
	{
		mp_pD->f_ReportInformation(_Heading, _Information);
	}

	EReportError CService::f_ReportErrorYesNo(NStr::CStr const &_Error, EReportError _Default)
	{
		return mp_pD->f_ReportErrorYesNo(_Error, _Default);
	}

	void CService::fs_QuitDaemon()
	{
		CService::CDetails::fs_AbortDebug();
		NProcess::NPlatform::fg_Process_AbortWaitForTermination();
	}

	bool CService::fs_SupportsAutoRestart()
	{
		return false;
	}
}
