// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 Sandisk Corporation or its affiliates.
 *
 *   Author: Jeff Lien <jeff.lien@sandisk.com>
 *           Brandon Paupore <brandon.paupore@sandisk.com>
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>

#include "common.h"
#include "nvme.h"
#include "libnvme.h"
#include "plugin.h"
#include "linux/types.h"
#include "util/cleanup.h"
#include "util/types.h"
#include "nvme-print.h"

#define CREATE_CMD
#include "sandisk-nvme.h"
#include "sandisk-utils.h"
#include "plugins/wdc/wdc-nvme-cmds.h"


static int sndk_vs_internal_fw_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_internal_fw_log(argc, argv, command, plugin);
}

static int sndk_vs_nand_stats(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_nand_stats(argc, argv, command, plugin);
}

static int sndk_vs_smart_add_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_smart_add_log(argc, argv, command, plugin);
}

static int sndk_clear_pcie_correctable_errors(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_clear_pcie_correctable_errors(argc, argv, command, plugin);
}

static int sndk_drive_status(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_drive_status(argc, argv, command, plugin);

#if 0  // jal
	char *desc = "Get Drive Status.";
	struct nvme_dev *dev;
	int ret = 0;
	nvme_root_t r;
	void *dev_mng_log = NULL;
	__u32 system_eol_state;
	__u32 user_eol_state;
	__u32 format_corrupt_reason = 0xFFFFFFFF;
	__u32 eol_status;
	__u32 assert_status = 0xFFFFFFFF;
	__u32 thermal_status = 0xFFFFFFFF;
	__u64 capabilities = 0;

	OPT_ARGS(opts) = {
		OPT_END()
	};

	ret = parse_and_open(&dev, argc, argv, desc, opts);
	if (ret)
		return ret;

	r = nvme_scan(NULL);
	capabilities = sndk_get_drive_capabilities(r, dev);
	if ((capabilities & SNDK_DRIVE_CAP_DRIVE_STATUS) != SNDK_DRIVE_CAP_DRIVE_STATUS) {
		fprintf(stderr, "ERROR: SNDK: unsupported device for this command\n");
		ret = -1;
		goto out;
	}

	/* verify the 0xC2 Device Manageability log page is supported */
	if (sndk_nvme_check_supported_log_page(r, dev,
					      SNDK_NVME_GET_DEV_MGMNT_LOG_PAGE_ID) == false) {
		fprintf(stderr, "ERROR: SNDK: 0xC2 Log Page not supported\n");
		ret = -1;
		goto out;
	} */

	if (!sndk_get_dev_mgment_data(r, dev, &dev_mng_log)) {
		fprintf(stderr, "ERROR: SNDK: 0xC2 Log Page not found\n");
		ret = -1;
		goto out;
	}

	/* Get the assert dump present status */
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &assert_status,
			WDC_C2_ASSERT_DUMP_PRESENT_ID))
		fprintf(stderr, "ERROR: SNDK: Get Assert Status Failed\n");

	/* Get the thermal throttling status */
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &thermal_status,
			WDC_C2_THERMAL_THROTTLE_STATUS_ID))
		fprintf(stderr, "ERROR: SNDK: Get Thermal Throttling Status Failed\n");

	/* Get EOL status */
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &eol_status,
			WDC_C2_USER_EOL_STATUS_ID)) {
		fprintf(stderr, "ERROR: SNDK: Get User EOL Status Failed\n");
		eol_status = cpu_to_le32(-1);
	}

	/* Get Customer EOL state */
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &user_eol_state,
			WDC_C2_USER_EOL_STATE_ID))
		fprintf(stderr, "ERROR: SNDK: Get User EOL State Failed\n");

	/* Get System EOL state*/
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &system_eol_state,
			WDC_C2_SYSTEM_EOL_STATE_ID))
		fprintf(stderr, "ERROR: SNDK: Get System EOL State Failed\n");

	/* Get format corrupt reason*/
	if (!wdc_nvme_parse_dev_status_log_entry(dev_mng_log, &format_corrupt_reason,
			WDC_C2_FORMAT_CORRUPT_REASON_ID))
		fprintf(stderr, "ERROR: SNDK: Get Format Corrupt Reason Failed\n");

	printf("  Drive Status :-\n");
	if ((int)le32_to_cpu(eol_status) >= 0)
		printf("  Percent Used:				%"PRIu32"%%\n",
		       le32_to_cpu(eol_status));
	else
		printf("  Percent Used:				Unknown\n");
	if (system_eol_state == WDC_EOL_STATUS_NORMAL && user_eol_state == WDC_EOL_STATUS_NORMAL)
		printf("  Drive Life Status:			Normal\n");
	else if (system_eol_state == WDC_EOL_STATUS_END_OF_LIFE ||
		 user_eol_state == WDC_EOL_STATUS_END_OF_LIFE)
		printf("  Drive Life Status:			End Of Life\n");
	else if (system_eol_state == WDC_EOL_STATUS_READ_ONLY ||
		 user_eol_state == WDC_EOL_STATUS_READ_ONLY)
		printf("  Drive Life Status:			Read Only\n");
	else
		printf("  Drive Life Status:			Unknown : 0x%08x/0x%08x\n",
		       le32_to_cpu(user_eol_state), le32_to_cpu(system_eol_state));

	if (assert_status == WDC_ASSERT_DUMP_PRESENT)
		printf("  Assert Dump Status:			Present\n");
	else if (assert_status == WDC_ASSERT_DUMP_NOT_PRESENT)
		printf("  Assert Dump Status:			Not Present\n");
	else
		printf("  Assert Dump Status:			Unknown : 0x%08x\n", le32_to_cpu(assert_status));

	if (thermal_status == WDC_THERMAL_THROTTLING_OFF)
		printf("  Thermal Throttling Status:		Off\n");
	else if (thermal_status == WDC_THERMAL_THROTTLING_ON)
		printf("  Thermal Throttling Status:		On\n");
	else if (thermal_status == WDC_THERMAL_THROTTLING_UNAVAILABLE)
		printf("  Thermal Throttling Status:		Unavailable\n");
	else
		printf("  Thermal Throttling Status:		Unknown : 0x%08x\n", le32_to_cpu(thermal_status));

	if (format_corrupt_reason == WDC_FORMAT_NOT_CORRUPT)
		printf("  Format Corrupt Reason:		Format Not Corrupted\n");
	else if (format_corrupt_reason == WDC_FORMAT_CORRUPT_FW_ASSERT)
		printf("  Format Corrupt Reason:	        Format Corrupt due to FW Assert\n");
	else if (format_corrupt_reason == WDC_FORMAT_CORRUPT_UNKNOWN)
		printf("  Format Corrupt Reason:	        Format Corrupt for Unknown Reason\n");
	else
		printf("  Format Corrupt Reason:	        Unknown : 0x%08x\n", le32_to_cpu(format_corrupt_reason));

	free(dev_mng_log);
