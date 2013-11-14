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
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

///////////////////////
#define WITH_TRIGGER
#define WITH_RESAMPLING
#undef  LIMIT_YSCALE
#define WITH_MARKERS
///////////////////////

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <gtk/gtk.h>
#include <cairo.h>

#include "lv2/lv2plug.in/ns/extensions/ui/ui.h"
#include "./uris.h"

#ifdef WITH_RESAMPLING
#include "./zita-resampler/resampler.h"
using namespace LV2S;
#endif

#ifndef MIN
#define MIN(A,B) ( (A) < (B) ? (A) : (B) )
#endif

enum TriggerState {
  TS_DISABLED = 0,
  TS_INITIALIZING,
  TS_WAITMANUAL,
  TS_PREBUFFER,
  TS_TRIGGERED,
  TS_COLLECT,
  TS_END,
  TS_DELAY,
};

/* drawing area size */
#define DAWIDTH  (640)
#define DAHEIGHT (200) // per channel (!)

#define ANHEIGHT (20)  // annotation footer
#define ANWIDTH  (10)  // annotation right-side

/* trigger buffer - 2 * sizeof max audio-buffer size
 * times two becasue with trigger pos at 100% it must be able
 * to hold the prev buffer and the next buffer without
 * overwriting the any data.
 */
#ifdef WITH_RESAMPLING
#define MAX_UPSAMPLING (12)
#define TRBUFSZ  (16384 * MAX_UPSAMPLING)
#else
#define TRBUFSZ  (16384)
#endif

/* max continuous points on path.
 * many short-path segments are expensive|inefficient
 * long paths are not supported by all surfaces
 * (usually its a miter - not point - limit,
 * depending on used cairo backend)
 */
#define MAX_CAIRO_PATH (128)


typedef struct {
  float *data_min;
  float *data_max;

  uint32_t idx;
  uint32_t sub;
  uint32_t bufsiz;
  pthread_mutex_t lock;
} ScoChan;

#ifdef WITH_MARKERS
typedef struct {
  uint32_t xpos;
  uint32_t chn;
  float ymin, ymax; // derived
} MarkerX;
#endif

typedef struct {
  LV2_Atom_Forge forge;
  LV2_URID_Map*  map;
  ScoLV2URIs     uris;

  LV2UI_Write_Function write;
  LV2UI_Controller controller;
  GtkWidget *hbox, *ctable;

  GtkWidget *sep[3];
  GtkWidget *darea;
  GtkWidget *btn_pause;
  GtkWidget *lbl_speed, *lbl_amp, *lbl_off_x, *lbl_off_y;
  GtkWidget *lbl_chn[MAX_CHANNELS];
  GtkWidget *spb_amp[MAX_CHANNELS];
  GtkWidget *cmx_speed;
  GtkWidget *spb_yoff[MAX_CHANNELS], *spb_xoff[MAX_CHANNELS];
  GtkAdjustment *spb_amp_adj[MAX_CHANNELS];
  GtkAdjustment *spb_yoff_adj[MAX_CHANNELS], *spb_xoff_adj[MAX_CHANNELS];

  cairo_surface_t *gridnlabels;
  PangoFontDescription *font[2];

  ScoChan  chn[MAX_CHANNELS];
  float    xoff[MAX_CHANNELS];
  float    yoff[MAX_CHANNELS];
  float    gain[MAX_CHANNELS];
  float    grid_spacing;
  uint32_t stride;
  uint32_t n_channels;
  bool     paused;
  bool     update_ann;
  float    rate;
  uint32_t cur_period;

#ifdef WITH_TRIGGER
  GtkWidget     *cmx_trigger_mode;
  GtkWidget     *cmx_trigger_type;
  GtkWidget     *btn_trigger_man;
  GtkWidget     *spb_trigger_lvl;
  GtkAdjustment *spb_trigger_lvl_adj;
  GtkWidget     *spb_trigger_pos;
  GtkAdjustment *spb_trigger_pos_adj;
  GtkWidget     *spb_trigger_hld;
  GtkAdjustment *spb_trigger_hld_adj;
  GtkWidget     *lbl_tpos, *lbl_tlvl, *lbl_thld, *lbl_trig;

  uint32_t trigger_cfg_pos;
  float    trigger_cfg_lvl;
  uint32_t trigger_cfg_channel;

  uint32_t trigger_cfg_mode;
  uint32_t trigger_cfg_type;

  enum TriggerState trigger_state;
  enum TriggerState trigger_state_n;

  ScoChan  trigger_buf[MAX_CHANNELS];
  float    trigger_prev;
  uint32_t trigger_offset;
  uint32_t trigger_delay;
  bool     trigger_collect_ok;
  bool     trigger_manual;
#endif

#ifdef WITH_RESAMPLING
  Resampler *src[MAX_CHANNELS];
  float src_fact;
  float src_buf[MAX_CHANNELS][TRBUFSZ]; // TODO dyn alloc
#endif

#ifdef WITH_MARKERS
  MarkerX mrk[2];
  GtkWidget     *lbl_marker;
  GtkWidget     *lbl_mpos0, *lbl_mpos1, *lbl_mchn0, *lbl_mchn1;
  GtkWidget     *spb_marker_x0, *spb_marker_c0;
  GtkAdjustment *spb_marker_x0_adj, *spb_marker_c0_adj;
  GtkWidget     *spb_marker_x1, *spb_marker_c1;
  GtkAdjustment *spb_marker_x1_adj, *spb_marker_c1_adj;
#endif
} SiScoUI;

static const float color_grd[4] = {0.9, 0.9, 0.0, 0.3};
static const float color_zro[4] = {0.4, 0.4, 0.6, 0.3};
static const float color_trg[4] = {0.1, 0.1, 0.9, 0.9};
static const float color_lvl[4] = {0.3, 0.3, 1.0, 1.0};
static const float color_tbg[4] = {0.0, 0.0, 0.0, 0.5};

static const float color_blk[4] = {0.0, 0.0, 0.0, 1.0};
static const float color_gry[4] = {0.5, 0.5, 0.5, 1.0};
static const float color_wht[4] = {1.0, 1.0, 1.0, 1.0};


static const float color_chn[MAX_CHANNELS][4] = {
  {0.0, 1.0, 0.0, 1.0},
  {1.0, 0.0, 0.0, 1.0}
};

static void update_annotations(SiScoUI* ui);

#define CairoSetSouerceRGBA(COL) \
  cairo_set_source_rgba (cr, COL[0], COL[1], COL[2], COL [3])


#ifdef WITH_RESAMPLING
/******************************************************************************
 * Setup re-sampling (upsample)
 */
static void setup_src(SiScoUI* ui, float oversample) {
  const int hlen = 12; // 8..96
  const float frel = 1.0; // 1.0 - 2.6 / (float) hlen;
  uint32_t bsiz = 8192;
  float *scratch = (float*) calloc(bsiz, sizeof(float));
  float *resampl = (float*) malloc(bsiz * oversample * sizeof(float));

  ui->src_fact = oversample;

  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    if (ui->src[c] != 0) {
      delete ui->src[c];
      ui->src[c] = 0;
    }
    if (oversample <= 1) continue;

    ui->src[c] = new Resampler();
    ui->src[c]->setup(ui->rate, ui->rate * oversample, 1, hlen, frel);

    /* q/d initialize */
    ui->src[c]->inp_count = bsiz;
    ui->src[c]->inp_data = scratch;
    ui->src[c]->out_count = bsiz * oversample;
    ui->src[c]->out_data = resampl;
    ui->src[c]->process ();
  }

  free(scratch);
  free(resampl);
}
#endif


/******************************************************************************
 * Allocate Data structures
 */


static void zero_sco_chan(ScoChan *sc) {
  sc->idx = 0;
  sc->sub = 0;
  memset(sc->data_min, 0, sizeof(float) * sc->bufsiz);
  memset(sc->data_max, 0, sizeof(float) * sc->bufsiz);
}

static void alloc_sco_chan(ScoChan *sc) {
  sc->data_min = (float*) malloc(sizeof(float) * sc->bufsiz);
  sc->data_max = (float*) malloc(sizeof(float) * sc->bufsiz);
  zero_sco_chan(sc);
  pthread_mutex_init(&sc->lock, NULL);
}

static void free_sco_chan(ScoChan *sc) {
  pthread_mutex_destroy(&sc->lock);
  free(sc->data_min);
  free(sc->data_max);
}


#ifdef WITH_TRIGGER
static void setup_trigger(SiScoUI* ui) {
  ui->trigger_state_n = TS_INITIALIZING;
}
#endif


/******************************************************************************
 * Communication with DSP backend -- send/receive settings
 */


/** send current settings to backend */
static void ui_state(LV2UI_Handle handle)
{
  SiScoUI* ui = (SiScoUI*)handle;
  uint8_t obj_buf[4096];
  struct channelstate cs[MAX_CHANNELS];
  const int32_t grid = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_speed));
#ifdef WITH_TRIGGER
  struct triggerstate ts;
  ts.mode = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_trigger_mode));
  ts.type = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_trigger_type));
  ts.xpos = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_pos));
  ts.hold = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_hld));
  ts.level = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_lvl));
#endif

  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    cs[c].gain = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_amp[c]));
    cs[c].xoff = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_xoff[c]));
    cs[c].yoff = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_yoff[c]));
  }

  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 1024);
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&ui->forge, 0);
  LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(&ui->forge, &frame, 1, ui->uris.ui_state);


  lv2_atom_forge_property_head(&ui->forge, ui->uris.ui_state_grid, 0);
  lv2_atom_forge_int(&ui->forge, grid);

#ifdef WITH_TRIGGER
  lv2_atom_forge_property_head(&ui->forge, ui->uris.ui_state_trig, 0);
  lv2_atom_forge_vector(&ui->forge, sizeof(float), ui->uris.atom_Float,
      sizeof(struct triggerstate) / sizeof(float), &ts);
