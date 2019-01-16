/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2001-2019
 */

#include "ice_common.h"
#include "ice_flex_pipe.h"
#include "ice_protocol_type.h"
#include "ice_flow.h"

static const struct ice_tunnel_type_scan tnls[] = {
	{ TNL_VXLAN,		"TNL_VXLAN" },
	{ TNL_GTPC,		"TNL_GTPC" },
	{ TNL_GTPC_TEID,	"TNL_GTPC_TEID" },
	{ TNL_GTPU,		"TNL_GTPC" },
	{ TNL_GTPU_TEID,	"TNL_GTPU_TEID" },
	{ TNL_VXLAN_GPE,	"TNL_VXLAN_GPE" },
	{ TNL_GENEVE,		"TNL_GENEVE" },
	{ TNL_NAT,		"TNL_NAT" },
	{ TNL_ROCE_V2,		"TNL_ROCE_V2" },
	{ TNL_MPLSO_UDP,	"TNL_MPLSO_UDP" },
	{ TNL_UDP2_END,		"TNL_UDP2_END" },
	{ TNL_UPD_END,		"TNL_UPD_END" },
	{ TNL_LAST,		"" }
};

static const u32 ice_sect_lkup[ICE_BLK_COUNT][ICE_SECT_COUNT] = {
	/* SWITCH */
	{
		ICE_SID_XLT0_SW,
		ICE_SID_XLT_KEY_BUILDER_SW,
		ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW,
		ICE_SID_CDID_KEY_BUILDER_SW,
		ICE_SID_CDID_REDIR_SW
	},

	/* ACL */
	{
		ICE_SID_XLT0_ACL,
		ICE_SID_XLT_KEY_BUILDER_ACL,
		ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL,
		ICE_SID_CDID_KEY_BUILDER_ACL,
		ICE_SID_CDID_REDIR_ACL
	},

	/* FD */
	{
		ICE_SID_XLT0_FD,
		ICE_SID_XLT_KEY_BUILDER_FD,
		ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD,
		ICE_SID_CDID_KEY_BUILDER_FD,
		ICE_SID_CDID_REDIR_FD
	},

	/* RSS */
	{
		ICE_SID_XLT0_RSS,
		ICE_SID_XLT_KEY_BUILDER_RSS,
		ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS,
		ICE_SID_CDID_KEY_BUILDER_RSS,
		ICE_SID_CDID_REDIR_RSS
	},

	/* PE */
	{
		ICE_SID_XLT0_PE,
		ICE_SID_XLT_KEY_BUILDER_PE,
		ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE,
		ICE_SID_CDID_KEY_BUILDER_PE,
		ICE_SID_CDID_REDIR_PE
	}
};

/**
 * ice_sect_id - returns section ID
 * @blk: block type
 * @sect: section type
 *
 * This helper function returns the proper section ID given a block type and a
 * section type.
 */
static u32 ice_sect_id(enum ice_block blk, enum ice_sect sect)
{
	return ice_sect_lkup[blk][sect];
}

/**
 * ice_pkg_val_buf
 * @buf: pointer to the ice buffer
 *
 * This helper function validates a buffer's header.
 */
static struct ice_buf_hdr *ice_pkg_val_buf(struct ice_buf *buf)
{
	struct ice_buf_hdr *hdr;
	u16 section_count;
	u16 data_end;

	hdr = (struct ice_buf_hdr *)buf->buf;
	/* verify data */
	section_count = LE16_TO_CPU(hdr->section_count);
	if (section_count < ICE_MIN_S_COUNT || section_count > ICE_MAX_S_COUNT)
		return NULL;

	data_end = LE16_TO_CPU(hdr->data_end);
	if (data_end < ICE_MIN_S_DATA_END || data_end > ICE_MAX_S_DATA_END)
		return NULL;

	return hdr;
}

/**
 * ice_find_buf_table
 * @ice_seg: pointer to the ice segment
 *
 * Returns the address of the buffer table within the ice segment.
 */
static struct ice_buf_table *ice_find_buf_table(struct ice_seg *ice_seg)
{
	struct ice_nvm_table *nvms;

	nvms = (struct ice_nvm_table *)(ice_seg->device_table +
		LE32_TO_CPU(ice_seg->device_table_count));

	return (struct ice_buf_table *)
		(nvms->vers + LE32_TO_CPU(nvms->table_count));
}

/**
 * ice_pkg_enum_buf
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This function will enumerate all the buffers in the ice segment. The first
 * call is made with the ice_seg parameter non-NULL; on subsequent calls,
 * ice_seg is set to NULL which continues the enumeration. When the function
 * returns a NULL pointer, then the end of the buffers has been reached, or an
 * unexpected value has been detected (for example an invalid section count or
 * an invalid buffer end value).
 */
static struct ice_buf_hdr *
ice_pkg_enum_buf(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (ice_seg) {
		state->buf_table = ice_find_buf_table(ice_seg);
		if (!state->buf_table)
			return NULL;

		state->buf_idx = 0;
		return ice_pkg_val_buf(state->buf_table->buf_array);
	}

	if (++state->buf_idx < LE32_TO_CPU(state->buf_table->buf_count))
		return ice_pkg_val_buf(state->buf_table->buf_array +
				       state->buf_idx);
	else
		return NULL;
}

/**
 * ice_pkg_advance_sect
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 *
 * This helper function will advance the section within the ice segment,
 * also advancing the buffer if needed.
 */
static bool
ice_pkg_advance_sect(struct ice_seg *ice_seg, struct ice_pkg_enum *state)
{
	if (!ice_seg && !state->buf)
		return false;

	if (!ice_seg && state->buf)
		if (++state->sect_idx < LE16_TO_CPU(state->buf->section_count))
			return true;

	state->buf = ice_pkg_enum_buf(ice_seg, state);
	if (!state->buf)
		return false;

	/* start of new buffer, reset section index */
	state->sect_idx = 0;
	return true;
}

/**
 * ice_pkg_enum_section
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 *
 * This function will enumerate all the sections of a particular type in the
 * ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the matching
 * sections has been reached.
 */
static void *
ice_pkg_enum_section(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
		     u32 sect_type)
{
	u16 offset, size;

	if (ice_seg)
		state->type = sect_type;

	if (!ice_pkg_advance_sect(ice_seg, state))
		return NULL;

	/* scan for next matching section */
	while (state->buf->section_entry[state->sect_idx].type !=
	       CPU_TO_LE32(state->type))
		if (!ice_pkg_advance_sect(NULL, state))
			return NULL;

	/* validate section */
	offset = LE16_TO_CPU(state->buf->section_entry[state->sect_idx].offset);
	if (offset < ICE_MIN_S_OFF || offset > ICE_MAX_S_OFF)
		return NULL;

	size = LE16_TO_CPU(state->buf->section_entry[state->sect_idx].size);
	if (size < ICE_MIN_S_SZ || size > ICE_MAX_S_SZ)
		return NULL;

	/* make sure the section fits in the buffer */
	if (offset + size > ICE_PKG_BUF_SIZE)
		return NULL;

	state->sect_type =
		LE32_TO_CPU(state->buf->section_entry[state->sect_idx].type);

	/* calc pointer to this section */
	state->sect = ((u8 *)state->buf) +
		LE16_TO_CPU(state->buf->section_entry[state->sect_idx].offset);

	return state->sect;
}

/**
 * ice_pkg_enum_entry
 * @ice_seg: pointer to the ice segment (or NULL on subsequent calls)
 * @state: pointer to the enum state
 * @sect_type: section type to enumerate
 * @offset: pointer to variable that receives the offset in the table (optional)
 * @handler: function that handles access to the entries into the section type
 *
 * This function will enumerate all the entries in particular section type in
 * the ice segment. The first call is made with the ice_seg parameter non-NULL;
 * on subsequent calls, ice_seg is set to NULL which continues the enumeration.
 * When the function returns a NULL pointer, then the end of the entries has
 * been reached.
 *
 * Since each section may have a different header and entry size, the handler
 * function is needed to determine the number and location entries in each
 * section.
 *
 * The offset parameter is optional, but should be used for sections that
 * contain an offset for each section table. For such cases, the section handler
 * function must return the appropriate offset + index to give the absolution
 * offset for each entry. For example, if the base for a section's header
 * indicates a base offset of 10, and the index for the entry is 2, then
 * section handler function should set the offset to 10 + 2 = 12.
 */
static void *
ice_pkg_enum_entry(struct ice_seg *ice_seg, struct ice_pkg_enum *state,
		   u32 sect_type, u32 *offset,
		   void *(*handler)(u32 sect_type, void *section,
				    u32 index, u32 *offset))
{
	void *entry;

	if (ice_seg) {
		if (!handler)
			return NULL;

		if (!ice_pkg_enum_section(ice_seg, state, sect_type))
			return NULL;

		state->entry_idx = 0;
		state->handler = handler;
	} else {
		state->entry_idx++;
	}

	if (!state->handler)
		return NULL;

	/* get entry */
	entry = state->handler(state->sect_type, state->sect, state->entry_idx,
			       offset);
	if (!entry) {
		/* end of a section, look for another section of this type */
		if (!ice_pkg_enum_section(NULL, state, 0))
			return NULL;

		state->entry_idx = 0;
		entry = state->handler(state->sect_type, state->sect,
				       state->entry_idx, offset);
	}

	return entry;
}

/**
 * ice_boost_tcam_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the boost tcam entry to be returned
 * @offset: pointer to receive absolute offset, always 0 for boost tcam sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual boost tcam entries.
 */
static void *
ice_boost_tcam_handler(u32 sect_type, void *section, u32 index, u32 *offset)
{
	struct ice_boost_tcam_section *boost;

	if (!section)
		return NULL;

	if (sect_type != ICE_SID_RXPARSER_BOOST_TCAM)
		return NULL;

	if (index > ICE_MAX_BST_TCAMS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	boost = (struct ice_boost_tcam_section *)section;
	if (index >= LE16_TO_CPU(boost->count))
		return NULL;

	return boost->tcam + index;
}

/**
 * ice_find_boost_entry
 * @ice_seg: pointer to the ice segment (non-NULL)
 * @addr: Boost TCAM address of entry to search for
 * @entry: returns pointer to the entry
 *
 * Finds a particular Boost TCAM entry and returns a pointer to that entry
 * if it is found. The ice_seg parameter must not be NULL since the first call
 * to ice_pkg_enum_entry requires a pointer to an actual ice_segment structure.
 */
static enum ice_status
ice_find_boost_entry(struct ice_seg *ice_seg, u16 addr,
		     struct ice_boost_tcam_entry **entry)
{
	struct ice_boost_tcam_entry *tcam;
	struct ice_pkg_enum state;

	ice_memset(&state, 0, sizeof(state), ICE_NONDMA_MEM);

	if (!ice_seg)
		return ICE_ERR_PARAM;

	do {
		tcam = (struct ice_boost_tcam_entry *)
		       ice_pkg_enum_entry(ice_seg, &state,
					  ICE_SID_RXPARSER_BOOST_TCAM, NULL,
					  ice_boost_tcam_handler);
		if (tcam && LE16_TO_CPU(tcam->addr) == addr) {
			*entry = tcam;
			return ICE_SUCCESS;
		}

		ice_seg = NULL;
	} while (tcam);

	*entry = NULL;
	return ICE_ERR_CFG;
}

/**
 * ice_label_enum_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the label entry to be returned
 * @offset: pointer to receive absolute offset, always zero for label sections
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * Handles enumeration of individual label entries.
 */
static void *
ice_label_enum_handler(u32 __always_unused sect_type, void *section, u32 index,
		       u32 *offset)
{
	struct ice_label_section *labels;

	if (!section)
		return NULL;

	if (index > ICE_MAX_LABELS_IN_BUF)
		return NULL;

	if (offset)
		*offset = 0;

	labels = (struct ice_label_section *)section;
	if (index >= LE16_TO_CPU(labels->count))
		return NULL;

	return labels->label + index;
}

/**
 * ice_enum_labels
 * @ice_seg: pointer to the ice segment (NULL on subsequent calls)
 * @type: the section type that will contain the label (0 on subsequent calls)
 * @state: ice_pkg_enum structure that will hold the state of the enumeration
 * @value: pointer to a value that will return the label's value if found
 *
 * Enumerates a list of labels in the package. The caller will call
 * ice_enum_labels(ice_seg, type, ...) to start the enumeration, then call
 * ice_enum_labels(NULL, 0, ...) to continue. When the function returns a NULL
 * the end of the list has been reached.
 */
static char *
ice_enum_labels(struct ice_seg *ice_seg, u32 type, struct ice_pkg_enum *state,
		u16 *value)
{
	struct ice_label *label;

	/* Check for valid label section on first call */
	if (type && !(type >= ICE_SID_LBL_FIRST && type <= ICE_SID_LBL_LAST))
		return NULL;

	label = (struct ice_label *)ice_pkg_enum_entry(ice_seg, state, type,
						       NULL,
						       ice_label_enum_handler);
	if (!label)
		return NULL;

	*value = LE16_TO_CPU(label->value);
	return label->name;
}

/**
 * ice_init_pkg_hints
 * @hw: pointer to the HW structure
 * @ice_seg: pointer to the segment of the package scan (non-NULL)
 *
 * This function will scan the package and save off relevant information
 * (hints or metadata) for driver use. The ice_seg parameter must not be NULL
 * since the first call to ice_enum_labels requires a pointer to an actual
 * ice_seg structure.
 */
void ice_init_pkg_hints(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_pkg_enum state;
	char *label_name;
	u16 val;
	int i;

	ice_memset(&hw->tnl, 0, sizeof(hw->tnl), ICE_NONDMA_MEM);

	if (!ice_seg)
		return;

	label_name = ice_enum_labels(ice_seg, ICE_SID_LBL_RXPARSER_TMEM, &state,
				     &val);

	while (label_name && hw->tnl.count < ICE_TUNNEL_MAX_ENTRIES) {
		for (i = 0; tnls[i].type != TNL_LAST; i++) {
			if (!strncmp(label_name, tnls[i].label_prefix,
				     strlen(tnls[i].label_prefix))) {
				hw->tnl.tbl[hw->tnl.count].type = tnls[i].type;
				hw->tnl.tbl[hw->tnl.count].valid = false;
				hw->tnl.tbl[hw->tnl.count].in_use = false;
				hw->tnl.tbl[hw->tnl.count].marked = false;
				hw->tnl.tbl[hw->tnl.count].boost_addr = val;
				hw->tnl.tbl[hw->tnl.count].port = 0;
				hw->tnl.count++;
				break;
			}
		}

		label_name = ice_enum_labels(NULL, 0, &state, &val);
	}

	/* Cache the appropriate boost tcam entry pointers */
	for (i = 0; i < hw->tnl.count; i++) {
		ice_find_boost_entry(ice_seg, hw->tnl.tbl[i].boost_addr,
				     &hw->tnl.tbl[i].boost_entry);
		if (hw->tnl.tbl[i].boost_entry)
			hw->tnl.tbl[i].valid = true;
	}
}

/* Key creation */

#define ICE_DC_KEY	0x1	/* don't care */
#define ICE_DC_KEYINV	0x1
#define ICE_NM_KEY	0x0	/* never match */
#define ICE_NM_KEYINV	0x0
#define ICE_0_KEY	0x1	/* match 0 */
#define ICE_0_KEYINV	0x0
#define ICE_1_KEY	0x0	/* match 1 */
#define ICE_1_KEYINV	0x1

/**
 * ice_gen_key_word - generate 16-bits of a key/mask word
 * @val: the value
 * @valid: valid bits mask (change only the valid bits)
 * @dont_care: don't care mask
 * @nvr_mtch: never match mask
 * @key: pointer to an array of where the resulting key portion
 * @key_inv: pointer to an array of where the resulting key invert portion
 *
 * This function generates 16-bits from a 8-bit value, an 8-bit don't care mask
 * and an 8-bit never match mask. The 16-bits of output are divided into 8 bits
 * of key and 8 bits of key invert.
 *
 *     '0' =    b01, always match a 0 bit
 *     '1' =    b10, always match a 1 bit
 *     '?' =    b11, don't care bit (always matches)
 *     '~' =    b00, never match bit
 *
 * Input:
 *          val:         b0  1  0  1  0  1
 *          dont_care:   b0  0  1  1  0  0
 *          never_mtch:  b0  0  0  0  1  1
 *          ------------------------------
 * Result:  key:        b01 10 11 11 00 00
 */
static enum ice_status
ice_gen_key_word(u8 val, u8 valid, u8 dont_care, u8 nvr_mtch, u8 *key,
		 u8 *key_inv)
{
	u8 in_key = *key, in_key_inv = *key_inv;
	u8 i;

	/* 'dont_care' and 'nvr_mtch' masks cannot overlap */
	if ((dont_care ^ nvr_mtch) != (dont_care | nvr_mtch))
		return ICE_ERR_CFG;

	*key = 0;
	*key_inv = 0;

	/* encode the 8 bits into 8-bit key and 8-bit key invert */
	for (i = 0; i < 8; i++) {
		*key >>= 1;
		*key_inv >>= 1;

		if (!(valid & 0x1)) { /* change only valid bits */
			*key |= (in_key & 0x1) << 7;
			*key_inv |= (in_key_inv & 0x1) << 7;
		} else if (dont_care & 0x1) { /* don't care bit */
			*key |= ICE_DC_KEY << 7;
			*key_inv |= ICE_DC_KEYINV << 7;
		} else if (nvr_mtch & 0x1) { /* never match bit */
			*key |= ICE_NM_KEY << 7;
			*key_inv |= ICE_NM_KEYINV << 7;
		} else if (val & 0x01) { /* exact 1 match */
			*key |= ICE_1_KEY << 7;
			*key_inv |= ICE_1_KEYINV << 7;
		} else { /* exact 0 match */
			*key |= ICE_0_KEY << 7;
			*key_inv |= ICE_0_KEYINV << 7;
		}

		dont_care >>= 1;
		nvr_mtch >>= 1;
		valid >>= 1;
		val >>= 1;
		in_key >>= 1;
		in_key_inv >>= 1;
	}

	return ICE_SUCCESS;
}

/**
 * ice_bits_max_set - determine if the number of bits set is within a maximum
 * @mask: pointer to the byte array which is the mask
 * @size: the number of bytes in the mask
 * @max: the max number of set bits
 *
 * This function determines if there are at most 'max' number of bits set in an
 * array. Returns true if the number for bits set is <= max or will return false
 * otherwise.
 */
static bool ice_bits_max_set(const u8 *mask, u16 size, u16 max)
{
	u16 count = 0;
	u16 i, j;

	/* check each byte */
	for (i = 0; i < size; i++) {
		/* if 0, go to next byte */
		if (!mask[i])
			continue;

		/* We know there is at least one set bit in this byte because of
		 * the above check; if we already have found 'max' number of
		 * bits set, then we can return failure now.
		 */
		if (count == max)
			return false;

		/* count the bits in this byte, checking threshold */
		for (j = 0; j < BITS_PER_BYTE; j++) {
			count += (mask[i] & (0x1 << j)) ? 1 : 0;
			if (count > max)
				return false;
		}
	}

	return true;
}

/**
 * ice_set_key - generate a variable sized key with multiples of 16-bits
 * @key: pointer to where the key will be stored
 * @size: the size of the complete key in bytes (must be even)
 * @val: array of 8-bit values that makes up the value portion of the key
 * @upd: array of 8-bit masks that determine what key portion to update
 * @dc: array of 8-bit masks that make up the dont' care mask
 * @nm: array of 8-bit masks that make up the never match mask
 * @off: the offset of the first byte in the key to update
 * @len: the number of bytes in the key update
 *
 * This function generates a key from a value, a don't care mask and a never
 * match mask.
 * upd, dc, and nm are optional parameters, and can be NULL:
 *	upd == NULL --> udp mask is all 1's (update all bits)
 *	dc == NULL --> dc mask is all 0's (no don't care bits)
 *	nm == NULL --> nm mask is all 0's (no never match bits)
 */
enum ice_status
ice_set_key(u8 *key, u16 size, u8 *val, u8 *upd, u8 *dc, u8 *nm, u16 off,
	    u16 len)
{
	u16 half_size;
	u16 i;

	/* size must be a multiple of 2 bytes. */
	if (size % 2)
		return ICE_ERR_CFG;
	half_size = size / 2;

	if (off + len > half_size)
		return ICE_ERR_CFG;

	/* Make sure at most one bit is set in the never match mask. Having more
	 * than one never match mask bit set will cause HW to consume excessive
	 * power otherwise; this is a power management efficiency check.
	 */
#define ICE_NVR_MTCH_BITS_MAX	1
	if (nm && !ice_bits_max_set(nm, len, ICE_NVR_MTCH_BITS_MAX))
		return ICE_ERR_CFG;

	for (i = 0; i < len; i++)
		if (ice_gen_key_word(val[i], upd ? upd[i] : 0xff,
				     dc ? dc[i] : 0, nm ? nm[i] : 0,
				     key + off + i, key + half_size + off + i))
			return ICE_ERR_CFG;

	return ICE_SUCCESS;
}

/**
 * ice_acquire_global_cfg_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the global config lock for reading
 * or writing of the package. When attempting to obtain write access, the
 * caller must check for the following two return values:
 *
 * ICE_SUCCESS        - Means the caller has acquired the global config lock
 *                      and can perform writing of the package.
 * ICE_ERR_AQ_NO_WORK - Indicates another driver has already written the
 *                      package or has found that no update was necessary; in
 *                      this case, the caller can just skip performing any
 *                      update of the package.
 */
static enum ice_status
ice_acquire_global_cfg_lock(struct ice_hw *hw,
			    enum ice_aq_res_access_type access)
{
	enum ice_status status;

	ice_debug(hw, ICE_DBG_TRACE, "ice_acquire_global_cfg_lock");

	status = ice_acquire_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID, access,
				 ICE_GLOBAL_CFG_LOCK_TIMEOUT);

	if (status == ICE_ERR_AQ_NO_WORK)
		ice_debug(hw, ICE_DBG_PKG,
			  "Global config lock: No work to do\n");

	return status;
}

