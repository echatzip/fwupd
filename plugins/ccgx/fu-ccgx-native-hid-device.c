/*
 * Copyright (C) 2023 Framework Computer Inc
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-ccgx-common.h"
#include "fu-ccgx-firmware.h"
#include "fu-ccgx-hpi-common.h"
#include "fu-ccgx-native-hid-device.h"
#include "fu-ccgx-struct.h"

/**
 * FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART:
 *
 * Device is in restart and should not be closed manually.
 *
 * Since: 1.9.2
 */
#define FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART (1 << 0)

struct _FuCcgxNativeHidDevice {
	FuHidDevice parent_instance;
	FuCcgxFwMode fw_mode;
	guint32 versions[FU_CCGX_FW_MODE_LAST];
	guint32 silicon_id;
	guint32 flash_row_size; // FIXME: unused?
	guint32 flash_size;	// FIXME: unused?
};
G_DEFINE_TYPE(FuCcgxNativeHidDevice, fu_ccgx_native_hid_device, FU_TYPE_HID_DEVICE)

typedef enum {
	FU_CCGX_NATIVE_HID_REPORT_ID_INFO = 0xE0,
	FU_CCGX_NATIVE_HID_REPORT_ID_COMMAND = 0xE1,
	FU_CCGX_NATIVE_HID_REPORT_ID_WRITE = 0xE2,
	FU_CCGX_NATIVE_HID_REPORT_ID_READ = 0xE3,
	FU_CCGX_NATIVE_HID_REPORT_ID_CUSTOM = 0xE4,
} FuCcgxNativeHidReportId;

typedef enum {
	CCGX_HID_CMD_JUMP = 0x01,
	CCGX_HID_CMD_FLASH = 0x02,
	CCGX_HID_CMD_SET_BOOT = 0x04,
	CCGX_HID_CMD_MODE = 0x06,
} FuCcgxNativeHidDeviceCommand;

#define FU_CCGX_NATIVE_HID_DEVICE_TIMEOUT     5000 /* ms */
#define FU_CCGX_NATIVE_HID_DEVICE_RETRY_DELAY 30   /* ms */
#define FU_CCGX_NATIVE_HID_DEVICE_RETRY_CNT   5