#endif
  lv2_atom_forge_property_head(&ui->forge, ui->uris.ui_state_chn, 0);
  lv2_atom_forge_vector(&ui->forge, sizeof(float), ui->uris.atom_Float,
      ui->n_channels * sizeof(struct channelstate) / sizeof(float), cs);

  lv2_atom_forge_pop(&ui->forge, &frame);
  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

/** notfiy backend that UI is closed */
static void ui_disable(LV2UI_Handle handle)
{
  SiScoUI* ui = (SiScoUI*)handle;
  ui_state(handle);

  uint8_t obj_buf[64];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&ui->forge, 0);
  LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(&ui->forge, &frame, 1, ui->uris.ui_off);
  lv2_atom_forge_pop(&ui->forge, &frame);
  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

/** notify backend that UI is active:
 * request state and enable data-transmission */
static void ui_enable(LV2UI_Handle handle)
{
  SiScoUI* ui = (SiScoUI*)handle;
  uint8_t obj_buf[64];
  lv2_atom_forge_set_buffer(&ui->forge, obj_buf, 64);
  LV2_Atom_Forge_Frame frame;
  lv2_atom_forge_frame_time(&ui->forge, 0);
  LV2_Atom* msg = (LV2_Atom*)lv2_atom_forge_blank(&ui->forge, &frame, 1, ui->uris.ui_on);
  lv2_atom_forge_pop(&ui->forge, &frame);
  ui->write(ui->controller, 0, lv2_atom_total_size(msg), ui->uris.atom_eventTransfer, msg);
}

static void apply_state_chn(SiScoUI* ui, LV2_Atom_Vector* vof) {
  if (vof->atom.type != ui->uris.atom_Float) {
    return;
  }
  struct channelstate *cs = (struct channelstate *) LV2_ATOM_BODY(&vof->atom);
  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_amp[c]), cs[c].gain);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_xoff[c]), cs[c].xoff);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_yoff[c]), cs[c].yoff);
  }
}

#ifdef WITH_TRIGGER
static void apply_state_trig(SiScoUI* ui, LV2_Atom_Vector* vof) {
  if (vof->atom.type != ui->uris.atom_Float) {
    return;
  }
  struct triggerstate *ts = (struct triggerstate *) LV2_ATOM_BODY(&vof->atom);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_trigger_lvl), ts->level);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_trigger_pos), ts->xpos);
  gtk_spin_button_set_value(GTK_SPIN_BUTTON(ui->spb_trigger_hld), ts->hold);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_trigger_type), ts->type);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_trigger_mode), ts->mode);
}
#endif

/******************************************************************************
 * GTK WIDGET CALLBACKS
 */


static gboolean cfg_changed (GtkWidget *widget, gpointer data)
{
  ui_state(data);
  return TRUE;
}

#ifdef WITH_MARKERS
static gboolean mrk_changed (GtkWidget *widget, gpointer data)
{
  SiScoUI* ui = (SiScoUI*) data;
  if (ui->paused) {
    gtk_widget_queue_draw(ui->darea);
  }
  return TRUE;
}
#endif

#ifdef WITH_TRIGGER
static gboolean trigger_btn_callback (GtkWidget *widget, gpointer data)
{
  SiScoUI* ui = (SiScoUI*) data;
  if (ui->trigger_cfg_mode == 1) {
    ui->trigger_manual = true;
  }
  return TRUE;
}

static gboolean trigger_cmx_callback (GtkWidget *widget, gpointer data)
{
  SiScoUI* ui = (SiScoUI*) data;
  ui->trigger_cfg_mode = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_trigger_mode));
  // set widget sensitivity depending on mode
  gtk_widget_set_sensitive(ui->btn_trigger_man, ui->trigger_cfg_mode == 1);
  gtk_widget_set_sensitive(ui->spb_trigger_lvl, true);
  ui->trigger_manual = false;

  switch(ui->trigger_cfg_mode) {
    default:
    case 0:
      gtk_widget_set_sensitive(ui->btn_pause, true);
      gtk_widget_set_sensitive(ui->spb_trigger_hld, false);
      ui->trigger_state_n = TS_DISABLED;
      break;
    case 1:
      gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(ui->btn_pause), false);
      gtk_widget_set_sensitive(ui->btn_pause, false);
      gtk_widget_set_sensitive(ui->spb_trigger_hld, false);
      setup_trigger(ui);
      break;
    case 2:
      gtk_widget_set_sensitive(ui->btn_pause, true);
      gtk_widget_set_sensitive(ui->spb_trigger_hld, true);
      setup_trigger(ui);
      break;
  }

  ui_state(data);
  gtk_widget_queue_draw(ui->darea);
  return TRUE;
}
#endif


/******************************************************************************
 * Data Preprocessing and triggering logic
 */


/** parse raw audio data from and prepare for later drawing
 *
 * NB. This is a very simple & stupid example.
 * any serious scope will not display samples as is.
 * Signals above maybe 1/10 of the sampling-rate will not yield
 * a useful visual display and result in a rather unintuitive
 * representation of the actual waveform.
 *
 * ideally the audio-data would be buffered and upsampled here
 * and after that written in a display buffer for later use.
 *
 * please see
 * https://wiki.xiph.org/Videos/Digital_Show_and_Tell
 * and http://lac.linuxaudio.org/2013/papers/36.pdf
 *
 * This simple algorithm serves as example how *not* to do it.
 */
static int process_channel(SiScoUI *ui, ScoChan *chn,
    const size_t n_elem, float const *data,
    uint32_t *idx_start, uint32_t *idx_end)
{
  /* TODO: write into ringbuffer instead of locking data.
   * possibly draw directly into a cairo-surface (that can later be annotated)
   */
  int overflow = 0;
  *idx_start = chn->idx;
  for (uint32_t i = 0; i < n_elem; ++i) {
    if (data[i] < chn->data_min[chn->idx]) { chn->data_min[chn->idx] = data[i]; }
    if (data[i] > chn->data_max[chn->idx]) { chn->data_max[chn->idx] = data[i]; }
    if (++chn->sub >= ui->stride) {
      chn->sub = 0;
      chn->idx = (chn->idx + 1) % chn->bufsiz;
      if (chn->idx == 0) {
	++overflow;
      }
      chn->data_min[chn->idx] =  1.0;
      chn->data_max[chn->idx] = -1.0;
    }
  }
  *idx_end = chn->idx;
  return overflow;
}


#ifdef WITH_TRIGGER
static int process_trigger(SiScoUI* ui, uint32_t channel, size_t *n_samples_p, float const *audiobuffer)
{
  size_t n_samples = *n_samples_p;

  if (ui->trigger_state == TS_DISABLED) {
    return 0;
  }

  else if (ui->trigger_state == TS_INITIALIZING) {
    if (ui->trigger_cfg_mode == 1) {
      ui->trigger_state_n = TS_WAITMANUAL;
    } else {
      ui->trigger_state_n = TS_PREBUFFER;
    }
    ui->trigger_collect_ok = false;
    zero_sco_chan(&ui->trigger_buf[channel]);
    if (ui->trigger_cfg_mode == 1) {
      zero_sco_chan(&ui->chn[channel]);
    }
    ui->trigger_prev = ui->trigger_cfg_lvl;

    if (channel + 1 == ui->n_channels) {
      if (ui->update_ann) { update_annotations(ui); }
      gtk_widget_queue_draw(ui->darea);
    }
    return -1;
  }

  else if (ui->trigger_state == TS_WAITMANUAL) {
    if (ui->trigger_manual) {
      gtk_widget_set_sensitive(ui->btn_trigger_man, false);
      gtk_widget_set_sensitive(ui->spb_trigger_lvl, false);
      ui->trigger_manual = false;
      ui->trigger_state_n = TS_PREBUFFER;
    }
    return -1;
  }

  else if (ui->trigger_state == TS_PREBUFFER) {
    uint32_t idx_start, idx_end;
    idx_start = idx_end = 0;

    int overflow = process_channel(ui, &ui->trigger_buf[channel], n_samples, audiobuffer, &idx_start, &idx_end);
    size_t trigger_scan_start;

    if (channel != ui->trigger_cfg_channel) {
      return -1;
    }

    if (ui->trigger_collect_ok) {
      trigger_scan_start = 0;
    } else if (overflow > 0 || idx_end >= ui->trigger_cfg_pos) {
      ui->trigger_collect_ok = true;
      uint32_t voff = (idx_end + TRBUFSZ - ui->trigger_cfg_pos) % TRBUFSZ;
      assert(n_samples >= voff * ui->stride);
      trigger_scan_start = n_samples - voff * ui->stride;
    } else {
      /* no scan yet, keep buffering */
      return -1;
    }

    const float trigger_lvl = ui->trigger_cfg_lvl;
    if (ui->trigger_cfg_type == 0) {
      // RISING EDGE
      for (uint32_t i = trigger_scan_start; i < n_samples; ++i) {
	if (ui->trigger_prev < trigger_lvl && audiobuffer[i] >= trigger_lvl) {
	  ui->trigger_state_n = TS_TRIGGERED;
	  ui->trigger_offset = idx_start + i / ui->stride;
	  break;
	}
	ui->trigger_prev = audiobuffer[i];
      }
    } else {
      // FALLING EDGE
      for (uint32_t i = trigger_scan_start; i < n_samples; ++i) {
	if (ui->trigger_prev > trigger_lvl && audiobuffer[i] <= trigger_lvl) {
	  ui->trigger_state_n = TS_TRIGGERED;
	  ui->trigger_offset = idx_start + i / ui->stride;
	  break;
	}
	ui->trigger_prev = audiobuffer[i];
      }
    }
    return -1;
  }

  else if (ui->trigger_state == TS_TRIGGERED) {
    ScoChan *chn = &ui->chn[channel];
    ScoChan *tbf = &ui->trigger_buf[channel];
    zero_sco_chan(chn);
    const uint32_t pos = ui->trigger_cfg_pos;

    const uint32_t ofx = ui->trigger_offset % TRBUFSZ;
    const uint32_t exs = (tbf->idx + TRBUFSZ - ofx) % TRBUFSZ;

    // when  i == pos;  then (i+off)%DW == ui->trigger_offset
    const uint32_t off = (ui->trigger_offset + TRBUFSZ - pos) % TRBUFSZ;
    const uint32_t ncp = MIN(DAWIDTH, ui->trigger_cfg_pos + exs + 1);

    for (uint32_t i=0; i < ncp; ++i) {
      chn->data_min[i] = tbf->data_min[(i+off)%TRBUFSZ];
      chn->data_max[i] = tbf->data_max[(i+off)%TRBUFSZ];
    }
    chn->idx = (ncp + DAWIDTH - 1)%DAWIDTH;
    chn->sub = tbf->sub;

    if (channel + 1 == ui->n_channels) {
      if (ui->update_ann) { update_annotations(ui); }
      gtk_widget_queue_draw(ui->darea);
    }

    if (ncp == DAWIDTH) {
      ui->trigger_state_n = TS_END;
      return -1;
    } else {
      ui->trigger_state_n = TS_COLLECT;
      const size_t max_remain = MIN(n_samples, (DAWIDTH - chn->idx - 1) * ui->stride);
      *n_samples_p = max_remain;
      return 0;
    }
  }

  else if (ui->trigger_state == TS_COLLECT) {
    // limit-audio-data
    ScoChan *chn = &ui->chn[channel];
    const size_t max_remain = MIN(n_samples, (DAWIDTH - chn->idx - 1) * ui->stride);
    if (max_remain < n_samples) {
      ui->trigger_state_n = TS_END;
    }
    *n_samples_p = max_remain;
    return 0;
  }

  else if (ui->trigger_state == TS_END) {
    if (ui->trigger_cfg_mode == 2) {
      float holdoff = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_hld));
      if (holdoff > 0) {
	ui->trigger_state_n = TS_DELAY;
	ui->trigger_delay = ceilf(holdoff * ui->rate / ui->cur_period);
      } else {
	ui->trigger_state_n = TS_INITIALIZING;
      }
    } else if (ui->trigger_cfg_mode == 1) {
      gtk_widget_set_sensitive(ui->btn_trigger_man, true);
      gtk_widget_set_sensitive(ui->spb_trigger_lvl, true);
      if (ui->trigger_manual) {
	gtk_widget_set_sensitive(ui->btn_trigger_man, false);
	gtk_widget_set_sensitive(ui->spb_trigger_lvl, false);
	ui->trigger_state_n = TS_INITIALIZING;
      }
    }
    return -1;
  }

  else if (ui->trigger_state == TS_DELAY) {
    if (ui->trigger_delay == 0) {
      ui->trigger_state_n = TS_INITIALIZING;
    }
    return -1;
  }

  else {
    fprintf(stderr, "INVALID Trigger state!\n");
    return -1;
  }

}
#endif