/**
 * ice_release_global_cfg_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the global config lock.
 */
static void ice_release_global_cfg_lock(struct ice_hw *hw)
{
	ice_release_res(hw, ICE_GLOBAL_CFG_LOCK_RES_ID);
}

/**
 * ice_acquire_change_lock
 * @hw: pointer to the HW structure
 * @access: access type (read or write)
 *
 * This function will request ownership of the change lock.
 */
static enum ice_status
ice_acquire_change_lock(struct ice_hw *hw, enum ice_aq_res_access_type access)
{
	ice_debug(hw, ICE_DBG_TRACE, "ice_acquire_change_lock");

	return ice_acquire_res(hw, ICE_CHANGE_LOCK_RES_ID, access,
			       ICE_CHANGE_LOCK_TIMEOUT);
}

/**
 * ice_release_change_lock
 * @hw: pointer to the HW structure
 *
 * This function will release the change lock using the proper Admin Command.
 */
static void ice_release_change_lock(struct ice_hw *hw)
{
	ice_debug(hw, ICE_DBG_TRACE, "ice_release_change_lock");

	ice_release_res(hw, ICE_CHANGE_LOCK_RES_ID);
}

/**
 * ice_aq_download_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package buffer to transfer
 * @buf_size: the size of the package buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Download Package (0x0C40)
 */
static enum ice_status
ice_aq_download_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		    u16 buf_size, bool last_buf, u32 *error_offset,
		    u32 *error_info, struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	ice_debug(hw, ICE_DBG_TRACE, "ice_aq_download_pkg");

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_download_pkg);
	desc.flags |= CPU_TO_LE16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = LE32_TO_CPU(resp->error_offset);
		if (error_info)
			*error_info = LE32_TO_CPU(resp->error_info);
	}

	return status;
}

/**
 * ice_aq_upload_section
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package buffer which will receive the section
 * @buf_size: the size of the package buffer
 * @cd: pointer to command details structure or NULL
 *
 * Upload Section (0x0C41)
 */
enum ice_status
ice_aq_upload_section(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf,
		      u16 buf_size, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_debug(hw, ICE_DBG_TRACE, "ice_aq_upload_section");
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_upload_section);
	desc.flags |= CPU_TO_LE16(ICE_AQ_FLAG_RD);

	return ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
}

/**
 * ice_aq_update_pkg
 * @hw: pointer to the hardware structure
 * @pkg_buf: the package cmd buffer
 * @buf_size: the size of the package cmd buffer
 * @last_buf: last buffer indicator
 * @error_offset: returns error offset
 * @error_info: returns error information
 * @cd: pointer to command details structure or NULL
 *
 * Update Package (0x0C42)
 */
static enum ice_status
ice_aq_update_pkg(struct ice_hw *hw, struct ice_buf_hdr *pkg_buf, u16 buf_size,
		  bool last_buf, u32 *error_offset, u32 *error_info,
		  struct ice_sq_cd *cd)
{
	struct ice_aqc_download_pkg *cmd;
	struct ice_aq_desc desc;
	enum ice_status status;

	ice_debug(hw, ICE_DBG_TRACE, "ice_aq_update_pkg");

	if (error_offset)
		*error_offset = 0;
	if (error_info)
		*error_info = 0;

	cmd = &desc.params.download_pkg;
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_update_pkg);
	desc.flags |= CPU_TO_LE16(ICE_AQ_FLAG_RD);

	if (last_buf)
		cmd->flags |= ICE_AQC_DOWNLOAD_PKG_LAST_BUF;

	status = ice_aq_send_cmd(hw, &desc, pkg_buf, buf_size, cd);
	if (status == ICE_ERR_AQ_ERROR) {
		/* Read error from buffer only when the FW returned an error */
		struct ice_aqc_download_pkg_resp *resp;

		resp = (struct ice_aqc_download_pkg_resp *)pkg_buf;
		if (error_offset)
			*error_offset = LE32_TO_CPU(resp->error_offset);
		if (error_info)
			*error_info = LE32_TO_CPU(resp->error_info);
	}

	return status;
}

/**
 * ice_find_seg_in_pkg
 * @hw: pointer to the hardware structure
 * @seg_type: the segment type to search for (i.e., SEGMENT_TYPE_CPK)
 * @pkg_hdr: pointer to the package header to be searched
 *
 * This function searches a package file for a particular segment type. On
 * success it returns a pointer to the segment header, otherwise it will
 * return NULL.
 */
struct ice_generic_seg_hdr *
ice_find_seg_in_pkg(struct ice_hw *hw, u32 seg_type,
		    struct ice_pkg_hdr *pkg_hdr)
{
	u32 i;

	ice_debug(hw, ICE_DBG_TRACE, "%s\n", __func__);
	ice_debug(hw, ICE_DBG_PKG, "Package version: %d.%d.%d.%d\n",
		  pkg_hdr->format_ver.major, pkg_hdr->format_ver.minor,
		  pkg_hdr->format_ver.update, pkg_hdr->format_ver.draft);

	/* Search all package segments for the requested segment type */
	for (i = 0; i < LE32_TO_CPU(pkg_hdr->seg_count); i++) {
		struct ice_generic_seg_hdr *seg;

		seg = (struct ice_generic_seg_hdr *)
			((u8 *)pkg_hdr + LE32_TO_CPU(pkg_hdr->seg_offset[i]));

		if (LE32_TO_CPU(seg->seg_type) == seg_type)
			return seg;
	}

	return NULL;
}

/**
 * ice_update_pkg
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains change lock and updates package.
 */
enum ice_status
ice_update_pkg(struct ice_hw *hw, struct ice_buf *bufs, u32 count)
{
	enum ice_status status;
	u32 offset, info, i;

	status = ice_acquire_change_lock(hw, ICE_RES_WRITE);
	if (status)
		return status;

	for (i = 0; i < count; i++) {
		bool last = ((i + 1) == count);

		struct ice_buf_hdr *bh = (struct ice_buf_hdr *)(bufs + i);

		status = ice_aq_update_pkg(hw, bh, LE16_TO_CPU(bh->data_end),
					   last, &offset, &info, NULL);

		if (status) {
			ice_debug(hw, ICE_DBG_PKG,
				  "Update pkg failed: err %d off %d inf %d\n",
				  status, offset, info);
			break;
		}
	}

	ice_release_change_lock(hw);

	return status;
}

/**
 * ice_dwnld_cfg_bufs
 * @hw: pointer to the hardware structure
 * @bufs: pointer to an array of buffers
 * @count: the number of buffers in the array
 *
 * Obtains global config lock and downloads the package configuration buffers
 * to the firmware. Metadata buffers are skipped, and the first metadata buffer
 * found indicates that the rest of the buffers are all metadata buffers.
 */
static enum ice_status
ice_dwnld_cfg_bufs(struct ice_hw *hw, struct ice_buf *bufs, u32 count)
{
	enum ice_status status;
	struct ice_buf_hdr *bh;
	u32 offset, info, i;

	if (!bufs || !count)
		return ICE_ERR_PARAM;

	/* If the first buffer's first section has its metadata bit set
	 * then there are no buffers to be downloaded, and the operation is
	 * considered a success.
	 */
	bh = (struct ice_buf_hdr *)bufs;
	if (LE32_TO_CPU(bh->section_entry[0].type) & ICE_METADATA_BUF)
		return ICE_SUCCESS;

	status = ice_acquire_global_cfg_lock(hw, ICE_RES_WRITE);
	if (status)
		return status;

	for (i = 0; i < count; i++) {
		bool last = ((i + 1) == count);

		if (!last) {
			/* check next buffer for metadata flag */
			bh = (struct ice_buf_hdr *)(bufs + i + 1);

			/* A set metadata flag in the next buffer will signal
			 * that the current buffer will be the last buffer
			 * downloaded
			 */
			if (LE16_TO_CPU(bh->section_count))
				if (LE32_TO_CPU(bh->section_entry[0].type) &
				    ICE_METADATA_BUF)
					last = true;
		}

		bh = (struct ice_buf_hdr *)(bufs + i);

		status = ice_aq_download_pkg(hw, bh, LE16_TO_CPU(bh->data_end),
					     last, &offset, &info, NULL);

		if (status) {
			ice_debug(hw, ICE_DBG_PKG,
				  "Pkg download failed: err %d off %d inf %d\n",
				  status, offset, info);
			break;
		}

		if (last)
			break;
	}

	ice_release_global_cfg_lock(hw);

	return status;
}

/**
 * ice_aq_get_pkg_info_list
 * @hw: pointer to the hardware structure
 * @pkg_info: the buffer which will receive the information list
 * @buf_size: the size of the pkg_info information buffer
 * @cd: pointer to command details structure or NULL
 *
 * Get Package Info List (0x0C43)
 */
static enum ice_status
ice_aq_get_pkg_info_list(struct ice_hw *hw,
			 struct ice_aqc_get_pkg_info_resp *pkg_info,
			 u16 buf_size, struct ice_sq_cd *cd)
{
	struct ice_aq_desc desc;

	ice_debug(hw, ICE_DBG_TRACE, "ice_aq_get_pkg_info_list");
	ice_fill_dflt_direct_cmd_desc(&desc, ice_aqc_opc_get_pkg_info_list);

	return ice_aq_send_cmd(hw, &desc, pkg_info, buf_size, cd);
}

/**
 * ice_download_pkg
 * @hw: pointer to the hardware structure
 * @ice_seg: pointer to the segment of the package to be downloaded
 *
 * Handles the download of a complete package.
 */
enum ice_status ice_download_pkg(struct ice_hw *hw, struct ice_seg *ice_seg)
{
	struct ice_buf_table *ice_buf_tbl;

	ice_debug(hw, ICE_DBG_TRACE, "%s\n", __func__);
	ice_debug(hw, ICE_DBG_PKG, "Segment version: %d.%d.%d.%d\n",
		  ice_seg->hdr.seg_ver.major, ice_seg->hdr.seg_ver.minor,
		  ice_seg->hdr.seg_ver.update, ice_seg->hdr.seg_ver.draft);

	ice_debug(hw, ICE_DBG_PKG, "Seg: type 0x%X, size %d, name %s\n",
		  LE32_TO_CPU(ice_seg->hdr.seg_type),
		  LE32_TO_CPU(ice_seg->hdr.seg_size), ice_seg->hdr.seg_name);

	ice_buf_tbl = ice_find_buf_table(ice_seg);

	ice_debug(hw, ICE_DBG_PKG, "Seg buf count: %d\n",
		  LE32_TO_CPU(ice_buf_tbl->buf_count));

	return ice_dwnld_cfg_bufs(hw, ice_buf_tbl->buf_array,
				  LE32_TO_CPU(ice_buf_tbl->buf_count));
}

/**
 * ice_init_pkg_info
 * @hw: pointer to the hardware structure
 * @pkg_hdr: pointer to the driver's package hdr
 *
 * Saves off the package details into the hw structure.
 */
enum ice_status
ice_init_pkg_info(struct ice_hw *hw, struct ice_pkg_hdr *pkg_hdr)
{
	struct ice_aqc_get_pkg_info_resp *pkg_info;
	struct ice_global_metadata_seg *meta_seg;
	struct ice_generic_seg_hdr *seg_hdr;
	enum ice_status status;
	u16 size;
	u32 i;

	ice_debug(hw, ICE_DBG_TRACE, "%s\n", __func__);
	if (!pkg_hdr)
		return ICE_ERR_PARAM;

	meta_seg = (struct ice_global_metadata_seg *)
		   ice_find_seg_in_pkg(hw, SEGMENT_TYPE_METADATA, pkg_hdr);
	if (meta_seg) {
		hw->pkg_ver = meta_seg->pkg_ver;
		ice_memcpy(hw->pkg_name, meta_seg->pkg_name,
			   sizeof(hw->pkg_name), ICE_NONDMA_TO_NONDMA);

		ice_debug(hw, ICE_DBG_PKG, "Pkg: %d.%d.%d.%d, %s\n",
			  meta_seg->pkg_ver.major, meta_seg->pkg_ver.minor,
			  meta_seg->pkg_ver.update, meta_seg->pkg_ver.draft,
			  meta_seg->pkg_name);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find metadata segment in driver package\n");
		return ICE_ERR_CFG;
	}

	seg_hdr = ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE, pkg_hdr);
	if (seg_hdr) {
		hw->ice_pkg_ver = seg_hdr->seg_ver;
		ice_memcpy(hw->ice_pkg_name, seg_hdr->seg_name,
			   sizeof(hw->ice_pkg_name), ICE_NONDMA_TO_NONDMA);

		ice_debug(hw, ICE_DBG_PKG, "Ice Pkg: %d.%d.%d.%d, %s\n",
			  seg_hdr->seg_ver.major, seg_hdr->seg_ver.minor,
			  seg_hdr->seg_ver.update, seg_hdr->seg_ver.draft,
			  seg_hdr->seg_name);
	} else {
		ice_debug(hw, ICE_DBG_INIT,
			  "Did not find ice segment in driver package\n");
		return ICE_ERR_CFG;
	}

#define ICE_PKG_CNT	4
	size = sizeof(*pkg_info) + (sizeof(pkg_info->pkg_info[0]) *
				    (ICE_PKG_CNT - 1));
	pkg_info = (struct ice_aqc_get_pkg_info_resp *)ice_malloc(hw, size);
	if (!pkg_info)
		return ICE_ERR_NO_MEMORY;

	status = ice_aq_get_pkg_info_list(hw, pkg_info, size, NULL);
	if (status)
		goto init_pkg_free_alloc;

	for (i = 0; i < LE32_TO_CPU(pkg_info->count); i++) {
#define ICE_PKG_FLAG_COUNT	4
		char flags[ICE_PKG_FLAG_COUNT + 1] = { 0 };
		u8 place = 0;

		if (pkg_info->pkg_info[i].is_active) {
			flags[place++] = 'A';
			hw->active_pkg_ver = pkg_info->pkg_info[i].ver;
			ice_memcpy(hw->active_pkg_name,
				   pkg_info->pkg_info[i].name,
				   sizeof(hw->active_pkg_name),
				   ICE_NONDMA_TO_NONDMA);
		}
		if (pkg_info->pkg_info[i].is_active_at_boot)
			flags[place++] = 'B';
		if (pkg_info->pkg_info[i].is_modified)
			flags[place++] = 'M';
		if (pkg_info->pkg_info[i].is_in_nvm)
			flags[place++] = 'N';

		ice_debug(hw, ICE_DBG_PKG, "Pkg[%d]: %d.%d.%d.%d,%s,%s\n",
			  i, pkg_info->pkg_info[i].ver.major,
			  pkg_info->pkg_info[i].ver.minor,
			  pkg_info->pkg_info[i].ver.update,
			  pkg_info->pkg_info[i].ver.draft,
			  pkg_info->pkg_info[i].name, flags);
	}

init_pkg_free_alloc:
	ice_free(hw, pkg_info);

	return status;
}

/**
 * ice_find_label_value
 * @ice_seg: pointer to the ice segment (non-NULL)
 * @name: name of the label to search for
 * @type: the section type that will contain the label
 * @value: pointer to a value that will return the label's value if found
 *
 * Finds a label's value given the label name and the section type to search.
 * The ice_seg parameter must not be NULL since the first call to
 * ice_enum_labels requires a pointer to an actual ice_seg structure.
 */
