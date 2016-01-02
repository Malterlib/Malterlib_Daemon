// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib
{
	namespace NService
	{
		CServiceSystemInterfaceShared::CServiceSystemInterfaceShared(CService *_pOwner)
			: mp_pOwner(_pOwner)
		{
		}

		bool CServiceSystemInterfaceShared::fp_CheckParamsSupported(CServiceParams const &_Params) const
		{
			bool bRet = true;

			if (CService::fs_SupportedFeatures() & EServiceFeature_EscapedPathBroken)
			{
				if (NStr::fg_StrEscapeBashQuotesNeeded(NStr::CStr(NMib::NFile::CFile::fs_GetProgramDirectory())))
				{
					mp_pOwner->f_ReportError("The service is in a directory with characters (for example space) that are unsupported on this Linux distribution");
					bRet = false;
				}
			}
			
			if (CService::fs_SupportedFeatures() & EServiceFeature_EscapeCharBroken)
			{
				if (NStr::CStr(NMib::NFile::CFile::fs_GetProgramDirectory()).f_FindChars("\"$`\\") >= 0)
				{
					mp_pOwner->f_ReportError("The service is in a directory with characters (\", $, `, \\) that are unsupported on this Linux distribution");
					bRet = false;
				}
			}
			
			if (!_Params.f_GetServiceGroup().f_IsEmpty())
			{
				mp_pOwner->f_ReportError("Service groups are not supported on this platform");
				bRet = false;
			}
			if (_Params.f_GetInteractive())
			{
				mp_pOwner->f_ReportError("Interactive services are not supported on this platform");
				bRet = false;
			}
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
			
			if (!_Params.f_GetServiceDependencies().f_IsEmpty())
			{
				mp_pOwner->f_ReportError("Service dependencies are not supported on this platform");
				bRet = false;
			}
			
			return bRet;			
		}


	} // namespace NService

} // namespace NMib