/******************************************************************************
 * Pango / Cairo Rendering, Expose
 */

static void render_text(
    cairo_t* cr,
    const char *txt,
    PangoFontDescription *font,
    const float x, const float y,
    const float ang, const int align,
    const float * const col)
{
  int tw, th;
  cairo_save(cr);

  PangoLayout * pl = pango_cairo_create_layout(cr);
  pango_layout_set_font_description(pl, font);
  pango_layout_set_text(pl, txt, -1);
  pango_layout_get_pixel_size(pl, &tw, &th);
  cairo_translate (cr, x, y);
  if (ang != 0) { cairo_rotate (cr, ang); }
  switch(abs(align)) {
    case 1:
      cairo_translate (cr, -tw, -th/2.0);
      break;
    case 2:
      cairo_translate (cr, -tw/2.0 - 0.5, -th/2.0);
      break;
    case 3:
      cairo_translate (cr, -0.5, -th/2.0);
      break;
    case 4:
      cairo_translate (cr, -tw, -th);
      break;
    case 5:
      cairo_translate (cr, -tw/2.0 - 0.5, -th);
      break;
    case 6:
      cairo_translate (cr, -0.5, -th);
      break;
    case 7:
      cairo_translate (cr, -tw, 0);
      break;
    case 8:
      cairo_translate (cr, -tw/2.0 - 0.5, 0);
      break;
    case 9:
      cairo_translate (cr, -0.5, 0);
      break;
    default:
      break;
  }
  if (align < 0) {
    CairoSetSouerceRGBA(color_tbg);
    cairo_rectangle (cr, 0, 0, tw, th);
    cairo_fill (cr);
  }
  cairo_set_source_rgba (cr, col[0], col[1], col[2], col[3]);
  pango_cairo_layout_path(cr, pl);
  pango_cairo_show_layout(cr, pl);
  g_object_unref(pl);
  cairo_restore(cr);
  cairo_new_path (cr);
}

/** called when backend notifies the UI about sample-rate (SR changes)
 */
static void calc_gridspacing(SiScoUI* ui) {
  /* base-grid: 1 sample / pixel
   * grid-spacing = SR / X; with X so that gs in [40-80] px
   */
  const uint32_t base = ceil(ui->rate / 10000.0) * 200;
  const float grid_spacing = ui->rate / base;
  assert(grid_spacing > 0);
  //fprintf(stderr, "GRID-SPACING: %.1f px\n", grid_spacing);
  ui->grid_spacing = grid_spacing;

  // TODO update ui->cmx_speed -
  // remove elements not available w/ current sample-rate * MAX_UPSAMPLING
}

static uint32_t calc_stride(SiScoUI* ui) {
  const uint32_t elem = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_speed));
  float us;
  // TODO -- there must be a better way even with st**id gtk.
  switch (elem) {
    case  0: us =     100; break;
    case  1: us =     200; break;
    case  2: us =     250; break;
    case  3: us =     500; break;
    case  4: us =    1000; break;
    case  5: us =    2000; break;
    case  6: us =    5000; break;
    case  7: us =   10000; break;
    case  8: us =   20000; break;
    case  9: us =   50000; break;
    case 10: us =  100000; break;
    case 11: us =  200000; break;
    case 12: us =  500000; break;
    case 13: us = 1000000; break;
    default: us = 1000000; break;
  }
  float stride = ui->rate * us / (1000000.0 * ui->grid_spacing);
  assert (stride > 0);

  // TODO non-int upsampling?! -- as long a samples are integer and SRC quality is appropriate
#ifdef WITH_RESAMPLING
  int upsample = 1;
  if (stride < 1) {
    upsample = MIN(MAX_UPSAMPLING, rintf(1.0 / stride));
    stride *= upsample;
  }
  if (upsample != ui->src_fact) {
    setup_src(ui, upsample);
  }
#endif
  return MAX(1, rintf(stride));
}

static void update_annotations(SiScoUI* ui) {
  cairo_t *cr;
  ui->update_ann = false;
  if (!ui->gridnlabels) {
    ui->gridnlabels = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
	ANWIDTH + DAWIDTH, ANHEIGHT + DAHEIGHT * ui->n_channels);
  }
  cr = cairo_create(ui->gridnlabels);
  CairoSetSouerceRGBA(color_blk);
  cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
  cairo_rectangle (cr, 0, 0, ANWIDTH + DAWIDTH, ANHEIGHT + DAHEIGHT * ui->n_channels);
  cairo_fill (cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_OVER);
  cairo_set_line_width(cr, 1.0);
  CairoSetSouerceRGBA(color_grd);

  int32_t gl = ceil(DAWIDTH / ui->grid_spacing / 2.0);

  for (int32_t i = -gl; i <= gl; ++i) {
    const int xp = DAWIDTH / 2.0 + ui->grid_spacing * i;
    if (xp < 0 || xp > DAWIDTH) continue;
    cairo_move_to(cr, xp - .5, 0);
    cairo_line_to(cr, xp - .5, DAHEIGHT * ui->n_channels - .5);
    cairo_stroke(cr);
  }

  for (uint32_t i = 0; i < ui->n_channels * 4; ++i) {
    const int yp = i * DAHEIGHT / 4.0;
    cairo_move_to(cr, 0, yp - .5);
    cairo_line_to(cr, DAWIDTH, yp - .5);
    cairo_stroke(cr);
  }

  /* x ticks */
  const float y0 = rint(DAHEIGHT * ui->n_channels / 2.0);
  for (int32_t i = -gl * 5; i <= gl * 5; ++i) {
    if (abs(i)%5 == 0) continue;
    int xp = DAWIDTH / 2.0 + i * ui->grid_spacing / 5.0;
    if (xp < 0 || xp > DAWIDTH) continue;
    cairo_move_to(cr, xp - .5, y0 - 3.0);
    cairo_line_to(cr, xp - .5, y0 + 2.5);
    cairo_stroke(cr);
  }

  /* y ticks */
  const float x0 = rint(DAWIDTH / 2.0);
  for (uint32_t i = 0; i < ui->n_channels * 20; ++i) {
    if (abs(i)%5 == 0) continue;
    const int yp = i * DAHEIGHT / 20.0;
    cairo_move_to(cr, x0-3.0, yp - .5);
    cairo_line_to(cr, x0+2.5, yp - .5);
    cairo_stroke(cr);
  }

  /* border */
  CairoSetSouerceRGBA(color_gry);
  cairo_move_to(cr, 0, DAHEIGHT * ui->n_channels - .5);
  cairo_line_to(cr, ANWIDTH + DAWIDTH, DAHEIGHT * ui->n_channels - .5);
  cairo_stroke(cr);

  cairo_move_to(cr, DAWIDTH -.5 , 0);
  cairo_line_to(cr, DAWIDTH -.5 , DAHEIGHT * ui->n_channels);
  cairo_stroke(cr);

  /* bottom annotation (grid spacing text)*/
  const float gs_us = (ui->grid_spacing * 1000000.0 * ui->stride / ui->rate
#ifdef WITH_RESAMPLING
      / ui->src_fact
#endif
      );
  char tmp[128];
  if (gs_us >= 900000.0) {
    snprintf(tmp, 128, "Grid: %.1f s (%.1f Hz)", gs_us / 1000000.0, 1000000.0 / gs_us);
  } else if (gs_us >= 900.0) {
    snprintf(tmp, 128, "Grid: %.1f ms (%.1f Hz)", gs_us / 1000.0, 1000000.0 / gs_us);
  } else {
    snprintf(tmp, 128, "Grid: %.1f us (%.1f KHz)", gs_us, 1000.0 / gs_us);
  }
  render_text(cr, tmp, ui->font[0],
      ANWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
      0, 3, color_wht);

  const float ts_us = gs_us * DAWIDTH / ui->grid_spacing;

  if (ts_us >= 800000.0) {
    snprintf(tmp, 128, "Total: %.1f s", ts_us / 1000000.0);
  } else {
    snprintf(tmp, 128, "Total: %.1f ms (%.1f Hz)", ts_us / 1000.0, 1000000.0 / ts_us);
  }
  render_text(cr, tmp, ui->font[0],
      DAWIDTH / 2, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
      0, 2, color_wht);

  /* limit to right border */
  cairo_rectangle (cr, 0, 0, ANWIDTH + DAWIDTH + .5, DAHEIGHT * ui->n_channels);
  cairo_clip(cr);

  cairo_set_operator (cr, CAIRO_OPERATOR_ADD);

  /* y-scale for each channel in right border */
  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    const float yoff = ui->yoff[c];
    const float gainL = MIN(1.0, fabsf(ui->gain[c]));
    const float gainU = fabsf(ui->gain[c]);
    float y0 = yoff + DAHEIGHT * (c + .5);

    // TODO use fixed alpha pattern & scale
    cairo_pattern_t *cpat = cairo_pattern_create_linear(0, 0, 0, gainU * DAHEIGHT);

    cairo_pattern_add_color_stop_rgba(cpat, 0.0, color_chn[c][0], color_chn[c][1], color_chn[c][2], .1);
    cairo_pattern_add_color_stop_rgba(cpat, 0.2, color_chn[c][0], color_chn[c][1], color_chn[c][2], .2);
    cairo_pattern_add_color_stop_rgba(cpat, 0.5, color_chn[c][0], color_chn[c][1], color_chn[c][2], .6);
    cairo_pattern_add_color_stop_rgba(cpat, 0.8, color_chn[c][0], color_chn[c][1], color_chn[c][2], .2);
    cairo_pattern_add_color_stop_rgba(cpat, 1.0, color_chn[c][0], color_chn[c][1], color_chn[c][2], .1);

    cairo_matrix_t m;
