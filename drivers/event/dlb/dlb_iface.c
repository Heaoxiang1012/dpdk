/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2016-2020 Intel Corporation
 */

#include <stdint.h>

#include "dlb_priv.h"

/* DLB PMD Internal interface function pointers.
 * If VDEV (bifurcated PMD),  these will resolve to functions that issue ioctls
 * serviced by DLB kernel module.
 * If PCI (PF PMD),  these will be implemented locally in user mode.
 */

void (*dlb_iface_low_level_io_init)(struct dlb_eventdev *dlb);

int (*dlb_iface_open)(struct dlb_hw_dev *handle, const char *name);

int (*dlb_iface_get_device_version)(struct dlb_hw_dev *handle,
				    uint8_t *revision);

int (*dlb_iface_get_num_resources)(struct dlb_hw_dev *handle,
				   struct dlb_get_num_resources_args *rsrcs);

int (*dlb_iface_get_cq_poll_mode)(struct dlb_hw_dev *handle,
				  enum dlb_cq_poll_modes *mode);

