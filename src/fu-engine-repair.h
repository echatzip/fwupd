/*
 * Copyright (C) 2017 Kate Hsuan <hpa@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#include "fwupd-security-attr-private.h"

gboolean
fu_engine_repair_do_undo (FuEngine *self, const gchar *key, const gchar *value, GError **error);
