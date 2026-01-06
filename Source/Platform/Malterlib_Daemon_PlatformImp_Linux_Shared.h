// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Daemon/Daemon>

#include "Malterlib_Daemon_PlatformImp_Linux_Interface.h"

namespace NMib::NDaemon
{
	class CDaemonSystemInterfaceShared : public CDaemonSystemInterface
	{
	protected:
		enum ESupportedFeature
		{
			ESupportedFeature_None = 0
			, ESupportedFeature_LocalUser = DMibBit(0)
			, ESupportedFeature_AllUsers = DMibBit(1)
		};

		CDaemonSystemInterfaceShared(CDaemon *_pOwner, ESupportedFeature _SupportedFeatures);

		bool fp_CheckParamsSupported(CDaemonParams const &_Params) const;

		CDaemon *mp_pOwner;
		ESupportedFeature mp_SupportedFeatures = ESupportedFeature_None;
	};
}
