/*
 * Copyright (C) 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#pragma once

#include <fwupdplugin.h>

#define FU_TYPE_CCGX_NATIVE_HID_DEVICE (fu_ccgx_native_hid_device_get_type())
G_DECLARE_FINAL_TYPE(FuCcgxNativeHidDevice,
		     fu_ccgx_native_hid_device,
		     FU,
		     CCGX_NATIVE_HID_DEVICE,
		     FuHidDevice)
