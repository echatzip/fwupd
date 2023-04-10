/*
 * Copyright (C) 2015 Kate Hsuan <hpa@redhat.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#define G_LOG_DOMAIN "FuEngine"

#include "config.h"

#include <fwupdplugin.h>

#include <fcntl.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>
#ifdef HAVE_GIO_UNIX
#include <gio/gunixfdlist.h>
#include <glib-unix.h>
#endif
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

#include "fu-console.h"
#include "fu-engine.h"
#include "fu-plugin-private.h"
#include "fu-engine-repair.h"
#include "fu-util-common.h"


static gboolean
grubby_set(gboolean enable, const gchar *grubby_arg,  GError **error)
{
	g_autofree gchar *output = NULL;
	g_autofree gchar *arg_string = NULL;
	const gchar *argv_grubby[] = {"",
				      "--update-kernel=DEFAULT",
				      "",
				      NULL};
	g_autofree gchar *grubby = NULL;
	grubby = fu_path_find_program("grubby", NULL);
	argv_grubby[0] = grubby;

	if (enable)
		arg_string = g_strdup_printf("--args=%s", grubby_arg);
	else
		arg_string = g_strdup_printf("--remove-args=%s", grubby_arg);

	argv_grubby[2] = arg_string;

	if (!g_spawn_sync(NULL,
			  (gchar **)argv_grubby,
			  NULL,
			  G_SPAWN_DEFAULT,
			  NULL,
			  NULL,
			  &output,
			  NULL,
			  NULL,
			  error))
		return FALSE;

	return TRUE;
}

static gboolean
grubby_set_lockdown(gboolean enable, GError **error)
{
	if (enable)
		return grubby_set(TRUE, "lockdown=confidentiality", error);
	else
		return grubby_set(FALSE, "lockdown=confidentiality", error);
}

static gboolean
grubby_set_iomme(gboolean enable, GError **error)
{
	if (enable)
		return grubby_set(TRUE, "iommu=force", error);
	else
		return grubby_set(FALSE, "iommu=force", error);
}

static gboolean
fu_engine_repair_kernel_lockdown (const gchar *action, GError **error)
{
	if (!g_strcmp0 (action, "undo"))
		return grubby_set_lockdown (FALSE, NULL);

	return grubby_set_lockdown(TRUE, NULL);
}

static gboolean
fu_engine_repair_iommu (const gchar *action, GError **error)
{
	if (!g_strcmp0 (action, "undo"))
		return grubby_set_iomme (FALSE, error);

	return grubby_set_iomme (TRUE, error);
}

static gboolean
fu_engine_repair_unsupport (GError **error)
{
	g_set_error_literal (error,
		     FWUPD_ERROR_NOT_SUPPORTED,
		     FWUPD_ERROR_NOTHING_TO_DO,
		     /*TRANSLATOR: This is an error string */
		     _("Repair item is not supported.")
		     );
	return FALSE;
}


gboolean
fu_engine_repair_do_undo (FuEngine *self, const gchar *key, const gchar *value, GError **error)
{
	if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_PREBOOT_DMA_PROTECTION)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_ENCRYPTED_RAM)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_FWUPD_ATTESTATION)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_FWUPD_PLUGINS)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_FWUPD_UPDATES)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ENABLED)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_VERIFIED)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_ACM)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_POLICY )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_BOOTGUARD_OTP )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ENABLED)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_CET_ACTIVE )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_INTEL_SMAP)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_IOMMU )) {
		return fu_engine_repair_iommu (value, error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_KERNEL_LOCKDOWN )) {
		return fu_engine_repair_kernel_lockdown(value, error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_KERNEL_SWAP )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_KERNEL_TAINTED )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_MEI_MANUFACTURING_MODE )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_MEI_OVERRIDE_STRAP)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_MEI_KEY_MANIFEST)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_MEI_VERSION )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SPI_BIOSWE)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SPI_BLE)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SPI_SMM_BWP)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SPI_DESCRIPTOR )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_IDLE)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SUSPEND_TO_RAM )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_TPM_EMPTY_PCR)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_TPM_RECONSTRUCTION_PCR0)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_TPM_VERSION_20)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_UEFI_SECUREBOOT)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_ENABLED )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_PLATFORM_FUSED )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_PLATFORM_DEBUG_LOCKED )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_UEFI_PK)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_SUPPORTED_CPU)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_AMD_ROLLBACK_PROTECTION)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_AMD_SPI_WRITE_PROTECTION )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_AMD_SPI_REPLAY_PROTECTION )) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_HOST_EMULATION)) {
		return fu_engine_repair_unsupport (error);
	} else if (!g_strcmp0 (key, FWUPD_SECURITY_ATTR_ID_BIOS_ROLLBACK_PROTECTION)) {
		return fu_engine_repair_unsupport (error);
	} else {
		g_set_error_literal (error,
				     FWUPD_ERROR_NOT_SUPPORTED,
				     FWUPD_ERROR_NOTHING_TO_DO,
				     /*TRANSLATOR: This is an error string */
				     _("Repair item is not found.")
				     );
	}

	return FALSE;
}
