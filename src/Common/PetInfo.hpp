/*
Copyright (C) 2008-2016 Vana Development Team

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; version 2
of the License.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
#pragma once

#include "Common/Types.hpp"
#include <string>

namespace Vana {
	struct PetInfo {
		bool noRevive = false;
		bool noStoringInCashShop = false;
		bool autoReact = false;
		int8_t evolveLevel = 0;
		int32_t hunger = 0;
		int32_t life = 0;
		int32_t limitedLife = 0;
		item_id_t evolveItem = 0;
		string_t name;
	};
}