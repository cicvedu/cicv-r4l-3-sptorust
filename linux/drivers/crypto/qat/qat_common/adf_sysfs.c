// SPDX-License-Identifier: (BSD-3-Clause OR GPL-2.0-only)
/* Copyright(c) 2022 Intel Corporation */
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/pci.h>
#include "adf_accel_devices.h"
#include "adf_cfg.h"
#include "adf_common_drv.h"

static const char * const state_operations[] = {
	[DEV_DOWN] = "down",
	[DEV_UP] = "up",
};

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct adf_accel_dev *accel_dev;
	char *state;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	state = adf_dev_started(accel_dev) ? "up" : "down";
	return sysfs_emit(buf, "%s\n", state);
}

static ssize_t state_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct adf_accel_dev *accel_dev;
	u32 accel_id;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	accel_id = accel_dev->accel_id;

	if (adf_devmgr_in_reset(accel_dev) || adf_dev_in_use(accel_dev)) {
		dev_info(dev, "Device qat_dev%d is busy\n", accel_id);
		return -EBUSY;
	}

	ret = sysfs_match_string(state_operations, buf);
	if (ret < 0)
		return ret;

	switch (ret) {
	case DEV_DOWN:
		if (!adf_dev_started(accel_dev)) {
			dev_info(dev, "Device qat_dev%d already down\n",
				 accel_id);
			return -EINVAL;
		}

		dev_info(dev, "Stopping device qat_dev%d\n", accel_id);

		ret = adf_dev_shutdown_cache_cfg(accel_dev);
		if (ret < 0)
			return -EINVAL;

		break;
	case DEV_UP:
		if (adf_dev_started(accel_dev)) {
			dev_info(dev, "Device qat_dev%d already up\n",
				 accel_id);
			return -EINVAL;
		}

		dev_info(dev, "Starting device qat_dev%d\n", accel_id);

		ret = GET_HW_DATA(accel_dev)->dev_config(accel_dev);
		if (!ret)
			ret = adf_dev_init(accel_dev);
		if (!ret)
			ret = adf_dev_start(accel_dev);

		if (ret < 0) {
			dev_err(dev, "Failed to start device qat_dev%d\n",
				accel_id);
			adf_dev_shutdown_cache_cfg(accel_dev);
			return ret;
		}
		break;
	default:
		return -EINVAL;
	}

	return count;
}

static const char * const services_operations[] = {
	ADF_CFG_CY,
	ADF_CFG_DC,
};

static ssize_t cfg_services_show(struct device *dev, struct device_attribute *attr,
				 char *buf)
{
	char services[ADF_CFG_MAX_VAL_LEN_IN_BYTES] = {0};
	struct adf_accel_dev *accel_dev;
	int ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	ret = adf_cfg_get_param_value(accel_dev, ADF_GENERAL_SEC,
				      ADF_SERVICES_ENABLED, services);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%s\n", services);
}

static int adf_sysfs_update_dev_config(struct adf_accel_dev *accel_dev,
				       const char *services)
{
	return adf_cfg_add_key_value_param(accel_dev, ADF_GENERAL_SEC,
					   ADF_SERVICES_ENABLED, services,
					   ADF_STR);
}

static ssize_t cfg_services_store(struct device *dev, struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct adf_hw_device_data *hw_data;
	struct adf_accel_dev *accel_dev;
	int ret;

	ret = sysfs_match_string(services_operations, buf);
	if (ret < 0)
		return ret;

	accel_dev = adf_devmgr_pci_to_accel_dev(to_pci_dev(dev));
	if (!accel_dev)
		return -EINVAL;

	if (adf_dev_started(accel_dev)) {
		dev_info(dev, "Device qat_dev%d must be down to reconfigure the service.\n",
			 accel_dev->accel_id);
		return -EINVAL;
	}

	ret = adf_sysfs_update_dev_config(accel_dev, services_operations[ret]);
	if (ret < 0)
		return ret;

	hw_data = GET_HW_DATA(accel_dev);

	/* Update capabilities mask after change in configuration.
	 * A call to this function is required as capabilities are, at the
	 * moment, tied to configuration
	 */
	hw_data->accel_capabilities_mask = hw_data->get_accel_cap(accel_dev);
	if (!hw_data->accel_capabilities_mask)
		return -EINVAL;

	return count;
}

static DEVICE_ATTR_RW(state);
static DEVICE_ATTR_RW(cfg_services);

static struct attribute *qat_attrs[] = {
	&dev_attr_state.attr,
	&dev_attr_cfg_services.attr,
	NULL,
};

static struct attribute_group qat_group = {
	.attrs = qat_attrs,
	.name = "qat",
};

int adf_sysfs_init(struct adf_accel_dev *accel_dev)
{
	int ret;

	ret = devm_device_add_group(&GET_DEV(accel_dev), &qat_group);
	if (ret) {
		dev_err(&GET_DEV(accel_dev),
			"Failed to create qat attribute group: %d\n", ret);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(adf_sysfs_init);
