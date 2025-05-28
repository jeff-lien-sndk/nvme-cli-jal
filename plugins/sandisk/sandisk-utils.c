// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2025 Sandisk Corporation or its affiliates.
 *
 *   Author: Jeff Lien <jeff.lien@sandisk.com>
 *           Brandon Paupore <brandon.paupore@sandisk.com>
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "nvme.h"
#include "libnvme.h"
#include "nvme-print.h"
#include "sandisk-utils.h"
#include "plugins/wdc/wdc-nvme-cmds.h"

/*  SNDK UUID value */
const uint8_t SNDK_UUID[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x2d, 0xb9, 0x8c, 0x52, 0x0c, 0x4c,
	0x5a, 0x15, 0xab, 0xe6, 0x33, 0x29, 0x9a, 0x70, 0xdf, 0xd0
};

/* UUID field with value of 0 indicates end of UUID List*/
const uint8_t SNDK_UUID_END[] = {
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

int sndk_get_pci_ids(nvme_root_t r, struct nvme_dev *dev,
			   uint32_t *device_id, uint32_t *vendor_id)
{
	char vid[256], did[256], id[32];
	nvme_ctrl_t c = NULL;
	nvme_ns_t n = NULL;
	int fd, ret;

	c = nvme_scan_ctrl(r, dev->name);
	if (c) {
		snprintf(vid, sizeof(vid), "%s/device/vendor",
			nvme_ctrl_get_sysfs_dir(c));
		snprintf(did, sizeof(did), "%s/device/device",
			nvme_ctrl_get_sysfs_dir(c));
		nvme_free_ctrl(c);
	} else {
		n = nvme_scan_namespace(dev->name);
		if (!n) {
			fprintf(stderr, "Unable to find %s\n", dev->name);
			return -1;
		}

		snprintf(vid, sizeof(vid), "%s/device/device/vendor",
			nvme_ns_get_sysfs_dir(n));
		snprintf(did, sizeof(did), "%s/device/device/device",
			nvme_ns_get_sysfs_dir(n));
		nvme_free_ns(n);
	}

	fd = open(vid, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ERROR: SNDK: %s : Open vendor file failed\n", __func__);
		return -1;
	}

	ret = read(fd, id, 32);
	close(fd);

	if (ret < 0) {
		fprintf(stderr, "%s: Read of pci vendor id failed\n", __func__);
		return -1;
	}
	id[ret < 32 ? ret : 31] = '\0';
	if (id[strlen(id) - 1] == '\n')
		id[strlen(id) - 1] = '\0';

	*vendor_id = strtol(id, NULL, 0);
	ret = 0;

	fd = open(did, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "ERROR: SNDK: %s : Open device file failed\n", __func__);
		return -1;
	}

	ret = read(fd, id, 32);
	close(fd);

	if (ret < 0) {
		fprintf(stderr, "ERROR: SNDK: %s: Read of pci device id failed\n", __func__);
		return -1;
	}
	id[ret < 32 ? ret : 31] = '\0';
	if (id[strlen(id) - 1] == '\n')
		id[strlen(id) - 1] = '\0';

	*device_id = strtol(id, NULL, 0);
	return 0;
}

int sndk_get_vendor_id(struct nvme_dev *dev, uint32_t *vendor_id)
{
	int ret;
	struct nvme_id_ctrl ctrl;

	memset(&ctrl, 0, sizeof(struct nvme_id_ctrl));
	ret = nvme_identify_ctrl(dev_fd(dev), &ctrl);
	if (ret) {
		fprintf(stderr, "ERROR: SNDK: nvme_identify_ctrl() failed 0x%x\n", ret);
		return -1;
	}

	*vendor_id = (uint32_t) ctrl.vid;

	return ret;
}

bool sndk_check_device(nvme_root_t r, struct nvme_dev *dev)
{
	int ret;
	bool supported;
	uint32_t read_device_id = -1, read_vendor_id = -1;

	ret = sndk_get_pci_ids(r, dev, &read_device_id, &read_vendor_id);
	if (ret < 0) {
		/* Use the identify nvme command to get vendor id due to NVMeOF device. */
		if (sndk_get_vendor_id(dev, &read_vendor_id) < 0)
			return false;
	}

	supported = false;

	if (read_vendor_id == SNDK_NVME_SNDK_VID ||
	    read_vendor_id == SNDK_NVME_WDC_VID)
		supported = true;
	else
		fprintf(stderr,
			"ERROR: SNDK: unsupported Sandisk device, Vendor ID = 0x%x, Device ID = 0x%x\n",
			read_vendor_id, read_device_id);

	return supported;
}

