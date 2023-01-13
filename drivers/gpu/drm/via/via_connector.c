/*
 * Copyright © 2017-2020 Kevin Brace.
 * Copyright 2012 James Simmons. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) OR COPYRIGHT HOLDER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Author(s):
 * Kevin Brace <kevinbrace@bracecomputerlab.com>
 * James Simmons <jsimmons@infradead.org>
 */

#include <drm/drm_crtc.h>

#include "via_drv.h"

void via_connector_destroy(struct drm_connector *connector)
{
	struct via_connector *con = container_of(connector, struct via_connector, base);
	struct drm_property *property, *tmp;

	list_for_each_entry_safe(property, tmp, &con->props, head)
		drm_property_destroy(connector->dev, property);
	list_del(&con->props);

	drm_connector_update_edid_property(connector, NULL);
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

