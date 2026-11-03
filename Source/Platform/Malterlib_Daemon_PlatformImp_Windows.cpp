// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Windows.h"

namespace NMib::NDaemon
{
	CDaemon::CDaemon(CDaemonParams const &_Params)
		: mp_pDetails(fg_Construct(this))
		, mp_Params(_Params)
	{
	}

	CDaemon::~CDaemon()
	{
	}

	EDaemonFeature CDaemon::fs_SupportedFeatures()
	{
		return EDaemonFeature_GlobalDaemon | EDaemonFeature_LocalUserDaemon | EDaemonFeature_AllUsersDaemon;
	}

	EDaemonFeature CDaemon::fs_SessionSupportedFeatures()
	{
		return fs_SupportedFeatures();
	}

	EActionResult CDaemon::f_Start()
	{
		return mp_pDetails->f_Start();
	}

	EActionResult CDaemon::f_Stop(bool _bWait)
	{
		return mp_pDetails->f_Stop(_bWait);
	}

	EActionResult CDaemon::f_Restart(bool _bWait)
	{
		return mp_pDetails->f_Restart(_bWait);
	}

	EActionResult CDaemon::f_Exists(bool &_bExists) const
	{
		return mp_pDetails->f_Exists(_bExists);
	}

	EActionResult CDaemon::f_Add(bool _bCheckForExisting)
	{
		return mp_pDetails->f_Add(_bCheckForExisting);
	}

	EActionResult CDaemon::f_Remove()
	{
		return mp_pDetails->f_Remove();
	}

	EActionResult CDaemon::f_Run()
	{
		return mp_pDetails->f_Run();
	}

	EActionResult CDaemon::f_RunAsProgram(bool _bDebug)
	{
		return mp_pDetails->f_RunAsProgram(_bDebug);
	}

	bool CDaemon::f_IsShutdown() const
	{
		return mp_pDetails->f_IsShutdown();
	}

	void CDaemon::f_ReportError(NStr::CStr const &_Error)
	{
		mp_pDetails->f_ReportError(_Error);
	}

	void CDaemon::f_ReportInformation(NStr::CStr const &_Heading, NStr::CStr const &_Information)
	{
		mp_pDetails->f_ReportInformation(_Heading, _Information);
	}

	EReportError CDaemon::f_ReportErrorYesNo(NStr::CStr const &_Error, EReportError _Default)
	{
		return mp_pDetails->f_ReportErrorYesNo(_Error, _Default);
	}

	void CDaemon::fs_QuitDaemon()
	{
		CDaemon::CDetails::fs_AbortService();
		CDaemon::CDetails::fs_AbortDebug();
		NProcess::NPlatform::fg_Process_AbortWaitForTermination();
	}

	bool CDaemon::fs_SupportsAutoRestart()
	{
		return true;
	}
}
