/*
 * Copyright (C) 2018 Collabora Ltd.
 *   Author: Xavier Claessens <xavier.claessens@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstamc-internal-ml.h"
#include "gstamc-surfacetexture-ml.h"
#include "../gstamc-codec.h"
#include "../gstamc-constants.h"

#include <ml_media_codec.h>

struct _GstAmcCodec
{
  MLHandle handle;
  GstAmcSurfaceTexture *surface_texture;
};

gboolean
gst_amc_codec_static_init (void)
{
  return TRUE;
}

void
gst_amc_buffer_free (GstAmcBuffer * buffer)
{
  g_free (buffer);
}

gboolean
gst_amc_buffer_set_position_and_limit (GstAmcBuffer * buffer, GError ** err,
    gint position, gint limit)
{
/* FIXME: Do we need to do something?
  buffer->data = buffer->data + position;
  buffer->size = limit;
*/
  return TRUE;
}

GstAmcCodec *
gst_amc_codec_new (const gchar * name, gboolean is_encoder, GError ** err)
{
  GstAmcCodec *codec = NULL;
  MLResult result;
  MLMediaCodecType type;

  g_return_val_if_fail (name != NULL, NULL);

  codec = g_slice_new0 (GstAmcCodec);
  codec->handle = ML_INVALID_HANDLE;
  type = is_encoder ? MLMediaCodecType_Encoder : MLMediaCodecType_Decoder;
  result =
      MLMediaCodecCreateCodec (MLMediaCodecCreation_ByName, type, name,
      &codec->handle);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to create codec by name %s: %d", name, result);
    gst_amc_codec_free (codec);
    return NULL;
  }

  return codec;
}

void
gst_amc_codec_free (GstAmcCodec * codec)
{
  g_return_if_fail (codec != NULL);

  if (codec->handle != ML_INVALID_HANDLE)
    MLMediaCodecDestroy (codec->handle);
  g_clear_object (&codec->surface_texture);
  g_slice_free (GstAmcCodec, codec);
}

gboolean
gst_amc_codec_configure (GstAmcCodec * codec, GstAmcFormat * format,
    GstAmcSurfaceTexture * surface_texture, GError ** err)
{
  MLResult result;
  MLHandle surface_handle = ML_INVALID_HANDLE;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (format != NULL, FALSE);
  g_return_val_if_fail (surface_texture == NULL
      || GST_IS_AMC_SURFACE_TEXTURE_ML (surface_texture), FALSE);

  g_set_object (&codec->surface_texture, surface_texture);
  if (surface_texture != NULL)
    surface_handle =
        gst_amc_surface_texture_ml_get_handle ((GstAmcSurfaceTextureML *)
        surface_texture);

  result = MLMediaCodecConfigureWithSurface (codec->handle,
      gst_amc_format_get_handle (format), surface_handle, 0);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to configure codec %d", result);
    return FALSE;
  }

  return TRUE;
}

GstAmcFormat *
gst_amc_codec_get_output_format (GstAmcCodec * codec, GError ** err)
{
  MLHandle format_handle;
  MLResult result;

  g_return_val_if_fail (codec != NULL, NULL);

  result = MLMediaCodecGetOutputFormat (codec->handle, &format_handle);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to get output format %d", result);
    return NULL;
  }

  return gst_amc_format_new_handle (format_handle);
}

