// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib::NDaemon
{
	CDaemonSystemInterfaceShared::CDaemonSystemInterfaceShared(CDaemon *_pOwner, ESupportedFeature _SupportedFeatures)
		: mp_pOwner(_pOwner)
		, mp_SupportedFeatures(_SupportedFeatures)
	{
	}

	bool CDaemonSystemInterfaceShared::fp_CheckParamsSupported(CDaemonParams const &_Params) const
	{
		bool bRet = true;

		if (CDaemon::fs_SupportedFeatures() & EDaemonFeature_EscapedPathBroken)
		{
			if (NStr::fg_StrEscapeBashQuotesNeeded(NStr::CStr(NMib::NFile::CFile::fs_GetProgramDirectory())))
			{
				mp_pOwner->f_ReportError("The daemon is in a directory with characters (for example space) that are unsupported on this Linux distribution");
				bRet = false;
			}
		}

		if (CDaemon::fs_SupportedFeatures() & EDaemonFeature_EscapeCharBroken)
		{
			if (NStr::CStr(NMib::NFile::CFile::fs_GetProgramDirectory()).f_FindChars("\"$`\\") >= 0)
			{
				mp_pOwner->f_ReportError("The daemon is in a directory with characters (\", $, `, \\) that are unsupported on this Linux distribution");
				bRet = false;
			}
		}

		if (!_Params.f_GetDaemonGroup().f_IsEmpty())
		{
			mp_pOwner->f_ReportError("Daemon groups are not supported on this platform");
			bRet = false;
		}
		if (_Params.f_GetInteractive())
		{
			mp_pOwner->f_ReportError("Interactive services are not supported on this platform");
			bRet = false;
		}
		switch (_Params.f_GetDaemonMode())
		{
		case EDaemonMode_AllUsers:
			{
				if (!(mp_SupportedFeatures & ESupportedFeature_AllUsers))
				{
					mp_pOwner->f_ReportError("User services (-AllUsers) are not supported on this platform");
					bRet = false;
				}
			}
			break;
		case EDaemonMode_LocalUser:
			{
				if (!(mp_SupportedFeatures & ESupportedFeature_LocalUser))
				{
					mp_pOwner->f_ReportError("Local user services (-LocalUser) are not supported on this platform");
					bRet = false;
				}
			}
			break;
		case EDaemonMode_Global:
			break;
		}

		if (!_Params.f_GetDaemonDependencies().f_IsEmpty())
		{
			mp_pOwner->f_ReportError("Daemon dependencies are not supported on this platform");
			bRet = false;
		}

		return bRet;
	}
}