enum ice_status
ice_find_label_value(struct ice_seg *ice_seg, char const *name, u32 type,
		     u16 *value)
{
	struct ice_pkg_enum state;
	char *label_name;
	u16 val;

	if (!ice_seg)
		return ICE_ERR_PARAM;

	do {
		label_name = ice_enum_labels(ice_seg, type, &state, &val);
		if (label_name && !strcmp(label_name, name)) {
			*value = val;
			return ICE_SUCCESS;
		}

		ice_seg = NULL;
	} while (label_name);

	return ICE_ERR_CFG;
}

/**
 * ice_verify_pkg - verify package
 * @pkg: pointer to the package buffer
 * @len: size of the package buffer
 *
 * Verifies various attributes of the package file, including length, format
 * version, and the requirement of at least one segment.
 */
static enum ice_status ice_verify_pkg(struct ice_pkg_hdr *pkg, u32 len)
{
	u32 seg_count;
	u32 i;

	if (len < sizeof(*pkg))
		return ICE_ERR_BUF_TOO_SHORT;

	if (pkg->format_ver.major != ICE_PKG_FMT_VER_MAJ ||
	    pkg->format_ver.minor != ICE_PKG_FMT_VER_MNR ||
	    pkg->format_ver.update != ICE_PKG_FMT_VER_UPD ||
	    pkg->format_ver.draft != ICE_PKG_FMT_VER_DFT)
		return ICE_ERR_CFG;

	/* pkg must have at least one segment */
	seg_count = LE32_TO_CPU(pkg->seg_count);
	if (seg_count < 1)
		return ICE_ERR_CFG;

	/* make sure segment array fits in package length */
	if (len < sizeof(*pkg) + ((seg_count - 1) * sizeof(pkg->seg_offset)))
		return ICE_ERR_BUF_TOO_SHORT;

	/* all segments must fit within length */
	for (i = 0; i < seg_count; i++) {
		u32 off = LE32_TO_CPU(pkg->seg_offset[i]);
		struct ice_generic_seg_hdr *seg;

		/* segment header must fit */
		if (len < off + sizeof(*seg))
			return ICE_ERR_BUF_TOO_SHORT;

		seg = (struct ice_generic_seg_hdr *)((u8 *)pkg + off);

		/* segment body must fit */
		if (len < off + LE32_TO_CPU(seg->seg_size))
			return ICE_ERR_BUF_TOO_SHORT;
	}

	return ICE_SUCCESS;
}

/**
 * ice_free_seg - free package segment pointer
 * @hw: pointer to the hardware structure
 *
 * Frees the package segment pointer in the proper manner, depending on if the
 * segment was allocated or just the passed in pointer was stored.
 */
void ice_free_seg(struct ice_hw *hw)
{
	if (hw->pkg_copy) {
		ice_free(hw, hw->pkg_copy);
		hw->pkg_copy = NULL;
	}
	hw->seg = NULL;
}

/**
 * ice_init_pkg_regs - initialize additional package registers
 * @hw: pointer to the hardware structure
 */
static void ice_init_pkg_regs(struct ice_hw *hw)
{
#define ICE_SW_BLK_INP_MASK_L 0xFFFFFFFF
#define ICE_SW_BLK_INP_MASK_H 0x0000FFFF
#define ICE_SW_BLK_IDX	0

	/* setup Switch block input mask, which is 48-bits in two parts */
	wr32(hw, GL_PREEXT_L2_PMASK0(ICE_SW_BLK_IDX), ICE_SW_BLK_INP_MASK_L);
	wr32(hw, GL_PREEXT_L2_PMASK1(ICE_SW_BLK_IDX), ICE_SW_BLK_INP_MASK_H);
}

/**
 * ice_init_pkg - initialize/download package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function initializes a package. The package contains HW tables
 * required to do packet processing. First, the function extracts package
 * information such as version. Then it finds the ice configuration segment
 * within the package; this function then saves a copy of the segment pointer
 * within the supplied package buffer. Next, the function will cache any hints
 * from the package, followed by downloading the package itself. Note, that if
 * a previous PF driver has already downloaded the package successfully, then
 * the current driver will not have to download the package again.
 *
 * The local package contents will be used to query default behavior and to
 * update specific sections of the HW's version of the package (e.g. to update
 * the parse graph to understand new protocols).
 *
 * This function stores a pointer to the package buffer memory, and it is
 * expected that the supplied buffer will not be freed immediately. If the
 * package buffer needs to be freed, such as when read from a file, use
 * ice_copy_and_init_pkg() instead of directly calling ice_init_pkg() in this
 * case.
 */
static enum ice_status ice_init_pkg(struct ice_hw *hw, u8 *buf, u32 len)
{
	struct ice_pkg_hdr *pkg;
	enum ice_status status;
	struct ice_seg *seg;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	pkg = (struct ice_pkg_hdr *)buf;
	status = ice_verify_pkg(pkg, len);
	if (status) {
		ice_debug(hw, ICE_DBG_INIT, "failed to verify pkg (err: %d)\n",
			  status);
		return status;
	}

	/* initialize package info */
	status = ice_init_pkg_info(hw, pkg);
	if (status)
		return status;

	/* find segment in given package */
	seg = (struct ice_seg *)ice_find_seg_in_pkg(hw, SEGMENT_TYPE_ICE, pkg);
	if (!seg) {
		ice_debug(hw, ICE_DBG_INIT, "no ice segment in package.\n");
		return ICE_ERR_CFG;
	}

	/* initialize package hints and then download package */
	ice_init_pkg_hints(hw, seg);
	status = ice_download_pkg(hw, seg);
	if (status == ICE_ERR_AQ_NO_WORK) {
		ice_debug(hw, ICE_DBG_INIT,
			  "package previously loaded - no work.\n");
		status = ICE_SUCCESS;
	}

	/* Free a previous segment, if necessary */
	ice_free_seg(hw);
	if (!status) {
		hw->seg = seg;
		/* on successful package download, update other required
		 * registers to support the package
		 */
		ice_init_pkg_regs(hw);
	} else {
		ice_debug(hw, ICE_DBG_INIT, "package load failed, %d\n",
			  status);
	}

	return status;
}

/**
 * ice_copy_and_init_pkg - initialize/download a copy of the package
 * @hw: pointer to the hardware structure
 * @buf: pointer to the package buffer
 * @len: size of the package buffer
 *
 * This function copies the package buffer, and then calls ice_init_pkg() to
 * initialize the copied package contents.
 *
 * The copying is necessary if the package buffer supplied is constant, or if
 * the memory may disappear shortly after calling this function.
 *
 * If the package buffer resides in the data segment and can be modified, the
 * caller is free to use ice_init_pkg() instead of ice_copy_and_init_pkg().
 *
 * However, if the package buffer needs to be copied first, such as when being
 * read from a file, the caller should use ice_copy_and_init_pkg().
 *
 * This function will first copy the package buffer, before calling
 * ice_init_pkg(). The caller is free to immediately destroy the original
 * package buffer, as the new copy will be managed by this function and
 * related routines.
 */
enum ice_status ice_copy_and_init_pkg(struct ice_hw *hw, const u8 *buf, u32 len)
{
	enum ice_status status;
	u8 *buf_copy;

	if (!buf || !len)
		return ICE_ERR_PARAM;

	buf_copy = (u8 *)ice_memdup(hw, buf, len, ICE_NONDMA_TO_NONDMA);

	status = ice_init_pkg(hw, buf_copy, len);
	if (status)
		/* Free the copy, since we failed to initialize the package */
		ice_free(hw, buf_copy);
	else
		/* Track the copied pkg so we can free it later */
		hw->pkg_copy = buf_copy;

	return status;
}

/**
 * ice_pkg_buf_alloc
 * @hw: pointer to the HW structure
 *
 * Allocates a package buffer and returns a pointer to the buffer header.
 * Note: all package contents must be in Little Endian form.
 */
struct ice_buf_build *ice_pkg_buf_alloc(struct ice_hw *hw)
{
	struct ice_buf_build *bld;
	struct ice_buf_hdr *buf;

	bld = (struct ice_buf_build *)ice_malloc(hw, sizeof(*bld));
	if (!bld)
		return NULL;

	buf = (struct ice_buf_hdr *)bld;
	buf->data_end = CPU_TO_LE16(sizeof(*buf) -
				    sizeof(buf->section_entry[0]));
	return bld;
}

/**
 * ice_sw_fv_handler
 * @sect_type: section type
 * @section: pointer to section
 * @index: index of the field vector entry to be returned
 * @offset: ptr to variable that receives the offset in the field vector table
 *
 * This is a callback function that can be passed to ice_pkg_enum_entry.
 * This function treats the given section as of type ice_sw_fv_section and
 * enumerates offset field. "offset" is an index into the field vector
 * vector table.
 */
static void *
ice_sw_fv_handler(u32 sect_type, void *section, u32 index, u32 *offset)
{
	struct ice_sw_fv_section *fv_section =
		(struct ice_sw_fv_section *)section;

	if (!section || sect_type != ICE_SID_FLD_VEC_SW)
		return NULL;
	if (index >= LE16_TO_CPU(fv_section->count))
		return NULL;
	if (offset)
		/* "index" passed in to this function is relative to a given
		 * 4k block. To get to the true index into the field vector
		 * table need to add the relative index to the base_offset
		 * field of this section
		 */
		*offset = LE16_TO_CPU(fv_section->base_offset) + index;
	return fv_section->fv + index;
}

/**
 * ice_get_sw_fv_list
 * @hw: pointer to the HW structure
 * @prot_ids: field vector to search for with a given protocol id
 * @ids_cnt: lookup/protocol count
 * @fv_list: Head of a list
 *
 * Finds all the field vector entries from switch block that contain
 * a given protocol id and returns a list of structures of type
 * "ice_sw_fv_list_entry". Every structure in the list has a field vector
 * definition and profile id information
 * NOTE: The caller of the function is responsible for freeing the memory
 * allocated for every list entry.
 */
enum ice_status
ice_get_sw_fv_list(struct ice_hw *hw, u16 *prot_ids, u8 ids_cnt,
		   struct LIST_HEAD_TYPE *fv_list)
{
	struct ice_sw_fv_list_entry *fvl;
	struct ice_sw_fv_list_entry *tmp;
	struct ice_pkg_enum state;
	struct ice_seg *ice_seg;
	struct ice_fv *fv;
	u32 offset;

	if (!ids_cnt || !hw->seg)
		return ICE_ERR_PARAM;

	ice_seg = hw->seg;
	do {
		u8 i;

		fv = (struct ice_fv *)
			ice_pkg_enum_entry(ice_seg, &state, ICE_SID_FLD_VEC_SW,
					   &offset, ice_sw_fv_handler);

		for (i = 0; i < ids_cnt && fv; i++) {
			int j;

			/* This code assumes that if a switch field vector line
			 * has a matching protocol, then this line will contain
			 * the entries necessary to represent every field in
			 * that protocol header.
			 */
			for (j = 0; j < hw->blk[ICE_BLK_SW].es.fvw; j++)
				if (fv->ew[j].prot_id == prot_ids[i])
					break;
			if (j >= hw->blk[ICE_BLK_SW].es.fvw)
				break;
			if (i + 1 == ids_cnt) {
				fvl = (struct ice_sw_fv_list_entry *)
					ice_malloc(hw, sizeof(*fvl));
				if (!fvl)
					goto err;
				fvl->fv_ptr = fv;
				fvl->profile_id = offset;
				LIST_ADD(&fvl->list_entry, fv_list);
				break;
			}
		}
		ice_seg = NULL;
	} while (fv);
	if (LIST_EMPTY(fv_list))
		return ICE_ERR_CFG;
	return ICE_SUCCESS;

err:
	LIST_FOR_EACH_ENTRY_SAFE(fvl, tmp, fv_list, ice_sw_fv_list_entry,
				 list_entry) {
		LIST_DEL(&fvl->list_entry);
		ice_free(hw, fvl);
	}

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_pkg_buf_alloc_single_section
 * @hw: pointer to the HW structure
 * @type: the section type value
 * @size: the size of the section to reserve (in bytes)
 * @section: returns pointer to the section
 *
 * Allocates a package buffer with a single section.
 * Note: all package contents must be in Little Endian form.
 */
static struct ice_buf_build *
ice_pkg_buf_alloc_single_section(struct ice_hw *hw, u32 type, u16 size,
				 void **section)
{
	struct ice_buf_build *buf;

	if (!section)
		return NULL;

	buf = ice_pkg_buf_alloc(hw);
	if (!buf)
		return NULL;

	if (ice_pkg_buf_reserve_section(buf, 1))
		goto ice_pkg_buf_alloc_single_section_err;

	*section = ice_pkg_buf_alloc_section(buf, type, size);
	if (!*section)
		goto ice_pkg_buf_alloc_single_section_err;

	return buf;

ice_pkg_buf_alloc_single_section_err:
	ice_pkg_buf_free(hw, buf);
	return NULL;
}

/**
 * ice_pkg_buf_reserve_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @count: the number of sections to reserve
 *
 * Reserves one or more section table entries in a package buffer. This routine
 * can be called multiple times as long as they are made before calling
 * ice_pkg_buf_alloc_section(). Once ice_pkg_buf_alloc_section()
 * is called once, the number of sections that can be allocated will not be able
 * to be increased; not using all reserved sections is fine, but this will
 * result in some wasted space in the buffer.
 * Note: all package contents must be in Little Endian form.
 */
enum ice_status
ice_pkg_buf_reserve_section(struct ice_buf_build *bld, u16 count)
{
	struct ice_buf_hdr *buf;
	u16 section_count;
	u16 data_end;

	if (!bld)
		return ICE_ERR_PARAM;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* already an active section, can't increase table size */
	section_count = LE16_TO_CPU(buf->section_count);
	if (section_count > 0)
		return ICE_ERR_CFG;

	if (bld->reserved_section_table_entries + count > ICE_MAX_S_COUNT)
		return ICE_ERR_CFG;
	bld->reserved_section_table_entries += count;

	data_end = LE16_TO_CPU(buf->data_end) +
		   (count * sizeof(buf->section_entry[0]));
	buf->data_end = CPU_TO_LE16(data_end);

	return ICE_SUCCESS;
}

/**
 * ice_pkg_buf_unreserve_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @count: the number of sections to unreserve
 *
 * Unreserves one or more section table entries in a package buffer, releasing
 * space that can be used for section data. This routine can be called
 * multiple times as long as they are made before calling
 * ice_pkg_buf_alloc_section(). Once ice_pkg_buf_alloc_section()
 * is called once, the number of sections that can be allocated will not be able
 * to be increased; not using all reserved sections is fine, but this will
 * result in some wasted space in the buffer.
 * Note: all package contents must be in Little Endian form.
 */
enum ice_status
ice_pkg_buf_unreserve_section(struct ice_buf_build *bld, u16 count)
{
	struct ice_buf_hdr *buf;
	u16 section_count;
	u16 data_end;

	if (!bld)
		return ICE_ERR_PARAM;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* already an active section, can't decrease table size */
	section_count = LE16_TO_CPU(buf->section_count);
	if (section_count > 0)
		return ICE_ERR_CFG;

	if (count > bld->reserved_section_table_entries)
		return ICE_ERR_CFG;
	bld->reserved_section_table_entries -= count;

	data_end = LE16_TO_CPU(buf->data_end) -
		   (count * sizeof(buf->section_entry[0]));
	buf->data_end = CPU_TO_LE16(data_end);

	return ICE_SUCCESS;
}

/**
 * ice_pkg_buf_alloc_section
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 * @type: the section type value
 * @size: the size of the section to reserve (in bytes)
 *
 * Reserves memory in the buffer for a section's content and updates the
 * buffers' status accordingly. This routine returns a pointer to the first
 * byte of the section start within the buffer, which is used to fill in the
 * section contents.
 * Note: all package contents must be in Little Endian form.
 */
void *
ice_pkg_buf_alloc_section(struct ice_buf_build *bld, u32 type, u16 size)
{
	struct ice_buf_hdr *buf;
	u16 sect_count;
	u16 data_end;

	if (!bld || !type || !size)
		return NULL;

	buf = (struct ice_buf_hdr *)&bld->buf;

	/* check for enough space left in buffer */
	data_end = LE16_TO_CPU(buf->data_end);

	/* section start must align on 4 byte boundary */
	data_end = ICE_ALIGN(data_end, 4);

	if ((data_end + size) > ICE_MAX_S_DATA_END)
		return NULL;

	/* check for more available section table entries */
	sect_count = LE16_TO_CPU(buf->section_count);
	if (sect_count < bld->reserved_section_table_entries) {
		void *section_ptr = ((u8 *)buf) + data_end;

		buf->section_entry[sect_count].offset = CPU_TO_LE16(data_end);
		buf->section_entry[sect_count].size = CPU_TO_LE16(size);
		buf->section_entry[sect_count].type = CPU_TO_LE32(type);

		data_end += size;
		buf->data_end = CPU_TO_LE16(data_end);

		buf->section_count = CPU_TO_LE16(sect_count + 1);
		return section_ptr;
	}

	/* no free section table entries */
	return NULL;
}

/**
 * ice_pkg_buf_get_free_space
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Returns the number of free bytes remaining in the buffer.
 * Note: all package contents must be in Little Endian form.
 */
u16 ice_pkg_buf_get_free_space(struct ice_buf_build *bld)
{
	struct ice_buf_hdr *buf;

	if (!bld)
		return 0;

	buf = (struct ice_buf_hdr *)&bld->buf;
	return ICE_MAX_S_DATA_END - LE16_TO_CPU(buf->data_end);
}

/**
 * ice_pkg_buf_get_active_sections
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Returns the number of active sections. Before using the package buffer
 * in an update package command, the caller should make sure that there is at
 * least one active section - otherwise, the buffer is not legal and should
 * not be used.
 * Note: all package contents must be in Little Endian form.
 */
u16 ice_pkg_buf_get_active_sections(struct ice_buf_build *bld)
{
	struct ice_buf_hdr *buf;

	if (!bld)
		return 0;

	buf = (struct ice_buf_hdr *)&bld->buf;
	return LE16_TO_CPU(buf->section_count);
}

/**
 * ice_pkg_buf_header
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Return a pointer to the buffer's header
 */
struct ice_buf *ice_pkg_buf(struct ice_buf_build *bld)
{
	if (!bld)
		return NULL;

	return &bld->buf;
}

/**
 * ice_pkg_buf_free
 * @hw: pointer to the HW structure
 * @bld: pointer to pkg build (allocated by ice_pkg_buf_alloc())
 *
 * Frees a package buffer
 */
void ice_pkg_buf_free(struct ice_hw *hw, struct ice_buf_build *bld)
{
	ice_free(hw, bld);
}

/* PTG Management */

/**
 * ice_ptg_update_xlt1 - Updates packet type groups in hw via xlt1 table
 * @hw: pointer to the hardware structure
 * @blk: hw block
 *
 * This function will update the xlt1 hardware table to reflect the new
 * packet type group configuration.
 */
enum ice_status ice_ptg_update_xlt1(struct ice_hw *hw, enum ice_block blk)
{
	struct ice_xlt1_section *sect;
	struct ice_buf_build *bld;
	enum ice_status status;
	u16 index;

