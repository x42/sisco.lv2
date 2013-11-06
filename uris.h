/* pipe raw audio data to UI
 *
 * Copyright (C) 2013 Robin Gareus <robin@gareus.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SCO_URIS_H
#define SCO_URIS_H

#include "lv2/lv2plug.in/ns/ext/atom/atom.h"
#include "lv2/lv2plug.in/ns/ext/atom/forge.h"
#include "lv2/lv2plug.in/ns/ext/urid/urid.h"

#define SCO_URI "http://gareus.org/oss/lv2/sisco"

typedef struct {
	LV2_URID atom_Blank;
	LV2_URID atom_Vector;
	LV2_URID atom_Float;
	LV2_URID atom_Int;
	LV2_URID atom_eventTransfer;
	LV2_URID rawaudio;
	LV2_URID channelid;
	LV2_URID audiodata;
} ScoLV2URIs;

static inline void
map_sco_uris(LV2_URID_Map* map, ScoLV2URIs* uris) {
	uris->atom_Blank         = map->map(map->handle, LV2_ATOM__Blank);
	uris->atom_Vector        = map->map(map->handle, LV2_ATOM__Vector);
	uris->atom_Float         = map->map(map->handle, LV2_ATOM__Float);
	uris->atom_Int           = map->map(map->handle, LV2_ATOM__Int);
	uris->atom_eventTransfer = map->map(map->handle, LV2_ATOM__eventTransfer);
	uris->rawaudio           = map->map(map->handle, SCO_URI "#rawaudio");
	uris->audiodata          = map->map(map->handle, SCO_URI "#audiodata");
	uris->channelid          = map->map(map->handle, SCO_URI "#channelid");
}

#endif
