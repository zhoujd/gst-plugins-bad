/* GStreamer
 * Copyright (C) 2011 Robert Jobbagy <jobbagy.robert@gmail.com>
 * Copyright (C) 2014 Tim-Philipp Müller <tim centricular com>
 *
 * motioncells_dynamic_test: test to show effect of property changes at runtime
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */
#include <gst/gst.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "gstmotioncells_dynamic_test.h"
#include "gst_element_print_properties.h"

int
main (int argc, char *argv[])
{
  GstElement *pipeline, *source, *videor, *capsf;
  GstElement *colorsp0, *colorsp1, *mcells, *sink;
  GstCaps *caps;

  gst_init (&argc, &argv);

  pipeline = gst_pipeline_new ("motioncells-pipeline");
  if (argc == 2 && strcmp (argv[1], "test") == 0) {
    source = gst_element_factory_make ("videotestsrc", NULL);
    gst_util_set_object_arg (G_OBJECT (source), "pattern", "ball");
  } else if (argc == 1 || strncmp (argv[1], "v4l", 3) == 0) {
    source = gst_element_factory_make ("v4l2src", NULL);
  } else {
    g_printerr ("Usage: %s [v4l2|test]\n", argv[0]);
    exit (-1);
  }

  videor = gst_element_factory_make ("videorate", NULL);
  capsf = gst_element_factory_make ("capsfilter", NULL);
  colorsp0 = gst_element_factory_make ("videoconvert", NULL);
  mcells = gst_element_factory_make ("motioncells", NULL);
  colorsp1 = gst_element_factory_make ("videoconvert", NULL);
  sink = gst_element_factory_make ("autovideosink", "videosink");
  if (!pipeline || !source || !videor || !capsf || !colorsp0
      || !mcells || !colorsp1 || !sink) {
    g_printerr ("One element could not be created. Exiting.\n");
    return -1;
  }

  caps = gst_caps_from_string ("video/x-raw,framerate=10/1");
  g_object_set (G_OBJECT (capsf), "caps", caps, NULL);

  gst_bin_add_many (GST_BIN (pipeline), source, videor, capsf, colorsp0, mcells,
      colorsp1, sink, NULL);

  gst_element_link_many (source, videor, capsf, colorsp0, mcells, colorsp1,
      sink, NULL);

  g_print ("Going to playing..\n");
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  g_print ("You can use these properties: \n");
  gst_element_print_properties (mcells);
  g_print ("See 'gst-inspect-1.0 motioncells' for all the details.\n");
  g_print ("Change properties like this: propertyname=value\n");
  g_print ("Quit with 'q'\n");

  /* Read command line input */
  while (TRUE) {
    gchar *prop_name, *prop_value;
    gchar input_buf[1024];

    memset (input_buf, 0, sizeof (input_buf));
    if (fgets (input_buf, sizeof (input_buf), stdin) == NULL)
      break;

    /* strip off trailing newline */
    g_strdelimit (input_buf, "\n", '\0');

    if (strcmp (input_buf, "q") == 0 || strcmp (input_buf, "quit") == 0)
      break;

    prop_value = strchr (input_buf, '=');
    if (prop_value == NULL) {
      g_printerr ("Please enter either 'property=value' or 'quit'.\n");
      continue;
    }
    *prop_value++ = '\0';
    prop_name = input_buf;

    gst_util_set_object_arg (G_OBJECT (mcells), prop_name, prop_value);
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (pipeline);
  return 0;
}