bool sndk_CheckUuidListSupport(struct nvme_dev *dev, struct nvme_id_uuid_list *uuid_list)
{
	int err;
	struct nvme_id_ctrl ctrl;

	memset(&ctrl, 0, sizeof(struct nvme_id_ctrl));
	err = nvme_identify_ctrl(dev_fd(dev), &ctrl);
	if (err) {
		fprintf(stderr, "ERROR: SNDK: nvme_identify_ctrl() failed 0x%x\n", err);
		return false;
	}

	if ((ctrl.ctratt & NVME_CTRL_CTRATT_UUID_LIST) == NVME_CTRL_CTRATT_UUID_LIST) {
		err = nvme_identify_uuid(dev_fd(dev), uuid_list);
		if (!err)
			return true;
		else if (err > 0)
			nvme_show_status(err);
		else
			nvme_show_error("identify UUID list: %s", nvme_strerror(errno));
	}

	return false;
}

bool sndk_UuidEqual(struct nvme_id_uuid_list_entry *entry1, struct nvme_id_uuid_list_entry *entry2)
{
	return !memcmp(entry1->uuid, entry2->uuid, NVME_UUID_LEN);
}


__u64 sndk_get_drive_capabilities(nvme_root_t r, struct nvme_dev *dev)
{
	__u64 capabilities = 0;

	int ret;
	uint32_t read_device_id = -1, read_vendor_id = -1;
	__u32 cust_id;

	ret = sndk_get_pci_ids(r, dev, &read_device_id, &read_vendor_id);
	if (ret < 0) {
		if (sndk_get_vendor_id(dev, &read_vendor_id) < 0)
			return capabilities;
	}

	/*
	 * Below check condition is added due in NVMeOF device
	 * We aren't able to read the device_id in this case
	 * so we can only use the vendor_id
	 */
	if (read_device_id == -1 && read_vendor_id != -1) {
		capabilities = sndk_get_enc_drive_capabilities(r, dev);
		return capabilities;
	}

	switch (read_vendor_id) {
	case SNDK_NVME_WDC_VID:
		switch (read_device_id) {
		case SNDK_NVME_SN630_DEV_ID:
		case SNDK_NVME_SN630_DEV_ID_1:
			capabilities = (SNDK_DRIVE_CAP_INTERNAL_LOG |
					SNDK_DRIVE_CAP_DRIVE_STATUS |
					SNDK_DRIVE_CAP_CLEAR_ASSERT |
					SNDK_DRIVE_CAP_RESIZE |
					SNDK_DRIVE_CAP_CLEAR_PCIE);
			/* verify the 0xCA log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_DEVICE_INFO_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_CA_LOG_PAGE;

			/* verify the 0xD0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_VU_SMART_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_D0_LOG_PAGE;
			break;

		case SNDK_NVME_SN640_DEV_ID:
		case SNDK_NVME_SN640_DEV_ID_1:
		case SNDK_NVME_SN640_DEV_ID_2:
		case SNDK_NVME_SN640_DEV_ID_3:
			/* verify the 0xC0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_SMART_CLOUD_ATTR_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_C0_LOG_PAGE;

			capabilities |= (SNDK_DRIVE_CAP_INTERNAL_LOG |
					SNDK_DRIVE_CAP_DRIVE_STATUS |
					SNDK_DRIVE_CAP_CLEAR_ASSERT |
					SNDK_DRIVE_CAP_RESIZE |
					SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY |
					SNDK_DRIVE_CAP_DISABLE_CTLR_TELE_LOG |
					SNDK_DRIVE_CAP_REASON_ID |
					SNDK_DRIVE_CAP_LOG_PAGE_DIR);

			/* verify the 0xC1 (OCP Error Recovery) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_ERROR_REC_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C1_LOG_PAGE;

			/* verify the 0xC3 (OCP Latency Monitor) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_LATENCY_MON_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_C3_LOG_PAGE;

			/* verify the 0xC4 (OCP Device Capabilities) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_DEV_CAP_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C4_LOG_PAGE;

			/* verify the 0xC5 (OCP Unsupported Requirements) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_UNSUPPORTED_REQS_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C5_LOG_PAGE;

			/* verify the 0xCA log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_DEVICE_INFO_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_CA_LOG_PAGE;

			/* verify the 0xD0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_VU_SMART_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_D0_LOG_PAGE;

			cust_id = run_wdc_get_fw_cust_id(r, dev);
			if (cust_id == SNDK_INVALID_CUSTOMER_ID) {
				fprintf(stderr, "%s: ERROR: SNDK: invalid customer id\n", __func__);
				return -1;
			}

			if ((cust_id == SNDK_CUSTOMER_ID_0x1004) ||
				(cust_id == SNDK_CUSTOMER_ID_0x1008) ||
				(cust_id == SNDK_CUSTOMER_ID_0x1005) ||
				(cust_id == SNDK_CUSTOMER_ID_0x1304))
				capabilities |= (SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
						SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
						SNDK_DRIVE_CAP_INFO |
						SNDK_DRIVE_CAP_CLOUD_SSD_VERSION);
			else
				capabilities |= (SNDK_DRIVE_CAP_CLEAR_FW_ACT_HISTORY |
						SNDK_DRIVE_CAP_CLEAR_PCIE);

			break;

		case SNDK_NVME_SN840_DEV_ID:
		case SNDK_NVME_SN840_DEV_ID_1:
			/* verify the 0xC0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_EOL_STATUS_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_C0_LOG_PAGE;

			capabilities |= (SNDK_DRIVE_CAP_INTERNAL_LOG |
					SNDK_DRIVE_CAP_DRIVE_STATUS |
					SNDK_DRIVE_CAP_CLEAR_ASSERT |
					SNDK_DRIVE_CAP_RESIZE |
					SNDK_DRIVE_CAP_CLEAR_PCIE |
					SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY |
					SNDK_DRIVE_CAP_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_DISABLE_CTLR_TELE_LOG |
					SNDK_DRIVE_CAP_REASON_ID |
					SNDK_DRIVE_CAP_LOG_PAGE_DIR);

			/* verify the 0xCA log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_DEVICE_INFO_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_CA_LOG_PAGE;

			/* verify the 0xD0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_VU_SMART_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_D0_LOG_PAGE;
			break;

		case SNDK_NVME_SN650_DEV_ID:
		case SNDK_NVME_SN650_DEV_ID_1:
		case SNDK_NVME_SN650_DEV_ID_2:
		case SNDK_NVME_SN650_DEV_ID_3:
		case SNDK_NVME_SN650_DEV_ID_4:
		case SNDK_NVME_SN655_DEV_ID:
		case SNDK_NVME_SN655_DEV_ID_1:
			/* verify the 0xC0 log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_NVME_GET_SMART_CLOUD_ATTR_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_C0_LOG_PAGE;

			/* verify the 0xC1 (OCP Error Recovery) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_ERROR_REC_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C1_LOG_PAGE;

			/* verify the 0xC3 (OCP Latency Monitor) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_LATENCY_MON_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_C3_LOG_PAGE;

			/* verify the 0xC4 (OCP Device Capabilities) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_DEV_CAP_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C4_LOG_PAGE;

			/* verify the 0xC5 (OCP Unsupported Requirements) log page is supported */
			if (run_wdc_nvme_check_supported_log_page(r, dev,
				SNDK_UNSUPPORTED_REQS_LOG_ID))
				capabilities |= SNDK_DRIVE_CAP_OCP_C5_LOG_PAGE;

			capabilities |= (SNDK_DRIVE_CAP_INTERNAL_LOG |
					 SNDK_DRIVE_CAP_DRIVE_STATUS |
					 SNDK_DRIVE_CAP_CLEAR_ASSERT |
					 SNDK_DRIVE_CAP_RESIZE |
					 SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY |
					 SNDK_DRIVE_CAP_DISABLE_CTLR_TELE_LOG |
					 SNDK_DRIVE_CAP_REASON_ID |
					 SNDK_DRIVE_CAP_LOG_PAGE_DIR);

			cust_id = run_wdc_get_fw_cust_id(r, dev);
			if (cust_id == SNDK_INVALID_CUSTOMER_ID) {
				fprintf(stderr, "%s: ERROR: SNDK: invalid customer id\n", __func__);
				return -1;
			}

			if ((cust_id == SNDK_CUSTOMER_ID_0x1004) ||
			    (cust_id == SNDK_CUSTOMER_ID_0x1008) ||
			    (cust_id == SNDK_CUSTOMER_ID_0x1005) ||
			    (cust_id == SNDK_CUSTOMER_ID_0x1304))
				capabilities |= (SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
						 SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
						 SNDK_DRIVE_CAP_INFO |
						 SNDK_DRIVE_CAP_CLOUD_SSD_VERSION);
			else
				capabilities |= (SNDK_DRIVE_CAP_CLEAR_FW_ACT_HISTORY |
						 SNDK_DRIVE_CAP_CLEAR_PCIE);

			break;

		case SNDK_NVME_SN861_DEV_ID:
		case SNDK_NVME_SN861_DEV_ID_1:
		case SNDK_NVME_SN861_DEV_ID_2:
			capabilities |= (SNDK_DRIVE_CAP_C0_LOG_PAGE |
					SNDK_DRIVE_CAP_C3_LOG_PAGE |
					SNDK_DRIVE_CAP_CA_LOG_PAGE |
					SNDK_DRIVE_CAP_OCP_C4_LOG_PAGE |
					SNDK_DRIVE_CAP_OCP_C5_LOG_PAGE |
					SNDK_DRIVE_CAP_INTERNAL_LOG |
					SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY_C2 |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_INFO |
					SNDK_DRIVE_CAP_CLOUD_SSD_VERSION |
					SNDK_DRIVE_CAP_LOG_PAGE_DIR |
					SNDK_DRIVE_CAP_DRIVE_STATUS |
					SNDK_DRIVE_CAP_SET_LATENCY_MONITOR);
			break;

		case SNDK_NVME_SNTMP_DEV_ID:
			capabilities |= (SNDK_DRIVE_CAP_C0_LOG_PAGE |
					SNDK_DRIVE_CAP_C3_LOG_PAGE |
					SNDK_DRIVE_CAP_CA_LOG_PAGE |
					SNDK_DRIVE_CAP_OCP_C4_LOG_PAGE |
					SNDK_DRIVE_CAP_OCP_C5_LOG_PAGE |
					SNDK_DRIVE_CAP_DUI |
					SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY_C2 |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_INFO |
					SNDK_DRIVE_CAP_CLOUD_SSD_VERSION |
					SNDK_DRIVE_CAP_LOG_PAGE_DIR |
					SNDK_DRIVE_CAP_DRIVE_STATUS |
					SNDK_DRIVE_CAP_SET_LATENCY_MONITOR);
			break;

		default:
			capabilities = 0;
		}
		break;

	case SNDK_NVME_SNDK_VID:
		switch (read_device_id) {
		case SNDK_NVME_SN520_DEV_ID:
		case SNDK_NVME_SN520_DEV_ID_1:
		case SNDK_NVME_SN520_DEV_ID_2:
		case SNDK_NVME_SN810_DEV_ID:
			capabilities = SNDK_DRIVE_CAP_DUI_DATA;
			break;

		case SNDK_NVME_SN820CL_DEV_ID:
			capabilities = SNDK_DRIVE_CAP_DUI_DATA |
					SNDK_DRIVE_CAP_CLOUD_BOOT_SSD_VERSION |
					SNDK_DRIVE_CAP_CLOUD_LOG_PAGE |
					SNDK_DRIVE_CAP_C0_LOG_PAGE |
					SNDK_DRIVE_CAP_HW_REV_LOG_PAGE |
					SNDK_DRIVE_CAP_INFO |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
					SNDK_DRIVE_CAP_NAND_STATS |
					SNDK_DRIVE_CAP_DEVICE_WAF |
					SNDK_DRIVE_CAP_TEMP_STATS;
			break;

		case SNDK_NVME_SN720_DEV_ID:
			capabilities = SNDK_DRIVE_CAP_DUI_DATA |
					SNDK_DRIVE_CAP_NAND_STATS |
					SNDK_DRIVE_CAP_NS_RESIZE;
			break;

		case SNDK_NVME_SN730_DEV_ID:
			capabilities = SNDK_DRIVE_CAP_DUI |
					SNDK_DRIVE_CAP_NAND_STATS |
					SNDK_DRIVE_CAP_INFO |
					SNDK_DRIVE_CAP_TEMP_STATS |
					SNDK_DRIVE_CAP_VUC_CLEAR_PCIE |
					SNDK_DRIVE_CAP_PCIE_STATS;
			break;

		case SNDK_NVME_SN530_DEV_ID_1:
		case SNDK_NVME_SN530_DEV_ID_2:
		case SNDK_NVME_SN530_DEV_ID_3:
		case SNDK_NVME_SN530_DEV_ID_4:
		case SNDK_NVME_SN530_DEV_ID_5:
		case SNDK_NVME_SN350_DEV_ID:
		case SNDK_NVME_SN570_DEV_ID:
		case SNDK_NVME_SN850X_DEV_ID:
		case SNDK_NVME_SN5000_DEV_ID_1:
		case SNDK_NVME_SN5000_DEV_ID_2:
		case SNDK_NVME_SN5000_DEV_ID_3:
		case SNDK_NVME_SN5000_DEV_ID_4:
		case SNDK_NVME_SN7000S_DEV_ID_1:
		case SNDK_NVME_SN7150_DEV_ID_1:
		case SNDK_NVME_SN7150_DEV_ID_2:
		case SNDK_NVME_SN7150_DEV_ID_3:
		case SNDK_NVME_SN7150_DEV_ID_4:
		case SNDK_NVME_SN7150_DEV_ID_5:
		case SNDK_NVME_SN7100_DEV_ID_1:
		case SNDK_NVME_SN7100_DEV_ID_2:
		case SNDK_NVME_SN7100_DEV_ID_3:
		case SNDK_NVME_SN8000S_DEV_ID:
		case SNDK_NVME_SN5100S_DEV_ID_1:
		case SNDK_NVME_SN5100S_DEV_ID_2:
		case SNDK_NVME_SN5100S_DEV_ID_3:
		case SNDK_NVME_SN740_DEV_ID:
		case SNDK_NVME_SN740_DEV_ID_1:
		case SNDK_NVME_SN740_DEV_ID_2:
		case SNDK_NVME_SN740_DEV_ID_3:
		case SNDK_NVME_SN340_DEV_ID:
			capabilities = SNDK_DRIVE_CAP_DUI;
			break;

		case SNDK_NVME_ZN350_DEV_ID:
		case SNDK_NVME_ZN350_DEV_ID_1:
			capabilities = SNDK_DRIVE_CAP_DUI_DATA |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE |
					SNDK_DRIVE_CAP_C0_LOG_PAGE |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY_C2 |
					SNDK_DRIVE_CAP_INFO |
					SNDK_DRIVE_CAP_CLOUD_SSD_VERSION |
					SNDK_DRIVE_CAP_LOG_PAGE_DIR;
			break;

		default:
			capabilities = 0;
		}
		break;
	default:
		capabilities = 0;
	}

	return capabilities;
}