	bld = ice_pkg_buf_alloc_single_section(hw, ice_sect_id(blk, ICE_XLT1),
					       ICE_XLT1_SIZE(ICE_XLT1_CNT),
					       (void **)&sect);
	if (!bld)
		return ICE_ERR_NO_MEMORY;

	sect->count = CPU_TO_LE16(ICE_XLT1_CNT);
	sect->offset = CPU_TO_LE16(0);
	for (index = 0; index < ICE_XLT1_CNT; index++)
		sect->value[index] = hw->blk[blk].xlt1.ptypes[index].ptg;

	status = ice_update_pkg(hw, ice_pkg_buf(bld), 1);

	ice_pkg_buf_free(hw, bld);

	return status;
}

/**
 * ice_ptg_find_ptype - Search for packet type group using packet type (ptype)
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptype: the ptype to search for
 * @ptg: pointer to variable that receives the PTG
 *
 * This function will search the PTGs for a particular ptype, returning the
 * PTG ID that contains it through the ptg parameter, with the value of
 * ICE_DEFAULT_PTG (0) meaning it is part the default PTG.
 */
enum ice_status
ice_ptg_find_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 *ptg)
{
	if (ptype >= ICE_XLT1_CNT || !ptg)
		return ICE_ERR_PARAM;

	*ptg = hw->blk[blk].xlt1.ptypes[ptype].ptg;
	return ICE_SUCCESS;
}

/**
 * ice_ptg_alloc_val - Allocates a new packet type group ID by value
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptg: the ptg to allocate
 *
 * This function allocates a given packet type group ID specified by the ptg
 * parameter.
 */
static
void ice_ptg_alloc_val(struct ice_hw *hw, enum ice_block blk, u8 ptg)
{
	hw->blk[blk].xlt1.ptg_tbl[ptg].in_use = true;
}

/**
 * ice_ptg_alloc - Find a free entry and allocates a new packet type group ID
 * @hw: pointer to the hardware structure
 * @blk: hw block
 *
 * This function allocates and returns a new packet type group ID. Note
 * that 0 is the default packet type group, so successfully created PTGs will
 * have a non-zero ID value; which means a 0 return value indicates an error.
 */
u8 ice_ptg_alloc(struct ice_hw *hw, enum ice_block blk)
{
	u16 i;

	/* Skip the default PTG of 0 */
	for (i = 1; i < ICE_MAX_PTGS; i++)
		if (!hw->blk[blk].xlt1.ptg_tbl[i].in_use) {
			/* found a free PTG ID */
			ice_ptg_alloc_val(hw, blk, i);
			return (u8)i;
		}

	return 0;
}

/**
 * ice_ptg_free - Frees a packet type group
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptg: the ptg ID to free
 *
 * This function frees a packet type group, and returns all the current ptypes
 * within it to the default PTG.
 */
void ice_ptg_free(struct ice_hw *hw, enum ice_block blk, u8 ptg)
{
	struct ice_ptg_ptype *p, *temp;

	hw->blk[blk].xlt1.ptg_tbl[ptg].in_use = false;
	p = hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	while (p) {
		p->ptg = ICE_DEFAULT_PTG;
		temp = p->next_ptype;
		p->next_ptype = NULL;
		p = temp;
	}

	hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype = NULL;
}

/**
 * ice_ptg_remove_ptype - Removes ptype from a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptype: the ptype to remove
 * @ptg: the ptg to remove the ptype from
 *
 * This function will remove the ptype from the specific ptg, and move it to
 * the default PTG (ICE_DEFAULT_PTG).
 */
static enum ice_status
ice_ptg_remove_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 ptg)
{
	struct ice_ptg_ptype **ch;
	struct ice_ptg_ptype *p;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* Should not happen if .in_use is set, bad config */
	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype)
		return ICE_ERR_CFG;

	/* find the ptype within this PTG, and bypass the link over it */
	p = hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	ch = &hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	while (p) {
		if (ptype == (p - hw->blk[blk].xlt1.ptypes)) {
			*ch = p->next_ptype;
			break;
		}

		ch = &p->next_ptype;
		p = p->next_ptype;
	}

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ICE_DEFAULT_PTG;
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype = NULL;

	return ICE_SUCCESS;
}

/**
 * ice_ptg_add_mv_ptype - Adds/moves ptype to a particular packet type group
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptype: the ptype to add or move
 * @ptg: the ptg to add or move the ptype to
 *
 * This function will either add or move a ptype to a particular PTG depending
 * on if the ptype is already part of another group. Note that using a
 * a destination PTG ID of ICE_DEFAULT_PTG (0) will move the ptype to the
 * default PTG.
 */
enum ice_status
ice_ptg_add_mv_ptype(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 ptg)
{
	enum ice_status status;
	u8 original_ptg;

	if (ptype > ICE_XLT1_CNT - 1)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt1.ptg_tbl[ptg].in_use && ptg != ICE_DEFAULT_PTG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_ptg_find_ptype(hw, blk, ptype, &original_ptg);
	if (status)
		return status;

	/* Is ptype already in the correct PTG? */
	if (original_ptg == ptg)
		return ICE_SUCCESS;

	/* Remove from original PTG and move back to the default PTG */
	if (original_ptg != ICE_DEFAULT_PTG)
		ice_ptg_remove_ptype(hw, blk, ptype, original_ptg);

	/* Moving to default PTG? Then we're done with this request */
	if (ptg == ICE_DEFAULT_PTG)
		return ICE_SUCCESS;

	/* Add ptype to PTG at beginning of list */
	hw->blk[blk].xlt1.ptypes[ptype].next_ptype =
		hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype;
	hw->blk[blk].xlt1.ptg_tbl[ptg].first_ptype =
		&hw->blk[blk].xlt1.ptypes[ptype];

	hw->blk[blk].xlt1.ptypes[ptype].ptg = ptg;
	hw->blk[blk].xlt1.t[ptype] = ptg;

	return ICE_SUCCESS;
}

/* Block / table size info */
struct ice_blk_size_details {
	u16 xlt1;			/* # xlt1 entries */
	u16 xlt2;			/* # xlt2 entries */
	u16 prof_tcam;			/* # profile id tcam entries */
	u16 prof_id;			/* # profile ids */
	u8 prof_cdid_bits;		/* # cdid one-hot bits used in key */
	u16 prof_redir;			/* # profile redirection entries */
	u16 es;				/* # extraction sequence entries */
	u16 fvw;			/* # field vector words */
	u8 overwrite;			/* overwrite existing entries allowed */
	u8 reverse;			/* reverse FV order */
};

static const struct ice_blk_size_details blk_sizes[ICE_BLK_COUNT] = {
	/**
	 * Table Definitions
	 * XLT1 - Number of entries in XLT1 table
	 * XLT2 - Number of entries in XLT2 table
	 * TCAM - Number of entries Profile ID TCAM table
	 * CDID - Control Domain ID of the hardware block
	 * PRED - Number of entries in the Profile Redirection Table
	 * FV   - Number of entries in the Field Vector
	 * FVW  - Width (in WORDs) of the Field Vector
	 * OVR  - Overwrite existing table entries
	 * REV  - Reverse FV
	 */
	/*          XLT1        , XLT2        ,TCAM, PID,CDID,PRED,   FV, FVW */
	/*          Overwrite   , Reverse FV */
	/* SW  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 256,   0,  256, 256,  48,
		    false, false },
	/* ACL */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  32,
		    false, false },
	/* FD  */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    false, true  },
	/* RSS */ { ICE_XLT1_CNT, ICE_XLT2_CNT, 512, 128,   0,  128, 128,  24,
		    true,  true  },
	/* PE  */ { ICE_XLT1_CNT, ICE_XLT2_CNT,  64,  32,   0,   32,  32,  24,
		    false, false },
};

enum ice_sid_all {
	ICE_SID_XLT1_OFF = 0,
	ICE_SID_XLT2_OFF,
	ICE_SID_PR_OFF,
	ICE_SID_PR_REDIR_OFF,
	ICE_SID_ES_OFF,
	ICE_SID_OFF_COUNT,
};

/* Characteristic handling */

/**
 * ice_match_prop_lst - determine if properties of two lists match
 * @list1: first properties list
 * @list2: second properties list
 *
 * Count, cookies and the order must match in order to be considered equivalent.
 */
static bool
ice_match_prop_lst(struct LIST_HEAD_TYPE *list1, struct LIST_HEAD_TYPE *list2)
{
	struct ice_vsig_prof *tmp1;
	struct ice_vsig_prof *tmp2;
	u16 chk_count = 0;
	u16 count = 0;

	/* compare counts */
	LIST_FOR_EACH_ENTRY(tmp1, list1, ice_vsig_prof, list) {
		count++;
	}
	LIST_FOR_EACH_ENTRY(tmp2, list2, ice_vsig_prof, list) {
		chk_count++;
	}
	if (!count || count != chk_count)
		return false;

	tmp1 = LIST_FIRST_ENTRY(list1, struct ice_vsig_prof, list);
	tmp2 = LIST_FIRST_ENTRY(list2, struct ice_vsig_prof, list);

	/* profile cookies must compare, and in the exact same order to take
	 * into account priority
	 */
	while (--count) {
		if (tmp2->profile_cookie != tmp1->profile_cookie)
			return false;

		tmp1 = LIST_NEXT_ENTRY(tmp1, struct ice_vsig_prof, list);
		tmp2 = LIST_NEXT_ENTRY(tmp2, struct ice_vsig_prof, list);
	}

	return true;
}

/* VSIG Management */

/**
 * ice_vsig_update_xlt2_sect - update one section of xlt2 table
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsi: hw vsi number to program
 * @vsig: vsig for the vsi
 *
 * This function will update the xlt2 hardware table with the input vsi
 * group configuration.
 */
static enum ice_status
ice_vsig_update_xlt2_sect(struct ice_hw *hw, enum ice_block blk, u16 vsi,
			  u16 vsig)
{
	struct ice_xlt2_section *sect;
	struct ice_buf_build *bld;
	enum ice_status status;

	bld = ice_pkg_buf_alloc_single_section(hw, ice_sect_id(blk, ICE_XLT2),
					       sizeof(struct ice_xlt2_section),
					       (void **)&sect);
	if (!bld)
		return ICE_ERR_NO_MEMORY;

	sect->count = CPU_TO_LE16(1);
	sect->offset = CPU_TO_LE16(vsi);
	sect->value[0] = CPU_TO_LE16(vsig);

	status = ice_update_pkg(hw, ice_pkg_buf(bld), 1);

	ice_pkg_buf_free(hw, bld);

	return status;
}

/**
 * ice_vsig_update_xlt2 - update xlt2 table with VSIG configuration
 * @hw: pointer to the hardware structure
 * @blk: hw block
 *
 * This function will update the xlt2 hardware table with the input vsi
 * group configuration of used vsis.
 */
enum ice_status ice_vsig_update_xlt2(struct ice_hw *hw, enum ice_block blk)
{
	u16 vsi;

