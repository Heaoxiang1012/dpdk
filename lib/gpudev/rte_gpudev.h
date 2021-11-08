/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright (c) 2021 NVIDIA Corporation & Affiliates
 */

#ifndef RTE_GPUDEV_H
#define RTE_GPUDEV_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <rte_bitops.h>
#include <rte_compat.h>

/**
 * @file
 * Generic library to interact with GPU computing device.
 *
 * The API is not thread-safe.
 * Device management must be done by a single thread.
 *
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of devices if rte_gpu_init() is not called. */
#define RTE_GPU_DEFAULT_MAX 32

/** Empty device ID. */
#define RTE_GPU_ID_NONE -1
/** Catch-all device ID. */
#define RTE_GPU_ID_ANY INT16_MIN

/** Catch-all callback data. */
#define RTE_GPU_CALLBACK_ANY_DATA ((void *)-1)

/** Access variable as volatile. */
#define RTE_GPU_VOLATILE(x) (*(volatile typeof(x) *)&(x))

/** Store device info. */
struct rte_gpu_info {
	/** Unique identifier name. */
	const char *name;
	/** Opaque handler of the device context. */
	uint64_t context;
	/** Device ID. */
	int16_t dev_id;
	/** ID of the parent device, RTE_GPU_ID_NONE if no parent */
	int16_t parent;
	/** Total processors available on device. */
	uint32_t processor_count;
	/** Total memory available on device. */
	size_t total_memory;
	/* Local NUMA memory ID. -1 if unknown. */
	int16_t numa_node;
};

/** Flags passed in notification callback. */
enum rte_gpu_event {
	/** Device is just initialized. */
	RTE_GPU_EVENT_NEW,
	/** Device is going to be released. */
	RTE_GPU_EVENT_DEL,
};

/** Prototype of event callback function. */
typedef void (rte_gpu_callback_t)(int16_t dev_id,
		enum rte_gpu_event event, void *user_data);

/** Memory where communication flag is allocated. */
enum rte_gpu_comm_flag_type {
	/** Allocate flag on CPU memory visible from device. */
	RTE_GPU_COMM_FLAG_CPU = 0,
};

/** Communication flag to coordinate CPU with the device. */
struct rte_gpu_comm_flag {
	/** Device that will use the device flag. */
	uint16_t dev_id;
	/** Pointer to flag memory area. */
	uint32_t *ptr;
	/** Type of memory used to allocate the flag. */
	enum rte_gpu_comm_flag_type mtype;
};

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Initialize the device array before probing devices.
 * If not called, the maximum of probed devices is RTE_GPU_DEFAULT_MAX.
 *
 * @param dev_max
 *   Maximum number of devices.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENOMEM if out of memory
 *   - EINVAL if 0 size
 *   - EBUSY if already initialized
 */
__rte_experimental
int rte_gpu_init(size_t dev_max);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Return the number of GPU detected and associated to DPDK.
 *
 * @return
 *   The number of available computing devices.
 */
__rte_experimental
uint16_t rte_gpu_count_avail(void);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Check if the device is valid and initialized in DPDK.
 *
 * @param dev_id
 *   The input device ID.
 *
 * @return
 *   - True if dev_id is a valid and initialized computing device.
 *   - False otherwise.
 */
__rte_experimental
bool rte_gpu_is_valid(int16_t dev_id);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Create a virtual device representing a context in the parent device.
 *
 * @param name
 *   Unique string to identify the device.
 * @param parent
 *   Device ID of the parent.
 * @param child_context
 *   Opaque context handler.
 *
 * @return
 *   Device ID of the new created child, -rte_errno otherwise:
 *   - EINVAL if empty name
 *   - ENAMETOOLONG if long name
 *   - EEXIST if existing device name
 *   - ENODEV if invalid parent
 *   - EPERM if secondary process
 *   - ENOENT if too many devices
 *   - ENOMEM if out of space
 */
__rte_experimental
int16_t rte_gpu_add_child(const char *name,
		int16_t parent, uint64_t child_context);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Get the ID of the next valid GPU initialized in DPDK.
 *
 * @param dev_id
 *   The initial device ID to start the research.
 * @param parent
 *   The device ID of the parent.
 *   RTE_GPU_ID_NONE means no parent.
 *   RTE_GPU_ID_ANY means no or any parent.
 *
 * @return
 *   Next device ID corresponding to a valid and initialized computing device,
 *   RTE_GPU_ID_NONE if there is none.
 */
__rte_experimental
int16_t rte_gpu_find_next(int16_t dev_id, int16_t parent);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Macro to iterate over all valid GPU devices.
 *
 * @param dev_id
 *   The ID of the next possible valid device, usually 0 to iterate all.
 */
#define RTE_GPU_FOREACH(dev_id) \
	RTE_GPU_FOREACH_CHILD(dev_id, RTE_GPU_ID_ANY)

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Macro to iterate over all valid computing devices having no parent.
 *
 * @param dev_id
 *   The ID of the next possible valid device, usually 0 to iterate all.
 */
#define RTE_GPU_FOREACH_PARENT(dev_id) \
	RTE_GPU_FOREACH_CHILD(dev_id, RTE_GPU_ID_NONE)

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Macro to iterate over all valid children of a computing device parent.
 *
 * @param dev_id
 *   The ID of the next possible valid device, usually 0 to iterate all.
 * @param parent
 *   The device ID of the parent.
 */