#ifdef LIMIT_YSCALE
    cairo_matrix_init_translate (&m, 0, -y0 + DAHEIGHT * gainU * .5);
    cairo_pattern_set_matrix (cpat, &m);
    cairo_set_source (cr, cpat);
    cairo_rectangle (cr, DAWIDTH, y0 - DAHEIGHT * gainL * .5 - 1.0, ANWIDTH, DAHEIGHT * gainL + 1.0);
#else
    cairo_matrix_init_translate (&m, 0, -y0 + DAHEIGHT * gainU * .5);
    cairo_pattern_set_matrix (cpat, &m);
    cairo_set_source (cr, cpat);
    cairo_rectangle (cr, DAWIDTH, yoff + DAHEIGHT * c - DAHEIGHT * .5 * (gainU - 1.0),
	ANWIDTH, DAHEIGHT * gainU + 1.0);
#endif
    cairo_fill(cr);
    cairo_pattern_destroy(cpat);

    cairo_set_source_rgba (cr, color_chn[c][0], color_chn[c][1], color_chn[c][2], 1.0);
    int max_points = ceilf(gainL * 5.0) * 2; // XXX gainU
    for (int32_t i = -max_points; i <= max_points; ++i) {
      float yp = rintf(y0 + gainU * DAHEIGHT * i * .5 / max_points) - .5;
#ifdef LIMIT_YSCALE
      if (fabsf(i * gainU / max_points) > 1.0) continue;
#endif
      int ll;

      if (abs(i) == max_points || i==0) ll = ANWIDTH * 3 / 4;
      else if (abs(i) == max_points / 2) ll = ANWIDTH * 2 / 4;
      else ll = ANWIDTH * 1 / 4;

      cairo_move_to(cr, DAWIDTH, yp);
      cairo_line_to(cr, DAWIDTH + ll + .5,  yp);
      cairo_stroke (cr);
    }
  }

  cairo_destroy(cr);
}

static void invalidate_ann(SiScoUI* ui, int what)
{
  if (what & 1) {
    gtk_widget_queue_draw_area(ui->darea, 0, DAHEIGHT * ui->n_channels, DAWIDTH, ANHEIGHT);
  }
  if (what & 2) {
    gtk_widget_queue_draw_area(ui->darea, DAWIDTH, 0, ANWIDTH, DAHEIGHT * ui->n_channels);
  }
}

#ifdef WITH_MARKERS
static void marker_control_sensitivity(SiScoUI* ui, bool en) {
  gtk_widget_set_sensitive(ui->spb_marker_x0, en);
  gtk_widget_set_sensitive(ui->spb_marker_c0, en);
  gtk_widget_set_sensitive(ui->spb_marker_x1, en);
  gtk_widget_set_sensitive(ui->spb_marker_c1, en);
}

static void update_marker_data(SiScoUI* ui, uint32_t id) {
  MarkerX *mrk = &ui->mrk[id];
  const uint32_t c = mrk->chn;
  int pos = mrk->xpos;

  assert (c >=0 && c <= ui->n_channels);
  assert (pos >=0 && pos < DAWIDTH);

  ScoChan *chn = &ui->chn[c];

  // TODO check if pos is valid (between start/end)
  pos -= rintf(ui->xoff[c]);
  if (pos < 0 || pos >= DAWIDTH) {
    mrk->ymin = NAN;
    mrk->ymax = NAN;
  } else {
    mrk->ymin = chn->data_min[pos];
    mrk->ymax = chn->data_max[pos];
  }
}

static float coefficient_to_dB(float v) {
  return 20.0f * log10f (fabsf(v));
}

static void render_marker(SiScoUI* ui, cairo_t *cr, uint32_t id) {
  cairo_move_to(cr, ui->mrk[id].xpos - .5, 0);
  cairo_line_to(cr, ui->mrk[id].xpos - .5, DAHEIGHT * ui->n_channels);
  cairo_stroke (cr);

  if (!isnan(ui->mrk[id].ymax) && !isnan(ui->mrk[id].ymin)) {
    char tmp[128];
    const uint32_t c = ui->mrk[id].chn;
    const float yoff = ui->yoff[c];
    const float gain = ui->gain[c];
    const float chn_y_offset = yoff + DAHEIGHT * c + DAHEIGHT * .5f - .5f;
    const float chn_y_scale = DAHEIGHT * .5f * gain;

    float ypos = chn_y_offset - (ui->mrk[id].ymin) * chn_y_scale;
    cairo_move_to(cr, ui->mrk[id].xpos - 5.5, ypos);
    cairo_line_to(cr, ui->mrk[id].xpos + 5.0, ypos);
    cairo_stroke (cr);

    if (ui->stride > 1) {
      ypos = chn_y_offset - (ui->mrk[id].ymax) * chn_y_scale;
      cairo_move_to(cr, ui->mrk[id].xpos - 5.5, ypos);
      cairo_line_to(cr, ui->mrk[id].xpos + 5.0, ypos);
      cairo_stroke (cr);

      snprintf(tmp, 128, "Marker %d (chn:%d)\nMax: %+5.2f (%.1f dBFS)\nMin: %+5.2f (%.1f dBFS)",
	  id+1, c+1,
	  ui->mrk[id].ymax, coefficient_to_dB(ui->mrk[id].ymax),
	  ui->mrk[id].ymin, coefficient_to_dB(ui->mrk[id].ymin));
    } else {
      assert (ui->mrk[id].ymax == ui->mrk[id].ymin);
      snprintf(tmp, 128, "Marker %d (chn:%d)\nVal: %+5.2f (%.1f dBFS)",
	  id+1, c+1,
	  ui->mrk[id].ymin, coefficient_to_dB(ui->mrk[id].ymin));
    }

    int txtypos;
    int txtalign;
    if (id == 0) {
      txtypos  = 10;
      txtalign = (ui->mrk[id].xpos > DAWIDTH / 2) ? 7 : 9;
    } else {
      txtypos  = DAHEIGHT * ui->n_channels - 10;
      txtalign = (ui->mrk[id].xpos > DAWIDTH / 2) ? 4 : 6;
    }

    render_text(cr, tmp, ui->font[0],
	ui->mrk[id].xpos, txtypos, 0, -txtalign, color_wht);
  }
}

static void render_markers(SiScoUI* ui, cairo_t *cr) {

  ui->mrk[0].xpos = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_marker_x0));
  ui->mrk[0].chn = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_marker_c0)) - 1;
  ui->mrk[1].xpos = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_marker_x1));
  ui->mrk[1].chn = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_marker_c1)) - 1;

  update_marker_data(ui, 0);
  update_marker_data(ui, 1);

  cairo_set_line_width(cr, 1.0);
  CairoSetSouerceRGBA(color_wht); // TODO marker color
  render_marker(ui, cr, 0);

  CairoSetSouerceRGBA(color_wht);
  render_marker(ui, cr, 1);

  const float dt_us = ((float)ui->mrk[1].xpos - (float)ui->mrk[0].xpos) * 1000000.0 * ui->stride / ui->rate