__u64 sndk_get_enc_drive_capabilities(nvme_root_t r,
					    struct nvme_dev *dev)
{
	int ret;
	uint32_t read_vendor_id;
	__u64 capabilities = 0;
	__u32 cust_id;

	ret = sndk_get_vendor_id(dev, &read_vendor_id);
	if (ret < 0)
		return capabilities;

	switch (read_vendor_id) {
	case SNDK_NVME_WDC_VID:
		capabilities = (SNDK_DRIVE_CAP_INTERNAL_LOG |
			SNDK_DRIVE_CAP_DRIVE_STATUS |
			SNDK_DRIVE_CAP_CLEAR_ASSERT |
			SNDK_DRIVE_CAP_RESIZE);

		/* verify the 0xC3 log page is supported */
		if (run_wdc_nvme_check_supported_log_page(r, dev,
			SNDK_LATENCY_MON_LOG_ID))
			capabilities |= SNDK_DRIVE_CAP_C3_LOG_PAGE;

		/* verify the 0xCB log page is supported */
		if (run_wdc_nvme_check_supported_log_page(r, dev,
			SNDK_NVME_GET_FW_ACT_HISTORY_LOG_ID))
			capabilities |= SNDK_DRIVE_CAP_FW_ACTIVATE_HISTORY;

		/* verify the 0xCA log page is supported */
		if (run_wdc_nvme_check_supported_log_page(r, dev,
			SNDK_NVME_GET_DEVICE_INFO_LOG_ID))
			capabilities |= SNDK_DRIVE_CAP_CA_LOG_PAGE;

		/* verify the 0xD0 log page is supported */
		if (run_wdc_nvme_check_supported_log_page(r, dev,
			SNDK_NVME_GET_VU_SMART_LOG_ID))
			capabilities |= SNDK_DRIVE_CAP_D0_LOG_PAGE;

		cust_id = run_wdc_get_fw_cust_id(r, dev);
		if (cust_id == SNDK_INVALID_CUSTOMER_ID) {
			fprintf(stderr, "%s: ERROR: SNDK: invalid customer id\n", __func__);
			return -1;
		}

		if ((cust_id == SNDK_CUSTOMER_ID_0x1004) ||
			(cust_id == SNDK_CUSTOMER_ID_0x1008) ||
			(cust_id == SNDK_CUSTOMER_ID_0x1005) ||
			(cust_id == SNDK_CUSTOMER_ID_0x1304))
			capabilities |= (SNDK_DRIVE_CAP_VU_FID_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_VU_FID_CLEAR_PCIE);
		else
			capabilities |= (SNDK_DRIVE_CAP_CLEAR_FW_ACT_HISTORY |
					SNDK_DRIVE_CAP_CLEAR_PCIE);

		break;
	default:
		capabilities = 0;
	}

