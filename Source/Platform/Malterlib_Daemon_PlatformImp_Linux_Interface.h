// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once
#include <Mib/Daemon/Daemon>

namespace NMib::NDaemon
{
	class CDaemonSystemInterface
	{
	public:
		virtual ~CDaemonSystemInterface() {};

		virtual EActionResult f_Start(CDaemonParams const &_Params) = 0;
		virtual EActionResult f_Stop(CDaemonParams const &_Params, bool _bWait = false) = 0;
		virtual EActionResult f_Restart(CDaemonParams const &_Params, bool _bWait = false) = 0;

		virtual EActionResult f_Add(CDaemonParams const &_Params, bool _bCheckForExisting = false) = 0;
		virtual EActionResult f_Remove(CDaemonParams const &_Params) = 0;

		virtual EActionResult f_Exists(CDaemonParams const &_Params, bool &_bExists) const = 0;

		virtual bool f_SupportsAutoRestart() const = 0;
	};
}