#ifdef WITH_RESAMPLING
      / ui->src_fact
#endif
      ;
  char tmp[128];
  if (fabs(dt_us) >= 900000.0) {
    snprintf(tmp, 128, "Marker dt: %.2f s (%.1f Hz)", dt_us / 1000000.0, 1000000.0 / dt_us);
  } else if (fabs(dt_us) >= 900.0) {
    snprintf(tmp, 128, "Marker dt: %.1f ms (%.1f Hz)", dt_us / 1000.0, 1000000.0 / dt_us);
  } else {
    snprintf(tmp, 128, "Marker dt: %.1f us (%.1f KHz)", dt_us, 1000.0 / dt_us);
  }
  // TODO find a good place to put it :) -- currently trigger status
  render_text(cr, tmp, ui->font[0],
      DAWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
      0, 1, color_wht);



  if (!isnan(ui->mrk[0].ymax) && !isnan(ui->mrk[1].ymax)) {
    // TODO display diff of max
  }
  if (!isnan(ui->mrk[0].ymin) && !isnan(ui->mrk[1].ymin)) {
    // TODO display diff of min
  }

}
#endif

/* gdk drawing area draw callback
 * -- this runs in gtk's main thread */
static gboolean expose_event_callback (GtkWidget *widget, GdkEventExpose *ev, gpointer data)
{
  /* TODO: read from ringbuffer or blit cairo surface instead of [b]locking here */
  SiScoUI* ui = (SiScoUI*) data;

  cairo_t *cr;
  cr = gdk_cairo_create(ui->darea->window);

  /* limit cairo-drawing to widget */
  cairo_rectangle (cr, 0, 0, ANWIDTH + DAWIDTH, ANHEIGHT + DAHEIGHT * ui->n_channels);
  cairo_clip(cr);

  /* limit cairo-drawing to exposed area */
  cairo_rectangle (cr, ev->area.x, ev->area.y, ev->area.width, ev->area.height);
  cairo_clip(cr);

  cairo_set_source_surface(cr, ui->gridnlabels, 0, 0);
  cairo_paint (cr);

#ifdef WITH_TRIGGER
  if (!ui->paused) // XXX - shares space w/Marker
  switch(ui->trigger_state) {
    case TS_END:
      render_text(cr, "Acquisition complete", ui->font[1],
	  DAWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
	  0, 1, color_wht);
      break;
    case TS_PREBUFFER:
    case TS_WAITMANUAL:
      render_text(cr, "Waiting for trigger", ui->font[1],
	  DAWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
	  0, 1, color_wht);
      break;
    case TS_TRIGGERED:
    case TS_COLLECT:
      render_text(cr, "Triggered", ui->font[1],
	  DAWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
	  0, 1, color_wht);
      break;
    case TS_DELAY:
      render_text(cr, "Hold-off", ui->font[1],
	  DAWIDTH, DAHEIGHT * ui->n_channels + ANHEIGHT / 2,
	  0, 1, color_wht);
      break;
    default:
      break;
  }
#endif

  cairo_save(cr);
  /* limit cairo-drawing to scope-area */
  cairo_rectangle (cr, 0, 0, DAWIDTH, DAHEIGHT * ui->n_channels);
  cairo_clip(cr);

  cairo_set_line_width(cr, 1.0);
  cairo_set_operator (cr, CAIRO_OPERATOR_ADD);

  for(uint32_t c = 0 ; c < ui->n_channels; ++c) {
    const float gain = ui->gain[c];
    const float yoff = ui->yoff[c];
    const float x_offset = rintf(ui->xoff[c]);
    ScoChan *chn = &ui->chn[c];

    uint32_t start = MAX(MIN(DAWIDTH, ev->area.x - x_offset), 0);
    uint32_t end   = MAX(MIN(DAWIDTH, ev->area.x + ev->area.width - x_offset), 0);

#ifdef WITH_TRIGGER
    if (ui->trigger_cfg_mode > 0) {
      end = MIN(end, ui->chn[c].idx);
    }
#endif

    /* drawing area Y-position of given sample-value
     * note: cairo-pixel at 0 spans -.5 .. +.5, hence (DAHEIGHT / 2.0 -.5)
     * also the cairo Y-axis points upwards, thus  -VAL
     *
     * == (   DAHEIGHT * (CHN)
     *      + (DAHEIGHT / 2) - 0.5
     *      - (DAHEIGHT / 2) * (VAL) * (GAIN)
     *    )
     */
    const float chn_y_offset = yoff + DAHEIGHT * c + DAHEIGHT * .5f - .5f;
    const float chn_y_scale = DAHEIGHT * .5f * gain;
#define CYPOS(VAL) ( chn_y_offset - (VAL) * chn_y_scale )

    cairo_save(cr);
#ifdef LIMIT_YSCALE
    cairo_rectangle (cr, 0, yoff + DAHEIGHT * c, DAWIDTH, DAHEIGHT);
#else
    cairo_rectangle (cr, 0, yoff + DAHEIGHT * c - DAHEIGHT * .5 * (gain - 1.0),
	DAWIDTH, DAHEIGHT * gain);
#endif
    cairo_clip(cr);
    CairoSetSouerceRGBA(color_chn[c]);

    pthread_mutex_lock(&chn->lock);

    if (start == chn->idx) {
      start++;
    }
    if (start < end && start < DAWIDTH) {
      cairo_move_to(cr, start - .5 + x_offset, CYPOS(chn->data_max[start]));
    }

    uint32_t pathlength = 0;
    for (uint32_t i = start ; i < end; ++i) {
      // TODO choose draw-mode depending on zoom
      if (i == chn->idx) {
	continue;
      } else if (i%2) {
	cairo_line_to(cr, i - .5 + x_offset, CYPOS(chn->data_min[i]));
	cairo_line_to(cr, i - .5 + x_offset, CYPOS(chn->data_max[i]));
	++pathlength;
      } else {
	cairo_line_to(cr, i - .5 + x_offset, CYPOS(chn->data_max[i]));
	cairo_line_to(cr, i - .5 + x_offset, CYPOS(chn->data_min[i]));
	++pathlength;
      }

      if (pathlength > MAX_CAIRO_PATH) {
	pathlength = 0;
	cairo_stroke (cr);
	if (i%2) {
	  cairo_move_to(cr, i - .5 + x_offset, CYPOS(chn->data_max[i]));
	} else {
	  cairo_move_to(cr, i - .5 + x_offset, CYPOS(chn->data_min[i]));
	}
      }
    }
    if (pathlength > 0) {
      cairo_stroke (cr);
    }

    /* current position vertical-line */
    if (ui->stride >= ui->rate / 4800.0f || ui->paused) {
      cairo_set_source_rgba (cr, color_chn[c][0], color_chn[c][1], color_chn[c][2], .5);
      cairo_move_to(cr, chn->idx - .5 + x_offset, yoff + DAHEIGHT * c);
      cairo_line_to(cr, chn->idx - .5 + x_offset, yoff + DAHEIGHT * (c+1));
      cairo_stroke (cr);
    }
    pthread_mutex_unlock(&chn->lock);

#ifdef WITH_TRIGGER
    if (ui->trigger_cfg_mode > 0 && c == ui->trigger_cfg_channel) {
      CairoSetSouerceRGBA(color_trg);
      static const double dashed[] = {1.5};
      cairo_set_dash(cr, dashed, 1, 0);
      const float xoff = rintf(ui->trigger_cfg_pos + x_offset) - .5f;
      const float yval = CYPOS(ui->trigger_cfg_lvl);

      cairo_move_to(cr, xoff, yoff + DAHEIGHT * c - .5);
      cairo_line_to(cr, xoff, yoff + DAHEIGHT * (c+1) - .5);
      cairo_stroke (cr);
      cairo_set_dash(cr, NULL, 0, 0);

      CairoSetSouerceRGBA(color_lvl);
      cairo_move_to(cr, xoff-3.5, yval-3.5);
      cairo_line_to(cr, xoff+3.5, yval+3.5);
      cairo_move_to(cr, xoff-3.5, yval+3.5);
      cairo_line_to(cr, xoff+3.5, yval-3.5);
      cairo_stroke (cr);
    }
#endif
    cairo_restore(cr);

    /* channel separator */
    if (c > 0) {
      CairoSetSouerceRGBA(color_gry);
      cairo_move_to(cr, 0, DAHEIGHT * c - .5);
      cairo_line_to(cr, DAWIDTH, DAHEIGHT * c - .5);
      cairo_stroke (cr);
    }
    /* zero scale-line */
    CairoSetSouerceRGBA(color_zro);
    cairo_move_to(cr, 0, yoff + DAHEIGHT * (c + .5) - .5);
    cairo_line_to(cr, DAWIDTH, yoff + DAHEIGHT * (c + .5) - .5);
    cairo_stroke (cr);
  }

  cairo_restore(cr);

#ifdef WITH_MARKERS
  if (ui->paused) {
    render_markers(ui, cr);
  }
#endif

  cairo_destroy (cr);
  return TRUE;
}


/******************************************************************************/

/** this callback runs in the "communication" thread of the LV2-host
 * -- invoked via port_event(); please see notes there.
 *
 *  it acts as 'glue' between LV2 port_event() and GTK expose_event_callback()
 *
 *  This function processes the raw audio data:
 *  - check for trigger (if any)
 *  - limit data to be processed (ignore excess)
 *  - process data  into the display buffers
 *    (combine multiple samples into min/max)
 *  - calulates the minimal area of the display to be updated
 *    (depending on settings, offset, scale)
 */
