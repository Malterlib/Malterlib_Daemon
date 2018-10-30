// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Daemon/Daemon>

#include "Malterlib_Daemon_PlatformImp_Linux_Interface.h"

namespace NMib
{
	namespace NService
	{
		class CServiceSystemInterfaceShared : public CServiceSystemInterface
		{
		protected:
			enum ESupportedFeature
			{
				ESupportedFeature_None = 0
				, ESupportedFeature_LocalUser = DMibBit(0)
				, ESupportedFeature_AllUsers = DMibBit(1)
			};
				
			CServiceSystemInterfaceShared(CService *_pOwner, ESupportedFeature _SupportedFeatures);

			bool fp_CheckParamsSupported(CServiceParams const &_Params) const;

			CService *mp_pOwner;
			ESupportedFeature mp_SupportedFeatures = ESupportedFeature_None;
		};
		
	} // namespace NService

} // namespace NMib