static gboolean
fu_ccgx_native_hid_command(FuCcgxNativeHidDevice *self,
			   guint8 param1,
			   guint8 param2,
			   GError **error)
{
	guint8 buf[8] =
	    {FU_CCGX_NATIVE_HID_REPORT_ID_COMMAND, param1, param2, 0x00, 0xCC, 0xCC, 0xCC, 0xCC};
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_NATIVE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_enter_flashing_mode(FuCcgxNativeHidDevice *self, GError **error)
{
	if (!fu_ccgx_native_hid_command(self,
					CCGX_HID_CMD_FLASH,
					FU_CCGX_PD_RESP_ENTER_FLASHING_MODE_CMD_SIG,
					error)) {
		g_prefix_error(error, "flashing enable command error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_magic_unlock(FuCcgxNativeHidDevice *self, GError **error)
{
	guint8 buf[8] = {FU_CCGX_NATIVE_HID_REPORT_ID_CUSTOM,
			 FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG,
			 0x43,
			 0x59,
			 0x00,
			 0x00,
			 0x00,
			 0x0B};
	g_autoptr(GError) error_local = NULL;

	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_NATIVE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error)) {
		g_prefix_error(error, "magic enable command error: ");
		return FALSE;
	}

	/* ignore error: this always fails but has the correct behavior */
	if (!fu_ccgx_native_hid_command(self,
					CCGX_HID_CMD_MODE,
					FU_CCGX_PD_RESP_BRIDGE_MODE_CMD_SIG,
					&error_local)) {
		g_debug("expected HID report bridge mode failure: %s", error_local->message);
	}

	return TRUE;
}

static gboolean
fu_ccgx_native_hid_ensure_fw_info(FuCcgxNativeHidDevice *self, GError **error)
{
	guint8 buf[0x40] = {FU_CCGX_NATIVE_HID_REPORT_ID_INFO, 0};
	g_autofree gchar *bl_ver = NULL;
	g_autoptr(GByteArray) st_info = NULL;

	if (!fu_hid_device_get_report(FU_HID_DEVICE(self),
				      buf[0],
				      buf,
				      sizeof(buf),
				      FU_CCGX_NATIVE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_IS_FEATURE,
				      error))
		return FALSE;
	if (!fu_ccgx_native_hid_enter_flashing_mode(self, error))
		return FALSE;

	st_info = fu_struct_ccgx_native_hid_fw_info_parse(buf, sizeof(buf), 0x0, error);
	if (st_info == NULL)
		return FALSE;
	self->silicon_id = fu_struct_ccgx_native_hid_fw_info_get_silicon_id(st_info);
	self->fw_mode = fu_struct_ccgx_native_hid_fw_info_get_operating_mode(st_info);

	/* set current version
	 * Note that this is the base version, not the app version.
	 */
	if (self->fw_mode == FU_CCGX_FW_MODE_FW1) {
		guint version = fu_struct_ccgx_native_hid_fw_info_get_image1_base_version(st_info);
		fu_device_set_version_from_uint32(FU_DEVICE(self), version);
	} else if (self->fw_mode == FU_CCGX_FW_MODE_FW2) {
		guint version = fu_struct_ccgx_native_hid_fw_info_get_image2_base_version(st_info);
		fu_device_set_version_from_uint32(FU_DEVICE(self), version);
	}

	/* set bootloader version */
	fu_device_set_version_bootloader_raw(
	    FU_DEVICE(self),
	    fu_struct_ccgx_native_hid_fw_info_get_bl_version(st_info));
	bl_ver = fu_version_from_uint32(fu_struct_ccgx_native_hid_fw_info_get_bl_version(st_info),
					fu_device_get_version_format(self));
	fu_device_set_version_bootloader(FU_DEVICE(self), bl_ver);
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_device_attach(FuDevice *device, FuProgress *progress, GError **error)
{
	fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
	fu_device_remove_private_flag(device, FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART);
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_device_close(FuDevice *device, GError **error)
{
	/* do not close handle when device restarts */
	if (fu_device_has_private_flag(device, FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART))
		return TRUE;

	/* FuUsbDevice->close */
	return FU_DEVICE_CLASS(fu_ccgx_native_hid_device_parent_class)->close(device, error);
}

static gboolean
fu_ccgx_native_hid_device_setup(FuDevice *device, GError **error)
{
	FuCcgxNativeHidDevice *self = FU_CCGX_NATIVE_HID_DEVICE(device);

	/* FuUsbDevice->setup */
	if (!FU_DEVICE_CLASS(fu_ccgx_native_hid_device_parent_class)->setup(device, error))
		return FALSE;

	if (!fu_ccgx_native_hid_magic_unlock(self, error))
		return FALSE;
	if (!fu_ccgx_native_hid_ensure_fw_info(self, error))
		return FALSE;

	// TODO: Check if this is set properly
	fu_device_set_logical_id(device, fu_ccgx_fw_mode_to_string(self->fw_mode));
	fu_device_add_instance_strup(device, "MODE", fu_device_get_logical_id(device));
	if (!fu_device_build_instance_id(FU_DEVICE(self), error, "USB", "VID", "PID", "MODE", NULL))
		return FALSE;

	// TODO: Check if this is set properly
	fu_device_add_instance_u16(FU_DEVICE(self), "SID", self->silicon_id);
	if (!fu_device_build_instance_id_quirk(FU_DEVICE(self), error, "CCGX", "SID", NULL))
		return FALSE;

	if (self->fw_mode == FU_CCGX_FW_MODE_BOOT) {
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
		/* force an upgrade to any version */
		fu_device_set_version_from_uint32(FU_DEVICE(self), 0x0);
	} else {
		fu_device_remove_flag(device, FWUPD_DEVICE_FLAG_IS_BOOTLOADER);
	}

	/* ensure the remove delay is set, even if no quirk matched */
	if (fu_device_get_remove_delay(FU_DEVICE(self)) == 0)
		fu_device_set_remove_delay(FU_DEVICE(self), 5000);

	/* success */
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_write_row(FuCcgxNativeHidDevice *self,
			     guint16 address,
			     const guint8 *row,
			     guint16 rowsize,
			     GError **error)
{
	g_autoptr(GByteArray) st_hdr = fu_struct_ccgx_native_hid_write_hdr_new();

	fu_struct_ccgx_native_hid_write_hdr_set_pd_resp(st_hdr,
							FU_CCGX_PD_RESP_FLASH_READ_WRITE_CMD_SIG);
	fu_struct_ccgx_native_hid_write_hdr_set_addr(st_hdr, address);
	//	buf[0] = FU_CCGX_NATIVE_HID_REPORT_ID_WRITE;
	if (!fu_memcpy_safe(st_hdr->data,
			    st_hdr->len,
			    FU_STRUCT_CCGX_NATIVE_HID_WRITE_HDR_OFFSET_DATA,
			    row,
			    rowsize,
			    0,
			    rowsize,
			    error))
		return FALSE;
	if (!fu_hid_device_set_report(FU_HID_DEVICE(self),
				      st_hdr->data[0],
				      st_hdr->data,
				      st_hdr->len,
				      FU_CCGX_NATIVE_HID_DEVICE_TIMEOUT,
				      FU_HID_DEVICE_FLAG_NONE,
				      error)) {
		g_prefix_error(error, "write row command error: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_ccgx_native_hid_flash_firmware_image(FuCcgxNativeHidDevice *self,
					FuFirmware *firmware,
					FuProgress *progress,
					guint8 fw_img_no,
					GError **error)
{
	GPtrArray *records = fu_ccgx_firmware_get_records(FU_CCGX_FIRMWARE(firmware));

	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "unlock");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 90, NULL);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 5, "bootswitch");

	if (!fu_ccgx_native_hid_magic_unlock(self, error))
		return FALSE;
	// TODO: Probably not required
	if (!fu_ccgx_native_hid_ensure_fw_info(self, error))
		return FALSE;
	fu_progress_step_done(progress);

	for (guint i = 0; i < records->len; i++) {
		FuCcgxFirmwareRecord *rcd = g_ptr_array_index(records, i);

		g_debug("Writing row #%u @0x%04lx", i, rcd->row_number);
		if (!fu_ccgx_native_hid_write_row(self,
						  rcd->row_number,
						  g_bytes_get_data(rcd->data, NULL),
						  g_bytes_get_size(rcd->data),
						  error)) {
			g_prefix_error(error, "fw write error @0x%x: ", rcd->row_number);
			return FALSE;
		}

		fu_progress_set_percentage_full(fu_progress_get_child(progress),
						(gsize)i + 1,
						(gsize)records->len);
	}
	fu_progress_step_done(progress);

	// TODO: Doing this and reset by themselves (with magic unlock) doesn't
	// switch to the alternative image. Seems we always need to flash in
	// order to switch.
	g_debug("before bootswitch");
	// TODO: fw_img_no should just be
	// fw_img_no = fu_ccgx_firmware_get_fw_mode(FW_CCGX_FIRMWARE(firmware))
	if (!fu_ccgx_native_hid_command(self, CCGX_HID_CMD_SET_BOOT, fw_img_no, error)) {
		g_prefix_error(error, "bootswitch command error: ");
		return FALSE;
	}
	g_debug("After bootswitch");
	fu_progress_step_done(progress);

	g_debug("before reset");
	if (!fu_ccgx_native_hid_command(self,
					CCGX_HID_CMD_JUMP,
					FU_CCGX_PD_RESP_DEVICE_RESET_CMD_SIG,
					error)) {
		g_prefix_error(error, "reset command error: ");
		return FALSE;
	}
	g_debug("After reset");

	return TRUE;
}

static FuFirmware *
fu_ccgx_native_hid_device_prepare_firmware(FuDevice *device,
					   GBytes *fw,
					   FwupdInstallFlags flags,
					   GError **error)
{
	FuCcgxNativeHidDevice *self = FU_CCGX_NATIVE_HID_DEVICE(device);
	FuCcgxFwMode fw_mode;
	guint16 fw_silicon_id;
	g_autoptr(FuFirmware) firmware = fu_ccgx_firmware_new();

	/* parse all images */
	if (!fu_firmware_parse(firmware, fw, flags, error))
		return NULL;

	/* check the silicon ID */
	fw_silicon_id = fu_ccgx_firmware_get_silicon_id(FU_CCGX_FIRMWARE(firmware));
	if (fw_silicon_id != self->silicon_id) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "silicon id mismatch, expected 0x%x, got 0x%x",
			    self->silicon_id,
			    fw_silicon_id);
		return NULL;
	}
	// TODO: Check if we can get this from the device at run-time
	// if ((flags & FWUPD_INSTALL_FLAG_IGNORE_VID_PID) == 0) {
	//	fw_app_type = fu_ccgx_firmware_get_app_type(FU_CCGX_FIRMWARE(firmware));
	//	if (fw_app_type != self->fw_app_type) {
	//		g_set_error(error,
	//			    FWUPD_ERROR,
	//			    FWUPD_ERROR_NOT_SUPPORTED,
	//			    "app type mismatch, expected 0x%x, got 0x%x",
	//			    self->fw_app_type,
	//			    fw_app_type);
	//		return NULL;
	//	}
	//}
	fw_mode = fu_ccgx_firmware_get_fw_mode(FU_CCGX_FIRMWARE(firmware));
	if (fw_mode != fu_ccgx_fw_mode_get_alternate(self->fw_mode)) {
		g_set_error(error,
			    FWUPD_ERROR,
			    FWUPD_ERROR_NOT_SUPPORTED,
			    "FuCcgxFwMode mismatch, expected %s, got %s",
			    fu_ccgx_fw_mode_to_string(fu_ccgx_fw_mode_get_alternate(self->fw_mode)),
			    fu_ccgx_fw_mode_to_string(fw_mode));
		return NULL;
	}
	return g_steal_pointer(&firmware);
}

static gboolean
fu_ccgx_native_hid_device_write_firmware(FuDevice *device,
					 FuFirmware *firmware,
					 FuProgress *progress,
					 FwupdInstallFlags flags,
					 GError **error)
{
	FuCcgxNativeHidDevice *self = FU_CCGX_NATIVE_HID_DEVICE(device);

	g_debug("Operating Mode:  0x%02x", self->fw_mode);
	switch (self->fw_mode) {
	case FU_CCGX_FW_MODE_BOOT:
	case FU_CCGX_FW_MODE_FW2:
		// Update Image 1
		g_debug("Flashing Image 1");
		if (!fu_ccgx_native_hid_flash_firmware_image(self, firmware, progress, 1, error))
			return FALSE;

		g_debug("Add wait for replug");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_add_private_flag(device, FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART);
		break;
	case FU_CCGX_FW_MODE_FW1:
		// Update Image 2
		g_debug("Flashing Image 2");
		if (!fu_ccgx_native_hid_flash_firmware_image(self, firmware, progress, 2, error))
			return FALSE;

		g_debug("Add wait for replug");
		fu_device_add_flag(device, FWUPD_DEVICE_FLAG_WAIT_FOR_REPLUG);
		fu_device_add_private_flag(device, FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART);
		break;
	default:
		return FALSE;
	}

	return TRUE;
}

// TODO: Need this? Why are there two?
static void
fu_ccgx_native_hid_device_set_progress(FuDevice *self, FuProgress *progress)
{
	fu_progress_set_id(progress, G_STRLOC);
	fu_progress_add_flag(progress, FU_PROGRESS_FLAG_GUESSED);
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "detach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_WRITE, 45, "write");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_RESTART, 5, "attach");
	fu_progress_add_step(progress, FWUPD_STATUS_DEVICE_BUSY, 45, "reload");
}

static void
fu_ccgx_native_hid_device_init(FuCcgxNativeHidDevice *self)
{
	// FIXME: I don't think this is try anymore
	fu_device_add_protocol(FU_DEVICE(self), "com.infineon.ccgx");
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UNSIGNED_PAYLOAD);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_UPDATABLE);
	fu_device_add_flag(FU_DEVICE(self), FWUPD_DEVICE_FLAG_DUAL_IMAGE);
	fu_device_set_version_format(FU_DEVICE(self), FWUPD_VERSION_FORMAT_INTEL_ME2);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_ONLY_WAIT_FOR_REPLUG);
	fu_device_add_internal_flag(FU_DEVICE(self), FU_DEVICE_INTERNAL_FLAG_REPLUG_MATCH_GUID);
	fu_device_register_private_flag(FU_DEVICE(self),
					FU_CCGX_NATIVE_HID_DEVICE_IS_IN_RESTART,
					"is-in-restart");
}