	return capabilities;
}

#if 0 // jal
bool sndk_parse_dev_mng_log_entry(void *data,
		__u32 entry_id,
		struct sndk_c2_log_subpage_header **log_entry)
{
	__u32 remaining_len = 0;
	__u32 log_length = 0;
	__u32 log_entry_size = 0;
	__u32 log_entry_id = 0;
	__u32 offset = 0;
	bool found = false;
	struct sndk_c2_log_subpage_header *p_next_log_entry = NULL;
	struct sndk_c2_log_page_header *hdr_ptr = (struct sndk_c2_log_page_header *)data;

	log_length = le32_to_cpu(hdr_ptr->length);
	/* Ensure log data is large enough for common header */
	if (log_length < sizeof(struct sndk_c2_log_page_header)) {
		fprintf(stderr,
		    "ERROR: SNDK: %s: log smaller than header. log_len: 0x%x  HdrSize: %"PRIxPTR"\n",
		    __func__, log_length, sizeof(struct sndk_c2_log_page_header));
		return found;
	}

	/* Get pointer to first log Entry */
	offset = sizeof(struct sndk_c2_log_page_header);
	p_next_log_entry = (struct sndk_c2_log_subpage_header *)(((__u8 *)data) + offset);
	remaining_len = log_length - offset;