static void update_scope_real(SiScoUI* ui, const uint32_t channel, const size_t n_elem, float const * data)
{
  uint32_t idx_start, idx_end; // display pixel start/end
  int overflow = 0; // received more audio-data than display-pixel
  size_t n_samples;
  float const * audiobuffer;
  ScoChan *chn = &ui->chn[channel];

  idx_start = idx_end = 0;
  /* if buffer is larger than display, process only end */
  if (
#ifdef WITH_TRIGGER
      ui->trigger_state == TS_DISABLED &&
#endif
      n_elem / ui->stride >= DAWIDTH
      ) {
    n_samples = DAWIDTH * ui->stride;
    audiobuffer = &data[n_elem - n_samples];
    pthread_mutex_lock(&chn->lock);
    chn->idx=0;
    chn->sub=0;
    chn->data_min[chn->idx] =  1.0;
    chn->data_max[chn->idx] = -1.0;
    pthread_mutex_unlock(&chn->lock);
  } else {
    n_samples = n_elem;
    audiobuffer = data;
  }
  assert(n_samples <= n_elem);

  pthread_mutex_lock(&chn->lock);

#ifdef WITH_TRIGGER
  if (process_trigger(ui, channel, &n_samples, audiobuffer) >= 0)
  {
#endif

  /* process this channel's audio-data for display */
  overflow = process_channel(ui, chn, n_samples, audiobuffer, &idx_start, &idx_end);

#ifdef WITH_TRIGGER
  }
#endif

  pthread_mutex_unlock(&chn->lock);

  /* signal gtk's main thread to redraw the widget after the last channel */
  if (channel + 1 == ui->n_channels) {
    if (overflow == 0 && idx_end == idx_start) {
      ; // No update (waiting) don't update annotations either
    } else if (ui->update_ann) {
      /* redraw annotations and complete widget */
      update_annotations(ui);
      gtk_widget_queue_draw(ui->darea);
    } else if (overflow > 1 || (overflow == 1 && idx_end == idx_start)) {
      /* redraw complete widget */
      gtk_widget_queue_draw(ui->darea);
    } else if (idx_end > idx_start) {
      /* redraw area between start -> end pixel */
      for (uint32_t c = 0; c < ui->n_channels; ++c) {
#ifdef LIMIT_YSCALE
	gtk_widget_queue_draw_area(ui->darea, idx_start - 2 + ui->xoff[c], ui->yoff[c] + DAHEIGHT * c,
	    3 + idx_end - idx_start, DAHEIGHT);
#else
	const float gn = fabsf(ui->gain[c]);
	gtk_widget_queue_draw_area(ui->darea, idx_start - 2 + ui->xoff[c],
	    ui->yoff[c] + DAHEIGHT * c - DAHEIGHT * .5 * (gn - 1.0),
	    3 + idx_end - idx_start, DAHEIGHT * gn);
#endif
      }
    } else if (idx_end < idx_start) {
      /* wrap-around; redraw area between 0 -> start AND end -> right-end */
      for (uint32_t c = 0; c < ui->n_channels; ++c) {
#ifdef LIMIT_YSCALE
	gtk_widget_queue_draw_area(ui->darea, idx_start - 2 + ui->xoff[c], ui->yoff[c] + DAHEIGHT * c,
	    3 + DAWIDTH - idx_start, DAHEIGHT);
	gtk_widget_queue_draw_area(ui->darea, 0, ui->yoff[c] + DAHEIGHT * c,
	    idx_end + 1 + ui->xoff[c], DAHEIGHT);
#else
	const float gn = fabsf(ui->gain[c]);
	gtk_widget_queue_draw_area(ui->darea, idx_start - 2 + ui->xoff[c],
	    ui->yoff[c] + DAHEIGHT * c - DAHEIGHT * .5 * (gn - 1.0),
	    3 + DAWIDTH - idx_start, DAHEIGHT * gn);
	gtk_widget_queue_draw_area(ui->darea, 0,
	    ui->yoff[c] + DAHEIGHT * c - DAHEIGHT * .5 * (gn - 1.0),
	    idx_end + 1 + ui->xoff[c], DAHEIGHT * gn);
#endif
      }
    }

    /* check alignment (x-runs) */
    bool ok = true;
    const uint32_t cbx = ui->chn[0].idx;
#ifdef WITH_TRIGGER
    const uint32_t tbx = ui->trigger_buf[0].idx;
#endif
    for (uint32_t c = 1; c < ui->n_channels; ++c) {
      if (ui->chn[c].idx != cbx
#ifdef WITH_TRIGGER
	  || ui->trigger_buf[c].idx != tbx
#endif
	  ) {
	ok = false;
	break;
      }
    }
    /* reset buffers on x-run */
    if (!ok) {
      fprintf(stderr, "SiSco.lv2 UI: x-run (DSP <> UI comm buffer under/overflow)\n");
      for (uint32_t c = 0; c < ui->n_channels; ++c) {
	pthread_mutex_lock(&ui->chn[c].lock);
	zero_sco_chan(&ui->chn[c]);
#ifdef WITH_TRIGGER
	zero_sco_chan(&ui->trigger_buf[c]);
#endif
	pthread_mutex_unlock(&ui->chn[c].lock);
      }
#ifdef WITH_TRIGGER
      if (ui->trigger_state != TS_DISABLED) {
	ui->trigger_state_n = TS_INITIALIZING;
      }
#endif
    }
  }
}

/** this callback runs in the "communication" thread of the LV2-host
 * -- invoked via port_event(); please see notes there.
 *
 *  it acts as 'glue' between LV2 port_event() and GTK expose_event_callback()
 *
 *  This is a wrapper, around above 'update_scope_real()' it allows
 *  to update the UI state in sync with the 1st channel and
 *  upsamples the data if neccesary.
 */
static void update_scope(SiScoUI* ui, const uint32_t channel, const size_t n_elem, float const * data)
{
  /* this callback runs in the "communication" thread of the LV2-host
   * usually a g_timeout() at ~25fps
   */
  if (channel > ui->n_channels) {
    return;
  }
  /* update state in sync with 1st channel */
  if (channel == 0) {
    ui->cur_period = n_elem;

    bool paused = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(ui->btn_pause));
    if (paused != ui->paused) {
      ui->paused = paused;
#ifdef WITH_MARKERS
      marker_control_sensitivity(ui, paused);
#endif
      gtk_widget_queue_draw(ui->darea);
    }

#ifdef WITH_TRIGGER
    if (ui->trigger_state != ui->trigger_state_n) {
      invalidate_ann(ui, 1);
    }

    ui->trigger_state = ui->trigger_state_n;

    if (ui->trigger_delay > 0) ui->trigger_delay--;

    if (ui->trigger_state < TS_TRIGGERED || ui->trigger_state == TS_END) {
      const uint32_t p_pos = ui->trigger_cfg_pos;
      const uint32_t p_typ = ui->trigger_cfg_channel << 1 | ui->trigger_cfg_type;
      const float    p_lvl = ui->trigger_cfg_lvl;

      ui->trigger_cfg_pos = rintf(DAWIDTH * gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_pos)) * .01f);
      ui->trigger_cfg_lvl = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_trigger_lvl));

      const uint32_t type = gtk_combo_box_get_active(GTK_COMBO_BOX(ui->cmx_trigger_type));
      ui->trigger_cfg_channel = type >> 1;
      ui->trigger_cfg_type = type & 1;

      if (p_typ != type || p_pos != ui->trigger_cfg_pos || p_lvl != ui->trigger_cfg_lvl) {
	if (ui->trigger_state == TS_PREBUFFER) {
	  ui->trigger_state = TS_INITIALIZING;
	}
	gtk_widget_queue_draw(ui->darea);
      }
    }
#endif
  }

  const float oxoff = ui->xoff[channel];
  const float oyoff = ui->yoff[channel];
  const float ogain = ui->gain[channel];

  ui->xoff[channel] = DAWIDTH * .5 * gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_xoff[channel]));
  ui->yoff[channel] = DAHEIGHT * .5 * ui->n_channels * gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_yoff[channel]));
  ui->gain[channel] = gtk_spin_button_get_value(GTK_SPIN_BUTTON(ui->spb_amp[channel]));

  if (oxoff != ui->xoff[channel] || oyoff != ui->yoff[channel] || ogain != ui->gain[channel]) {
    ui->update_ann = true;
  }

  if (ui->paused
#ifdef WITH_TRIGGER
      && ( ui->trigger_state == TS_DISABLED
	|| ui->trigger_state == TS_END
	|| ui->trigger_state == TS_DELAY)
#endif
      ) {
    if (ui->update_ann) {
      update_annotations(ui);
      gtk_widget_queue_draw(ui->darea);
    }
    return;
  }

  /* update state in sync with 1st channel
   * when NOT paused
   */
  if (channel == 0) {

#ifdef WITH_RESAMPLING
    uint32_t p_srcfct = ui->src_fact;
#endif
    uint32_t p_stride = ui->stride;
    ui->stride = calc_stride(ui);

    if (p_stride != ui->stride
#ifdef WITH_RESAMPLING
	|| p_srcfct != ui->src_fact
#endif
	) {
      ui->update_ann = true;
    }
  }


#ifdef WITH_RESAMPLING
  if (ui->src_fact > 1) {
    ui->src[channel]->inp_count = n_elem;
    ui->src[channel]->inp_data = data;
    ui->src[channel]->out_count = n_elem * ui->src_fact;
    ui->src[channel]->out_data = ui->src_buf[channel];
    ui->src[channel]->process ();
    update_scope_real(ui, channel, n_elem * ui->src_fact, ui->src_buf[channel]);
  } else
#endif
  update_scope_real(ui, channel, n_elem, data);
}


/******************************************************************************
 * LV2
 */


