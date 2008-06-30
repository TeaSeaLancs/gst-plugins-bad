/**
 * Copyright (C) 2004 Billy Biggs <vektor@dumbterm.net>
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include "_stdint.h"
#include <string.h>

#include "gst/gst.h"
#include "gstdeinterlace2.h"
#include "plugins.h"

#include "tomsmocomp/tomsmocompmacros.h"
#include "x86-64_macros.inc"

#define SearchEffortDefault 5
#define UseStrangeBobDefault 0

static long SearchEffort;
static int UseStrangeBob;
static int IsOdd;
static const unsigned char *pWeaveSrc;
static const unsigned char *pWeaveSrcP;
static unsigned char *pWeaveDest;
static const unsigned char *pCopySrc;
static const unsigned char *pCopySrcP;
static unsigned char *pCopyDest;
static int src_pitch;
static int dst_pitch;
static int rowsize;
static int FldHeight;

static int
Fieldcopy (void *dest, const void *src, size_t count,
    int rows, int dst_pitch, int src_pitch)
{
  unsigned char *pDest = (unsigned char *) dest;
  unsigned char *pSrc = (unsigned char *) src;

  int i;

  for (i = 0; i < rows; i++) {
    memcpy (pDest, pSrc, count);
    pSrc += src_pitch;
    pDest += dst_pitch;
  }
  return 0;
}


#define IS_MMX
#define SSE_TYPE MMX
#define FUNCT_NAME tomsmocompDScaler_MMX
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_MMX
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_3DNOW
#define SSE_TYPE 3DNOW
#define FUNCT_NAME tomsmocompDScaler_3DNOW
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_3DNOW
#undef  SSE_TYPE
#undef  FUNCT_NAME

#define IS_SSE
#define SSE_TYPE SSE
#define FUNCT_NAME tomsmocompDScaler_SSE
#include "tomsmocomp/TomsMoCompAll.inc"
#undef  IS_SSE
#undef  SSE_TYPE
#undef  FUNCT_NAME

static void
tomsmocomp_init (void)
{
  SearchEffort = SearchEffortDefault;
  UseStrangeBob = UseStrangeBobDefault;
}

static void
tomsmocomp_filter_mmx (GstDeinterlace2 * object)
{
  tomsmocompDScaler_MMX (object);
}

static void
tomsmocomp_filter_3dnow (GstDeinterlace2 * object)
{
  tomsmocompDScaler_3DNOW (object);
}

static void
tomsmocomp_filter_sse (GstDeinterlace2 * object)
{
  tomsmocompDScaler_SSE (object);
}

static void
deinterlace_frame_di_tomsmocomp (GstDeinterlace2 * object)
{
  if (object->cpu_feature_flags & OIL_IMPL_FLAG_SSE) {
    tomsmocomp_filter_sse (object);
  } else if (object->cpu_feature_flags & OIL_IMPL_FLAG_3DNOW) {
    tomsmocomp_filter_3dnow (object);
  } else if (object->cpu_feature_flags & OIL_IMPL_FLAG_MMX) {
    tomsmocomp_filter_mmx (object);
  } else {
    g_assert_not_reached ();
  }
}

static deinterlace_method_t tomsmocompmethod = {
  0,                            //DEINTERLACE_PLUGIN_API_VERSION,
  "Motion Adaptive: Motion Search",
  "AdaptiveSearch",
  4,
  OIL_IMPL_FLAG_MMX,
  0,
  0,
  0,
  0,
  deinterlace_frame_di_tomsmocomp,
  {"Uses heuristics to detect motion in the input",
        "frames and reconstruct image detail where",
        "possible.  Use this for high quality output",
        "even on monitors set to an arbitrary refresh",
        "rate.",
        "",
        "Motion search mode finds and follows motion",
        "vectors for accurate interpolation.  This is",
        "the TomsMoComp deinterlacer from DScaler.",
      ""}
};

deinterlace_method_t *
dscaler_tomsmocomp_get_method (void)
{
  tomsmocomp_init ();
  return &tomsmocompmethod;
}