	for (vsi = 0; vsi < ICE_MAX_VSI; vsi++) {
		/* update only vsis that have been changed */
		if (hw->blk[blk].xlt2.vsis[vsi].changed) {
			enum ice_status status;
			u16 vsig;

			vsig = hw->blk[blk].xlt2.vsis[vsi].vsig;
			status = ice_vsig_update_xlt2_sect(hw, blk, vsi, vsig);
			if (status)
				return status;

			hw->blk[blk].xlt2.vsis[vsi].changed = 0;
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_vsig_find_vsi - find a VSIG that contains a specified vsi
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsi: vsi of interest
 * @vsig: pointer to receive the vsi group
 *
 * This function will lookup the vsi entry in the XLT2 list and return
 * the vsi group its associated with.
 */
enum ice_status
ice_vsig_find_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 *vsig)
{
	if (!vsig || vsi >= ICE_MAX_VSI)
		return ICE_ERR_PARAM;

	/* As long as there's a default or valid VSIG associated with the input
	 * vsi, the functions returns a success. Any handling of VSIG will be
	 * done by the following add, update or remove functions.
	 */
	*vsig = hw->blk[blk].xlt2.vsis[vsi].vsig;

	return ICE_SUCCESS;
}

/**
 * ice_vsig_alloc_val - allocate a new VSIG by value
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsig: the vsig to allocate
 *
 * This function will allocate a given VSIG specified by the vsig parameter.
 */
static u16 ice_vsig_alloc_val(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use) {
		INIT_LIST_HEAD(&hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst);
		hw->blk[blk].xlt2.vsig_tbl[idx].in_use = true;
	}

	return ICE_VSIG_VALUE(idx, hw->pf_id);
}

/**
 * ice_vsig_alloc - Finds a free entry and allocates a new VSIG
 * @hw: pointer to the hardware structure
 * @blk: hw block
 *
 * This function will iterate through the VSIG list and mark the first
 * unused entry for the new VSIG entry as used and return that value.
 */
static u16 ice_vsig_alloc(struct ice_hw *hw, enum ice_block blk)
{
	u16 i;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (!hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			return ice_vsig_alloc_val(hw, blk, i);

	return ICE_DEFAULT_VSIG;
}

/**
 * ice_find_dup_props_vsig - find vsi group with a specified set of properties
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @chs: characteristic list
 * @vsig: returns the VSIG with the matching profiles, if found
 *
 * Each VSIG is associated with a characteristic set; i.e. all vsis under
 * a group have the same characteristic set. To check if there exists a VSIG
 * which has the same characteristics as the input characteristics; this
 * function will iterate through the xlt2 list and return the VSIG that has a
 * matching configuration. In order to make sure that priorities are accounted
 * for, the list must match exactly, including the order in which the
 * characteristics are listed.
 */
enum ice_status
ice_find_dup_props_vsig(struct ice_hw *hw, enum ice_block blk,
			struct LIST_HEAD_TYPE *chs, u16 *vsig)
{
	struct ice_xlt2 *xlt2 = &hw->blk[blk].xlt2;
	u16 i;

	for (i = 0; i < xlt2->count; i++) {
		if (xlt2->vsig_tbl[i].in_use &&
		    ice_match_prop_lst(chs, &xlt2->vsig_tbl[i].prop_lst)) {
			*vsig = (i | ((hw->pf_id << ICE_PF_NUM_S) &
				      ICE_PF_NUM_M));
			*vsig = ICE_VSIG_VALUE(i, hw->pf_id);
			return ICE_SUCCESS;
		}
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_vsig_free - free vsi group
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsig: VSIG to remove
 *
 * The function will remove all vsis associated with the input VSIG and move
 * them to the DEFAULT_VSIG and mark the VSIG available.
 */
enum ice_status
ice_vsig_free(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	struct ice_vsig_prof *dtmp, *del;
	struct ice_vsig_vsi *vsi_cur;
	u16 idx;

	idx = vsig & ICE_VSIG_IDX_M;
	if (idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	hw->blk[blk].xlt2.vsig_tbl[idx].in_use = false;

	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	if (!vsi_cur)
		return ICE_ERR_CFG;

	/* remove all vsis associated with this VSIG XLT2 entry */
	do {
		struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;

		vsi_cur->vsig = ICE_DEFAULT_VSIG;
		vsi_cur->changed = 1;
		vsi_cur->next_vsi = NULL;
		vsi_cur = tmp;
	} while (vsi_cur);

	/* NULL terminate head of vsi list */
	hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi = NULL;

	/* free characteristic list */
	LIST_FOR_EACH_ENTRY_SAFE(del, dtmp,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 ice_vsig_prof, list) {
		LIST_DEL(&del->list);
		ice_free(hw, del);
	}

	return ICE_SUCCESS;
}

/**
 * ice_vsig_add_mv_vsi - add or move a vsi to a vsi group
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsi: vsi to move
 * @vsig: destination vsi group
 *
 * This function will move or add the input vsi to the target VSIG.
 * The function will find the original VSIG the vsi belongs to and
 * move the entry to the DEFAULT_VSIG, update the original VSIG and
 * then move entry to the new VSIG.
 */
enum ice_status
ice_vsig_add_mv_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig)
{
	struct ice_vsig_vsi *tmp;
	enum ice_status status;
	u16 orig_vsig, idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	/* if VSIG not in use and VSIG is not default type this VSIG
	 * doesn't exist.
	 */
	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use &&
	    vsig != ICE_DEFAULT_VSIG)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (status)
		return status;

	/* no update required if vsigs match */
	if (orig_vsig == vsig)
		return ICE_SUCCESS;

	if (orig_vsig != ICE_DEFAULT_VSIG) {
		/* remove entry from orig_vsig and add to default VSIG */
		status = ice_vsig_remove_vsi(hw, blk, vsi, orig_vsig);
		if (status)
			return status;
	}

	if (idx == ICE_DEFAULT_VSIG)
		return ICE_SUCCESS;

	/* Create vsi entry and add VSIG and prop_mask values */
	hw->blk[blk].xlt2.vsis[vsi].vsig = vsig;
	hw->blk[blk].xlt2.vsis[vsi].changed = 1;

	/* Add new entry to the head of the VSIG list */
	tmp = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi =
		&hw->blk[blk].xlt2.vsis[vsi];
	hw->blk[blk].xlt2.vsis[vsi].next_vsi = tmp;
	hw->blk[blk].xlt2.t[vsi] = vsig;

	return ICE_SUCCESS;
}

/**
 * ice_vsig_remove_vsi - remove vsi from VSIG
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsi: vsi to remove
 * @vsig: vsi group to remove from
 *
 * The function will remove the input vsi from its vsi group and move it
 * to the DEFAULT_VSIG.
 */
enum ice_status
ice_vsig_remove_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig)
{
	struct ice_vsig_vsi **vsi_head, *vsi_cur, *vsi_tgt;
	u16 idx;

	idx = vsig & ICE_VSIG_IDX_M;

	if (vsi >= ICE_MAX_VSI || idx >= ICE_MAX_VSIGS)
		return ICE_ERR_PARAM;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	/* entry already in default VSIG, dont have to remove */
	if (idx == ICE_DEFAULT_VSIG)
		return ICE_SUCCESS;

	vsi_head = &hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	if (!(*vsi_head))
		return ICE_ERR_CFG;

	vsi_tgt = &hw->blk[blk].xlt2.vsis[vsi];
	vsi_cur = (*vsi_head);

	/* iterate the vsi list, skip over the entry to be removed */
	while (vsi_cur) {
		if (vsi_tgt == vsi_cur) {
			(*vsi_head) = vsi_cur->next_vsi;
			break;
		}
		vsi_head = &vsi_cur->next_vsi;
		vsi_cur = vsi_cur->next_vsi;
	}

	/* verify if vsi was removed from group list */
	if (!vsi_cur)
		return ICE_ERR_DOES_NOT_EXIST;

	vsi_cur->vsig = ICE_DEFAULT_VSIG;
	vsi_cur->changed = 1;
	vsi_cur->next_vsi = NULL;

	return ICE_SUCCESS;
}

/**
 * ice_find_prof_id - find profile id for a given field vector
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @fv: field vector to search for
 * @prof_id: receives the profile id
 */
static enum ice_status
ice_find_prof_id(struct ice_hw *hw, enum ice_block blk,
		 struct ice_fv_word *fv, u8 *prof_id)
{
	struct ice_es *es = &hw->blk[blk].es;
	u16 off, i;

	for (i = 0; i < es->count; i++) {
		off = i * es->fvw;

		if (memcmp(&es->t[off], fv, es->fvw * 2))
			continue;

		*prof_id = i;
		return ICE_SUCCESS;
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_prof_id_rsrc_type - get profile id resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
static bool ice_prof_id_rsrc_type(enum ice_block blk, u16 *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_SW:
		*rsrc_type = ICE_AQC_RES_TYPE_SWITCH_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_ACL:
		*rsrc_type = ICE_AQC_RES_TYPE_ACL_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_FD:
		*rsrc_type = ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID;
		break;
	case ICE_BLK_PE:
		*rsrc_type = ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_PROFID;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_tcam_ent_rsrc_type - get tcam entry resource type for a block type
 * @blk: the block type
 * @rsrc_type: pointer to variable to receive the resource type
 */
static bool ice_tcam_ent_rsrc_type(enum ice_block blk, u16 *rsrc_type)
{
	switch (blk) {
	case ICE_BLK_SW:
		*rsrc_type = ICE_AQC_RES_TYPE_SWITCH_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_ACL:
		*rsrc_type = ICE_AQC_RES_TYPE_ACL_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_FD:
		*rsrc_type = ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_RSS:
		*rsrc_type = ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM;
		break;
	case ICE_BLK_PE:
		*rsrc_type = ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_TCAM;
		break;
	default:
		return false;
	}
	return true;
}

/**
 * ice_workaround_get_res_blk - determine the block from a resource type
 * @type: type of resource
 * @blk: pointer to a enum that will receive the block type
 * @tcam: pointer to variable that will be set to true for a TCAM resource type
 */
static enum
ice_status ice_workaround_get_res_blk(u16 type, enum ice_block *blk, bool *tcam)
{
	/* just need to support TCAM entries and Profile IDs for now */
	*tcam = false;

	switch (type) {
	case ICE_AQC_RES_TYPE_SWITCH_PROF_BLDR_TCAM:
		*blk = ICE_BLK_SW;
		*tcam = true;
		break;
	case ICE_AQC_RES_TYPE_ACL_PROF_BLDR_TCAM:
		*blk = ICE_BLK_ACL;
		*tcam = true;
		break;
	case ICE_AQC_RES_TYPE_FD_PROF_BLDR_TCAM:
		*blk = ICE_BLK_FD;
		*tcam = true;
		break;
	case ICE_AQC_RES_TYPE_HASH_PROF_BLDR_TCAM:
		*blk = ICE_BLK_RSS;
		*tcam = true;
		break;
	case ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_TCAM:
		*blk = ICE_BLK_PE;
		*tcam = true;
		break;
	case ICE_AQC_RES_TYPE_SWITCH_PROF_BLDR_PROFID:
		*blk = ICE_BLK_SW;
		break;
	case ICE_AQC_RES_TYPE_ACL_PROF_BLDR_PROFID:
		*blk = ICE_BLK_ACL;
		break;
	case ICE_AQC_RES_TYPE_FD_PROF_BLDR_PROFID:
		*blk = ICE_BLK_FD;
		break;
	case ICE_AQC_RES_TYPE_HASH_PROF_BLDR_PROFID:
		*blk = ICE_BLK_RSS;
		break;
	case ICE_AQC_RES_TYPE_QHASH_PROF_BLDR_PROFID:
		*blk = ICE_BLK_PE;
		break;
	default:
		return ICE_ERR_PARAM;
	}

	return ICE_SUCCESS;
}

/**
 * ice_alloc_res_workaround
 * @hw: pointer to the hw struct
 * @type: type of resource
 * @num: number of resources to allocate
 * @res: pointer to array that will receive the resources
 */
static enum ice_status
ice_alloc_res_workaround(struct ice_hw *hw, u16 type, u16 num, u16 *res)
{
	enum ice_block blk;
	u16 count = 0;
	bool tcam;
	u16 first;
	u16 last;
	u16 max;
	u16 i;

/* Number of PFs we support with this workaround */
#define ICE_WA_PF_COUNT	4
#define ICE_WA_1ST_TCAM	4
#define ICE_WA_1ST_FV	4

	/* Only allow our supported PFs */
	if (hw->pf_id >= ICE_WA_PF_COUNT)
		return ICE_ERR_AQ_ERROR;

	if (ice_workaround_get_res_blk(type, &blk, &tcam))
		return ICE_ERR_AQ_ERROR;

	if (tcam) {
		/* range of entries based on PF */
		max = hw->blk[blk].prof.count / ICE_WA_PF_COUNT;
		first = max * hw->pf_id;
		last = first + max;

		/* Profile IDs - start at non-zero index for PROF ID TCAM table
		 * The first few entries are for bypass, default and errors
		 * (only relevant for PF 0)
		 */
		first += hw->pf_id ? 0 : ICE_WA_1ST_TCAM;

		for (i = first; i < last && count < num; i++) {
			if (!hw->blk[blk].prof.resource_used_hack[i]) {
				res[count++] = i;
				hw->blk[blk].prof.resource_used_hack[i] = true;
			}
		}

		/* handle failure case */
		if (count < num) {
			for (i = 0; i < count; i++) {
				hw->blk[blk].prof.resource_used_hack[res[i]] =
					false;
				res[i] = 0;
			}

			return ICE_ERR_AQ_ERROR;
		}
	} else {
		/* range of entries based on PF */
		max = hw->blk[blk].es.count / ICE_WA_PF_COUNT;
		first = max * hw->pf_id;
		last = first + max;

		/* FV index - start at non-zero index for Field vector table
		 * The first few entries are for bypass, default and errors
		 * (only relevant for PF 0)
		 */
		first += hw->pf_id ? 0 : ICE_WA_1ST_FV;

		for (i = first; i < last && count < num; i++) {
			if (!hw->blk[blk].es.resource_used_hack[i]) {
				res[count++] = i;
				hw->blk[blk].es.resource_used_hack[i] = true;
			}
		}

		/* handle failure case */
		if (count < num) {
			for (i = 0; i < count; i++) {
				hw->blk[blk].es.resource_used_hack[res[i]] =
					false;
				res[i] = 0;
			}

			return ICE_ERR_AQ_ERROR;
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_free_res_workaround
 * @hw: pointer to the hw struct
 * @type: type of resource to free
 * @num: number of resources
 * @res: array of resource ids to free
 */
static enum ice_status
ice_free_res_workaround(struct ice_hw *hw, u16 type, u16 num, u16 *res)
{
	enum ice_block blk;
	bool tcam = false;
	u16 i;

	if (ice_workaround_get_res_blk(type, &blk, &tcam))
		return ICE_ERR_AQ_ERROR;

	if (tcam) {
		/* TCAM entries */
		for (i = 0; i < num; i++) {
			if (res[i] < hw->blk[blk].prof.count) {
				u16 idx = res[i];

				ice_free_hw_res(hw, type, 1, &idx);
				hw->blk[blk].prof.resource_used_hack[res[i]] =
					false;
			}
		}

	} else {
		/* Profile IDs */
		for (i = 0; i < num; i++) {
			if (res[i] < hw->blk[blk].es.count) {
				u16 idx = res[i];

				ice_free_hw_res(hw, type, 1, &idx);
				hw->blk[blk].es.resource_used_hack[res[i]] =
					false;
			}
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_alloc_tcam_ent - allocate hardware tcam entry
 * @hw: pointer to the hw struct
 * @blk: the block to allocate the tcam for
 * @tcam_idx: pointer to variable to receive the tcam entry
 *
 * This function allocates a new entry in a Profile ID TCAM for a specific
 * block.
 */
static enum ice_status
ice_alloc_tcam_ent(struct ice_hw *hw, enum ice_block blk, u16 *tcam_idx)
{
	u16 res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_alloc_res_workaround(hw, res_type, 1, tcam_idx);
}

/**
 * ice_free_tcam_ent - free hardware tcam entry
 * @hw: pointer to the hw struct
 * @blk: the block from which to free the tcam entry
 * @tcam_idx: the tcam entry to free
 *
 * This function frees an entry in a Profile ID TCAM for a specific block.
 */
static enum ice_status
ice_free_tcam_ent(struct ice_hw *hw, enum ice_block blk, u16 tcam_idx)
{
	u16 res_type;

	if (!ice_tcam_ent_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_res_workaround(hw, res_type, 1, &tcam_idx);
}

/**
 * ice_alloc_prof_id - allocate profile id
 * @hw: pointer to the hw struct
 * @blk: the block to allocate the profile id for
 * @prof_id: pointer to variable to receive the profile id
 *
 * This function allocates a new profile id, which also corresponds to a Field
 * Vector (Extraction Sequence) entry.
 */
static enum ice_status
ice_alloc_prof_id(struct ice_hw *hw, enum ice_block blk, u8 *prof_id)
{
	enum ice_status status;
	u16 res_type;
	u16 get_prof;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	status = ice_alloc_res_workaround(hw, res_type, 1, &get_prof);
	if (!status)
		*prof_id = (u8)get_prof;

	return status;
}

/**
 * ice_free_prof_id - free profile id
 * @hw: pointer to the hw struct
 * @blk: the block from which to free the profile id
 * @prof_id: the profile id to free
 *
 * This function frees a profile id, which also corresponds to a Field Vector.
 */
static enum ice_status
ice_free_prof_id(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	u16 tmp_prof_id = (u16)prof_id;
	u16 res_type;

	if (!ice_prof_id_rsrc_type(blk, &res_type))
		return ICE_ERR_PARAM;

	return ice_free_res_workaround(hw, res_type, 1, &tmp_prof_id);
	/* The following code is a WORKAROUND until DCR 076 is available.
	 * DCR 076 - Update to Profile ID TCAM Resource Allocation
	 *
	 * Once the DCR 076 changes are available in FW, this code can be
	 * restored. Original code:
	 *
	 * return ice_free_res(hw, res_type, 1, &tmp_prof_id);
	 */
}

/**
 * ice_prof_inc_ref - increment reference count for profile
 * @hw: pointer to the hw struct
 * @blk: the block from which to free the profile id
 * @prof_id: the profile id for which to increment the reference count
 */
static enum ice_status
ice_prof_inc_ref(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	hw->blk[blk].es.ref_count[prof_id]++;

	return ICE_SUCCESS;
}

/**
 * ice_prof_dec_ref - decrement reference count for profile
 * @hw: pointer to the hw struct
 * @blk: the block from which to free the profile id
 * @prof_id: the profile id for which to decrement the reference count
 */
static enum ice_status
ice_prof_dec_ref(struct ice_hw *hw, enum ice_block blk, u8 prof_id)
{
	if (prof_id > hw->blk[blk].es.count)
		return ICE_ERR_PARAM;

	if (hw->blk[blk].es.ref_count[prof_id] > 0) {
		if (!--hw->blk[blk].es.ref_count[prof_id])
			return ice_free_prof_id(hw, blk, prof_id);
	}

	return ICE_SUCCESS;
}

/**
 * ice_write_es - write an extraction sequence to hardware
 * @hw: pointer to the hw struct
 * @blk: the block in which to write the extraction sequence
 * @prof_id: the profile id to write
 * @fv: pointer to the extraction sequence to write
 */
static void
ice_write_es(struct ice_hw *hw, enum ice_block blk, u8 prof_id,
	     struct ice_fv_word *fv)
{
	u16 off;

	off = prof_id * hw->blk[blk].es.fvw;
	ice_memcpy(&hw->blk[blk].es.t[off], fv, hw->blk[blk].es.fvw * 2,
		   ICE_NONDMA_TO_NONDMA);
}

/* Block / table section IDs */
static const u32 ice_blk_sids[ICE_BLK_COUNT][ICE_SID_OFF_COUNT] = {
	/* SWITCH */
	{	ICE_SID_XLT1_SW,
		ICE_SID_XLT2_SW,
		ICE_SID_PROFID_TCAM_SW,
		ICE_SID_PROFID_REDIR_SW,
		ICE_SID_FLD_VEC_SW
	},

	/* ACL */
	{	ICE_SID_XLT1_ACL,
		ICE_SID_XLT2_ACL,
		ICE_SID_PROFID_TCAM_ACL,
		ICE_SID_PROFID_REDIR_ACL,
		ICE_SID_FLD_VEC_ACL
	},

	/* FD */
	{	ICE_SID_XLT1_FD,
		ICE_SID_XLT2_FD,
		ICE_SID_PROFID_TCAM_FD,
		ICE_SID_PROFID_REDIR_FD,
		ICE_SID_FLD_VEC_FD
	},

	/* RSS */
	{	ICE_SID_XLT1_RSS,
		ICE_SID_XLT2_RSS,
		ICE_SID_PROFID_TCAM_RSS,
		ICE_SID_PROFID_REDIR_RSS,
		ICE_SID_FLD_VEC_RSS
	},

	/* PE */
	{	ICE_SID_XLT1_PE,
		ICE_SID_XLT2_PE,
		ICE_SID_PROFID_TCAM_PE,
		ICE_SID_PROFID_REDIR_PE,
		ICE_SID_FLD_VEC_PE
	}
};

/**
 * ice_fill_tbl - Reads content of a single table type into database
 * @hw: pointer to the hardware structure
 * @block_id: Block ID of the table to copy
 * @sid: Section ID of the table to copy
 *
 * Will attempt to read the entire content of a given table of a single block
 * into the driver database. We assume that the buffer will always
 * be as large or larger than the data contained in the package. If
 * this condition is not met, there is most likely an error in the package
 * contents.
 */
static void ice_fill_tbl(struct ice_hw *hw, enum ice_block block_id, u32 sid)
{
	u32 dst_len, sect_len, offset = 0;
	struct ice_prof_redir_section *pr;
	struct ice_prof_id_section *pid;
	struct ice_xlt1_section *xlt1;
	struct ice_xlt2_section *xlt2;
	struct ice_sw_fv_section *es;
	struct ice_pkg_enum state;
	u8 *src, *dst;
	void *sect;

	/* if the hw segment pointer is null then the first iteration of
	 * ice_pkg_enum_section() will fail. In this case the Hw tables will
	 * not be filled and return success.
	 */
	if (!hw->seg) {
		ice_debug(hw, ICE_DBG_PKG, "hw->seg is NULL, tables are not filled\n");
		return;
	}

	ice_memset(&state, 0, sizeof(state), ICE_NONDMA_MEM);

	sect = ice_pkg_enum_section(hw->seg, &state, sid);

	while (sect) {
		switch (sid) {
		case ICE_SID_XLT1_FD:
		case ICE_SID_XLT1_RSS:
		case ICE_SID_XLT1_ACL:
			xlt1 = (struct ice_xlt1_section *)sect;
			src = xlt1->value;
			sect_len = LE16_TO_CPU(xlt1->count) *
				sizeof(*hw->blk[block_id].xlt1.t);
			dst = hw->blk[block_id].xlt1.t;
			dst_len = hw->blk[block_id].xlt1.count *
				sizeof(*hw->blk[block_id].xlt1.t);
			break;
		case ICE_SID_XLT2_FD:
		case ICE_SID_XLT2_RSS:
		case ICE_SID_XLT2_ACL:
			xlt2 = (struct ice_xlt2_section *)sect;
			src = (u8 *)xlt2->value;
			sect_len = LE16_TO_CPU(xlt2->count) *
				sizeof(*hw->blk[block_id].xlt2.t);
			dst = (u8 *)hw->blk[block_id].xlt2.t;
			dst_len = hw->blk[block_id].xlt2.count *
				sizeof(*hw->blk[block_id].xlt2.t);
			break;
		case ICE_SID_PROFID_TCAM_FD:
		case ICE_SID_PROFID_TCAM_RSS:
		case ICE_SID_PROFID_TCAM_ACL:
			pid = (struct ice_prof_id_section *)sect;
			src = (u8 *)pid->entry;
			sect_len = LE16_TO_CPU(pid->count) *
				sizeof(*hw->blk[block_id].prof.t);
			dst = (u8 *)hw->blk[block_id].prof.t;
			dst_len = hw->blk[block_id].prof.count *
				sizeof(*hw->blk[block_id].prof.t);
			break;
		case ICE_SID_PROFID_REDIR_FD:
		case ICE_SID_PROFID_REDIR_RSS:
		case ICE_SID_PROFID_REDIR_ACL:
			pr = (struct ice_prof_redir_section *)sect;
			src = pr->redir_value;
			sect_len = LE16_TO_CPU(pr->count) *
				sizeof(*hw->blk[block_id].prof_redir.t);
			dst = hw->blk[block_id].prof_redir.t;
			dst_len = hw->blk[block_id].prof_redir.count *
				sizeof(*hw->blk[block_id].prof_redir.t);
			break;
		case ICE_SID_FLD_VEC_FD:
		case ICE_SID_FLD_VEC_RSS:
		case ICE_SID_FLD_VEC_ACL:
			es = (struct ice_sw_fv_section *)sect;
			src = (u8 *)es->fv;
			sect_len = LE16_TO_CPU(es->count) *
				sizeof(*hw->blk[block_id].prof_redir.t);
			dst = (u8 *)hw->blk[block_id].es.t;
			dst_len = hw->blk[block_id].es.count *
				sizeof(*hw->blk[block_id].es.t);
			break;
		default:
			return;
		}

		/* if the section offset exceeds destination length, terminate
		 * table fill.
		 */
		if (offset > dst_len)
			return;

		/* if the sum of section size and offset exceed destination size
		 * then we are out of bounds of the Hw table size for that PF.
		 * Changing section length to fill the remaining table space
		 * of that PF.
		 */
		if ((offset + sect_len) > dst_len)
			sect_len = dst_len - offset;

		ice_memcpy(dst + offset, src, sect_len, ICE_NONDMA_TO_NONDMA);
		offset += sect_len;
		sect = ice_pkg_enum_section(NULL, &state, sid);
	}
}

/**
 * ice_fill_blk_tbls - Read package content for tables of a block
 * @hw: pointer to the hardware structure
 * @block_id: The block ID which contains the tables to be copied
 *
 * Reads the current package contents and populates the driver
 * database with the data it contains to allow for advanced driver
 * features.
 */
static void ice_fill_blk_tbls(struct ice_hw *hw, enum ice_block block_id)
{
	ice_fill_tbl(hw, block_id, hw->blk[block_id].xlt1.sid);
	ice_fill_tbl(hw, block_id, hw->blk[block_id].xlt2.sid);
	ice_fill_tbl(hw, block_id, hw->blk[block_id].prof.sid);
	ice_fill_tbl(hw, block_id, hw->blk[block_id].prof_redir.sid);
	ice_fill_tbl(hw, block_id, hw->blk[block_id].es.sid);
}

/**
 * ice_free_prof_map - frees the profile map
 * @hw: pointer to the hardware structure
 * @blk: the hw block which contains the profile map to be freed
 */
static void ice_free_prof_map(struct ice_hw *hw, enum ice_block blk)
{
	struct ice_prof_map *del, *tmp;

	if (LIST_EMPTY(&hw->blk[blk].es.prof_map))
		return;

	LIST_FOR_EACH_ENTRY_SAFE(del, tmp, &hw->blk[blk].es.prof_map,
				 ice_prof_map, list) {
		LIST_DEL(&del->list);
		ice_free(hw, del);
	}
}

/**
 * ice_free_vsig_tbl - free complete VSIG table entries
 * @hw: pointer to the hardware structure
 * @blk: the hw block on which to free the VSIG table entries
 */
static void ice_free_vsig_tbl(struct ice_hw *hw, enum ice_block blk)
{
	u16 i;

	if (!hw->blk[blk].xlt2.vsig_tbl)
		return;

	for (i = 1; i < ICE_MAX_VSIGS; i++)
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use)
			ice_vsig_free(hw, blk, i);
}

/**
 * ice_free_hw_tbls - free hardware table memory
 * @hw: pointer to the hardware structure
 */
void ice_free_hw_tbls(struct ice_hw *hw)
{
	u8 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		ice_free_prof_map(hw, (enum ice_block)i);
		ice_free_vsig_tbl(hw, (enum ice_block)i);
		ice_free(hw, hw->blk[i].xlt1.ptypes);
		ice_free(hw, hw->blk[i].xlt1.ptg_tbl);
		ice_free(hw, hw->blk[i].xlt1.t);
		ice_free(hw, hw->blk[i].xlt2.t);
		ice_free(hw, hw->blk[i].xlt2.vsig_tbl);
		ice_free(hw, hw->blk[i].xlt2.vsis);
		ice_free(hw, hw->blk[i].prof.t);
		ice_free(hw, hw->blk[i].prof_redir.t);
		ice_free(hw, hw->blk[i].es.t);
		ice_free(hw, hw->blk[i].es.ref_count);

		ice_free(hw, hw->blk[i].es.resource_used_hack);
		ice_free(hw, hw->blk[i].prof.resource_used_hack);
	}

	ice_memset(hw->blk, 0, sizeof(hw->blk), ICE_NONDMA_MEM);
}

/**
 * ice_init_flow_profs - init flow profile locks and list heads
 * @hw: pointer to the hardware structure
 */
static void ice_init_flow_profs(struct ice_hw *hw)
{
	u8 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		ice_init_lock(&hw->fl_profs_locks[i]);
		INIT_LIST_HEAD(&hw->fl_profs[i]);
	}
}

/**
 * ice_init_sw_xlt1_db - init software xlt1 database from hw tables
 * @hw: pointer to the hardware structure
 * @blk: the hw block to initialize
 */
static
void ice_init_sw_xlt1_db(struct ice_hw *hw, enum ice_block blk)
{
	u16 pt;

	for (pt = 0; pt < hw->blk[blk].xlt1.count; pt++) {
		u8 ptg;

		ptg = hw->blk[blk].xlt1.t[pt];
		if (ptg != ICE_DEFAULT_PTG) {
			ice_ptg_alloc_val(hw, blk, ptg);
			ice_ptg_add_mv_ptype(hw, blk, pt, ptg);
		}
	}
}

/**
 * ice_init_sw_xlt2_db - init software xlt2 database from hw tables
 * @hw: pointer to the hardware structure
 * @blk: the hw block to initialize
 */
static
void ice_init_sw_xlt2_db(struct ice_hw *hw, enum ice_block blk)
{
	u16 vsi;

	for (vsi = 0; vsi < hw->blk[blk].xlt2.count; vsi++) {
		u16 vsig;

		vsig = hw->blk[blk].xlt2.t[vsi];
		if (vsig) {
			ice_vsig_alloc_val(hw, blk, vsig);
			ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);
			/* no changes at this time, since this has been
			 * initialized from the original package
			 */
			hw->blk[blk].xlt2.vsis[vsi].changed = 0;
		}
	}
}

/**
 * ice_init_sw_db - init software database from hw tables
 * @hw: pointer to the hardware structure
 */
static
void ice_init_sw_db(struct ice_hw *hw)
{
	u16 i;

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		ice_init_sw_xlt1_db(hw, (enum ice_block)i);
		ice_init_sw_xlt2_db(hw, (enum ice_block)i);
	}
}

/**
 * ice_init_hw_tbls - init hardware table memory
 * @hw: pointer to the hardware structure
 */
enum ice_status ice_init_hw_tbls(struct ice_hw *hw)
{
	u8 i;

	ice_init_flow_profs(hw);

	for (i = 0; i < ICE_BLK_COUNT; i++) {
		struct ice_prof_redir *prof_redir = &hw->blk[i].prof_redir;
		struct ice_prof_tcam *prof = &hw->blk[i].prof;
		struct ice_xlt1 *xlt1 = &hw->blk[i].xlt1;
		struct ice_xlt2 *xlt2 = &hw->blk[i].xlt2;
		struct ice_es *es = &hw->blk[i].es;

		hw->blk[i].overwrite = blk_sizes[i].overwrite;
		es->reverse = blk_sizes[i].reverse;

		xlt1->sid = ice_blk_sids[i][ICE_SID_XLT1_OFF];
		xlt1->count = blk_sizes[i].xlt1;

		xlt1->ptypes = (struct ice_ptg_ptype *)
			ice_calloc(hw, xlt1->count, sizeof(*xlt1->ptypes));

		if (!xlt1->ptypes)
			goto err;

		xlt1->ptg_tbl = (struct ice_ptg_entry *)
			ice_calloc(hw, ICE_MAX_PTGS, sizeof(*xlt1->ptg_tbl));

		if (!xlt1->ptg_tbl)
			goto err;

		xlt1->t = (u8 *)ice_calloc(hw, xlt1->count, sizeof(*xlt1->t));
		if (!xlt1->t)
			goto err;

		xlt2->sid = ice_blk_sids[i][ICE_SID_XLT2_OFF];
		xlt2->count = blk_sizes[i].xlt2;

		xlt2->vsis = (struct ice_vsig_vsi *)
			ice_calloc(hw, xlt2->count, sizeof(*xlt2->vsis));

		if (!xlt2->vsis)
			goto err;

		xlt2->vsig_tbl = (struct ice_vsig_entry *)
			ice_calloc(hw, xlt2->count, sizeof(*xlt2->vsig_tbl));
		if (!xlt2->vsig_tbl)
			goto err;

		xlt2->t = (u16 *)ice_calloc(hw, xlt2->count, sizeof(*xlt2->t));
		if (!xlt2->t)
			goto err;

		prof->sid = ice_blk_sids[i][ICE_SID_PR_OFF];
		prof->count = blk_sizes[i].prof_tcam;
		prof->max_prof_id = blk_sizes[i].prof_id;
		prof->cdid_bits = blk_sizes[i].prof_cdid_bits;
		prof->t = (struct ice_prof_tcam_entry *)
			ice_calloc(hw, prof->count, sizeof(*prof->t));

		if (!prof->t)
			goto err;

		prof_redir->sid = ice_blk_sids[i][ICE_SID_PR_REDIR_OFF];
		prof_redir->count = blk_sizes[i].prof_redir;
		prof_redir->t = (u8 *)ice_calloc(hw, prof_redir->count,
						 sizeof(*prof_redir->t));

		if (!prof_redir->t)
			goto err;

		es->sid = ice_blk_sids[i][ICE_SID_ES_OFF];
		es->count = blk_sizes[i].es;
		es->fvw = blk_sizes[i].fvw;
		es->t = (struct ice_fv_word *)
			ice_calloc(hw, es->count * es->fvw, sizeof(*es->t));

		if (!es->t)
			goto err;

		es->ref_count = (u16 *)
			ice_calloc(hw, es->count, sizeof(*es->ref_count));

		if (!es->ref_count)
			goto err;

		es->resource_used_hack = (u8 *)
			ice_calloc(hw, hw->blk[i].es.count, sizeof(u8));

		if (!es->resource_used_hack)
			goto err;

		prof->resource_used_hack = (u8 *)ice_calloc(hw, prof->count,
							    sizeof(u8));

		if (!prof->resource_used_hack)
			goto err;

		INIT_LIST_HEAD(&es->prof_map);

		/* Now that tables are allocated, read in package data */
		ice_fill_blk_tbls(hw, (enum ice_block)i);
	}

	ice_init_sw_db(hw);

	return ICE_SUCCESS;

err:
	ice_free_hw_tbls(hw);
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_prof_gen_key - generate profile id key
 * @hw: pointer to the hw struct
 * @blk: the block in which to write profile id to
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: cdid portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 * @key: output of profile id key
 */
static enum ice_status
ice_prof_gen_key(struct ice_hw *hw, enum ice_block blk, u8 ptg, u16 vsig,
		 u8 cdid, u16 flags, u8 vl_msk[ICE_TCAM_KEY_VAL_SZ],
		 u8 dc_msk[ICE_TCAM_KEY_VAL_SZ], u8 nm_msk[ICE_TCAM_KEY_VAL_SZ],
		 u8 key[ICE_TCAM_KEY_SZ])
{
	struct ice_prof_id_key inkey;

	inkey.xlt1 = ptg;
	inkey.xlt2_cdid = CPU_TO_LE16(vsig);
	inkey.flags = CPU_TO_LE16(flags);

	switch (hw->blk[blk].prof.cdid_bits) {
	case 0:
		break;
	case 2:
#define ICE_CD_2_M 0xC000U
#define ICE_CD_2_S 14
		inkey.xlt2_cdid &= ~CPU_TO_LE16(ICE_CD_2_M);
		inkey.xlt2_cdid |= CPU_TO_LE16(BIT(cdid) << ICE_CD_2_S);
		break;
	case 4:
#define ICE_CD_4_M 0xF000U
#define ICE_CD_4_S 12
		inkey.xlt2_cdid &= ~CPU_TO_LE16(ICE_CD_4_M);
		inkey.xlt2_cdid |= CPU_TO_LE16(BIT(cdid) << ICE_CD_4_S);
		break;
	case 8:
#define ICE_CD_8_M 0xFF00U
#define ICE_CD_8_S 16
		inkey.xlt2_cdid &= ~CPU_TO_LE16(ICE_CD_8_M);
		inkey.xlt2_cdid |= CPU_TO_LE16(BIT(cdid) << ICE_CD_8_S);
		break;
	default:
		ice_debug(hw, ICE_DBG_PKG, "Error in profile config\n");
		break;
	};

	return ice_set_key(key, ICE_TCAM_KEY_SZ, (u8 *)&inkey, vl_msk, dc_msk,
			   nm_msk, 0, ICE_TCAM_KEY_SZ / 2);
}

/**
 * ice_tcam_write_entry - write tcam entry
 * @hw: pointer to the hw struct
 * @blk: the block in which to write profile id to
 * @idx: the entry index to write to
 * @prof_id: profile id
 * @ptg: packet type group (PTG) portion of key
 * @vsig: VSIG portion of key
 * @cdid: cdid portion of key
 * @flags: flag portion of key
 * @vl_msk: valid mask
 * @dc_msk: don't care mask
 * @nm_msk: never match mask
 */
static enum ice_status
ice_tcam_write_entry(struct ice_hw *hw, enum ice_block blk, u16 idx,
		     u8 prof_id, u8 ptg, u16 vsig, u8 cdid, u16 flags,
		     u8 vl_msk[ICE_TCAM_KEY_VAL_SZ],
		     u8 dc_msk[ICE_TCAM_KEY_VAL_SZ],
		     u8 nm_msk[ICE_TCAM_KEY_VAL_SZ])
{
	struct ice_prof_tcam_entry;
	enum ice_status status;

	status = ice_prof_gen_key(hw, blk, ptg, vsig, cdid, flags, vl_msk,
				  dc_msk, nm_msk, hw->blk[blk].prof.t[idx].key);
	if (!status) {
		hw->blk[blk].prof.t[idx].addr = CPU_TO_LE16(idx);
		hw->blk[blk].prof.t[idx].prof_id = prof_id;
	}

	return status;
}

/**
 * ice_vsig_get_ref - returns number of vsis belong to a VSIG
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsig: VSIG to query
 * @refs: pointer to variable to receive the reference count
 */
static enum ice_status
ice_vsig_get_ref(struct ice_hw *hw, enum ice_block blk, u16 vsig, u16 *refs)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *ptr;
	*refs = 0;

	if (!hw->blk[blk].xlt2.vsig_tbl[idx].in_use)
		return ICE_ERR_DOES_NOT_EXIST;

	ptr = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	while (ptr) {
		(*refs)++;
		ptr = ptr->next_vsi;
	}

	return ICE_SUCCESS;
}

/**
 * ice_get_ptg - get or allocate a ptg for a ptype
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @ptype: the ptype to retrieve the PTG for
 * @ptg: receives the PTG of the ptype
 * @add: receive boolean indicating whether PTG was added or not
 */
static enum ice_status
ice_get_ptg(struct ice_hw *hw, enum ice_block blk, u16 ptype, u8 *ptg,
	    bool *add)
{
	enum ice_status status;

	*ptg = ICE_DEFAULT_PTG;
	*add = false;

	status = ice_ptg_find_ptype(hw, blk, ptype, ptg);
	if (status)
		return status;

	if (*ptg == ICE_DEFAULT_PTG) {
		/* need to allocate a PTG, and add ptype to it */
		*ptg = ice_ptg_alloc(hw, blk);
		if (*ptg == ICE_DEFAULT_PTG)
			return ICE_ERR_HW_TABLE;

		status = ice_ptg_add_mv_ptype(hw, blk, ptype, *ptg);
		if (status)
			return ICE_ERR_HW_TABLE;

		*add = true;
	}

	return ICE_SUCCESS;
};

/**
 * ice_has_prof_vsig - check to see if VSIG has a specific profile
 * @hw: pointer to the hardware structure
 * @blk: hw block
 * @vsig: VSIG to check against
 * @hdl: profile handle
 */
static bool
ice_has_prof_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *ent;

	LIST_FOR_EACH_ENTRY(ent, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    ice_vsig_prof, list) {
		if (ent->profile_cookie == hdl)
			return true;
	}

	ice_debug(hw, ICE_DBG_INIT,
		  "Characteristic list for vsi group %d not found.\n",
		  vsig);
	return false;
}

/**
 * ice_prof_bld_es - build profile id extraction sequence changes
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_es(struct ice_hw *hw, enum ice_block blk,
		struct ice_buf_build *bld, struct LIST_HEAD_TYPE *chgs)
{
	u16 vec_size = hw->blk[blk].es.fvw * sizeof(struct ice_fv_word);
	struct ice_chs_chg *tmp;

	LIST_FOR_EACH_ENTRY(tmp, chgs, ice_chs_chg, list_entry) {
		if (tmp->type == ICE_PTG_ES_ADD && tmp->add_prof) {
			u16 off = tmp->prof_id * hw->blk[blk].es.fvw;
			struct ice_pkg_es *p;
			u32 id;

			id = ice_sect_id(blk, ICE_VEC_TBL);
			p = (struct ice_pkg_es *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p) +
							  vec_size -
							  sizeof(p->es[0]));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = CPU_TO_LE16(1);
			p->offset = CPU_TO_LE16(tmp->prof_id);

			ice_memcpy(p->es, &hw->blk[blk].es.t[off], vec_size,
				   ICE_NONDMA_TO_NONDMA);
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_prof_bld_tcam - build profile id tcam changes
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_tcam(struct ice_hw *hw, enum ice_block blk,
		  struct ice_buf_build *bld, struct LIST_HEAD_TYPE *chgs)
{
	struct ice_chs_chg *tmp;

	LIST_FOR_EACH_ENTRY(tmp, chgs, ice_chs_chg, list_entry) {
		if ((tmp->type == ICE_TCAM_ADD && tmp->add_tcam_idx) ||
		    tmp->type == ICE_TCAM_REM) {
			struct ice_prof_id_section *p;
			u32 id;

			id = ice_sect_id(blk, ICE_PROF_TCAM);
			p = (struct ice_prof_id_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = CPU_TO_LE16(1);
			p->entry[0].addr = CPU_TO_LE16(tmp->tcam_idx);
			p->entry[0].prof_id = tmp->prof_id;

			ice_memcpy(p->entry[0].key,
				   &hw->blk[blk].prof.t[tmp->tcam_idx].key,
				   sizeof(hw->blk[blk].prof.t->key),
				   ICE_NONDMA_TO_NONDMA);
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_prof_bld_xlt1 - build xlt1 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_xlt1(enum ice_block blk, struct ice_buf_build *bld,
		  struct LIST_HEAD_TYPE *chgs)
{
	struct ice_chs_chg *tmp;

	LIST_FOR_EACH_ENTRY(tmp, chgs, ice_chs_chg, list_entry) {
		if (tmp->type == ICE_PTG_ES_ADD && tmp->add_ptg) {
			struct ice_xlt1_section *p;
			u32 id;

			id = ice_sect_id(blk, ICE_XLT1);
			p = (struct ice_xlt1_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = CPU_TO_LE16(1);
			p->offset = CPU_TO_LE16(tmp->ptype);
			p->value[0] = tmp->ptg;
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_prof_bld_xlt2 - build xlt2 changes
 * @blk: hardware block
 * @bld: the update package buffer build to add to
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_prof_bld_xlt2(enum ice_block blk, struct ice_buf_build *bld,
		  struct LIST_HEAD_TYPE *chgs)
{
	struct ice_chs_chg *tmp;

	LIST_FOR_EACH_ENTRY(tmp, chgs, ice_chs_chg, list_entry) {
		bool found = false;

		if (tmp->type == ICE_VSIG_ADD)
			found = true;
		else if (tmp->type == ICE_VSI_MOVE)
			found = true;
		else if (tmp->type == ICE_VSIG_REM)
			found = true;

		if (found) {
			struct ice_xlt2_section *p;
			u32 id;

			id = ice_sect_id(blk, ICE_XLT2);
			p = (struct ice_xlt2_section *)
				ice_pkg_buf_alloc_section(bld, id, sizeof(*p));

			if (!p)
				return ICE_ERR_MAX_LIMIT;

			p->count = CPU_TO_LE16(1);
			p->offset = CPU_TO_LE16(tmp->vsi);
			p->value[0] = CPU_TO_LE16(tmp->vsig);
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_upd_prof_hw - update hardware using the change list
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @chgs: the list of changes to make in hardware
 */
static enum ice_status
ice_upd_prof_hw(struct ice_hw *hw, enum ice_block blk,
		struct LIST_HEAD_TYPE *chgs)
{
	struct ice_buf_build *b;
	struct ice_chs_chg *tmp;
	enum ice_status status;
	u16 pkg_sects = 0;
	u16 sects = 0;
	u16 xlt1 = 0;
	u16 xlt2 = 0;
	u16 tcam = 0;
	u16 es = 0;

	/* count number of sections we need */
	LIST_FOR_EACH_ENTRY(tmp, chgs, ice_chs_chg, list_entry) {
		switch (tmp->type) {
		case ICE_PTG_ES_ADD:
			if (tmp->add_ptg)
				xlt1++;
			if (tmp->add_prof)
				es++;
			break;
		case ICE_TCAM_ADD:
		case ICE_TCAM_REM:
			tcam++;
			break;
		case ICE_VSIG_ADD:
		case ICE_VSI_MOVE:
		case ICE_VSIG_REM:
			xlt2++;
			break;
		default:
			break;
		}
	}
	sects = xlt1 + xlt2 + tcam + es;

	if (!sects)
		return ICE_SUCCESS;

	/* Build update package buffer */
	b = ice_pkg_buf_alloc(hw);
	if (!b)
		return ICE_ERR_NO_MEMORY;

	status = ice_pkg_buf_reserve_section(b, sects);
	if (status)
		goto error_tmp;

	/* Preserve order of table update: ES, TCAM, PTG, VSIG */
	if (es) {
		status = ice_prof_bld_es(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (tcam) {
		status = ice_prof_bld_tcam(hw, blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt1) {
		status = ice_prof_bld_xlt1(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	if (xlt2) {
		status = ice_prof_bld_xlt2(blk, b, chgs);
		if (status)
			goto error_tmp;
	}

	/* After package buffer build check if the section count in buffer is
	 * non-zero and matches the number of sections detected for package
	 * update.
	 */
	pkg_sects = ice_pkg_buf_get_active_sections(b);
	if (!pkg_sects || pkg_sects != sects) {
		status = ICE_ERR_INVAL_SIZE;
		goto error_tmp;
	}

	/* update package */
	status = ice_update_pkg(hw, ice_pkg_buf(b), 1);
	if (status == ICE_ERR_AQ_ERROR)
		ice_debug(hw, ICE_DBG_INIT, "Unable to update HW profile.");

error_tmp:
	ice_pkg_buf_free(hw, b);
	return status;
}

/**
 * ice_add_prof - add profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking id
 * @ptypes: array of bitmaps indicating ptypes (ICE_FLOW_PTYPE_MAX bits)
 * @es: extraction sequence (length of array is determined by the block)
 *
 * This function registers a profile, which matches a set of PTYPES with a
 * particular extraction sequence. While the hardware profile is allocated
 * it will not be written until the first call to ice_add_flow that specifies
 * the id value used here.
 */
enum ice_status
ice_add_prof(struct ice_hw *hw, enum ice_block blk, u64 id, u8 ptypes[],
	     struct ice_fv_word *es)
{
	u32 bytes = DIVIDE_AND_ROUND_UP(ICE_FLOW_PTYPE_MAX, BITS_PER_BYTE);
	struct ice_prof_map *prof;
	enum ice_status status;
	u32 byte = 0;
	u8 prof_id;

	/* search for existing profile */
	status = ice_find_prof_id(hw, blk, es, &prof_id);
	if (status) {
		/* allocate profile id */
		status = ice_alloc_prof_id(hw, blk, &prof_id);
		if (status)
			goto err_ice_add_prof;

		/* and write new es */
		ice_write_es(hw, blk, prof_id, es);
	}

	/* add profile info */

	prof = (struct ice_prof_map *)ice_malloc(hw, sizeof(*prof));
	if (!prof)
		goto err_ice_add_prof;

	prof->profile_cookie = id;
	prof->prof_id = prof_id;
	prof->ptype_count = 0;
	prof->context = 0;

	/* build list of ptgs */
	while (bytes && prof->ptype_count < ICE_MAX_PTYPE_PER_PROFILE) {
		u32 bit;

		if (!ptypes[byte]) {
			bytes--;
			byte++;
			continue;
		}
		/* Examine 8 bits per byte */
		for (bit = 0; bit < 8; bit++) {
			if (ptypes[byte] & 1 << bit) {
				u16 ptype;
				u8 m;

				ptype = byte * 8 + bit;
				if (ptype < ICE_FLOW_PTYPE_MAX) {
					prof->ptype[prof->ptype_count] = ptype;

					if (++prof->ptype_count >=
						ICE_MAX_PTYPE_PER_PROFILE)
						break;
				}

				/* nothing left in byte, then exit */
				m = ~((1 << (bit + 1)) - 1);
				if (!(ptypes[byte] & m))
					break;
			}
		}

		bytes--;
		byte++;
	}
	LIST_ADD(&prof->list, &hw->blk[blk].es.prof_map);

	return ICE_SUCCESS;

err_ice_add_prof:
	return status;
}

/**
 * ice_search_prof_id - Search for a profile tracking ID
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will search for a profile tracking ID which was previously added.
 */
struct ice_prof_map *
ice_search_prof_id(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_prof_map *entry = NULL;
	struct ice_prof_map *map;

	LIST_FOR_EACH_ENTRY(map, &hw->blk[blk].es.prof_map, ice_prof_map,
			    list) {
		if (map->profile_cookie == id) {
			entry = map;
			break;
		}
	}

	return entry;
}

/**
 * ice_set_prof_context - Set context for a given profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking ID
 * @cntxt: context
 */
struct ice_prof_map *
ice_set_prof_context(struct ice_hw *hw, enum ice_block blk, u64 id, u64 cntxt)
{
	struct ice_prof_map *entry;

	entry = ice_search_prof_id(hw, blk, id);
	if (entry)
		entry->context = cntxt;

	return entry;
}

/**
 * ice_get_prof_context - Get context for a given profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking ID
 * @cntxt: pointer to variable to receive the context
 */
struct ice_prof_map *
ice_get_prof_context(struct ice_hw *hw, enum ice_block blk, u64 id, u64 *cntxt)
{
	struct ice_prof_map *entry;

	entry = ice_search_prof_id(hw, blk, id);
	if (entry)
		*cntxt = entry->context;

	return entry;
}

/**
 * ice_vsig_prof_id_count - count profiles in a VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 */
static u16
ice_vsig_prof_id_count(struct ice_hw *hw, enum ice_block blk, u16 vsig)
{
	u16 idx = vsig & ICE_VSIG_IDX_M, count = 0;
	struct ice_vsig_prof *p;

	LIST_FOR_EACH_ENTRY(p, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    ice_vsig_prof, list) {
		count++;
	}

	return count;
}

/**
 * ice_rel_tcam_idx - release a tcam index
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @idx: the index to release
 */
static enum ice_status
ice_rel_tcam_idx(struct ice_hw *hw, enum ice_block blk, u16 idx)
{
	/* Masks to invoke a never match entry */
	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFE, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
	enum ice_status status;

	/* write the tcam entry */
	status = ice_tcam_write_entry(hw, blk, idx, 0, 0, 0, 0, 0, vl_msk,
				      dc_msk, nm_msk);
	if (status)
		return status;

	/* release the tcam entry */
	status = ice_free_tcam_ent(hw, blk, idx);

	return status;
}

/**
 * ice_rem_prof_id - remove one profile from a VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 * @prof: pointer to profile structure to remove
 * @chg: pointer to list to record changes
 */
static enum ice_status
ice_rem_prof_id(struct ice_hw *hw, enum ice_block blk, u16 vsig,
		struct ice_vsig_prof *prof, struct LIST_HEAD_TYPE *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	u16 i;

	for (i = 0; i < prof->tcam_count; i++) {
		p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
		if (!p)
			goto err_ice_rem_prof_id;

		p->type = ICE_TCAM_REM;
		p->vsig = vsig;
		p->prof_id = prof->tcam[i].prof_id;
		p->tcam_idx = prof->tcam[i].tcam_idx;

		p->ptg = prof->tcam[i].ptg;
		prof->tcam[i].in_use = false;
		p->orig_ent = hw->blk[blk].prof.t[p->tcam_idx];
		status = ice_rel_tcam_idx(hw, blk, p->tcam_idx);
		if (!status)
			status = ice_prof_dec_ref(hw, blk, p->prof_id);

		LIST_ADD(&p->list_entry, chg);

		if (status)
			goto err_ice_rem_prof_id;
	}

	return ICE_SUCCESS;

err_ice_rem_prof_id:
	/* caller will clean up the change list */
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_rem_vsig - remove VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: the VSIG to remove
 * @chg: the change list
 */
static enum ice_status
ice_rem_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig,
	     struct LIST_HEAD_TYPE *chg)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_vsi *vsi_cur;
	struct ice_vsig_prof *d, *t;
	enum ice_status status;

	/* remove TCAM entries */
	LIST_FOR_EACH_ENTRY_SAFE(d, t,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 ice_vsig_prof, list) {
		status = ice_rem_prof_id(hw, blk, vsig, d, chg);
		if (status)
			goto err_ice_rem_vsig;

		LIST_DEL(&d->list);
		ice_free(hw, d);
	}

	/* Move all VSIS associated with this VSIG to the default VSIG */
	vsi_cur = hw->blk[blk].xlt2.vsig_tbl[idx].first_vsi;
	if (!vsi_cur)
		return ICE_ERR_CFG;

	do {
		struct ice_vsig_vsi *tmp = vsi_cur->next_vsi;
		struct ice_chs_chg *p;

		p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
		if (!p)
			goto err_ice_rem_vsig;

		p->type = ICE_VSIG_REM;
		p->orig_vsig = vsig;
		p->vsig = ICE_DEFAULT_VSIG;
		p->vsi = vsi_cur - hw->blk[blk].xlt2.vsis;

		LIST_ADD(&p->list_entry, chg);

		status = ice_vsig_free(hw, blk, vsig);
		if (status)
			return status;

		vsi_cur = tmp;
	} while (vsi_cur);

	return ICE_SUCCESS;

err_ice_rem_vsig:
	/* the caller will free up the change list */
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_rem_prof_id_vsig - remove a specific profile from a VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: VSIG to remove the profile from
 * @hdl: profile handle indicating which profile to remove
 * @chg: list to receive a record of changes
 */
static enum ice_status
ice_rem_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl,
		     struct LIST_HEAD_TYPE *chg)
{
	u16 idx = vsig & ICE_VSIG_IDX_M;
	struct ice_vsig_prof *p, *t;
	enum ice_status status;

	LIST_FOR_EACH_ENTRY_SAFE(p, t,
				 &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
				 ice_vsig_prof, list) {
		if (p->profile_cookie == hdl) {
			if (ice_vsig_prof_id_count(hw, blk, vsig) == 1)
				/* this is the last profile, remove the VSIG */
				return ice_rem_vsig(hw, blk, vsig, chg);

			status = ice_rem_prof_id(hw, blk, vsig, p, chg);
			if (!status) {
				LIST_DEL(&p->list);
				ice_free(hw, p);
			}
			return status;
		}
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_flow_all - remove all flows with a particular profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking ID
 */
static enum ice_status
ice_rem_flow_all(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	struct ice_chs_chg *del, *tmp;
	struct LIST_HEAD_TYPE chg;
	enum ice_status status;
	u16 i;

	INIT_LIST_HEAD(&chg);

	for (i = 1; i < ICE_MAX_VSIGS; i++) {
		if (hw->blk[blk].xlt2.vsig_tbl[i].in_use) {
			if (ice_has_prof_vsig(hw, blk, i, id)) {
				status = ice_rem_prof_id_vsig(hw, blk, i, id,
							      &chg);
				if (status)
					goto err_ice_rem_flow_all;
			}
		}
	}

	status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_flow_all:
	LIST_FOR_EACH_ENTRY_SAFE(del, tmp, &chg, ice_chs_chg, list_entry) {
		LIST_DEL(&del->list_entry);
		ice_free(hw, del);
	}

	return status;
}

/**
 * ice_rem_prof - remove profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @id: profile tracking ID
 *
 * This will remove the profile specified by the id parameter, which was
 * previously created through ice_add_prof. If any existing entries
 * are associated with this profile, they will be removed as well.
 */
enum ice_status ice_rem_prof(struct ice_hw *hw, enum ice_block blk, u64 id)
{
	enum ice_status status;
	struct ice_prof_map *pmap;

	pmap = ice_search_prof_id(hw, blk, id);
	if (!pmap)
		return ICE_ERR_DOES_NOT_EXIST;

	status = ice_free_prof_id(hw, blk, pmap->prof_id);

	if (status)
		return status;

	/* remove all flows with this profile */
	status = ice_rem_flow_all(hw, blk, pmap->profile_cookie);
	if (status)
		return status;
	LIST_DEL(&pmap->list);
	ice_free(hw, pmap);

	return ICE_SUCCESS;
}

/**
 * ice_get_prof_ptgs - get ptgs for profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @hdl: profile handle
 * @chg: change list
 */
static enum ice_status
ice_get_prof_ptgs(struct ice_hw *hw, enum ice_block blk, u64 hdl,
		  struct LIST_HEAD_TYPE *chg)
{
	struct ice_prof_map *map;
	struct ice_chs_chg *p;
	u16 i;

	/* Get the details on the profile specified by the handle id */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	for (i = 0; i < map->ptype_count; i++) {
		enum ice_status status;
		bool add;
		u8 ptg;

		status = ice_get_ptg(hw, blk, map->ptype[i], &ptg, &add);
		if (status)
			goto err_ice_get_prof_ptgs;

		if (add || !hw->blk[blk].es.ref_count[map->prof_id]) {
			/* add PTG to change list */
			p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
			if (!p)
				goto err_ice_get_prof_ptgs;

			p->type = ICE_PTG_ES_ADD;
			p->ptype = map->ptype[i];
			p->ptg = ptg;
			p->add_ptg = add;

			p->add_prof = !hw->blk[blk].es.ref_count[map->prof_id];
			p->prof_id = map->prof_id;

			LIST_ADD(&p->list_entry, chg);
		}
	}

	return ICE_SUCCESS;

err_ice_get_prof_ptgs:
	/* let caller clean up the change list */
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_get_profs_vsig - get a copy of the list of profiles from a VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: VSIG from which to copy the list
 * @lst: output list
 *
 * This routine makes a copy of the list of profiles in the specified VSIG.
 */
static enum ice_status
ice_get_profs_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig,
		   struct LIST_HEAD_TYPE *lst)
{
	struct ice_vsig_prof *ent1, *ent2;
	u16 idx = vsig & ICE_VSIG_IDX_M;

	LIST_FOR_EACH_ENTRY(ent1, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    ice_vsig_prof, list) {
		struct ice_vsig_prof *p;

		/* copy to the input list */
		p = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*p));
		if (!p)
			goto err_ice_get_profs_vsig;

		ice_memcpy(p, ent1, sizeof(*p), ICE_NONDMA_TO_NONDMA);

		LIST_ADD(&p->list, lst);
	}

	return ICE_SUCCESS;

err_ice_get_profs_vsig:
	LIST_FOR_EACH_ENTRY_SAFE(ent1, ent2, lst, ice_vsig_prof, list) {
		LIST_DEL(&ent1->list);
		ice_free(hw, ent1);
	}

	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_add_prof_to_lst - add profile entry to a list
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @lst: the list to be added to
 * @hdl: profile handle of entry to add
 */
static enum ice_status
ice_add_prof_to_lst(struct ice_hw *hw, enum ice_block blk,
		    struct LIST_HEAD_TYPE *lst, u64 hdl)
{
	struct ice_vsig_prof *p;
	struct ice_prof_map *map;
	u16 i;

	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	p = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	p->profile_cookie = map->profile_cookie;
	p->prof_id = map->prof_id;
	p->tcam_count = map->ptype_count;

	for (i = 0; i < map->ptype_count; i++) {
		enum ice_status status;
		u8 ptg;

		p->tcam[i].prof_id = map->prof_id;
		p->tcam[i].tcam_idx = ICE_INVALID_TCAM;

		status = ice_ptg_find_ptype(hw, blk, map->ptype[i], &ptg);
		if (status) {
			ice_free(hw, p);
			return status;
		}

		p->tcam[i].ptg = ptg;
	}

	LIST_ADD(&p->list, lst);

	return ICE_SUCCESS;
}

/**
 * ice_move_vsi - move VSI to another VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: the VSI to move
 * @vsig: the VSIG to move the VSI to
 * @chg: the change list
 */
static enum ice_status
ice_move_vsi(struct ice_hw *hw, enum ice_block blk, u16 vsi, u16 vsig,
	     struct LIST_HEAD_TYPE *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	u16 orig_vsig;

	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_vsig_find_vsi(hw, blk, vsi, &orig_vsig);
	if (!status)
		status = ice_vsig_add_mv_vsi(hw, blk, vsi, vsig);
	if (status) {
		ice_free(hw, p);
		return status;
	}

	p->type = ICE_VSI_MOVE;
	p->vsi = vsi;
	p->orig_vsig = orig_vsig;
	p->vsig = vsig;

	LIST_ADD(&p->list_entry, chg);

	return ICE_SUCCESS;
}

/**
 * ice_prof_tcam_ena_dis - add enable or disable tcam change
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @enable: true to enable, false to disable
 * @vsig: the vsig of the tcam entry
 * @tcam: pointer the tcam info structure of the tcam to disable
 * @chg: the change list
 *
 * This function appends an enable or disable tcam entry in the change log
 */
static enum ice_status
ice_prof_tcam_ena_dis(struct ice_hw *hw, enum ice_block blk, bool enable,
		      u16 vsig, struct ice_tcam_inf *tcam,
		      struct LIST_HEAD_TYPE *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;

	/* Default: enable means change the low flag bit to don't care */
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x01, 0x00, 0x00, 0x00, 0x00 };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x01, 0x00, 0x00, 0x00, 0x00 };

	/* If disabled, change the low flag bit to never match */
	if (!enable) {
		dc_msk[0] = 0x00;
		nm_msk[0] = 0x01;
	}

	/* add TCAM to change list */
	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	status = ice_tcam_write_entry(hw, blk, tcam->tcam_idx, tcam->prof_id,
				      tcam->ptg, vsig, 0, 0, vl_msk, dc_msk,
				      nm_msk);
	if (status)
		goto err_ice_prof_tcam_ena_dis;

	tcam->in_use = enable;

	p->type = ICE_TCAM_ADD;
	p->add_tcam_idx = true;
	p->prof_id = tcam->prof_id;
	p->ptg = tcam->ptg;
	p->vsig = 0;
	p->tcam_idx = tcam->tcam_idx;

	/* log change */
	LIST_ADD(&p->list_entry, chg);

	return ICE_SUCCESS;

err_ice_prof_tcam_ena_dis:
	ice_free(hw, p);
	return status;
}

/**
 * ice_adj_prof_priorities - adjust profile based on priorities
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: the VSIG for which to adjust profile priorities
 * @chg: the change list
 */
static enum ice_status
ice_adj_prof_priorities(struct ice_hw *hw, enum ice_block blk, u16 vsig,
			struct LIST_HEAD_TYPE *chg)
{
	ice_declare_bitmap(ptgs_used, ICE_XLT1_CNT);
	struct ice_vsig_prof *t;
	enum ice_status status;
	u16 idx;

	ice_memset(ptgs_used, 0, sizeof(ptgs_used), ICE_NONDMA_MEM);
	idx = vsig & ICE_VSIG_IDX_M;

	/* Priority is based on the order in which the profiles are added. The
	 * newest added profile has highest priority and the oldest added
	 * profile has the lowest priority. Since the profile property list for
	 * a VSIG is sorted from newest to oldest, this code traverses the list
	 * in order and enables the first of each PTG that it finds (that is not
	 * already enabled); it also disables any duplicate PTGs that it finds
	 * in the older profiles (that are currently enabled).
	 */

	LIST_FOR_EACH_ENTRY(t, &hw->blk[blk].xlt2.vsig_tbl[idx].prop_lst,
			    ice_vsig_prof, list) {
		u16 i;

		for (i = 0; i < t->tcam_count; i++) {
			/* Scan the priorities from newest to oldest.
			 * Make sure that the newest profiles take priority.
			 */
			if (ice_is_bit_set(ptgs_used, t->tcam[i].ptg) &&
			    t->tcam[i].in_use) {
				/* need to mark this PTG as never match, as it
				 * was already in use and therefore duplicate
				 * (and lower priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, false,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			} else if (!ice_is_bit_set(ptgs_used, t->tcam[i].ptg) &&
				   !t->tcam[i].in_use) {
				/* need to enable this PTG, as it in not in use
				 * and not enabled (highest priority)
				 */
				status = ice_prof_tcam_ena_dis(hw, blk, true,
							       vsig,
							       &t->tcam[i],
							       chg);
				if (status)
					return status;
			}

			/* keep track of used ptgs */
			ice_set_bit(t->tcam[i].ptg, ptgs_used);
		}
	}

	return ICE_SUCCESS;
}

/**
 * ice_add_prof_id_vsig - add profile to VSIG
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsig: the VSIG to which this profile is to be added
 * @hdl: the profile handle indicating the profile to add
 * @chg: the change list
 */
static enum ice_status
ice_add_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsig, u64 hdl,
		     struct LIST_HEAD_TYPE *chg)
{
	/* Masks that ignore flags */
	u8 vl_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
	u8 dc_msk[ICE_TCAM_KEY_VAL_SZ] = { 0xFF, 0xFF, 0x00, 0x00, 0x00 };
	u8 nm_msk[ICE_TCAM_KEY_VAL_SZ] = { 0x00, 0x00, 0x00, 0x00, 0x00 };
	struct ice_prof_map *map;
	struct ice_vsig_prof *t;
	struct ice_chs_chg *p;
	u16 i;

	/* Get the details on the profile specified by the handle id */
	map = ice_search_prof_id(hw, blk, hdl);
	if (!map)
		return ICE_ERR_DOES_NOT_EXIST;

	/* Error, if this VSIG already has this profile */
	if (ice_has_prof_vsig(hw, blk, vsig, hdl))
		return ICE_ERR_ALREADY_EXISTS;

	/* new VSIG profile structure */
	t = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*t));
	if (!t)
		goto err_ice_add_prof_id_vsig;

	t->profile_cookie = map->profile_cookie;
	t->prof_id = map->prof_id;
	t->tcam_count = map->ptype_count;

	/* create tcam entries */
	for (i = 0; i < map->ptype_count; i++) {
		enum ice_status status;
		u16 tcam_idx;
		bool add;
		u8 ptg;

		/* If properly sequenced, we should never have to allocate new
		 * PTGs
		 */
		status = ice_get_ptg(hw, blk, map->ptype[i], &ptg, &add);
		if (status)
			goto err_ice_add_prof_id_vsig;

		/* add TCAM to change list */
		p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
		if (!p)
			goto err_ice_add_prof_id_vsig;

		/* allocate the tcam entry index */
		status = ice_alloc_tcam_ent(hw, blk, &tcam_idx);
		if (status)
			goto err_ice_add_prof_id_vsig;

		t->tcam[i].ptg = ptg;
		t->tcam[i].prof_id = map->prof_id;
		t->tcam[i].tcam_idx = tcam_idx;
		t->tcam[i].in_use = true;

		p->type = ICE_TCAM_ADD;
		p->add_tcam_idx = true;
		p->prof_id = t->tcam[i].prof_id;
		p->ptg = t->tcam[i].ptg;
		p->vsig = vsig;
		p->tcam_idx = t->tcam[i].tcam_idx;

		/* write the tcam entry */
		status = ice_tcam_write_entry(hw, blk, t->tcam[i].tcam_idx,
					      t->tcam[i].prof_id,
					      t->tcam[i].ptg, vsig, 0, 0,
					      vl_msk, dc_msk, nm_msk);
		if (status)
			goto err_ice_add_prof_id_vsig;

		/* this increments the reference count of how many tcam entries
		 * are using this hw profile id
		 */
		status = ice_prof_inc_ref(hw, blk, t->tcam[i].prof_id);

		/* log change */
		LIST_ADD(&p->list_entry, chg);
	}

	/* add profile to VSIG */
	LIST_ADD(&t->list,
		 &hw->blk[blk].xlt2.vsig_tbl[(vsig & ICE_VSIG_IDX_M)].prop_lst);

	return ICE_SUCCESS;

err_ice_add_prof_id_vsig:
	/* let caller clean up the change list */
	return ICE_ERR_NO_MEMORY;
}

/**
 * ice_create_prof_id_vsig - add a new VSIG with a single profile
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @hdl: the profile handle of the profile that will be added to the VSIG
 * @chg: the change list
 */
static enum ice_status
ice_create_prof_id_vsig(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl,
			struct LIST_HEAD_TYPE *chg)
{
	enum ice_status status;
	struct ice_chs_chg *p;
	u16 new_vsig;

	p = (struct ice_chs_chg *)ice_malloc(hw, sizeof(*p));
	if (!p)
		return ICE_ERR_NO_MEMORY;

	new_vsig = ice_vsig_alloc(hw, blk);
	if (!new_vsig)
		return ICE_ERR_HW_TABLE;

	status = ice_move_vsi(hw, blk, vsi, new_vsig, chg);
	if (status)
		return status;

	status = ice_add_prof_id_vsig(hw, blk, new_vsig, hdl, chg);
	if (status)
		return status;

	p->type = ICE_VSIG_ADD;
	p->vsi = vsi;
	p->orig_vsig = ICE_DEFAULT_VSIG;
	p->vsig = new_vsig;

	LIST_ADD(&p->list_entry, chg);

	return ICE_SUCCESS;
}

/**
 * ice_create_vsig_from_list - create a new VSIG with a list of profiles
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: the initial VSI that will be in VSIG
 * @lst: the list of profile that will be added to the VSIG
 * @chg: the change list
 */
static enum ice_status
ice_create_vsig_from_lst(struct ice_hw *hw, enum ice_block blk, u16 vsi,
			 struct LIST_HEAD_TYPE *lst, struct LIST_HEAD_TYPE *chg)
{
	struct ice_vsig_prof *t;
	enum ice_status status;
	u16 vsig;

	vsig = ice_vsig_alloc(hw, blk);
	if (!vsig)
		return ICE_ERR_HW_TABLE;

	status = ice_move_vsi(hw, blk, vsi, vsig, chg);
	if (status)
		return status;

	LIST_FOR_EACH_ENTRY(t, lst, ice_vsig_prof, list) {
		status = ice_add_prof_id_vsig(hw, blk, vsig, t->profile_cookie,
					      chg);
		if (status)
			return status;
	}

	return ICE_SUCCESS;
}

/**
 * ice_find_prof_vsig - find a VSIG with a specific profile handle
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @hdl: the profile handle of the profile to search for
 * @vsig: returns the VSIG with the matching profile
 */
static bool
ice_find_prof_vsig(struct ice_hw *hw, enum ice_block blk, u64 hdl, u16 *vsig)
{
	struct ice_vsig_prof *t;
	struct LIST_HEAD_TYPE lst;
	enum ice_status status;

	INIT_LIST_HEAD(&lst);

	t = (struct ice_vsig_prof *)ice_malloc(hw, sizeof(*t));
	if (!t)
		return false;

	t->profile_cookie = hdl;
	LIST_ADD(&t->list, &lst);

	status = ice_find_dup_props_vsig(hw, blk, &lst, vsig);

	LIST_DEL(&t->list);
	ice_free(hw, t);

	return status == ICE_SUCCESS;
}

/**
 * ice_add_prof_id_flow - add profile flow
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: the vsi to enable with the profile specified by id
 * @hdl: profile handle
 *
 * Calling this function will update the hardware tables to enable the
 * profile indicated by the id parameter for the VSIs specified in the vsi
 * array. Once successfully called, the flow will be enabled.
 */
enum ice_status
ice_add_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct LIST_HEAD_TYPE union_lst;
	struct ice_chs_chg *tmp, *del;
	struct LIST_HEAD_TYPE chrs;
	struct LIST_HEAD_TYPE chg;
	enum ice_status status;
	u16 vsig, or_vsig = 0;

	INIT_LIST_HEAD(&union_lst);
	INIT_LIST_HEAD(&chrs);
	INIT_LIST_HEAD(&chg);

	status = ice_get_prof_ptgs(hw, blk, hdl, &chg);
	if (status)
		return status;

	/* determine if vsi is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool only_vsi;
		u16 ref;

		/* found in vsig */
		or_vsig = vsig;

		/* make sure that there is no overlap/conflict between the new
		 * characteristics and the existing ones; we don't support that
		 * scenario
		 */
		if (ice_has_prof_vsig(hw, blk, vsig, hdl)) {
			status = ICE_ERR_ALREADY_EXISTS;
			goto err_ice_add_prof_id_flow;
		}

		/* last VSI in the VSIG? */
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_add_prof_id_flow;
		only_vsi = (ref == 1);

		/* create a union of the current profiles and the one being
		 * added
		 */
		status = ice_get_profs_vsig(hw, blk, vsig, &union_lst);
		if (status)
			goto err_ice_add_prof_id_flow;

		status = ice_add_prof_to_lst(hw, blk, &union_lst, hdl);
		if (status)
			goto err_ice_add_prof_id_flow;

		/* search for an existing VSIG with an exact charc match */
		status = ice_find_dup_props_vsig(hw, blk, &union_lst, &vsig);
		if (!status) {
			/* found an exact match */
			/* move vsi to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* remove original VSIG if we just moved the only VSI
			 * from it
			 */
			if (only_vsi) {
				status = ice_rem_vsig(hw, blk, or_vsig, &chg);
				if (status)
					goto err_ice_add_prof_id_flow;
			}
		} else if (only_vsi) {
			/* If the original VSIG only contains one VSI, then it
			 * will be the requesting VSI. In this case the VSI is
			 * not sharing entries and we can simply add the new
			 * profile to the VSIG.
			 */
			status = ice_add_prof_id_vsig(hw, blk, vsig, hdl, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* No match, so we need a new VSIG */
			status = ice_create_vsig_from_lst(hw, blk, vsi,
							  &union_lst, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;

			/* Adjust priorities */
			status = ice_adj_prof_priorities(hw, blk, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	} else {
		/* need to find or add a VSIG */
		/* search for an exising VSIG with an exact charc match */
		if (ice_find_prof_vsig(hw, blk, hdl, &vsig)) {
			/* found an exact match */
			/* add or move vsi to the VSIG that matches */
			status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		} else {
			/* we did not find an exact match */
			/* we need to add a VSIG */
			status = ice_create_prof_id_vsig(hw, blk, vsi, hdl,
							 &chg);
			if (status)
				goto err_ice_add_prof_id_flow;
		}
	}

	/* update hardware */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_add_prof_id_flow:
	LIST_FOR_EACH_ENTRY_SAFE(del, tmp, &chg, ice_chs_chg, list_entry) {
		LIST_DEL(&del->list_entry);
		ice_free(hw, del);
	}

	LIST_FOR_EACH_ENTRY_SAFE(del1, tmp1, &union_lst, ice_vsig_prof, list) {
		LIST_DEL(&del1->list);
		ice_free(hw, del1);
	}

	LIST_FOR_EACH_ENTRY_SAFE(del1, tmp1, &chrs, ice_vsig_prof, list) {
		LIST_DEL(&del1->list);
		ice_free(hw, del1);
	}

	return status;
}

/**
 * ice_add_flow - add flow
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: array of VSIs to enable with the profile specified by id
 * @count: number of elements in the vsi array
 * @id: profile tracking id
 *
 * Calling this function will update the hardware tables to enable the
 * profile indicated by the id parameter for the VSIs specified in the vsi
 * array. Once successfully called, the flow will be enabled.
 */
enum ice_status
ice_add_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi[], u8 count,
	     u64 id)
{
	enum ice_status status;
	u16 i;

	for (i = 0; i < count; i++) {
		status = ice_add_prof_id_flow(hw, blk, vsi[i], id);
		if (status)
			return status;
	}

	return ICE_SUCCESS;
}

/**
 * ice_rem_prof_from_list - remove a profile from list
 * @hw: pointer to the hw struct
 * @lst: list to remove the profile from
 * @hdl: the profile handle indicating the profile to remove
 */
static enum ice_status
ice_rem_prof_from_list(struct ice_hw *hw, struct LIST_HEAD_TYPE *lst, u64 hdl)
{
	struct ice_vsig_prof *ent, *tmp;

	LIST_FOR_EACH_ENTRY_SAFE(ent, tmp, lst, ice_vsig_prof, list) {
		if (ent->profile_cookie == hdl) {
			LIST_DEL(&ent->list);
			ice_free(hw, ent);
			return ICE_SUCCESS;
		}
	}

	return ICE_ERR_DOES_NOT_EXIST;
}

/**
 * ice_rem_prof_id_flow - remove flow
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: the vsi from which to remove the profile specified by id
 * @hdl: profile tracking handle
 *
 * Calling this function will update the hardware tables to remove the
 * profile indicated by the id parameter for the VSIs specified in the vsi
 * array. Once successfully called, the flow will be disabled.
 */
enum ice_status
ice_rem_prof_id_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi, u64 hdl)
{
	struct ice_vsig_prof *tmp1, *del1;
	struct LIST_HEAD_TYPE chg, copy;
	struct ice_chs_chg *tmp, *del;
	enum ice_status status;
	u16 vsig;

	INIT_LIST_HEAD(&copy);
	INIT_LIST_HEAD(&chg);

	/* determine if vsi is already part of a VSIG */
	status = ice_vsig_find_vsi(hw, blk, vsi, &vsig);
	if (!status && vsig) {
		bool last_profile;
		bool only_vsi;
		u16 ref;

		/* found in VSIG */
		last_profile = ice_vsig_prof_id_count(hw, blk, vsig) == 1;
		status = ice_vsig_get_ref(hw, blk, vsig, &ref);
		if (status)
			goto err_ice_rem_prof_id_flow;
		only_vsi = (ref == 1);

		if (only_vsi) {
			/* If the original VSIG only contains one reference,
			 * which will be the requesting VSI, then the VSI is not
			 * sharing entries and we can simply remove the specific
			 * characteristics from the VSIG.
			 */

			if (last_profile) {
				/* If there are no profiles left for this VSIG,
				 * then simply remove the the VSIG.
				 */
				status = ice_rem_vsig(hw, blk, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				status = ice_rem_prof_id_vsig(hw, blk, vsig,
							      hdl, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}

		} else {
			/* Make a copy of the VSIG's list of Profiles */
			status = ice_get_profs_vsig(hw, blk, vsig, &copy);
			if (status)
				goto err_ice_rem_prof_id_flow;

			/* Remove specified profile entry from the list */
			status = ice_rem_prof_from_list(hw, &copy, hdl);
			if (status)
				goto err_ice_rem_prof_id_flow;

			if (LIST_EMPTY(&copy)) {
				status = ice_move_vsi(hw, blk, vsi,
						      ICE_DEFAULT_VSIG, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

			} else if (ice_find_dup_props_vsig(hw, blk, &copy,
							   &vsig)) {
				/* found an exact match */
				/* add or move vsi to the VSIG that matches */
				/* Search for a VSIG with a matching profile
				 * list
				 */

				/* Found match, move VSI to the matching VSIG */
				status = ice_move_vsi(hw, blk, vsi, vsig, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			} else {
				/* since no existing VSIG supports this
				 * characteristic pattern, we need to create a
				 * new VSIG and tcam entries
				 */
				status = ice_create_vsig_from_lst(hw, blk, vsi,
								  &copy, &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;

				/* Adjust priorities */
				status = ice_adj_prof_priorities(hw, blk, vsig,
								 &chg);
				if (status)
					goto err_ice_rem_prof_id_flow;
			}
		}
	} else {
		status = ICE_ERR_DOES_NOT_EXIST;
	}

	/* update hardware tables */
	if (!status)
		status = ice_upd_prof_hw(hw, blk, &chg);

err_ice_rem_prof_id_flow:
	LIST_FOR_EACH_ENTRY_SAFE(del, tmp, &chg, ice_chs_chg, list_entry) {
		LIST_DEL(&del->list_entry);
		ice_free(hw, del);
	}

	LIST_FOR_EACH_ENTRY_SAFE(del1, tmp1, &copy, ice_vsig_prof, list) {
		LIST_DEL(&del1->list);
		ice_free(hw, del1);
	}

	return status;
}

/**
 * ice_rem_flow - remove flow
 * @hw: pointer to the hw struct
 * @blk: hardware block
 * @vsi: array of VSIs from which to remove the profile specified by id
 * @count: number of elements in the vsi array
 * @id: profile tracking id
 *
 * The function will remove flows from the specified VSIs that were enabled
 * using ice_add_flow. The id value will indicated which profile will be
 * removed. Once successfully called, the flow will be disabled.
 */
enum ice_status
ice_rem_flow(struct ice_hw *hw, enum ice_block blk, u16 vsi[], u8 count,
	     u64 id)
{
	enum ice_status status;
	u16 i;

	for (i = 0; i < count; i++) {
		status = ice_rem_prof_id_flow(hw, blk, vsi[i], id);
		if (status)
			return status;
	}

	return ICE_SUCCESS;
}