static void
fu_ccgx_native_hid_device_to_string(FuDevice *device, guint idt, GString *str)
{
	FuCcgxNativeHidDevice *self = FU_CCGX_NATIVE_HID_DEVICE(device);
	fu_string_append_kx(str, idt, "SiliconId", self->silicon_id);
	fu_string_append(str, idt, "FwMode", fu_ccgx_fw_mode_to_string(self->fw_mode));
	if (self->flash_row_size > 0)
		fu_string_append_kx(str, idt, "CcgxFlashRowSize", self->flash_row_size);
	if (self->flash_size > 0)
		fu_string_append_kx(str, idt, "CcgxFlashSize", self->flash_size);
}

static gboolean
fu_ccgx_native_hid_device_set_quirk_kv(FuDevice *device,
				       const gchar *key,
				       const gchar *value,
				       GError **error)
{
	FuCcgxNativeHidDevice *self = FU_CCGX_NATIVE_HID_DEVICE(device);
	guint64 tmp = 0;

	if (g_strcmp0(key, "SiliconId") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT16, error))
			return FALSE;
		self->silicon_id = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CcgxFlashRowSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_row_size = tmp;
		return TRUE;
	}
	if (g_strcmp0(key, "CcgxFlashSize") == 0) {
		if (!fu_strtoull(value, &tmp, 0, G_MAXUINT32, error))
			return FALSE;
		self->flash_size = tmp;
		return TRUE;
	}
	g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED, "no supported");
	return FALSE;
}

static void
fu_ccgx_native_hid_device_class_init(FuCcgxNativeHidDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS(klass);
	klass_device->to_string = fu_ccgx_native_hid_device_to_string;
	klass_device->attach = fu_ccgx_native_hid_device_attach;
	// klass_device->detach = fu_ccgx_native_hid_device_detach;
	klass_device->setup = fu_ccgx_native_hid_device_setup;
	klass_device->write_firmware = fu_ccgx_native_hid_device_write_firmware;
	klass_device->set_progress = fu_ccgx_native_hid_device_set_progress;
	klass_device->set_quirk_kv = fu_ccgx_native_hid_device_set_quirk_kv;
	klass_device->close = fu_ccgx_native_hid_device_close;
	klass_device->prepare_firmware = fu_ccgx_native_hid_device_prepare_firmware;
}
