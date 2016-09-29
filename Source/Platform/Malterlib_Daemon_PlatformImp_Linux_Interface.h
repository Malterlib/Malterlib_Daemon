// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Daemon/Daemon>

namespace NMib
{
	namespace NService
	{
		class CServiceSystemInterface
		{
		public:
			virtual ~CServiceSystemInterface() {};

			virtual EActionResult f_Start(CServiceParams const &_Params) = 0;
			virtual EActionResult f_Stop(CServiceParams const &_Params, bint _bWait = false) = 0;
			virtual EActionResult f_Restart(CServiceParams const &_Params, bint _bWait = false) = 0;

			virtual EActionResult f_Add(CServiceParams const &_Params, bint _bCheckForExisting = false) = 0;
			virtual EActionResult f_Remove(CServiceParams const &_Params) = 0;

			virtual EActionResult f_Exists(CServiceParams const &_Params, bool &_bExists) const = 0;
			
			virtual bool f_SupportsAutoRestart() const = 0;
		};
		
	} // namespace NService

} // namespace NMib
