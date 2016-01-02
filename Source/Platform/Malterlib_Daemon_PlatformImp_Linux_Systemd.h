// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include <Mib/Daemon/Daemon>
#include "Malterlib_Daemon_PlatformImp_Linux_Shared.h"

namespace NMib
{
	namespace NService
	{
		class CService;

		class CSystemd : public CServiceSystemInterfaceShared
		{
		private:

			NStr::CStr fp_GetUnitConfigDirectory(EServiceMode _Mode) const;
			bool fp_IsUnitConfigThisExecutable(CService *pOwner, CServiceParams const &_Params) const;
			EActionResult fp_SetUnitEnable(CServiceParams const &_Params, bint _bEnable) const;
			EActionResult fp_IsUnitEnabled(CServiceParams const &_Params, bint& _bIsEnabled) const;
			
			NStr::CStr mp_SystemdSystemUnitDirectory;
			NStr::CStr mp_SystemdUserUnitDirectory;

		public:
			CSystemd(CService *);
			~CSystemd();

			static bint fs_IsSupported();

			virtual EActionResult f_Start(CServiceParams const &_Params) override;
			virtual EActionResult f_Stop(CServiceParams const &_Params, bint _bWait = false) override;

			virtual EActionResult f_Add(CServiceParams const &_Params, bint _bCheckForExisting = false) override;
			virtual EActionResult f_Remove(CServiceParams const &_Params) override;

			virtual EActionResult f_Exists(CServiceParams const &_Params, bool &_bExists) const override;
		};

	} // namespace NService

} // namespace NMib