#define RTE_GPU_FOREACH_CHILD(dev_id, parent) \
	for (dev_id = rte_gpu_find_next(0, parent); \
	     dev_id >= 0; \
	     dev_id = rte_gpu_find_next(dev_id + 1, parent))

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Close device or child context.
 * All resources are released.
 *
 * @param dev_id
 *   Device ID to close.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_close(int16_t dev_id);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Register a function as event callback.
 * A function may be registered multiple times for different events.
 *
 * @param dev_id
 *   Device ID to get notified about.
 *   RTE_GPU_ID_ANY means all devices.
 * @param event
 *   Device event to be registered for.
 * @param function
 *   Callback function to be called on event.
 * @param user_data
 *   Optional parameter passed in the callback.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if NULL function
 *   - ENOMEM if out of memory
 */
__rte_experimental
int rte_gpu_callback_register(int16_t dev_id, enum rte_gpu_event event,
		rte_gpu_callback_t *function, void *user_data);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Unregister for an event.
 *
 * @param dev_id
 *   Device ID to be silenced.
 *   RTE_GPU_ID_ANY means all devices.
 * @param event
 *   Registered event.
 * @param function
 *   Registered function.
 * @param user_data
 *   Optional parameter as registered.
 *   RTE_GPU_CALLBACK_ANY_DATA is a catch-all.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if NULL function
 */
__rte_experimental
int rte_gpu_callback_unregister(int16_t dev_id, enum rte_gpu_event event,
		rte_gpu_callback_t *function, void *user_data);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Return device specific info.
 *
 * @param dev_id
 *   Device ID to get info.
 * @param info
 *   Memory structure to fill with the info.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if NULL info
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_info_get(int16_t dev_id, struct rte_gpu_info *info);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Allocate a chunk of memory in the device.
 *
 * @param dev_id
 *   Device ID requiring allocated memory.
 * @param size
 *   Number of bytes to allocate.
 *   Requesting 0 will do nothing.
 *
 * @return
 *   A pointer to the allocated memory, otherwise NULL and rte_errno is set:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if reserved flags
 *   - ENOTSUP if operation not supported by the driver
 *   - E2BIG if size is higher than limit
 *   - ENOMEM if out of space
 *   - EPERM if driver error
 */
__rte_experimental
void *rte_gpu_mem_alloc(int16_t dev_id, size_t size)
__rte_alloc_size(2);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Deallocate a chunk of memory allocated with rte_gpu_mem_alloc().
 *
 * @param dev_id
 *   Reference device ID.
 * @param ptr
 *   Pointer to the memory area to be deallocated.
 *   NULL is a no-op accepted value.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - ENOTSUP if operation not supported by the driver
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_mem_free(int16_t dev_id, void *ptr);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Register a chunk of memory on the CPU usable by the device.
 *
 * @param dev_id
 *   Device ID requiring allocated memory.
 * @param size
 *   Number of bytes to allocate.
 *   Requesting 0 will do nothing.
 * @param ptr
 *   Pointer to the memory area to be registered.
 *   NULL is a no-op accepted value.

 * @return
 *   A pointer to the allocated memory, otherwise NULL and rte_errno is set:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if reserved flags
 *   - ENOTSUP if operation not supported by the driver
 *   - E2BIG if size is higher than limit
 *   - ENOMEM if out of space
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_mem_register(int16_t dev_id, size_t size, void *ptr);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Deregister a chunk of memory previously registered with rte_gpu_mem_register()
 *
 * @param dev_id
 *   Reference device ID.
 * @param ptr
 *   Pointer to the memory area to be unregistered.
 *   NULL is a no-op accepted value.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - ENOTSUP if operation not supported by the driver
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_mem_unregister(int16_t dev_id, void *ptr);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Enforce a GPU write memory barrier.
 *
 * @param dev_id
 *   Reference device ID.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - ENOTSUP if operation not supported by the driver
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_wmb(int16_t dev_id);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Create a communication flag that can be shared
 * between CPU threads and device workload to exchange some status info
 * (e.g. work is done, processing can start, etc..).
 *
 * @param dev_id
 *   Reference device ID.
 * @param devflag
 *   Pointer to the memory area of the devflag structure.
 * @param mtype
 *   Type of memory to allocate the communication flag.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if invalid inputs
 *   - ENOTSUP if operation not supported by the driver
 *   - ENOMEM if out of space
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_comm_create_flag(uint16_t dev_id,
		struct rte_gpu_comm_flag *devflag,
		enum rte_gpu_comm_flag_type mtype);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Deallocate a communication flag.
 *
 * @param devflag
 *   Pointer to the memory area of the devflag structure.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - ENODEV if invalid dev_id
 *   - EINVAL if NULL devflag
 *   - ENOTSUP if operation not supported by the driver
 *   - EPERM if driver error
 */
__rte_experimental
int rte_gpu_comm_destroy_flag(struct rte_gpu_comm_flag *devflag);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Set the value of a communication flag as the input value.
 * Flag memory area is treated as volatile.
 * The flag must have been allocated with RTE_GPU_COMM_FLAG_CPU.
 *
 * @param devflag
 *   Pointer to the memory area of the devflag structure.
 * @param val
 *   Value to set in the flag.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - EINVAL if invalid input params
 */
__rte_experimental
int rte_gpu_comm_set_flag(struct rte_gpu_comm_flag *devflag,
		uint32_t val);

/**
 * @warning
 * @b EXPERIMENTAL: this API may change without prior notice.
 *
 * Get the value of the communication flag.
 * Flag memory area is treated as volatile.
 * The flag must have been allocated with RTE_GPU_COMM_FLAG_CPU.
 *
 * @param devflag
 *   Pointer to the memory area of the devflag structure.
 * @param val
 *   Flag output value.
 *
 * @return
 *   0 on success, -rte_errno otherwise:
 *   - EINVAL if invalid input params
 */
__rte_experimental
int rte_gpu_comm_get_flag_value(struct rte_gpu_comm_flag *devflag,
		uint32_t *val);

#ifdef __cplusplus
}
#endif

#endif /* RTE_GPUDEV_H */