gboolean
gst_amc_codec_start (GstAmcCodec * codec, GError ** err)
{
  MLResult result;

  g_return_val_if_fail (codec != NULL, FALSE);

  result = MLMediaCodecStart (codec->handle);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to start codec %d", result);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_amc_codec_stop (GstAmcCodec * codec, GError ** err)
{
  MLResult result;

  g_return_val_if_fail (codec != NULL, FALSE);

  result = MLMediaCodecStop (codec->handle);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to stop codec %d", result);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_amc_codec_flush (GstAmcCodec * codec, GError ** err)
{
  MLResult result;

  g_return_val_if_fail (codec != NULL, FALSE);

  result = MLMediaCodecFlush (codec->handle);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to flush codec %d", result);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_amc_codec_release (GstAmcCodec * codec, GError ** err)
{
  g_return_val_if_fail (codec != NULL, FALSE);
  return TRUE;
}

GstAmcBuffer *
gst_amc_codec_get_output_buffer (GstAmcCodec * codec, gint index, GError ** err)
{
  MLResult result;
  GstAmcBuffer *ret;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (index >= 0, NULL);

  ret = g_new0 (GstAmcBuffer, 1);

  /* When configured with a surface, getting the buffer pointer makes no sense,
   * but on Android it's not an error, it just return NULL buffer.
   * But MLMediaCodecGetInputBufferPointer() will return an error instead. */
  if (codec->surface_texture != NULL) {
    return ret;
  }

  result =
      MLMediaCodecGetOutputBufferPointer (codec->handle, index,
      (const uint8_t **) &ret->data, &ret->size);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to get output buffer %d", result);
    g_free (ret);
    return NULL;
  }

  return ret;
}

GstAmcBuffer *
gst_amc_codec_get_input_buffer (GstAmcCodec * codec, gint index, GError ** err)
{
  MLResult result;
  GstAmcBuffer *ret;

  g_return_val_if_fail (codec != NULL, NULL);
  g_return_val_if_fail (index >= 0, NULL);

  ret = g_new0 (GstAmcBuffer, 1);

  result =
      MLMediaCodecGetInputBufferPointer (codec->handle, index, &ret->data,
      &ret->size);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to get input buffer %d", result);
    g_free (ret);
    return NULL;
  }

  return ret;
}

gint
gst_amc_codec_dequeue_input_buffer (GstAmcCodec * codec, gint64 timeoutUs,
    GError ** err)
{
  MLResult result;
  int64_t index;

  g_return_val_if_fail (codec != NULL, G_MININT);

  result = MLMediaCodecDequeueInputBuffer (codec->handle, timeoutUs, &index);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to dequeue input buffer %d", result);
    return G_MININT;
  }

  if (index == MLMediaCodec_TryAgainLater)
    return INFO_TRY_AGAIN_LATER;

  return index;
}

gint
gst_amc_codec_dequeue_output_buffer (GstAmcCodec * codec,
    GstAmcBufferInfo * info, gint64 timeoutUs, GError ** err)
{
  MLMediaCodecBufferInfo info_;
  MLResult result;
  int64_t index;

  g_return_val_if_fail (codec != NULL, G_MININT);

  result =
      MLMediaCodecDequeueOutputBuffer (codec->handle, &info_, timeoutUs,
      &index);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to dequeue output buffer %d", result);
    return G_MININT;
  }

  if (index == MLMediaCodec_OutputBuffersChanged) {
    return gst_amc_codec_dequeue_output_buffer (codec, info, timeoutUs, err);
  } else if (index == MLMediaCodec_FormatChanged) {
    return INFO_OUTPUT_FORMAT_CHANGED;
  } else if (index == MLMediaCodec_TryAgainLater) {
    return INFO_TRY_AGAIN_LATER;
  }

  info->flags = info_.flags;

  info->offset = info_.offset;
  info->presentation_time_us = info_.presentation_time_us;
  info->size = info_.size;

  return index;
}

gboolean
gst_amc_codec_queue_input_buffer (GstAmcCodec * codec, gint index,
    const GstAmcBufferInfo * info, GError ** err)
{
  MLResult result;

  g_return_val_if_fail (codec != NULL, FALSE);
  g_return_val_if_fail (info != NULL, FALSE);

  result = MLMediaCodecQueueInputBuffer (codec->handle, index, info->offset,
      info->size, info->presentation_time_us, info->flags);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to queue input buffer %d", result);
    return FALSE;
  }

  return TRUE;
}

gboolean
gst_amc_codec_release_output_buffer (GstAmcCodec * codec, gint index,
    gboolean render, GError ** err)
{
  MLResult result;

  g_return_val_if_fail (codec != NULL, FALSE);

  result = MLMediaCodecReleaseOutputBuffer (codec->handle, index, render);
  if (result != MLResult_Ok) {
    g_set_error (err, GST_LIBRARY_ERROR, GST_LIBRARY_ERROR_FAILED,
        "Failed to release output buffer %d", result);
    return FALSE;
  }

  return TRUE;
}

GstAmcSurfaceTexture *
gst_amc_codec_new_surface_texture (GError ** err)
{
  return (GstAmcSurfaceTexture *) gst_amc_surface_texture_ml_new (err);
}