out:
	nvme_free_tree(r);
	dev_close(dev);
	return ret;
#endif
}

static int sndk_clear_assert_dump(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_clear_assert_dump(argc, argv, command, plugin);
}

static int sndk_drive_resize(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_drive_resize(argc, argv, command, plugin);
}

static int sndk_vs_fw_activate_history(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_fw_activate_history(argc, argv, command, plugin);
}

static int sndk_clear_fw_activate_history(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_clear_fw_activate_history(argc, argv, command, plugin);
}

static int sndk_vs_telemetry_controller_option(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_telemetry_controller_option(argc, argv, command, plugin);
}

static int sndk_reason_identifier(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_reason_identifier(argc, argv, command, plugin);
}

static int sndk_log_page_directory(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_log_page_directory(argc, argv, command, plugin);
}

static int sndk_namespace_resize(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_namespace_resize(argc, argv, command, plugin);
}

static int sndk_vs_drive_info(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_drive_info(argc, argv, command, plugin);
}

static int sndk_capabilities(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	const char *desc = "Send a capabilities command.";
	uint64_t capabilities = 0;
	struct nvme_dev *dev;
	nvme_root_t r;
	int ret;

	OPT_ARGS(opts) = {
		OPT_END()
	};

	ret = parse_and_open(&dev, argc, argv, desc, opts);
	if (ret)
		return ret;

	/* get capabilities */
	r = nvme_scan(NULL);
	sndk_check_device(r, dev);
	capabilities = sndk_get_drive_capabilities(r, dev);

	/* print command and supported status */
	printf("Sandisk Plugin Capabilities for NVME device:%s\n", dev->name);
	printf("vs-internal-log               : %s\n",
	       capabilities & SNDK_DRIVE_CAP_INTERNAL_LOG_MASK ? "Supported" : "Not Supported");
	printf("vs-nand-stats                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_NAND_STATS ? "Supported" : "Not Supported");
	printf("vs-smart-add-log              : %s\n",
	       capabilities & SNDK_DRIVE_CAP_SMART_LOG_MASK ? "Supported" : "Not Supported");
	printf("--C0 Log Page                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_C0_LOG_PAGE ? "Supported" : "Not Supported");
	printf("--C1 Log Page                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_C1_LOG_PAGE ? "Supported" : "Not Supported");
	printf("--C3 Log Page                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_C3_LOG_PAGE ? "Supported" : "Not Supported");
	printf("--CA Log Page                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CA_LOG_PAGE ? "Supported" : "Not Supported");
	printf("--D0 Log Page                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_D0_LOG_PAGE ? "Supported" : "Not Supported");
	printf("clear-pcie-correctable-errors : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CLEAR_PCIE_MASK ? "Supported" : "Not Supported");
	printf("get-drive-status              : %s\n",
	       capabilities & SNDK_DRIVE_CAP_DRIVE_STATUS ? "Supported" : "Not Supported");
	printf("drive-resize                  : %s\n",
	       capabilities & SNDK_DRIVE_CAP_RESIZE ? "Supported" : "Not Supported");
	printf("vs-fw-activate-history        : %s\n",
	       capabilities & SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY_MASK ? "Supported" :
	       "Not Supported");
	printf("clear-fw-activate-history     : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CLEAR_FW_ACT_HISTORY_MASK ? "Supported" :
	       "Not Supported");
	printf("vs-telemetry-controller-option: %s\n",
	       capabilities & SNDK_DRIVE_CAP_DISABLE_CTLR_TELE_LOG ? "Supported" : "Not Supported");
	printf("vs-error-reason-identifier    : %s\n",
	       capabilities & SNDK_DRIVE_CAP_REASON_ID ? "Supported" : "Not Supported");
	printf("log-page-directory            : %s\n",
	       capabilities & SNDK_DRIVE_CAP_LOG_PAGE_DIR ? "Supported" : "Not Supported");
	printf("namespace-resize              : %s\n",
	       capabilities & SNDK_DRIVE_CAP_NS_RESIZE ? "Supported" : "Not Supported");
	printf("vs-drive-info                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_INFO ? "Supported" : "Not Supported");
	printf("vs-temperature-stats          : %s\n",
	       capabilities & SNDK_DRIVE_CAP_TEMP_STATS ? "Supported" : "Not Supported");
	printf("cloud-SSD-plugin-version      : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CLOUD_SSD_VERSION ? "Supported" : "Not Supported");
	printf("vs-pcie-stats                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_PCIE_STATS ? "Supported" : "Not Supported");
	printf("get-error-recovery-log        : %s\n",
	       capabilities & SNDK_DRIVE_CAP_OCP_C1_LOG_PAGE ? "Supported" : "Not Supported");
	printf("get-dev-capabilities-log      : %s\n",
	       capabilities & SNDK_DRIVE_CAP_OCP_C4_LOG_PAGE ? "Supported" : "Not Supported");
	printf("get-unsupported-reqs-log      : %s\n",
	       capabilities & SNDK_DRIVE_CAP_OCP_C5_LOG_PAGE ? "Supported" : "Not Supported");
	printf("get-latency-monitor-log       : %s\n",
	       capabilities & SNDK_DRIVE_CAP_C3_LOG_PAGE ? "Supported" : "Not Supported");
	printf("cloud-boot-SSD-version        : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CLOUD_BOOT_SSD_VERSION ? "Supported" :
	       "Not Supported");
	printf("vs-cloud-log                  : %s\n",
	       capabilities & SNDK_DRIVE_CAP_CLOUD_LOG_PAGE ? "Supported" : "Not Supported");
	printf("vs-hw-rev-log                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_HW_REV_LOG_PAGE ? "Supported" : "Not Supported");
	printf("vs-device_waf                 : %s\n",
	       capabilities & SNDK_DRIVE_CAP_DEVICE_WAF ? "Supported" : "Not Supported");
	printf("set-latency-monitor-feature   : %s\n",
	       capabilities & SNDK_DRIVE_CAP_SET_LATENCY_MONITOR ? "Supported" : "Not Supported");
	printf("capabilities                  : Supported\n");
	nvme_free_tree(r);
	dev_close(dev);

	return 0;
}

static int sndk_cloud_ssd_plugin_version(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_cloud_ssd_plugin_version(argc, argv, command, plugin);
}

static int sndk_vs_pcie_stats(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_pcie_stats(argc, argv, command, plugin);
}

static int sndk_get_latency_monitor_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_get_latency_monitor_log(argc, argv, command, plugin);
}

static int sndk_get_error_recovery_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_get_error_recovery_log(argc, argv, command, plugin);
}

static int sndk_get_dev_capabilities_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_get_dev_capabilities_log(argc, argv, command, plugin);
}

static int sndk_get_unsupported_reqs_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_get_unsupported_reqs_log(argc, argv, command, plugin);
}

static int sndk_cloud_boot_SSD_version(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_cloud_boot_SSD_version(argc, argv, command, plugin);
}

static int sndk_vs_cloud_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_cloud_log(argc, argv, command, plugin);
}

static int sndk_vs_hw_rev_log(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_hw_rev_log(argc, argv, command, plugin);
}

static int sndk_vs_device_waf(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_device_waf(argc, argv, command, plugin);
}

static int sndk_set_latency_monitor_feature(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_set_latency_monitor_feature(argc, argv, command, plugin);
}

static int sndk_vs_temperature_stats(int argc, char **argv,
		struct command *command,
		struct plugin *plugin)
{
	return run_wdc_vs_temperature_stats(argc, argv, command, plugin);
}