static LV2UI_Handle
instantiate(const LV2UI_Descriptor*   descriptor,
            const char*               plugin_uri,
            const char*               bundle_path,
            LV2UI_Write_Function      write_function,
            LV2UI_Controller          controller,
            LV2UI_Widget*             widget,
            const LV2_Feature* const* features)
{
  SiScoUI* ui = (SiScoUI*)calloc(1, sizeof(SiScoUI));

  if (!ui) {
    fprintf(stderr, "SiSco.lv2 UI: out of memory\n");
    return NULL;
  }

  ui->map = NULL;
  *widget = NULL;

  if (!strcmp(plugin_uri, SCO_URI "#Mono")) {
    ui->n_channels = 1;
  } else if (!strcmp(plugin_uri, SCO_URI "#Stereo")) {
    ui->n_channels = 2;
  } else {
    free(ui);
    return NULL;
  }

  for (int i = 0; features[i]; ++i) {
    if (!strcmp(features[i]->URI, LV2_URID_URI "#map")) {
      ui->map = (LV2_URID_Map*)features[i]->data;
    }
  }

  if (!ui->map) {
    fprintf(stderr, "SiSco.lv2 UI: Host does not support urid:map\n");
    free(ui);
    return NULL;
  }

  /* initialize private data structure */
  ui->write      = write_function;
  ui->controller = controller;

  ui->stride     = 25;
  ui->paused     = false;
  ui->rate       = 48000;

#ifdef WITH_MARKERS
  ui->mrk[0].xpos=50;
  ui->mrk[0].chn=0;
  ui->mrk[1].xpos=490;
  ui->mrk[1].chn=0;
#endif

#ifdef WITH_TRIGGER
  ui->trigger_cfg_mode = 0;
  ui->trigger_cfg_type = 0;
  ui->trigger_cfg_channel = 0;
  ui->trigger_cfg_pos = DAWIDTH * .5; // 50%
  ui->trigger_cfg_lvl = 0;

  ui->trigger_state = TS_DISABLED;
  ui->trigger_state_n = TS_DISABLED;

  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    ui->trigger_buf[c].bufsiz = TRBUFSZ;
    alloc_sco_chan(&ui->trigger_buf[c]);
  }
#endif
  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    ui->chn[c].bufsiz = DAWIDTH;
    alloc_sco_chan(&ui->chn[c]);
  }

  map_sco_uris(ui->map, &ui->uris);
  lv2_atom_forge_init(&ui->forge, ui->map);

  /* setup UI */
  ui->hbox = gtk_hbox_new(FALSE, 0);
  ui->ctable = gtk_table_new(7, 4, FALSE);

  ui->darea = gtk_drawing_area_new();
  gtk_widget_set_size_request(ui->darea,
      ANWIDTH + DAWIDTH, ANHEIGHT + DAHEIGHT * ui->n_channels);

  /* widgets */
  ui->lbl_speed = gtk_label_new("Grid");
  ui->lbl_off_x = gtk_label_new("X");
  ui->lbl_off_y = gtk_label_new("Y");
  ui->lbl_amp = gtk_label_new("Ampl");

  ui->sep[0] = gtk_hseparator_new();
  ui->sep[1] = gtk_hseparator_new();
  ui->sep[2] = gtk_label_new("");
  ui->btn_pause = gtk_toggle_button_new_with_label("Pause");

#ifdef WITH_TRIGGER
  ui->spb_trigger_lvl_adj = (GtkAdjustment *) gtk_adjustment_new(0.0, -1.0, 1.0, 0.05, 1.0, 0.0);
  ui->spb_trigger_lvl     = gtk_spin_button_new(ui->spb_trigger_lvl_adj, 0.1, 2);
  ui->spb_trigger_pos_adj = (GtkAdjustment *) gtk_adjustment_new(50.0, 0.0, 100.0, 1.0, 10.0, 0.0);
  ui->spb_trigger_pos     = gtk_spin_button_new(ui->spb_trigger_pos_adj, 1.0, 0);
  ui->spb_trigger_hld_adj = (GtkAdjustment *) gtk_adjustment_new(1.0, 0.0, 5.0, 0.1, 1.0, 0.0);
  ui->spb_trigger_hld     = gtk_spin_button_new(ui->spb_trigger_hld_adj, 1.0, 1);
  ui->btn_trigger_man     = gtk_button_new_with_label("Trigger");
  ui->lbl_trig = gtk_label_new("Trigger");
  ui->lbl_tpos = gtk_label_new("Xpos [%]");
  ui->lbl_tlvl = gtk_label_new("Level");
  ui->lbl_thld = gtk_label_new("Hold [s]");

  ui->cmx_trigger_mode = gtk_combo_box_text_new();
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_trigger_mode), 0, "No Trigger");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_trigger_mode), 1, "Manual Trigger");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_trigger_mode), 2, "Continuous");

  ui->cmx_trigger_type = gtk_combo_box_text_new();
  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    char tmp[64];
    snprintf(tmp, 64, "Chn %d Rising Edge", c+1);
    gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_trigger_type), 2*c, tmp);
    snprintf(tmp, 64, "Chn %d Falling Edge", c+1);
    gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_trigger_type), 2*c+1, tmp);
  }
  gtk_widget_set_sensitive(ui->btn_trigger_man, false);
  gtk_widget_set_sensitive(ui->spb_trigger_hld, false);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_trigger_mode), 0);
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_trigger_type), 0);
#endif

  ui->cmx_speed = gtk_combo_box_text_new();
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  0, "100 us");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  1, "200 us");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  2, "250 us");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  3, "500 us");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  4, "  1 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  5, "  2 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  6, "  5 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  7, " 10 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  8, " 20 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed),  9, " 50 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed), 10, "100 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed), 11, "200 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed), 12, "500 ms");
  gtk_combo_box_text_insert_text(GTK_COMBO_BOX_TEXT(ui->cmx_speed), 13, "1 sec");
  gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_speed), 9);

#ifdef WITH_MARKERS
  ui->lbl_marker = gtk_label_new("Markers (when paused)");
  ui->lbl_mpos0 = gtk_label_new("1: x-pos");
  ui->lbl_mpos1 = gtk_label_new("2: x-pos");
  ui->lbl_mchn0 = gtk_label_new("Channel");
  ui->lbl_mchn1 = gtk_label_new("Channel");

  ui->spb_marker_x0_adj = (GtkAdjustment *) gtk_adjustment_new(DAWIDTH * .25,
      0.0, DAWIDTH - 1, 1.0, 5.0, 0.0); // XXX think about range
  ui->spb_marker_x0     = gtk_spin_button_new(ui->spb_marker_x0_adj, 1.0, 0);
  ui->spb_marker_c0_adj = (GtkAdjustment *) gtk_adjustment_new(1.0, 1.0, ui->n_channels, 1.0, 1.0, 0.0);
  ui->spb_marker_c0     = gtk_spin_button_new(ui->spb_marker_c0_adj, 1.0, 0);

  ui->spb_marker_x1_adj = (GtkAdjustment *) gtk_adjustment_new(DAWIDTH * .75,
      0.0, DAWIDTH - 1, 1.0, 5.0, 0.0);
  ui->spb_marker_x1     = gtk_spin_button_new(ui->spb_marker_x1_adj, 1.0, 0);
  ui->spb_marker_c1_adj = (GtkAdjustment *) gtk_adjustment_new(1.0, 1.0, ui->n_channels, 1.0, 1.0, 0.0);
  ui->spb_marker_c1     = gtk_spin_button_new(ui->spb_marker_c1_adj, 1.0, 0);
#endif

  /* LAYOUT */
  int row = 0;

#define TBLADD(WIDGET, X0, X1, Y0, Y1) \
  gtk_table_attach(GTK_TABLE(ui->ctable), WIDGET, X0, X1, Y0, Y1,\
      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), GTK_SHRINK, 2, 2)

  TBLADD(ui->lbl_speed, 0, 2, row, row+1);
  TBLADD(ui->cmx_speed, 2, 4, row, row+1); row++;
  TBLADD(ui->sep[0], 0, 4, row, row+1); row++;

  TBLADD(ui->lbl_amp, 1, 2, row, row+1);
  TBLADD(ui->lbl_off_y, 2, 3, row, row+1);
  TBLADD(ui->lbl_off_x, 3, 4, row, row+1); row++;

  for (uint32_t c = 0; c < ui->n_channels; ++c) {
    char tmp[32];
    snprintf(tmp, 32, "Chn %d", c+1);
    ui->lbl_chn[c] = gtk_label_new(tmp);

    ui->spb_yoff_adj[c] = (GtkAdjustment *) gtk_adjustment_new(0.0, -1.0, 1.0, 0.01, 1.0, 0.0); //XXX think about range
    ui->spb_yoff[c] = gtk_spin_button_new(ui->spb_yoff_adj[c], 0.1, 2);

    ui->spb_xoff_adj[c] = (GtkAdjustment *) gtk_adjustment_new(0.0, -1.0, 1.0, 0.01, 1.0, 0.0); //XXX think about range
    ui->spb_xoff[c] = gtk_spin_button_new(ui->spb_xoff_adj[c], 0.1, 2);

    ui->spb_amp_adj[c] = (GtkAdjustment *) gtk_adjustment_new(1.0, -6.0, 6.0, 0.1, 1.0, 0.0);
    ui->spb_amp[c] = gtk_spin_button_new(ui->spb_amp_adj[c], 0.1, 1);

    TBLADD(ui->lbl_chn[c], 0, 1, row, row+1);
    TBLADD(ui->spb_amp[c], 1, 2, row, row+1);
    TBLADD(ui->spb_yoff[c], 2, 3, row, row+1);
    TBLADD(ui->spb_xoff[c], 3, 4, row, row+1);

    g_signal_connect(G_OBJECT(ui->spb_amp[c]),  "value-changed", G_CALLBACK(cfg_changed), ui);
    g_signal_connect(G_OBJECT(ui->spb_yoff[c]), "value-changed", G_CALLBACK(cfg_changed), ui);
    g_signal_connect(G_OBJECT(ui->spb_xoff[c]), "value-changed", G_CALLBACK(cfg_changed), ui);
    row++;
  }

  gtk_table_attach(GTK_TABLE(ui->ctable), ui->sep[2], 0, 4, row, row+1,
      (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), (GtkAttachOptions) (GTK_EXPAND | GTK_FILL), 2, 2);
  row++;

