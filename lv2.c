/* simple scope -- example pipe raw audio data to UI
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "lv2/lv2plug.in/ns/lv2core/lv2.h"
#include "./uris.h"

typedef struct {
  float* input[2];
  float* output[2];
  LV2_Atom_Sequence* notify;

  LV2_URID_Map* map;
  ScoLV2URIs uris;
  LV2_Atom_Forge forge;
  LV2_Atom_Forge_Frame frame;
  uint32_t n_channels;
} SiSco;

typedef enum {
  SCO_NOTIFY   = 0,
  SCO_INPUT0   = 1,
  SCO_OUTPUT0  = 2,

  SCO_INPUT1   = 3,
  SCO_OUTPUT1  = 4,
} PortIndex;


static LV2_Handle
instantiate(const LV2_Descriptor*     descriptor,
            double                    rate,
            const char*               bundle_path,
            const LV2_Feature* const* features)
{
  (void) descriptor; /* unused variable */
  (void) bundle_path; /* unused variable */

  SiSco* self = (SiSco*)calloc(1, sizeof(SiSco));
  if(!self) {
    return NULL;
  }

  int i;
  for (i=0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID__map)) {
      self->map = (LV2_URID_Map*)features[i]->data;
    }
  }

  if (!self->map) {
    fprintf(stderr, "SiSco.lv2 error: Host does not support urid:map\n");
    free(self);
    return NULL;
  }

  if (!strcmp(descriptor->URI, SCO_URI "#Stereo")) {
    self->n_channels = 2;
  } else if (!strcmp(descriptor->URI, SCO_URI "#Mono")) {
    self->n_channels = 1;
  } else {
    free(self);
    return NULL;
  }

  lv2_atom_forge_init(&self->forge, self->map);
  map_sco_uris(self->map, &self->uris);
  return (LV2_Handle)self;
}

static void
connect_port(LV2_Handle handle,
             uint32_t   port,
             void*      data)
{
  SiSco* self = (SiSco*)handle;

  switch ((PortIndex)port) {
    case SCO_NOTIFY:
      self->notify = (LV2_Atom_Sequence*)data;
      break;
    case SCO_INPUT0:
      self->input[0] = (float*) data;
      break;
    case SCO_OUTPUT0:
      self->output[0] = (float*) data;
      break;
    case SCO_INPUT1:
      self->input[1] = (float*) data;
      break;
    case SCO_OUTPUT1:
      self->output[1] = (float*) data;
      break;
  }
}

static void tx_rawaudio(LV2_Atom_Forge *forge, ScoLV2URIs *uris,
    const int32_t channel, const size_t n_samples, void *data)
{
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(forge, 0);
  lv2_atom_forge_blank(forge, &frame, 1, uris->rawaudio);
  lv2_atom_forge_property_head(forge, uris->channelid, 0);
  lv2_atom_forge_int(forge, channel);
  lv2_atom_forge_property_head(forge, uris->audiodata, 0);
  lv2_atom_forge_vector(forge, sizeof(float), uris->atom_Float, n_samples, data);
  lv2_atom_forge_pop(forge, &frame);
}

static void
run(LV2_Handle handle, uint32_t n_samples)
{
  SiSco* self = (SiSco*)handle;
  const size_t size = (sizeof(float) * n_samples + 64) * self->n_channels;
  const uint32_t capacity = self->notify->atom.size;

  if (capacity < size) {
    fprintf(stderr, "SiSco.lv2 error: LV2 comm-buffersize is insufficient.\n");
    return;
  }

  lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->notify, capacity);
  lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);

  for (uint32_t c = 0; c < self->n_channels; ++c) {
    tx_rawaudio(&self->forge, &self->uris, c, n_samples, self->input[c]);
    /* if not processing in-place, forward audio */
    if (self->input[c] != self->output[c]) {
      memcpy(self->output[c], self->input[c], sizeof(float) * n_samples);
    }
  }

  lv2_atom_forge_pop(&self->forge, &self->frame);
}

static void
cleanup(LV2_Handle handle)
{
  free(handle);
}

static const LV2_Descriptor descriptor_mono = {
  SCO_URI "#Mono",
  instantiate,
  connect_port,
  NULL,
  run,
  NULL,
  cleanup,
  NULL
};

static const LV2_Descriptor descriptor_stereo = {
  SCO_URI "#Stereo",
  instantiate,
  connect_port,
  NULL,
  run,
  NULL,
  cleanup,
  NULL
};

LV2_SYMBOL_EXPORT
const LV2_Descriptor*
lv2_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor_mono;
  case 1:
    return &descriptor_stereo;
  default:
    return NULL;
  }
}

/* vi:set ts=8 sts=2 sw=2: */
