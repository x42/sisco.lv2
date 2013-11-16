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
#include "lv2/lv2plug.in/ns/ext/state/state.h"

#include "./uris.h"

static bool printed_capacity_warning = false;

typedef struct {
  /* I/O ports */
  float* input[MAX_CHANNELS];
  float* output[MAX_CHANNELS];
  const LV2_Atom_Sequence* control;
  LV2_Atom_Sequence* notify;

  /* atom-forge and URI mapping */
  LV2_URID_Map* map;
  ScoLV2URIs uris;
  LV2_Atom_Forge forge;
  LV2_Atom_Forge_Frame frame;

  uint32_t n_channels;
  double rate;

  /* the state of the UI is stored here, so that
   * the GUI can be displayed & closed
   * without loosing current settings.
   */
  bool ui_active;
  bool send_settings_to_ui;
  uint32_t ui_grid;
  struct channelstate channelstate[MAX_CHANNELS];
  struct triggerstate triggerstate;

} SiSco;

typedef enum {
  SCO_CONTROL  = 0,
  SCO_NOTIFY   = 1,
  SCO_INPUT0   = 2,
  SCO_OUTPUT0  = 3,
  SCO_INPUT1   = 4,
  SCO_OUTPUT1  = 5,
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

  self->ui_active = false;
  self->send_settings_to_ui = false;
  self->rate = rate;

  /* default settings */
  self->ui_grid = 10;
  self->triggerstate.mode = 0;
  self->triggerstate.type = 0;
  self->triggerstate.xpos = 50;
  self->triggerstate.hold = 0.5;
  self->triggerstate.level = 0.0;

  for (uint32_t c = 0; c < self->n_channels; ++c) {
    self->channelstate[c].gain = 1.0;
    self->channelstate[c].xoff = 0.0;
    self->channelstate[c].yoff = 0.0;
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
    case SCO_CONTROL:
      self->control = (const LV2_Atom_Sequence*)data;
      break;
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

/** forge atom-vector of raw data */
static void tx_rawaudio(LV2_Atom_Forge *forge, ScoLV2URIs *uris,
    const int32_t channel, const size_t n_samples, void *data)
{
  LV2_Atom_Forge_Frame frame;
  /* forge container object of type 'rawaudio' */
  lv2_atom_forge_frame_time(forge, 0);
  lv2_atom_forge_blank(forge, &frame, 1, uris->rawaudio);

  /* add integer attribute 'channelid' */
  lv2_atom_forge_property_head(forge, uris->channelid, 0);
  lv2_atom_forge_int(forge, channel);

  /* add vector of floats raw 'audiodata' */
  lv2_atom_forge_property_head(forge, uris->audiodata, 0);
  lv2_atom_forge_vector(forge, sizeof(float), uris->atom_Float, n_samples, data);

  /* close off atom-object */
  lv2_atom_forge_pop(forge, &frame);
}

static void
run(LV2_Handle handle, uint32_t n_samples)
{
  SiSco* self = (SiSco*)handle;
  const size_t size = (sizeof(float) * n_samples + 64) * self->n_channels;
  const uint32_t capacity = self->notify->atom.size;

  /* check if atom-port buffer is large enough to hold
   * all audio-samples and configuration settings */
  if (capacity < size + 160 + self->n_channels * 32) {
    if (!printed_capacity_warning) {
      fprintf(stderr, "SiSco.lv2 error: LV2 comm-buffersize is insufficient %d/%d bytes.\n",
	  capacity, size + 160 + self->n_channels * 32);
      printed_capacity_warning = true;
    }
    return;
  }

  /* prepare forge buffer and initialize atom-sequence */
  lv2_atom_forge_set_buffer(&self->forge, (uint8_t*)self->notify, capacity);
  lv2_atom_forge_sequence_head(&self->forge, &self->frame, 0);

  /* Send settings to UI */
  if (self->send_settings_to_ui && self->ui_active) {
    self->send_settings_to_ui = false;
    /* forge container object of type 'ui_state' */
    LV2_Atom_Forge_Frame frame;
    lv2_atom_forge_frame_time(&self->forge, 0);
    lv2_atom_forge_blank(&self->forge, &frame, 1, self->uris.ui_state);
    /* forge attributes for 'ui_state' */
    lv2_atom_forge_property_head(&self->forge, self->uris.samplerate, 0);
    lv2_atom_forge_float(&self->forge, self->rate);
    lv2_atom_forge_property_head(&self->forge, self->uris.ui_state_grid, 0);
    lv2_atom_forge_int(&self->forge, self->ui_grid);
    lv2_atom_forge_property_head(&self->forge, self->uris.ui_state_trig, 0);
    lv2_atom_forge_vector(&self->forge, sizeof(float), self->uris.atom_Float,
	sizeof(struct triggerstate) / sizeof(float), &self->triggerstate);
    lv2_atom_forge_property_head(&self->forge, self->uris.ui_state_chn, 0);
    lv2_atom_forge_vector(&self->forge, sizeof(float), self->uris.atom_Float,
	self->n_channels * sizeof(struct channelstate) / sizeof(float), self->channelstate);
    lv2_atom_forge_pop(&self->forge, &frame);
  }

  /* Process incoming events from GUI */
  if (self->control) {
    LV2_Atom_Event* ev = lv2_atom_sequence_begin(&(self->control)->body);
    /* for each message from UI... */
    while(!lv2_atom_sequence_is_end(&(self->control)->body, (self->control)->atom.size, ev)) {
      /* .. only look at atom-events.. */
      if (ev->body.type == self->uris.atom_Blank) {
	const LV2_Atom_Object* obj = (LV2_Atom_Object*)&ev->body;
	/* interpret atom-objects: */
	if (obj->body.otype == self->uris.ui_on) {
	  /* UI was activated */
	  self->ui_active = true;
	  self->send_settings_to_ui = true;
	} else if (obj->body.otype == self->uris.ui_off) {
	  /* UI was closed */
	  self->ui_active = false;
	} else if (obj->body.otype == self->uris.ui_state) {
	  /* UI sends current settings */
	  const LV2_Atom* grid = NULL;
	  const LV2_Atom* trig = NULL;
	  const LV2_Atom* chn = NULL;
	  lv2_atom_object_get(obj,
	      self->uris.ui_state_grid, &grid,
	      self->uris.ui_state_trig, &trig,
	      self->uris.ui_state_chn, &chn,
	      0);
	  if (grid && grid->type == self->uris.atom_Int) {
	    self->ui_grid = ((LV2_Atom_Int*)grid)->body;
	  }
	  if (trig && trig->type == self->uris.atom_Vector) {
	    LV2_Atom_Vector *vof = (LV2_Atom_Vector*)LV2_ATOM_BODY(trig);
	    if (vof->atom.type == self->uris.atom_Float) {
	      struct triggerstate *ts = (struct triggerstate *) LV2_ATOM_BODY(&vof->atom);
	      memcpy(&self->triggerstate, ts, sizeof(struct triggerstate));
	    }
	  }
	  if (chn && chn->type == self->uris.atom_Vector) {
	    LV2_Atom_Vector *vof = (LV2_Atom_Vector*)LV2_ATOM_BODY(chn);
	    if (vof->atom.type == self->uris.atom_Float) {
	      struct channelstate *cs = (struct channelstate *) LV2_ATOM_BODY(&vof->atom);
	      memcpy(self->channelstate, cs, self->n_channels * sizeof(struct channelstate));
	    }
	  }
	}
      }
      ev = lv2_atom_sequence_next(ev);
    }
  }

  /* process audio data */
  for (uint32_t c = 0; c < self->n_channels; ++c) {
    if (self->ui_active) {
      /* if UI is active, send raw audio data to UI */
      tx_rawaudio(&self->forge, &self->uris, c, n_samples, self->input[c]);
    }
    /* if not processing in-place, forward audio */
    if (self->input[c] != self->output[c]) {
      memcpy(self->output[c], self->input[c], sizeof(float) * n_samples);
    }
  }

  /* close off atom-sequence */
  lv2_atom_forge_pop(&self->forge, &self->frame);
}

static void
cleanup(LV2_Handle handle)
{
  free(handle);
}

struct VectorOfFloat {
  LV2_Atom_Vector_Body vb;
  float    cfg[(3 * MAX_CHANNELS)]; // XXX at least 5 floats, also used for triggerstate
};

static LV2_State_Status
state_save(
    LV2_Handle                instance,
    LV2_State_Store_Function  store,
    LV2_State_Handle          handle,
    uint32_t                  flags,
    const LV2_Feature* const* features)
{
  SiSco* self = (SiSco*)instance;
  if (!self) return LV2_STATE_SUCCESS;
  store(handle, self->uris.ui_state_grid,
      (void*) &self->ui_grid, sizeof(uint32_t),
      self->uris.atom_Int,
      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

  struct VectorOfFloat vof;
  vof.vb.child_type = self->uris.atom_Float;
  vof.vb.child_size = sizeof(float);

  assert (sizeof(struct triggerstate) <= sizeof(vof.cfg));
  assert (self->n_channels * sizeof(struct channelstate) <= sizeof(vof.cfg));

  memcpy(&vof.cfg, &self->triggerstate, sizeof(struct triggerstate));
  store(handle, self->uris.ui_state_trig,
      (void*) &vof, sizeof(LV2_Atom_Vector_Body) + sizeof(struct triggerstate),
      self->uris.atom_Vector,
      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);

  memcpy(&vof.cfg, self->channelstate, self->n_channels * sizeof(struct channelstate));
  store(handle, self->uris.ui_state_chn,
      (void*) &vof, sizeof(LV2_Atom_Vector_Body) + self->n_channels * sizeof(struct channelstate),
      self->uris.atom_Vector,
      LV2_STATE_IS_POD | LV2_STATE_IS_PORTABLE);
  return LV2_STATE_SUCCESS;
}

static LV2_State_Status
state_restore(
    LV2_Handle                  instance,
    LV2_State_Retrieve_Function retrieve,
    LV2_State_Handle            handle,
    uint32_t                    flags,
    const LV2_Feature* const*   features)
{
  SiSco* self = (SiSco*)instance;
  size_t   size;
  uint32_t type;
  uint32_t valflags;

  const void * value = retrieve(handle, self->uris.ui_state_grid, &size, &type, &valflags);
  if (value && size == sizeof(uint32_t) && type == self->uris.atom_Int) {
    self->ui_grid = *((uint32_t*) value);
    self->send_settings_to_ui = true;
  }

  value = retrieve(handle, self->uris.ui_state_trig, &size, &type, &valflags);
  if (value
      && size == sizeof(LV2_Atom_Vector_Body) + sizeof(struct triggerstate)
      && type == self->uris.atom_Vector) {
    memcpy(&self->triggerstate, LV2_ATOM_BODY(value), sizeof(struct triggerstate));
    self->send_settings_to_ui = true;
  }

  value = retrieve(handle, self->uris.ui_state_chn, &size, &type, &valflags);
  if (value
      && size == sizeof(LV2_Atom_Vector_Body) + self->n_channels * sizeof(struct channelstate)
      && type == self->uris.atom_Vector) {
    memcpy(self->channelstate, LV2_ATOM_BODY(value), self->n_channels * sizeof(struct channelstate));
    self->send_settings_to_ui = true;
  }
  return LV2_STATE_SUCCESS;
}


static const void*
extension_data(const char* uri)
{
  static const LV2_State_Interface  state  = { state_save, state_restore };
  if (!strcmp(uri, LV2_STATE__interface)) {
    return &state;
  }
  return NULL;
}


static const LV2_Descriptor descriptor_mono = {
  SCO_URI "#Mono",
  instantiate,
  connect_port,
  NULL,
  run,
  NULL,
  cleanup,
  extension_data
};

static const LV2_Descriptor descriptor_stereo = {
  SCO_URI "#Stereo",
  instantiate,
  connect_port,
  NULL,
  run,
  NULL,
  cleanup,
  extension_data
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