#ifdef WITH_MARKERS
  TBLADD(ui->lbl_marker, 0, 4, row, row+1); row++;

  TBLADD(ui->lbl_mpos0, 0, 1, row, row+1);
  TBLADD(ui->spb_marker_x0, 1, 2, row, row+1);

  if (ui->n_channels > 1) {
    TBLADD(ui->lbl_mchn0, 2, 3, row, row+1);
    TBLADD(ui->spb_marker_c0, 3, 4, row, row+1);
    row++;
    TBLADD(ui->lbl_mpos1, 0, 1, row, row+1);
    TBLADD(ui->spb_marker_x1, 1, 2, row, row+1);
    TBLADD(ui->lbl_mchn1, 2, 3, row, row+1);
    TBLADD(ui->spb_marker_c1, 3, 4, row, row+1);
  } else {
    TBLADD(ui->lbl_mpos1, 2, 3, row, row+1);
    TBLADD(ui->spb_marker_x1, 3, 4, row, row+1);
  }
  row++;
  marker_control_sensitivity(ui, false);
  TBLADD(ui->sep[1], 0, 4, row, row+1); row++;
#endif


#ifdef WITH_TRIGGER
  TBLADD(ui->cmx_trigger_mode, 0, 2, row, row+1);
  TBLADD(ui->cmx_trigger_type, 2, 4, row, row+1); row++;

  TBLADD(ui->lbl_tlvl, 1, 2, row, row+1);
  TBLADD(ui->lbl_tpos, 2, 3, row, row+1);
  TBLADD(ui->lbl_thld, 3, 4, row, row+1); row++;

  TBLADD(ui->lbl_trig, 0, 1, row, row+1);
  TBLADD(ui->spb_trigger_lvl, 1, 2, row, row+1);
  TBLADD(ui->spb_trigger_pos, 2, 3, row, row+1);
  TBLADD(ui->spb_trigger_hld, 3, 4, row, row+1); row++;

  TBLADD(ui->btn_trigger_man, 0, 2, row, row+1);
  TBLADD(ui->btn_pause, 2, 4, row, row+1); row++;
#else
  TBLADD(ui->btn_pause, 0, 4, row, row+1); row++;
#endif

  /* signals */
  g_signal_connect(G_OBJECT(ui->darea), "expose_event", G_CALLBACK(expose_event_callback), ui);
  g_signal_connect(G_OBJECT(ui->cmx_speed), "changed", G_CALLBACK(cfg_changed), ui);

#ifdef WITH_TRIGGER
  g_signal_connect(G_OBJECT(ui->btn_trigger_man), "clicked", G_CALLBACK(trigger_btn_callback), ui);
  g_signal_connect(G_OBJECT(ui->cmx_trigger_mode), "changed", G_CALLBACK(trigger_cmx_callback), ui);
  g_signal_connect(G_OBJECT(ui->spb_trigger_lvl), "value-changed", G_CALLBACK(cfg_changed), ui);
  g_signal_connect(G_OBJECT(ui->spb_trigger_pos), "value-changed", G_CALLBACK(cfg_changed), ui);
#endif

#ifdef WITH_MARKERS
  g_signal_connect(G_OBJECT(ui->spb_marker_x0), "value-changed", G_CALLBACK(mrk_changed), ui);
  g_signal_connect(G_OBJECT(ui->spb_marker_c0), "value-changed", G_CALLBACK(mrk_changed), ui);
  g_signal_connect(G_OBJECT(ui->spb_marker_x1), "value-changed", G_CALLBACK(mrk_changed), ui);
  g_signal_connect(G_OBJECT(ui->spb_marker_c1), "value-changed", G_CALLBACK(mrk_changed), ui);
#endif

  /* main layout */
  gtk_box_pack_start(GTK_BOX(ui->hbox), ui->darea, FALSE, FALSE, 0);
  gtk_box_pack_start(GTK_BOX(ui->hbox), ui->ctable, FALSE, FALSE, 4);
  *widget = ui->hbox;

  /* On Screen Display -- annotations */
  ui->font[0] = pango_font_description_from_string("Mono 9");
  ui->font[1] = pango_font_description_from_string("Sans 10");

  calc_gridspacing(ui);
  ui->stride = calc_stride(ui);
  update_annotations(ui);
#ifdef WITH_RESAMPLING
  setup_src(ui, 1);
#endif

  /* send message to DSP backend:
   * enable message transmission & request state
   */
  ui_enable(ui);

  return ui;
}

static void
cleanup(LV2UI_Handle handle)
{
  SiScoUI* ui = (SiScoUI*)handle;
  /* send message to DSP backend:
   * save state & disable message transmission
   */
  ui_disable(ui);

  for (uint32_t c = 0; c < ui->n_channels; ++c) {
#ifdef WITH_TRIGGER
    free_sco_chan(&ui->trigger_buf[c]);
#endif
    free_sco_chan(&ui->chn[c]);
#ifdef WITH_RESAMPLING
    delete ui->src[c];
#endif
  }
  cairo_surface_destroy(ui->gridnlabels);
  pango_font_description_free(ui->font[0]);
  pango_font_description_free(ui->font[1]);
  gtk_widget_destroy(ui->darea);
  free(ui);
}

/** receive data from the DSP-backend.
 *
 * this callback runs in the "communication" thread of the LV2-host
 * jalv and ardour do this via a g_timeout() function at ~25fps
 *
 * the atom-events from the DSP backend are written into a ringbuffer
 * in the host (in the DSP|jack realtime thread) the host then
 * empties this ringbuffer by sending port_event()s to the UI at some
 * random time later.  When CPU and DSP load are large the host-buffer
 * may overflow and some events may get lost.
 *
 * This thread does is not [usually] the 'drawing' thread (it does not
 * have X11 or gl context).
 */
static void
port_event(LV2UI_Handle handle,
           uint32_t     port_index,
           uint32_t     buffer_size,
           uint32_t     format,
           const void*  buffer)
{
  SiScoUI* ui = (SiScoUI*)handle;
  LV2_Atom* atom = (LV2_Atom*)buffer;

  /* check type of data received
   *  format == 0: [float] control-port event
   *  format > 0: message
   *  Every event message is sent as separate port-event
   */
  if (format == ui->uris.atom_eventTransfer
      && atom->type == ui->uris.atom_Blank
      )
  {
    /* cast the buffer to Atom Object */
    LV2_Atom_Object* obj = (LV2_Atom_Object*)atom;
    LV2_Atom *a0 = NULL;
    LV2_Atom *a1 = NULL;
    LV2_Atom *a2 = NULL;
    LV2_Atom *a3 = NULL;
    if (
	/* handle raw-audio data objects */
	obj->body.otype == ui->uris.rawaudio
	/* retrieve properties from object and
	 * check that there the [here] two required properties are set.. */
	&& 2 == lv2_atom_object_get(obj, ui->uris.channelid, &a0, ui->uris.audiodata, &a1, NULL)
	/* ..and non-null.. */
	&& a0
	&& a1
	/* ..and match the expected type */
	&& a0->type == ui->uris.atom_Int
	&& a1->type == ui->uris.atom_Vector
	)
    {
      /* single integer value can be directly dereferenced */
      const int32_t chn = ((LV2_Atom_Int*)a0)->body;

      /* dereference and typecast vector pointer */
      LV2_Atom_Vector* vof = (LV2_Atom_Vector*)LV2_ATOM_BODY(a1);
      /* check if atom is indeed a vector of the expected type*/
      if (vof->atom.type == ui->uris.atom_Float) {
	/* get number of elements in vector
	 * = (raw 8bit data-length - header-length) / sizeof(expected data type:float) */
	const size_t n_elem = (a1->size - sizeof(LV2_Atom_Vector_Body)) / vof->atom.size;
	/* typecast, dereference pointer to vector */
	const float *data = (float*) LV2_ATOM_BODY(&vof->atom);
	/* call function that handles the actual data */
	update_scope(ui, chn, n_elem, data);
      }
    }
    else if (
	/* handle 'state/settings' data object */
	obj->body.otype == ui->uris.ui_state
	/* retrieve properties from object and
	 * check that there the [here] three required properties are set.. */
	&& 4 == lv2_atom_object_get(obj,
	  ui->uris.ui_state_chn, &a0,
	  ui->uris.ui_state_grid, &a1,
	  ui->uris.ui_state_trig, &a2,
	  ui->uris.samplerate, &a3, NULL)
	/* ..and non-null.. */
	&& a0 && a1 && a2 && a3
	/* ..and match the expected type */
	&& a0->type == ui->uris.atom_Vector
	&& a1->type == ui->uris.atom_Int
	&& a2->type == ui->uris.atom_Vector
	&& a3->type == ui->uris.atom_Float
	)
    {
      ui->rate = ((LV2_Atom_Float*)a3)->body;
      const int32_t grid = ((LV2_Atom_Int*)a1)->body;
      gtk_combo_box_set_active(GTK_COMBO_BOX(ui->cmx_speed), grid);

      apply_state_chn(ui, (LV2_Atom_Vector*)LV2_ATOM_BODY(a0));
#ifdef WITH_TRIGGER
      apply_state_trig(ui, (LV2_Atom_Vector*)LV2_ATOM_BODY(a2));
#endif
      /* re-draw grid -- rate may have changed */
      calc_gridspacing(ui);
      ui->update_ann=true;
    }
  }
}

static const LV2UI_Descriptor descriptor = {
  SCO_URI "#ui",
  instantiate,
  cleanup,
  port_event,
  NULL
};

LV2_SYMBOL_EXPORT
const LV2UI_Descriptor*
lv2ui_descriptor(uint32_t index)
{
  switch (index) {
  case 0:
    return &descriptor;
  default:
    return NULL;
  }
}

/* vi:set ts=8 sts=2 sw=2: */