	if (!log_entry) {
		fprintf(stderr, "ERROR: SNDK: - %s: No log entry pointer.\n", __func__);
		return found;
	}
	*log_entry = NULL;

	/* Proceed only if there is at least enough data to read an entry header */
	while (remaining_len >= sizeof(struct sndk_c2_log_subpage_header)) {
		/* Get size of the next entry */
		log_entry_size = le32_to_cpu(p_next_log_entry->length);
		log_entry_id = le32_to_cpu(p_next_log_entry->entry_id);

		/*
		 * If log entry size is 0 or the log entry goes past the end
		 * of the data, we must be at the end of the data
		 */
		if (!log_entry_size || log_entry_size > remaining_len) {
			fprintf(stderr, "ERROR: SNDK: %s: Detected unaligned end of the data. ",
				__func__);
			fprintf(stderr, "Data Offset: 0x%x Entry Size: 0x%x, ",
				offset, log_entry_size);
			fprintf(stderr, "Remaining Log Length: 0x%x Entry Id: 0x%x\n",
				remaining_len, log_entry_id);

			/* Force the loop to end */
			remaining_len = 0;
		} else if (!log_entry_id || log_entry_id > 200) {
			/* Invalid entry - fail the search */
			fprintf(stderr, "ERROR: SNDK: %s: Invalid entry found at offset: 0x%x ",
				__func__, offset);
			fprintf(stderr, "Entry Size: 0x%x, Remaining Log Length: 0x%x ",
				log_entry_size, remaining_len);
			fprintf(stderr, "Entry Id: 0x%x\n", log_entry_id);

			/* Force the loop to end */
			remaining_len = 0;
		} else {
			if (log_entry_id == entry_id) {
				found = true;
				*log_entry = p_next_log_entry;
				remaining_len = 0;
			} else {
				remaining_len -= log_entry_size;
			}

			if (remaining_len > 0) {
				/* Increment the offset counter */
				offset += log_entry_size;

				/* Get the next entry */
				p_next_log_entry =
				(struct sndk_c2_log_subpage_header *)(((__u8 *)data) + offset);
			}
		}
	}

