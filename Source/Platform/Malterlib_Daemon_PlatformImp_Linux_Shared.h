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
			CService *mp_pOwner;
			
			CServiceSystemInterfaceShared(CService *_pOwner);
			
			bool fp_CheckParamsSupported(CServiceParams const &_Params) const;
		};
		
	} // namespace NService

} // namespace NMib