	return found;
}

static bool sndk_nvme_parse_dev_status_log_entry(void *log_data,
		__u32 *ret_data,
		__u32 entry_id)
{
	struct sndk_c2_log_subpage_header *entry_data = NULL;

	if (sndk_parse_dev_mng_log_entry(log_data, entry_id, &entry_data)) {
		if (entry_data) {
			*ret_data = le32_to_cpu(entry_data->data);
			return true;
		}
	}

	*ret_data = 0;
	return false;
}

static bool sndk_get_dev_mgment_data(nvme_root_t r, struct nvme_dev *dev,
				void **data)
{
	bool found = false;
	*data = NULL;
	__u32 device_id = 0, vendor_id = 0;
	bool uuid_present = false;
	int index = 0, uuid_index = 0;
	struct nvme_id_uuid_list uuid_list;

	/* The sndk_get_pci_ids function could fail when drives are connected
	 * via a PCIe switch.  Therefore, the return code is intentionally
	 * being ignored.  The device_id and vendor_id variables have been
	 * initialized to 0 so the code can continue on without issue for
	 * both cases: wdc_get_pci_ids successful or failed.
	 */
	sndk_get_pci_ids(r, dev, &device_id, &vendor_id);

	typedef struct nvme_id_uuid_list_entry *uuid_list_entry;

	memset(&uuid_list, 0, sizeof(struct nvme_id_uuid_list));
	if (sndk_CheckUuidListSupport(dev, &uuid_list)) {
		uuid_list_entry uuid_list_entry_ptr = (uuid_list_entry)&uuid_list.entry[0];

		while (index <= NVME_ID_UUID_LIST_MAX &&
		       !sndk_UuidEqual(uuid_list_entry_ptr, (uuid_list_entry)UUID_END)) {

			if (sndk_UuidEqual(uuid_list_entry_ptr,
					  (uuid_list_entry)SNDK_UUID)) {
				uuid_present = true;
				break;
			}
			index++;
			uuid_list_entry_ptr = (uuid_list_entry)&uuid_list.entry[index];
		}
		if (uuid_present)
			uuid_index = index + 1;
	}

	if (uuid_present) {
		/* use the uuid index found above */
		found = get_dev_mgmt_log_page_data(dev, data, uuid_index);
	} else {
		if (!uuid_index && needs_c2_log_page_check(device_id)) {
			/* In certain devices that don't support UUID lists, there are multiple
			 * definitions of the C2 logpage. In those cases, the code
			 * needs to try two UUID indexes and use an identification algorithm
			 * to determine which is returning the correct log page data.
			 */
			uuid_index = 1;
		}

		found = get_dev_mgmt_log_page_data(dev, data, uuid_index);

		if (!found) {
			/* not found with uuid = 1 try with uuid = 0 */
			uuid_index = 0;
			fprintf(stderr, "Not found, requesting log page with uuid_index %d\n",
					uuid_index);

			found = get_dev_mgmt_log_page_data(dev, data, uuid_index);
		}
	}

	return found;
}
#endif

