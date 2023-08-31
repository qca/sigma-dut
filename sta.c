/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2021, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <ctype.h>
#ifdef __linux__
#include <regex.h>
#include <dirent.h>
#include <sys/time.h>
#include <netpacket/packet.h>
#include <linux/if_ether.h>
#ifdef ANDROID
#include <cutils/properties.h>
#include <android/log.h>
#include "keystore_get.h"
#else /* ANDROID */
#include <ifaddrs.h>
#endif /* ANDROID */
#include <netdb.h>
#endif /* __linux__ */
#ifdef __QNXNTO__
#include <net/if_dl.h>
#endif /* __QNXNTO__ */
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "miracast.h"
#include "qca-vendor_copy.h"
#include "nl80211_copy.h"

/* Temporary files for sta_send_addba */
#define VI_QOS_TMP_FILE     "/tmp/vi-qos.tmp"
#define VI_QOS_FILE         "/tmp/vi-qos.txt"
#define VI_QOS_REFFILE      "/etc/vi-qos.txt"

/*
 * MTU for Ethernet need to take into account 8-byte SNAP header
 * to be added when encapsulating Ethernet frame into 802.11
 */
#ifndef IEEE80211_MAX_DATA_LEN_DMG
#define IEEE80211_MAX_DATA_LEN_DMG 7920
#endif
#ifndef IEEE80211_SNAP_LEN_DMG
#define IEEE80211_SNAP_LEN_DMG 8
#endif

#define NON_PREF_CH_LIST_SIZE 100
#define NEIGHBOR_REPORT_SIZE 1000
#define DEFAULT_NEIGHBOR_BSSID_INFO "17"
#define DEFAULT_NEIGHBOR_PHY_TYPE "1"

#define WIL_DEFAULT_BI	100

/* default remain on channel time for transmitting frames (milliseconds) */
#define WIL_TRANSMIT_FRAME_DEFAULT_ROC 500
#define IEEE80211_P2P_ATTR_DEVICE_ID 3
#define IEEE80211_P2P_ATTR_GROUP_ID 15

/* describes tagged bytes in template frame file */
struct template_frame_tag {
	int num;
	int offset;
	size_t len;
};

extern char *sigma_wpas_ctrl;
extern char *sigma_cert_path;
extern enum driver_type wifi_chip_type;
extern char *sigma_radio_ifname[];

#ifdef __linux__
#define WIL_WMI_MAX_PAYLOAD	248
#define WIL_WMI_ESE_CFG_CMDID	0xa01
#define WIL_WMI_BF_TRIG_CMDID	0x83a
#define WIL_WMI_UNIT_TEST_CMDID	0x900
#define WIL_WMI_P2P_CFG_CMDID	0x910
#define WIL_WMI_START_LISTEN_CMDID	0x914
#define WIL_WMI_DISCOVERY_STOP_CMDID	0x917

struct wil_wmi_header {
	uint8_t mid;
	uint8_t reserved;
	uint16_t cmd;
	uint32_t ts;
} __attribute__((packed));

enum wil_wmi_bf_trig_type {
	WIL_WMI_SLS,
	WIL_WMI_BRP_RX,
	WIL_WMI_BRP_TX,
};

struct wil_wmi_bf_trig_cmd {
	/* enum wil_wmi_bf_trig_type */
	uint32_t bf_type;
	/* cid when type == WMI_BRP_RX */
	uint32_t sta_id;
	uint32_t reserved;
	/* mac address when type = WIL_WMI_SLS */
	uint8_t dest_mac[6];
} __attribute__((packed));

enum wil_wmi_sched_scheme_advertisment {
	WIL_WMI_ADVERTISE_ESE_DISABLED,
	WIL_WMI_ADVERTISE_ESE_IN_BEACON,
	WIL_WMI_ADVERTISE_ESE_IN_ANNOUNCE_FRAME,
};

enum wil_wmi_ese_slot_type {
	WIL_WMI_ESE_SP,
	WIL_WMI_ESE_CBAP,
	WIL_WMI_ESE_ANNOUNCE_NO_ACK,
};

struct wil_wmi_ese_slot {
	/* offset from start of BI in microseconds */
	uint32_t tbtt_offset;
	uint8_t flags;
	/* enum wil_wmi_ese_slot_type */
	uint8_t slot_type;
	/* duration in microseconds */
	uint16_t duration;
	/* frame exchange sequence duration, microseconds */
	uint16_t tx_op;
	/* time between 2 blocks for periodic allocation(microseconds) */
	uint16_t period;
	/* number of blocks in periodic allocation */
	uint8_t num_blocks;
	/* for semi-active allocations */
	uint8_t idle_period;
	uint8_t src_aid;
	uint8_t dst_aid;
	uint32_t reserved;
} __attribute__((packed));

#define WIL_WMI_MAX_ESE_SLOTS	4
struct wil_wmi_ese_cfg {
	uint8_t serial_num;
	/* wil_wmi_sched_scheme_advertisment */
	uint8_t ese_advertisment;
	uint16_t flags;
	uint8_t num_allocs;
	uint8_t reserved[3];
	uint64_t start_tbtt;
	/* allocations list */
	struct wil_wmi_ese_slot slots[WIL_WMI_MAX_ESE_SLOTS];
} __attribute__((packed));

#define WIL_WMI_UT_FORCE_MCS	6
struct wil_wmi_force_mcs {
	/* WIL_WMI_UT_HW_SYSAPI */
	uint16_t module_id;
	/* WIL_WMI_UT_FORCE_MCS */
	uint16_t subtype_id;
	/* cid (ignored in oob_mode, affects all stations) */
	uint32_t cid;
	/* 1 to force MCS, 0 to restore default behavior */
	uint32_t force_enable;
	/* MCS index, 0-12 */
	uint32_t mcs;
} __attribute__((packed));

#define WIL_WMI_UT_HW_SYSAPI 10
#define WIL_WMI_UT_FORCE_RSN_IE	0x29
struct wil_wmi_force_rsn_ie {
	/* WIL_WMI_UT_HW_SYSAPI */
	uint16_t module_id;
	/* WIL_WMI_UT_FORCE_RSN_IE */
	uint16_t subtype_id;
	/* 0 = no change, 1 = remove if exists, 2 = add if does not exist */
	uint32_t state;
} __attribute__((packed));

enum wil_wmi_discovery_mode {
	WMI_DISCOVERY_MODE_NON_OFFLOAD,
	WMI_DISCOVERY_MODE_OFFLOAD,
	WMI_DISCOVERY_MODE_PEER2PEER,
};

struct wil_wmi_p2p_cfg_cmd {
	/* enum wil_wmi_discovery_mode */
	uint8_t discovery_mode;
	/* 0-based (wireless channel - 1) */
	uint8_t channel;
	/* set to WIL_DEFAULT_BI */
	uint16_t bcon_interval;
} __attribute__((packed));
#endif /* __linux__ */

#ifdef ANDROID

static int add_ipv6_rule(struct sigma_dut *dut, const char *ifname);

#define ANDROID_KEYSTORE_GET 'g'
#define ANDROID_KEYSTORE_GET_PUBKEY 'b'

static int android_keystore_get(char cmd, const char *key, unsigned char *val)
{
	/* Android 4.3 changed keystore design, so need to use keystore_get() */
#ifndef KEYSTORE_MESSAGE_SIZE
#define KEYSTORE_MESSAGE_SIZE 65535
#endif /* KEYSTORE_MESSAGE_SIZE */

	ssize_t len;
	uint8_t *value = NULL;

	__android_log_print(ANDROID_LOG_DEBUG, "sigma_dut",
			    "keystore command '%c' key '%s' --> keystore_get",
			    cmd, key);

	len = keystore_get(key, strlen(key), &value);
	if (len < 0) {
		__android_log_print(ANDROID_LOG_DEBUG, "sigma_dut",
				    "keystore_get() failed");
		return -1;
	}

	if (len > KEYSTORE_MESSAGE_SIZE)
		len = KEYSTORE_MESSAGE_SIZE;
	memcpy(val, value, len);
	free(value);
	return len;
}
#endif /* ANDROID */


#ifdef NL80211_SUPPORT
static int nl80211_sta_set_power_save(struct sigma_dut *dut,
				      const char *intf,
				      enum nl80211_ps_state ps_state)
{
	struct nl_msg *msg;
	int ifindex, ret;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s not found",
				__func__, intf);
		return -1;
	}

	msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
			      NL80211_CMD_SET_POWER_SAVE);
	if (!msg) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in creating nl80211 msg", __func__);
		return -1;
	}

	if (nla_put_u32(msg, NL80211_ATTR_PS_STATE, ps_state)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in populating nl80211 msg", __func__);
		nlmsg_free(msg);
		return -1;
	}

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d (%s)",
				__func__, ret, strerror(-ret));
		return -1;
	}

	return 0;
}
#endif /* NL80211_SUPPORT */


static int set_power_save_wcn(struct sigma_dut *dut, const char *intf, int ps)
{
	char buf[100];
#ifdef NL80211_SUPPORT
	enum nl80211_ps_state ps_state;

	ps_state = ps == 1 ? NL80211_PS_ENABLED : NL80211_PS_DISABLED;
	if (nl80211_sta_set_power_save(dut, intf, ps_state) == 0)
		return 0;
#endif /* NL80211_SUPPORT */

	snprintf(buf, sizeof(buf), "iwpriv %s setPower %d", intf, ps);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv setPower %d failed", ps);
		return -1;
	}
	return 0;
}


int set_ps(const char *intf, struct sigma_dut *dut, int enabled)
{
#ifdef __linux__
	char buf[100];

	if (wifi_chip_type == DRIVER_WCN) {
		if (enabled) {
			if (set_power_save_wcn(dut, intf, 1) < 0) {
				snprintf(buf, sizeof(buf),
					 "iwpriv wlan0 dump 906");
				if (system(buf) != 0)
					goto set_power_save;
			}
		} else {
			if (set_power_save_wcn(dut, intf, 2) < 0) {
				snprintf(buf, sizeof(buf),
					 "iwpriv wlan0 dump 905");
				if (system(buf) != 0)
					goto set_power_save;
				snprintf(buf, sizeof(buf),
					 "iwpriv wlan0 dump 912");
				if (system(buf) != 0)
					goto set_power_save;
			}
		}

		return 0;
	}

set_power_save:
	snprintf(buf, sizeof(buf), "./iw dev %s set power_save %s",
		 intf, enabled ? "on" : "off");
	if (system(buf) != 0) {
		snprintf(buf, sizeof(buf), "iw dev %s set power_save %s",
			 intf, enabled ? "on" : "off");
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set power save %s",
					enabled ? "on" : "off");
			return -1;
		}
	}

	return 0;
#else /* __linux__ */
	return -1;
#endif /* __linux__ */
}


#ifdef __linux__

static int wil6210_get_debugfs_dir(struct sigma_dut *dut, char *path,
				   size_t len)
{
	DIR *dir, *wil_dir;
	struct dirent *entry;
	int ret = -1;
	const char *root_path = "/sys/kernel/debug/ieee80211";

	dir = opendir(root_path);
	if (!dir)
		return -2;

	while ((entry = readdir(dir))) {
		if (strcmp(entry->d_name, ".") == 0 ||
		    strcmp(entry->d_name, "..") == 0)
			continue;

		if (snprintf(path, len, "%s/%s/wil6210",
			     root_path, entry->d_name) >= (int) len) {
			ret = -3;
			break;
		}

		wil_dir = opendir(path);
		if (wil_dir) {
			closedir(wil_dir);
			ret = 0;
			break;
		}
	}

	closedir(dir);
	return ret;
}


static int wil6210_wmi_send(struct sigma_dut *dut, uint16_t command,
			    void *payload, uint16_t length)
{
	struct {
		struct wil_wmi_header hdr;
		char payload[WIL_WMI_MAX_PAYLOAD];
	} __attribute__((packed)) cmd;
	char buf[128], fname[128];
	size_t towrite, written;
	FILE *f;
	int res;

	if (length > WIL_WMI_MAX_PAYLOAD) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"payload too large(%u, max %u)",
				length, WIL_WMI_MAX_PAYLOAD);
		return -1;
	}

	memset(&cmd.hdr, 0, sizeof(cmd.hdr));
	cmd.hdr.cmd = command;
	memcpy(cmd.payload, payload, length);

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	res = snprintf(fname, sizeof(fname), "%s/wmi_send", buf);
	if (res < 0 || res >= sizeof(fname))
		return -1;
	f = fopen(fname, "wb");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	towrite = sizeof(cmd.hdr) + length;
	written = fwrite(&cmd, 1, towrite, f);
	fclose(f);
	if (written != towrite) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to send wmi %u", command);
		return -1;
	}

	return 0;
}


static int wil6210_get_sta_info_field(struct sigma_dut *dut, const char *bssid,
				      const char *pattern, unsigned int *field)
{
	char buf[128], fname[128];
	FILE *f;
	regex_t re;
	regmatch_t m[2];
	int rc, ret = -1, res;

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	res = snprintf(fname, sizeof(fname), "%s/stations", buf);
	if (res < 0 || res >= sizeof(fname))
		return -1;
	f = fopen(fname, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	if (regcomp(&re, pattern, REG_EXTENDED)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"regcomp failed: %s", pattern);
		goto out;
	}

	/*
	 * find the entry for the mac address
	 * line is of the form: [n] 11:22:33:44:55:66 state AID aid
	 */
	while (fgets(buf, sizeof(buf), f)) {
		if (strcasestr(buf, bssid)) {
			/* extract the field (CID/AID/state) */
			rc = regexec(&re, buf, 2, m, 0);
			if (!rc && (m[1].rm_so >= 0)) {
				buf[m[1].rm_eo] = 0;
				*field = atoi(&buf[m[1].rm_so]);
				ret = 0;
				break;
			}
		}
	}

	regfree(&re);
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"could not extract field");

out:
	fclose(f);

	return ret;
}


static int wil6210_get_cid(struct sigma_dut *dut, const char *bssid,
			   unsigned int *cid)
{
	const char *pattern = "\\[([0-9]+)\\]";

	return wil6210_get_sta_info_field(dut, bssid, pattern, cid);
}


static int wil6210_send_brp_rx(struct sigma_dut *dut, const char *mac,
			       int l_rx)
{
	struct wil_wmi_bf_trig_cmd cmd;
	unsigned int cid;

	memset(&cmd, 0, sizeof(cmd));

	if (wil6210_get_cid(dut, mac, &cid))
		return -1;

	cmd.bf_type = WIL_WMI_BRP_RX;
	cmd.sta_id = cid;
	/* training length (l_rx) is ignored, FW always uses length 16 */
	return wil6210_wmi_send(dut, WIL_WMI_BF_TRIG_CMDID,
				&cmd, sizeof(cmd));
}


static int wil6210_send_sls(struct sigma_dut *dut, const char *mac)
{
	struct wil_wmi_bf_trig_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));

	if (parse_mac_address(dut, mac, (unsigned char *)&cmd.dest_mac))
		return -1;

	cmd.bf_type = WIL_WMI_SLS;
	return wil6210_wmi_send(dut, WIL_WMI_BF_TRIG_CMDID,
				&cmd, sizeof(cmd));
}


int wil6210_set_ese(struct sigma_dut *dut, int count,
		    struct sigma_ese_alloc *allocs)
{
	struct wil_wmi_ese_cfg cmd = { };
	int i;

	if (count == 0 || count > WIL_WMI_MAX_ESE_SLOTS)
		return -1;

	if (dut->ap_bcnint <= 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"invalid beacon interval(%d), check test",
				dut->ap_bcnint);
		return -1;
	}

	cmd.ese_advertisment = WIL_WMI_ADVERTISE_ESE_IN_BEACON;
	cmd.flags = 0x1d;
	cmd.num_allocs = count;
	for (i = 0; i < count; i++) {
		/*
		 * Convert percent from BI (BI specified in milliseconds)
		 * to absolute duration in microseconds.
		 */
		cmd.slots[i].duration =
			(allocs[i].percent_bi * dut->ap_bcnint * 1000) / 100;
		switch (allocs[i].type) {
		case ESE_CBAP:
			cmd.slots[i].slot_type = WIL_WMI_ESE_CBAP;
			break;
		case ESE_SP:
			cmd.slots[i].slot_type = WIL_WMI_ESE_SP;
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"invalid slot type(%d) at index %d",
					allocs[i].type, i);
			return -1;
		}
		cmd.slots[i].src_aid = allocs[i].src_aid;
		cmd.slots[i].dst_aid = allocs[i].dst_aid;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"slot %d, duration %u, type %d, srcAID %u dstAID %u",
				i, cmd.slots[i].duration,
				cmd.slots[i].slot_type, cmd.slots[i].src_aid,
				cmd.slots[i].dst_aid);
	}

	return wil6210_wmi_send(dut, WIL_WMI_ESE_CFG_CMDID, &cmd, sizeof(cmd));
}


int wil6210_set_force_mcs(struct sigma_dut *dut, int force, int mcs)
{
	struct wil_wmi_force_mcs cmd = { };

	cmd.module_id = WIL_WMI_UT_HW_SYSAPI;
	cmd.subtype_id = WIL_WMI_UT_FORCE_MCS;
	cmd.force_enable = (uint32_t) force;
	cmd.mcs = (uint32_t) mcs;

	return wil6210_wmi_send(dut, WIL_WMI_UNIT_TEST_CMDID,
				&cmd, sizeof(cmd));
}


static int wil6210_force_rsn_ie(struct sigma_dut *dut, int state)
{
	struct wil_wmi_force_rsn_ie cmd = { };

	cmd.module_id = WIL_WMI_UT_HW_SYSAPI;
	cmd.subtype_id = WIL_WMI_UT_FORCE_RSN_IE;
	cmd.state = (uint32_t) state;

	return wil6210_wmi_send(dut, WIL_WMI_UNIT_TEST_CMDID,
				&cmd, sizeof(cmd));
}


/*
 * this function is also used to configure generic remain-on-channel
 */
static int wil6210_p2p_cfg(struct sigma_dut *dut, int freq)
{
	struct wil_wmi_p2p_cfg_cmd cmd = { };
	int channel = freq_to_channel(freq);

	if (channel < 0)
		return -1;
	cmd.discovery_mode = WMI_DISCOVERY_MODE_NON_OFFLOAD;
	cmd.channel = channel - 1;
	cmd.bcon_interval = WIL_DEFAULT_BI;
	cmd.discovery_mode = WMI_DISCOVERY_MODE_PEER2PEER;

	return wil6210_wmi_send(dut, WIL_WMI_P2P_CFG_CMDID,
				&cmd, sizeof(cmd));
}


static int wil6210_remain_on_channel(struct sigma_dut *dut, int freq)
{
	int ret = wil6210_p2p_cfg(dut, freq);

	if (ret)
		return ret;

	ret = wil6210_wmi_send(dut, WIL_WMI_START_LISTEN_CMDID, NULL, 0);
	if (!ret) {
		/*
		 * wait a bit to allow FW to setup the radio
		 * especially important if we switch channels
		 */
		usleep(500000);
	}

	return ret;
}


static int wil6210_stop_discovery(struct sigma_dut *dut)
{
	return wil6210_wmi_send(dut, WIL_WMI_DISCOVERY_STOP_CMDID, NULL, 0);
}


static int wil6210_transmit_frame(struct sigma_dut *dut, int freq,
				  int wait_duration,
				  const char *frame, size_t frame_len)
{
	char buf[128], fname[128];
	FILE *f;
	int res = 0, r;
	size_t written;

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}
	r = snprintf(fname, sizeof(fname), "%s/tx_mgmt", buf);
	if (r < 0 || r >= sizeof(fname))
		return -1;

	if (wil6210_remain_on_channel(dut, freq)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to listen on channel");
		return -1;
	}

	f = fopen(fname, "wb");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		res = -1;
		goto out_stop;
	}
	written = fwrite(frame, 1, frame_len, f);
	fclose(f);

	if (written != frame_len) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to transmit frame (got %zd, expected %zd)",
				written, frame_len);
		res = -1;
		goto out_stop;
	}

	usleep(wait_duration * 1000);

out_stop:
	wil6210_stop_discovery(dut);
	return res;
}


static int find_template_frame_tag(struct template_frame_tag *tags,
				   int total_tags, int tag_num)
{
	int i;

	for (i = 0; i < total_tags; i++) {
		if (tag_num == tags[i].num)
			return i;
	}

	return -1;
}


static int replace_p2p_attribute(struct sigma_dut *dut, char *buf, size_t len,
				 int id, const char *value, size_t val_len)
{
	struct wfa_p2p_attribute *attr = (struct wfa_p2p_attribute *) buf;

	if (len < 3 + val_len) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"not enough space to replace P2P attribute");
		return -1;
	}

	if (attr->len != val_len) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"attribute length mismatch (need %zu have %hu)",
				val_len, attr->len);
		return -1;
	}

	if (attr->id != id) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"incorrect attribute id (expected %d actual %d)",
				id, attr->id);
		return -1;
	}

	memcpy(attr->variable, value, val_len);

	return 0;
}


static int parse_template_frame_file(struct sigma_dut *dut, const char *fname,
				     char *buf, size_t *length,
				     struct template_frame_tag *tags,
				     size_t *num_tags)
{
	char line[512];
	FILE *f;
	size_t offset = 0, tag_index = 0;
	int num, index;
	int in_tag = 0, tag_num = 0, tag_offset = 0;

	if (*length < sizeof(struct ieee80211_hdr_3addr)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"supplied buffer is too small");
		return -1;
	}

	f = fopen(fname, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open template file %s", fname);
		return -1;
	}

	/*
	 * template file format: lines beginning with # are comments and
	 * ignored.
	 * It is possible to tag bytes in the frame to make it easy
	 * to replace fields in the template, espcially if they appear
	 * in variable-sized sections (such as IEs)
	 * This is done by a line beginning with $NUM where NUM is an integer
	 * tag number. It can be followed by space(s) and comment.
	 * The next line is considered the tagged bytes. The parser will fill
	 * the tag number, offset and length of the tagged bytes.
	 * rest of the lines contain frame bytes as sequence of hex digits,
	 * 2 digits for each byte. Spaces are allowed between bytes.
	 * On bytes lines only hex digits and spaces are allowed
	 */
	while (!feof(f)) {
		if (!fgets(line, sizeof(line), f))
			break;
		index = 0;
		while (isspace((unsigned char) line[index]))
			index++;
		if (!line[index] || line[index] == '#')
			continue;
		if (line[index] == '$') {
			if (tags) {
				index++;
				tag_num = strtol(&line[index], NULL, 0);
				tag_offset = offset;
				in_tag = 1;
			}
			continue;
		}
		while (line[index]) {
			if (isspace((unsigned char) line[index])) {
				index++;
				continue;
			}
			num = hex_byte(&line[index]);
			if (num < 0)
				break;
			buf[offset++] = num;
			if (offset == *length)
				goto out;
			index += 2;
		}

		if (in_tag) {
			if (tag_index < *num_tags) {
				tags[tag_index].num = tag_num;
				tags[tag_index].offset = tag_offset;
				tags[tag_index].len = offset - tag_offset;
				tag_index++;
			} else {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"too many tags, tag ignored");
			}
			in_tag = 0;
		}
	}

	if (num_tags)
		*num_tags = tag_index;
out:
	fclose(f);
	if (offset < sizeof(struct ieee80211_hdr_3addr)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"template frame is too small");
		return -1;
	}

	*length = offset;
	return 0;
}

#endif /* __linux__ */


static void static_ip_file(int proto, const char *addr, const char *mask,
			   const char *gw)
{
	if (proto) {
		FILE *f = fopen("static-ip", "w");
		if (f) {
			fprintf(f, "%d %s %s %s\n", proto, addr,
				mask ? mask : "N/A",
				gw ? gw : "N/A");
			fclose(f);
		}
	} else {
		unlink("static-ip");
	}
}


static int send_neighbor_request(struct sigma_dut *dut, const char *intf,
				 const char *ssid)
{
	char buf[100];
	int ret = 0;

	/* Use wpa_supplicant to send neighbor report request */
	snprintf(buf, sizeof(buf), "NEIGHBOR_REP_REQUEST ssid=\"%s\"",
		 ssid);
	ret = wpa_command(intf, buf);
	if (ret == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Neighbor report request sent through wpa_command");
		return 0;
	}

#ifdef __linux__
	sigma_dut_print(dut, DUT_MSG_INFO,
			"wpa_command failed, ret:%d; fall back to iwpriv", ret);

	snprintf(buf, sizeof(buf), "iwpriv %s neighbor %s",
		 intf, ssid);
	sigma_dut_print(dut, DUT_MSG_INFO, "Request: %s", buf);

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv neighbor request failed");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_INFO, "iwpriv neighbor request send");

	return 0;
#else /* __linux__ */
	return -1;
#endif /* __linux__ */
}


static int send_trans_mgmt_query(struct sigma_dut *dut, const char *intf,
				 struct sigma_cmd *cmd)
{
	const char *val;
	int reason_code = 0;
	char buf[1024];

	/*
	 * In the earlier builds we used WNM_QUERY and in later
	 * builds used WNM_BSS_QUERY.
	 */

	val = get_param(cmd, "BTMQuery_Reason_Code");
	if (val)
		reason_code = atoi(val);

	val = get_param(cmd, "Cand_List");
	if (val && atoi(val) == 1 && dut->btm_query_cand_list) {
		snprintf(buf, sizeof(buf), "WNM_BSS_QUERY %d%s", reason_code,
			 dut->btm_query_cand_list);
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
	} else {
		snprintf(buf, sizeof(buf), "WNM_BSS_QUERY %d", reason_code);
	}

	if (wpa_command(intf, buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"transition management query failed");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"transition management query sent");

	return 0;
}


int is_ip_addr(const char *str)
{
	const char *pos = str;
	struct in_addr addr;

	while (*pos) {
		if (*pos != '.' && (*pos < '0' || *pos > '9'))
			return 0;
		pos++;
	}

	return inet_aton(str, &addr);
}


int get_ip_config(struct sigma_dut *dut, const char *ifname, char *buf,
		  size_t buf_len)
{
	char tmp[256];
	char ip[16], mask[15], dns[16], sec_dns[16];
	int is_dhcp = 0;
	int s;
#ifdef ANDROID
	char prop[PROPERTY_VALUE_MAX];
#else /* ANDROID */
	FILE *f;
#ifdef __linux__
	const char *str_ps;
#endif /* __linux__ */
#endif /* ANDROID */

	ip[0] = '\0';
	mask[0] = '\0';
	dns[0] = '\0';
	sec_dns[0] = '\0';

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s >= 0) {
		struct ifreq ifr;
		struct sockaddr_in saddr;

		memset(&ifr, 0, sizeof(ifr));
		strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
		if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to get "
					"%s IP address: %s",
					ifname, strerror(errno));
		} else {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strlcpy(ip, inet_ntoa(saddr.sin_addr), sizeof(ip));
		}

		if (ioctl(s, SIOCGIFNETMASK, &ifr) == 0) {
			memcpy(&saddr, &ifr.ifr_addr,
			       sizeof(struct sockaddr_in));
			strlcpy(mask, inet_ntoa(saddr.sin_addr), sizeof(mask));
		}
		close(s);
	}

#ifdef ANDROID
	snprintf(tmp, sizeof(tmp), "dhcp.%s.pid", ifname);
	if (property_get(tmp, prop, NULL) != 0 && atoi(prop) > 0) {
		snprintf(tmp, sizeof(tmp), "dhcp.%s.result", ifname);
		if (property_get(tmp, prop, NULL) != 0 &&
		    strcmp(prop, "ok") == 0) {
			snprintf(tmp, sizeof(tmp), "dhcp.%s.ipaddress",
				 ifname);
			if (property_get(tmp, prop, NULL) != 0 &&
			    strcmp(ip, prop) == 0)
				is_dhcp = 1;
		}
	}

	snprintf(tmp, sizeof(tmp), "dhcp.%s.dns1", ifname);
	if (property_get(tmp, prop, NULL) != 0)
		strlcpy(dns, prop, sizeof(dns));
	else if (property_get("net.dns1", prop, NULL) != 0)
		strlcpy(dns, prop, sizeof(dns));

	snprintf(tmp, sizeof(tmp), "dhcp.%s.dns2", ifname);
	if (property_get(tmp, prop, NULL) != 0)
		strlcpy(sec_dns, prop, sizeof(sec_dns));
#else /* ANDROID */
#ifdef __linux__
	if (get_driver_type(dut) == DRIVER_OPENWRT)
		str_ps = "ps -w";
	else
		str_ps = "ps ax";
	snprintf(tmp, sizeof(tmp),
		 "%s | grep dhclient | grep -v grep | grep -q %s",
		 str_ps, ifname);
	if (system(tmp) == 0)
		is_dhcp = 1;
	else {
		snprintf(tmp, sizeof(tmp),
			 "%s | grep udhcpc | grep -v grep | grep -q %s",
			 str_ps, ifname);
		if (system(tmp) == 0)
			is_dhcp = 1;
		else {
			snprintf(tmp, sizeof(tmp),
				 "%s | grep dhcpcd | grep -v grep | grep -q %s",
				 str_ps, ifname);
			if (system(tmp) == 0)
				is_dhcp = 1;
		}
	}
#endif /* __linux__ */

	f = fopen("/etc/resolv.conf", "r");
	if (f) {
		char *pos, *pos2;

		while (fgets(tmp, sizeof(tmp), f)) {
			if (strncmp(tmp, "nameserver", 10) != 0)
				continue;
			pos = tmp + 10;
			while (*pos == ' ' || *pos == '\t')
				pos++;
			pos2 = pos;
			while (*pos2) {
				if (*pos2 == '\n' || *pos2 == '\r') {
					*pos2 = '\0';
					break;
				}
				pos2++;
			}
			if (!dns[0])
				strlcpy(dns, pos, sizeof(dns));
			else if (!sec_dns[0])
				strlcpy(sec_dns, pos, sizeof(sec_dns));
		}
		fclose(f);
	}
#endif /* ANDROID */

	snprintf(buf, buf_len, "dhcp,%d,ip,%s,mask,%s,primary-dns,%s",
		 is_dhcp, ip, mask, dns);
	buf[buf_len - 1] = '\0';

	return 0;
}




int get_ipv6_config(struct sigma_dut *dut, const char *ifname, char *buf,
		    size_t buf_len)
{
#ifdef __linux__
#ifdef ANDROID
	char cmd[200], result[1000], *pos, *end;
	FILE *f;
	size_t len;

	snprintf(cmd, sizeof(cmd), "ip addr show dev %s scope global", ifname);
	f = popen(cmd, "r");
	if (f == NULL)
		return -1;
	len = fread(result, 1, sizeof(result) - 1, f);
	pclose(f);
	if (len == 0)
		return -1;
	result[len] = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "%s result: %s\n", cmd, result);

	pos = strstr(result, "inet6 ");
	if (pos == NULL)
		return -1;
	pos += 6;
	end = strchr(pos, ' ');
	if (end)
		*end = '\0';
	end = strchr(pos, '/');
	if (end)
		*end = '\0';
	snprintf(buf, buf_len, "ip,%s", pos);
	buf[buf_len - 1] = '\0';
	return 0;
#else /* ANDROID */
	struct ifaddrs *ifaddr, *ifa;
	int res, found = 0;
	char host[NI_MAXHOST];

	if (getifaddrs(&ifaddr) < 0) {
		perror("getifaddrs");
		return -1;
	}

	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (strcasecmp(ifname, ifa->ifa_name) != 0)
			continue;
		if (ifa->ifa_addr == NULL ||
		    ifa->ifa_addr->sa_family != AF_INET6)
			continue;

		res = getnameinfo(ifa->ifa_addr, sizeof(struct sockaddr_in6),
				  host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
		if (res != 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "getnameinfo: %s",
					gai_strerror(res));
			continue;
		}
		if (strncmp(host, "fe80::", 6) == 0)
			continue; /* skip link-local */

		sigma_dut_print(dut, DUT_MSG_DEBUG, "ifaddr: %s", host);
		found = 1;
		break;
	}

	freeifaddrs(ifaddr);

	if (found) {
		char *pos;
		pos = strchr(host, '%');
		if (pos)
			*pos = '\0';
		snprintf(buf, buf_len, "ip,%s", host);
		buf[buf_len - 1] = '\0';
		return 0;
	}

#endif /* ANDROID */
#endif /* __linux__ */
	return -1;
}


static enum sigma_cmd_result cmd_sta_get_ip_config(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
	const char *val;
	int type = 1;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	/*
	 * UCC may assume the IP address to be available immediately after
	 * association without trying to run sta_get_ip_config multiple times.
	 * Sigma CAPI does not specify this command as a block command that
	 * would wait for the address to become available, but to pass tests
	 * more reliably, it looks like such a wait may be needed here.
	 */
	if (wait_ip_addr(dut, ifname, 15) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Could not get IP address "
				"for sta_get_ip_config");
		/*
		 * Try to continue anyway since many UCC tests do not really
		 * care about the return value from here..
		 */
	}

	val = get_param(cmd, "Type");
	if (val)
		type = atoi(val);
	if (type == 2 || dut->last_set_ip_config_ipv6) {
		int i;

		/*
		 * Since we do not have proper wait for IPv6 addresses, use a
		 * fixed two second delay here as a workaround for UCC script
		 * assuming IPv6 address is available when this command returns.
		 * Some scripts did not use Type,2 properly for IPv6, so include
		 * also the cases where the previous sta_set_ip_config indicated
		 * use of IPv6.
		 */
		sigma_dut_print(dut, DUT_MSG_INFO, "Wait up to extra ten seconds in sta_get_ip_config for IPv6 address");
		for (i = 0; i < 10; i++) {
			sleep(1);
			if (get_ipv6_config(dut, ifname, buf, sizeof(buf)) == 0)
			{
				sigma_dut_print(dut, DUT_MSG_INFO, "Found IPv6 address");
				send_resp(dut, conn, SIGMA_COMPLETE, buf);
#ifdef ANDROID
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Adding IPv6 rule on Android");
				add_ipv6_rule(dut, intf);
#endif /* ANDROID */

				return 0;
			}
		}
	}
	if (type == 1) {
		if (get_ip_config(dut, ifname, buf, sizeof(buf)) < 0)
			return -2;
	} else if (type == 2) {
		if (get_ipv6_config(dut, ifname, buf, sizeof(buf)) < 0)
			return -2;
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported address type");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static void kill_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char buf[200];
	char path[128];
	struct stat s;

#ifdef ANDROID
	snprintf(path, sizeof(path), "/data/misc/dhcp/dhcpcd-%s.pid", ifname);
#else /* ANDROID */
	snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid", ifname);
#endif /* ANDROID */
	if (stat(path, &s) == 0) {
		snprintf(buf, sizeof(buf), "kill `cat %s`", path);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Kill previous DHCP client: %s", buf);
		if (system(buf) != 0)
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to kill DHCP client");
		unlink(path);
		sleep(1);
	} else {
		snprintf(path, sizeof(path), "/var/run/dhcpcd-%s.pid", ifname);

		if (stat(path, &s) == 0) {
			snprintf(buf, sizeof(buf), "kill `cat %s`", path);
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Kill previous DHCP client: %s", buf);
			if (system(buf) != 0)
				sigma_dut_print(dut, DUT_MSG_INFO,
						"Failed to kill DHCP client");
			unlink(path);
			sleep(1);
		}
	}

	dut->dhcp_client_running = 0;
#endif /* __linux__ */
}


/* Get DHCP client path. e.g. dhclient -> /sbin/dhclient
 *
 * name: input, DHCP client name
 * buf: Output, DHCP client full path name
 * buf_size: Input, maximum size of the output buffer
 * Returns 0 for success, -1 if not found
 */
static int get_dhcp_client_path(const char *name, char *buf, size_t buf_size)
{
	char *path[] = {
#ifdef ANDROID
		"/system/bin",
		"/vendor/bin"
#else /* ANDROID */
		"/bin",
		"/sbin",
		"/usr/bin",
		"/usr/sbin"
#endif /* ANDROID */
	};
	int i, num = sizeof(path) / sizeof(path[0]);

	for (i = 0; i < num; i++) {
		snprintf(buf, buf_size, "%s/%s", path[i], name);
		if (access(buf, F_OK) != -1)
			return 0;
	}

	/* not found */
	return -1;
}


static int start_dhcp_client(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char fpath[128];
	char buf[200];

#ifdef ANDROID
	if (get_dhcp_client_path("dhcpcd", fpath, sizeof(fpath)) == 0) {
		snprintf(buf, sizeof(buf), "%s -b %s", fpath, ifname);
	} else if (get_dhcp_client_path("dhcptool", fpath, sizeof(fpath)) ==
		   0) {
		snprintf(buf, sizeof(buf), "%s %s &", fpath, ifname);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"DHCP client program missing");
		return 0;
	}
#else /* ANDROID */
	if (get_dhcp_client_path("dhclient", fpath, sizeof(fpath)) == 0) {
		snprintf(buf, sizeof(buf),
			 "%s -nw -pf /var/run/dhclient-%s.pid %s",
			 fpath, ifname, ifname);
	} else if (get_dhcp_client_path("dhcpcd", fpath, sizeof(fpath)) == 0) {
		snprintf(buf, sizeof(buf), "%s -t 0 %s &", fpath, ifname);
	} else if (get_dhcp_client_path("udhcpc", fpath, sizeof(fpath)) == 0) {
		snprintf(buf, sizeof(buf),
			 "%s -i %s -p /var/run/dhclient-%s.pid -b",
			 fpath, ifname, ifname);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"DHCP client program missing");
		return 0;
	}
#endif /* ANDROID */
	sigma_dut_print(dut, DUT_MSG_INFO, "Start DHCP client: %s", buf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to start DHCP client");
#ifndef ANDROID
		return -1;
#endif /* ANDROID */
	}

	dut->dhcp_client_running = 1;
#endif /* __linux__ */

	return 0;
}


static int clear_ip_addr(struct sigma_dut *dut, const char *ifname)
{
#ifdef __linux__
	char buf[200];

	snprintf(buf, sizeof(buf), "ip addr flush dev %s", ifname);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to clear IP addresses");
		return -1;
	}
#endif /* __linux__ */

	return 0;
}


#ifdef ANDROID
static int add_ipv6_rule(struct sigma_dut *dut, const char *ifname)
{
	char cmd[200], *result, *pos;
	FILE *fp;
	int tableid;
	size_t len, result_len = 1000;

	snprintf(cmd, sizeof(cmd), "ip -6 route list table all | grep %s",
		 ifname);
	fp = popen(cmd, "r");
	if (fp == NULL)
		return -1;

	result = malloc(result_len);
	if (result == NULL) {
		fclose(fp);
		return -1;
	}

	len = fread(result, 1, result_len - 1, fp);
	fclose(fp);

	if (len == 0) {
		free(result);
		return -1;
	}
	result[len] = '\0';

	pos = strstr(result, "table ");
	if (pos == NULL) {
		free(result);
		return -1;
	}

	pos += strlen("table ");
	tableid = atoi(pos);
	if (tableid != 0) {
		if (system("ip -6 rule del prio 22000") != 0) {
			/* ignore any error */
		}
		snprintf(cmd, sizeof(cmd),
			 "ip -6 rule add from all lookup %d prio 22000",
			 tableid);
		if (system(cmd) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to run %s", cmd);
			free(result);
			return -1;
		}
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"No Valid Table Id found %s", pos);
		free(result);
		return -1;
	}
	free(result);

	return 0;
}
#endif /* ANDROID */


int set_ipv4_addr(struct sigma_dut *dut, const char *ifname,
		  const char *ip, const char *mask)
{
	char buf[200];

	snprintf(buf, sizeof(buf), "ifconfig %s %s netmask %s",
		 ifname, ip, mask);
	return system(buf) == 0;
}


int set_ipv4_gw(struct sigma_dut *dut, const char *gw)
{
	char buf[200];

	if (!is_ip_addr(gw)) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Invalid gw addr - %s", gw);
		return -1;
	}

	snprintf(buf, sizeof(buf), "route add default gw %s", gw);
	if (!dut->no_ip_addr_set && system(buf) != 0) {
		snprintf(buf, sizeof(buf), "ip ro re default via %s",
			 gw);
		if (system(buf) != 0)
			return 0;
	}

	return 1;
}


static void enable_sta_ipv6_configuration(struct sigma_dut *dut,
					      const char *ifname,
					      char *buf, size_t buf_size)
{
#if defined(ANDROID)
	snprintf(buf, buf_size,
		 "sysctl net.ipv6.conf.%s.disable_ipv6=0",
		 ifname);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Failed to enable IPv6 address");
	}
#elif defined(LINUX_EMBEDDED)
	snprintf(buf, buf_size,
		"echo 2 > /proc/sys/net/ipv6/conf/%s/accept_ra",
		ifname);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Failed to set accept_ra for IPv6 address");
	}
#else
	/* No configuration changes needed */
#endif
}


static enum sigma_cmd_result cmd_sta_set_ip_config(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	char buf[200];
	const char *val, *ip, *mask, *gw;
	int type = 1;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	if (if_nametoindex(ifname) == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown interface");
		return 0;
	}

	val = get_param(cmd, "Type");
	if (val) {
		type = atoi(val);
		if (type < 1 || type > 3) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported address type");
			return 0;
		}
	}

	dut->last_set_ip_config_ipv6 = 0;

	val = get_param(cmd, "dhcp");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "true") == 0)) {
		static_ip_file(0, NULL, NULL, NULL);
#ifdef __linux__
		if (type == 2) {
			dut->last_set_ip_config_ipv6 = 1;
			sigma_dut_print(dut, DUT_MSG_INFO, "Using IPv6 "
					"stateless address autoconfiguration");

			enable_sta_ipv6_configuration(dut, ifname, buf,
						      sizeof(buf));
#ifdef ANDROID
			/*
			 * This sleep is required as the assignment in case of
			 * Android is taking time and is done by the kernel.
			 * The subsequent ping for IPv6 is impacting HS20 test
			 * case.
			 */
			sleep(2);
			add_ipv6_rule(dut, intf);
#endif /* ANDROID */
			/* Assume this happens by default */
			return 1;
		}
		if (type != 3) {
			kill_dhcp_client(dut, ifname);
			if (start_dhcp_client(dut, ifname) < 0)
				return -2;
		} else {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Using FILS HLP DHCPv4 Rapid Commit");
		}

		return 1;
#endif /* __linux__ */
		return -2;
	}

	ip = get_param(cmd, "ip");
	if (!ip) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing IP address");
		return 0;
	}

	mask = get_param(cmd, "mask");
	if (!mask) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing subnet mask");
		return 0;
	}

	if (type == 2) {
		int net = atoi(mask);

		if ((net < 0 && net > 64) || !is_ipv6_addr(ip))
			return -1;

		if (dut->no_ip_addr_set) {
			snprintf(buf, sizeof(buf),
				 "sysctl net.ipv6.conf.%s.disable_ipv6=1",
				 ifname);
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Failed to disable IPv6 address before association");
			}
		} else {
			if (set_ipv6_addr(dut, ip, mask, ifname) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set IPv6 address");
				return 0;
			}
		}

		dut->last_set_ip_config_ipv6 = 1;
		static_ip_file(6, ip, mask, NULL);
		return 1;
	} else if (type == 1) {
		if (!is_ip_addr(ip) || !is_ip_addr(mask))
			return -1;
	}

	kill_dhcp_client(dut, ifname);

	if (!dut->no_ip_addr_set) {
		if (!set_ipv4_addr(dut, ifname, ip, mask)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set IP address");
			return 0;
		}
	}

	gw = get_param(cmd, "defaultGateway");
	if (gw) {
		if (set_ipv4_gw(dut, gw) < 1) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set default gateway");
			return 0;
		}
	}

	val = get_param(cmd, "primary-dns");
	if (val) {
#ifdef ANDROID
		char dns_cmd[200];
		int len;
		char dnsmasq[100];

		kill_pid(dut, concat_sigma_tmpdir(dut, "/sigma_dut-dnsmasq.pid",
						  dnsmasq, sizeof(dnsmasq)));

		len = snprintf(dns_cmd, sizeof(dns_cmd),
			       "/system/bin/dnsmasq -uroot --no-resolv -S%s -x/%s", val,
			       dnsmasq);
		if (len < 0 || len >= sizeof(dns_cmd))
			return ERROR_SEND_STATUS;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Running %s", dns_cmd);
		if (system(dns_cmd) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set primary-dns");
			return STATUS_SENT_ERROR;
		}
#else /* ANDROID */
		char dns_cmd[200];
		int len;

		if (system("sed -i '/nameserver/d' /etc/resolv.conf") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear nameserver entries in /etc/resolv.conf");
			return ERROR_SEND_STATUS;
		}

		len = snprintf(dns_cmd, sizeof(dns_cmd),
			       "sed -i '1 i nameserver %s' /etc/resolv.conf", val);
		if (len < 0 || len >= sizeof(dns_cmd))
			return ERROR_SEND_STATUS;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Running %s", dns_cmd);
		if (system(dns_cmd) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set primary-dns");
			return STATUS_SENT_ERROR;
		}
#endif /* ANDROID */
	}

	val = get_param(cmd, "secondary-dns");
	if (val) {
		/* TODO */
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignored secondary-dns %s "
				"setting", val);
	}

	static_ip_file(4, ip, mask, gw);

	return 1;
}


static enum sigma_cmd_result cmd_sta_get_info(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	/* TODO: could report more details here */
	send_resp(dut, conn, SIGMA_COMPLETE, "vendor,Atheros");
	return 0;
}


static enum sigma_cmd_result cmd_sta_get_mac_address(struct sigma_dut *dut,
						     struct sigma_conn *conn,
						     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char addr[20], resp[50];
	const char *ap_link_mac = get_param(cmd, "AP_Link_MAC");

	if (dut->dev_role == DEVROLE_STA_CFON)
		return sta_cfon_get_mac_address(dut, conn, cmd);

	start_sta_mode(dut);
	if (ap_link_mac && strcasecmp(ap_link_mac, "mld") != 0) {
		if (get_mlo_link_mac_ap_link(dut, get_station_ifname(dut),
					     ap_link_mac, addr,
					     sizeof(addr)) < 0)
			return -2;
	} else {
		if (get_wpa_status(get_station_ifname(dut), "address",
				   addr, sizeof(addr)) < 0)
			return -2;
	}

	snprintf(resp, sizeof(resp), "mac,%s", addr);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static enum sigma_cmd_result cmd_sta_is_connected(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	int connected = 0;
	char result[32];
	if (get_wpa_status(get_station_ifname(dut), "wpa_state", result,
			   sizeof(result)) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Could not get interface %s status",
				get_station_ifname(dut));
		return -2;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "wpa_state=%s", result);
	if (strncmp(result, "COMPLETED", 9) == 0)
		connected = 1;

	if (connected)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");

	return 0;
}


static enum sigma_cmd_result
cmd_sta_verify_ip_connection(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *dst, *timeout;
	int wait_time = 90;
	char buf[100];
	int res;

	dst = get_param(cmd, "destination");
	if (dst == NULL || !is_ip_addr(dst))
		return -1;

	timeout = get_param(cmd, "timeout");
	if (timeout) {
		wait_time = atoi(timeout);
		if (wait_time < 1)
			wait_time = 1;
	}

	/* TODO: force renewal of IP lease if DHCP is enabled */

	snprintf(buf, sizeof(buf), "ping %s -c 3 -W %d", dst, wait_time);
	res = system(buf);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "ping returned: %d", res);
	if (res == 0)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,1");
	else if (res == 256)
		send_resp(dut, conn, SIGMA_COMPLETE, "connected,0");
	else
		return -2;

	return 0;
}


static enum sigma_cmd_result cmd_sta_get_bssid(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	char bssid[20], resp[50];

	if (get_wpa_status(get_station_ifname(dut), "bssid",
			   bssid, sizeof(bssid)) < 0)
		strlcpy(bssid, "00:00:00:00:00:00", sizeof(bssid));

	snprintf(resp, sizeof(resp), "bssid,%s", bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


#ifdef __SAMSUNG__
static int add_use_network(const char *ifname)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "USE_NETWORK ON");
	wpa_command(ifname, buf);
	return 0;
}
#endif /* __SAMSUNG__ */


static int add_network_common(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *ifname, struct sigma_cmd *cmd)
{
	const char *ssid = get_param(cmd, "ssid");
	int id;
	const char *val;

	if (ssid == NULL)
		return -1;

	start_sta_mode(dut);

#ifdef __SAMSUNG__
	add_use_network(ifname);
#endif /* __SAMSUNG__ */

	id = add_network(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding network %d", id);

	if (set_network_quoted(ifname, id, "ssid", ssid) < 0)
		return -2;

	dut->infra_network_id = id;
	snprintf(dut->infra_ssid, sizeof(dut->infra_ssid), "%s", ssid);

	val = get_param(cmd, "program");
	if (!val)
		val = get_param(cmd, "prog");
	if (val && strcasecmp(val, "hs2") == 0) {
		char buf[100];
		snprintf(buf, sizeof(buf), "ENABLE_NETWORK %d no-connect", id);
		wpa_command(ifname, buf);

		val = get_param(cmd, "prefer");
		if (val && atoi(val) > 0)
			set_network(ifname, id, "priority", "1");
	}

	return id;
}


static enum sigma_cmd_result cmd_sta_set_encryption(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ssid = get_param(cmd, "ssid");
	const char *type = get_param(cmd, "encpType");
	const char *ifname;
	char buf[200];
	int id;

	if (intf == NULL || ssid == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
		return -2;

	if (type && strcasecmp(type, "wep") == 0) {
		const char *val;
		int i;

		val = get_param(cmd, "activeKey");
		if (val) {
			int keyid;
			keyid = atoi(val);
			if (keyid < 1 || keyid > 4)
				return -1;
			snprintf(buf, sizeof(buf), "%d", keyid - 1);
			if (set_network(ifname, id, "wep_tx_keyidx", buf) < 0)
				return -2;
		}

		for (i = 0; i < 4; i++) {
			snprintf(buf, sizeof(buf), "key%d", i + 1);
			val = get_param(cmd, buf);
			if (val == NULL)
				continue;
			snprintf(buf, sizeof(buf), "wep_key%d", i);
			if (set_network(ifname, id, buf, val) < 0)
				return -2;
		}
	}

	return 1;
}


static int set_akm_suites(struct sigma_dut *dut, const char *ifname,
			  int id, const char *val)
{
	char key_mgmt[200], *end, *pos;
	const char *in_pos = val;

	dut->akm_values = 0;
	pos = key_mgmt;
	end = pos + sizeof(key_mgmt);
	while (*in_pos) {
		int res, akm = atoi(in_pos);
		const char *str;

		if (akm >= 0 && akm < 32)
			dut->akm_values |= 1 << akm;

		switch (akm) {
		case AKM_WPA_EAP:
			str = "WPA-EAP";
			break;
		case AKM_WPA_PSK:
			str = "WPA-PSK";
			break;
		case AKM_FT_EAP:
			str = "FT-EAP";
			break;
		case AKM_FT_PSK:
			str = "FT-PSK";
			break;
		case AKM_EAP_SHA256:
			str = "WPA-EAP-SHA256";
			break;
		case AKM_PSK_SHA256:
			str = "WPA-PSK-SHA256";
			break;
		case AKM_SAE:
			str = "SAE";
			break;
		case AKM_FT_SAE:
			str = "FT-SAE";
			break;
		case AKM_SUITE_B:
			str = "WPA-EAP-SUITE-B-192";
			break;
		case AKM_FT_SUITE_B:
			str = "FT-EAP-SHA384";
			break;
		case AKM_FILS_SHA256:
			str = "FILS-SHA256";
			break;
		case AKM_FILS_SHA384:
			str = "FILS-SHA384";
			break;
		case AKM_FT_FILS_SHA256:
			str = "FT-FILS-SHA256";
			break;
		case AKM_FT_FILS_SHA384:
			str = "FT-FILS-SHA384";
			break;
		case AKM_SAE_EXT_KEY:
			str = "SAE-EXT-KEY";
			break;
		case AKM_FT_SAE_EXT_KEY:
			str = "FT-SAE-EXT-KEY";
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported AKMSuitetype %d", akm);
			return -1;
		}

		res = snprintf(pos, end - pos, "%s%s",
			       pos == key_mgmt ? "" : " ", str);
		if (res < 0 || res >= end - pos)
			return -1;
		pos += res;

		in_pos = strchr(in_pos, ';');
		if (!in_pos)
			break;
		while (*in_pos == ';')
			in_pos++;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "AKMSuiteType %s --> %s",
			val, key_mgmt);
	return set_network(ifname, id, "key_mgmt", key_mgmt);
}


static int set_wpa_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val;
	int id;
	int cipher_set = 0;
	int owe;
	int suite_b = 0;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "Type");
	owe = val && strcasecmp(val, "OWE") == 0;

	val = get_param(cmd, "keyMgmtType");
	if (!val && owe)
		val = "OWE";
	if (val == NULL) {
		/* keyMgmtType is being replaced with AKMSuiteType, so ignore
		 * this missing parameter and assume proto=WPA2. */
		if (set_network(ifname, id, "proto", "WPA2") < 0)
			return ERROR_SEND_STATUS;
	} else if (strcasecmp(val, "wpa") == 0 ||
		   strcasecmp(val, "wpa-psk") == 0) {
		if (set_network(ifname, id, "proto", "WPA") < 0)
			return -2;
	} else if (strcasecmp(val, "wpa2") == 0 ||
		   strcasecmp(val, "wpa2-psk") == 0 ||
		   strcasecmp(val, "wpa2-ft") == 0 ||
		   strcasecmp(val, "wpa2-sha256") == 0) {
		if (set_network(ifname, id, "proto", "WPA2") < 0)
			return -2;
	} else if (strcasecmp(val, "wpa2-wpa-psk") == 0 ||
		   strcasecmp(val, "wpa2-wpa-ent") == 0) {
		if (set_network(ifname, id, "proto", "WPA WPA2") < 0)
			return -2;
	} else if (strcasecmp(val, "SuiteB") == 0) {
		suite_b = 1;
		if (set_network(ifname, id, "proto", "WPA2") < 0)
			return -2;
	} else if (strcasecmp(val, "OWE") == 0) {
	} else if (strcasecmp(val, "WPA3") == 0) {
		if (set_network(ifname, id, "proto", "RSN") < 0)
			return -2;
	} else {
		send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unrecognized keyMgmtType value");
		return 0;
	}

	val = get_param(cmd, "encpType");
	if (val) {
		cipher_set = 1;
		if (strcasecmp(val, "tkip") == 0) {
			if (set_network(ifname, id, "pairwise", "TKIP") < 0)
				return -2;
		} else if (strcasecmp(val, "aes-ccmp") == 0) {
			if (set_network(ifname, id, "pairwise", "CCMP") < 0)
				return -2;
		} else if (strcasecmp(val, "aes-ccmp-tkip") == 0) {
			if (set_network(ifname, id, "pairwise",
					"CCMP TKIP") < 0)
				return -2;
		} else if (strcasecmp(val, "aes-gcmp") == 0) {
			if (set_network(ifname, id, "pairwise", "GCMP") < 0)
				return -2;
			if (set_network(ifname, id, "group", "GCMP") < 0)
				return -2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unrecognized encpType value");
			return 0;
		}
	}

	val = get_param(cmd, "PairwiseCipher");
	if (val) {
		cipher_set = 1;
		/* TODO: Support space separated list */
		if (strcasecmp(val, "AES-GCMP-256") == 0) {
			if (set_network(ifname, id, "pairwise", "GCMP-256") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-CCMP-256") == 0) {
			if (set_network(ifname, id, "pairwise",
					"CCMP-256") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-GCMP-128") == 0) {
			if (set_network(ifname, id, "pairwise",	"GCMP") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-CCMP-128") == 0) {
			if (set_network(ifname, id, "pairwise",	"CCMP") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-CCMP-128 AES-GCMP-256") == 0 ||
			   strcasecmp(val, "AES-GCMP-256 AES-CCMP-128") == 0) {
			if (set_network(ifname, id, "pairwise",
					"GCMP-256 CCMP") < 0)
				return -2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unrecognized PairwiseCipher value");
			return 0;
		}
	} else if (dut->device_mode == MODE_11BE) {
		cipher_set = 1;
		if (set_network(ifname, id, "pairwise", "GCMP-256") < 0)
			return -2;
	}

	if (!cipher_set && !owe) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing encpType and PairwiseCipher");
		return 0;
	}

	if (dut->device_mode == MODE_11BE &&
	    set_network(ifname, id, "beacon_prot", "1") < 0)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to enable beacon protection");

	val = get_param(cmd, "GroupCipher");
	if (val) {
		if (strcasecmp(val, "AES-GCMP-256") == 0) {
			if (set_network(ifname, id, "group", "GCMP-256") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-CCMP-256") == 0) {
			if (set_network(ifname, id, "group", "CCMP-256") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-GCMP-128") == 0) {
			if (set_network(ifname, id, "group", "GCMP") < 0)
				return -2;
		} else if (strcasecmp(val, "AES-CCMP-128") == 0) {
			if (set_network(ifname, id, "group", "CCMP") < 0)
				return -2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unrecognized GroupCipher value");
			return 0;
		}
	} else if (dut->device_mode == MODE_11BE) {
		if (set_network(ifname, id, "group", "GCMP-256") < 0)
			return -2;
	}

	val = get_param(cmd, "GroupMgntCipher");
	if (val) {
		const char *cipher;

		if (strcasecmp(val, "BIP-GMAC-256") == 0) {
			cipher = "BIP-GMAC-256";
		} else if (strcasecmp(val, "BIP-CMAC-256") == 0) {
			cipher = "BIP-CMAC-256";
		} else if (strcasecmp(val, "BIP-GMAC-128") == 0) {
			cipher = "BIP-GMAC-128";
		} else if (strcasecmp(val, "BIP-CMAC-128") == 0) {
			cipher = "AES-128-CMAC";
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Unsupported GroupMgntCipher");
			return 0;
		}
		if (set_network(ifname, id, "group_mgmt", cipher) < 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Failed to set GroupMgntCipher");
			return 0;
		}
	}

	val = get_param(cmd, "AKMSuiteType");
	if (val && set_akm_suites(dut, ifname, id, val) < 0)
		return ERROR_SEND_STATUS;

	dut->sta_pmf = STA_PMF_DISABLED;

	if (dut->program == PROGRAM_OCE) {
		dut->sta_pmf = STA_PMF_OPTIONAL;
		if (set_network(ifname, id, "ieee80211w", "1") < 0)
			return -2;
	}

	val = get_param(cmd, "BeaconProtection");
	if (val)
		dut->beacon_prot = atoi(val);

	val = get_param(cmd, "PMF");
	if (val) {
		if (strcasecmp(val, "Required") == 0 ||
		    strcasecmp(val, "Forced_Required") == 0) {
			dut->sta_pmf = STA_PMF_REQUIRED;
			if (set_network(ifname, id, "ieee80211w", "2") < 0)
				return -2;
		} else if (strcasecmp(val, "Optional") == 0) {
			dut->sta_pmf = STA_PMF_OPTIONAL;
			if (set_network(ifname, id, "ieee80211w", "1") < 0)
				return -2;
		} else if (strcasecmp(val, "Disabled") == 0 ||
			   strcasecmp(val, "Disable") == 0 ||
			   strcasecmp(val, "Forced_Disabled") == 0) {
			dut->sta_pmf = STA_PMF_DISABLED;
		} else {
			send_resp(dut, conn, SIGMA_INVALID, "errorCode,Unrecognized PMF value");
			return 0;
		}
	} else if (owe || suite_b) {
		dut->sta_pmf = STA_PMF_REQUIRED;
		if (set_network(ifname, id, "ieee80211w", "2") < 0)
			return -2;
	} else if (dut->beacon_prot || dut->ocvc) {
		dut->sta_pmf = STA_PMF_OPTIONAL;
		if (set_network(ifname, id, "ieee80211w", "1") < 0)
			return ERROR_SEND_STATUS;
	}

	if (dut->beacon_prot && set_network(ifname, id, "beacon_prot", "1") < 0)
		return ERROR_SEND_STATUS;

	if (dut->ocvc && dut->device_mode != MODE_11BE &&
	    set_network(ifname, id, "ocv", "1") < 0)
		return ERROR_SEND_STATUS;

	return id;
}

static int sta_set_eht_mlo_active_tx_links(struct sigma_dut *dut,
					   const char *intf, uint8_t num_links,
					   uint8_t link_addr[2][6])
{
#ifdef NL80211_SUPPORT
	struct nl_msg *msg;
	struct nlattr *params, *attr;
	int ret, i;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION)) {
		nlmsg_free(msg);
		return -1;
	}

	if (!(params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    !(attr = nla_nest_start(
		      msg, QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_ACTIVE_LINKS))) {
		nlmsg_free(msg);
		return -1;
	}

	for (i = 0; i < num_links; i++) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"link_mac[%d]: %02x:%02x:%02x:%02x:%02x:%02x",
				i, link_addr[i][0], link_addr[i][1],
				link_addr[i][2], link_addr[i][3],
				link_addr[i][4], link_addr[i][5]);
		nla_put(msg, NL80211_ATTR_MAC, ETH_ALEN, &link_addr[i][0]);

	}
	nla_nest_end(msg, attr);
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	return ret;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_set_ignore_h2e_rsnxe(struct sigma_dut *dut, const char *intf,
				    uint8_t cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_IGNORE_H2E_RSNXE, cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Ignore SAE H2E requirement mismatch can't be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static enum sigma_cmd_result cmd_sta_set_psk(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *type = get_param(cmd, "Type");
	const char *pmf = get_param(cmd, "PMF");
	const char *network_mode = get_param(cmd, "network_mode");
	const char *akm = get_param(cmd, "AKMSuiteType");
	const char *ifname, *val, *alg;
	int id;
	char buf[50];
	int sae_pwe = -1;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	alg = get_param(cmd, "micAlg");

	if (type && strcasecmp(type, "SAE") == 0) {
		if (!akm && val && strcasecmp(val, "wpa2-ft") == 0) {
			if (set_network(ifname, id, "key_mgmt", "FT-SAE") < 0)
				return -2;
		} else if (!akm) {
			const char *key_mgmt = "SAE";

			if (dut->device_mode == MODE_11BE)
				key_mgmt = "SAE-EXT-KEY SAE";
			if (set_network(ifname, id, "key_mgmt", key_mgmt) < 0)
				return -2;
		}
		if (wpa_command(ifname, "SET sae_groups ") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear sae_groups to default");
			return -2;
		}
		if (!pmf) {
			dut->sta_pmf = STA_PMF_REQUIRED;
			if (set_network(ifname, id, "ieee80211w", "2") < 0)
				return -2;
		}
	} else if (type && strcasecmp(type, "PSK-SAE") == 0) {
		if (val && strcasecmp(val, "wpa2-ft") == 0) {
			if (set_network(ifname, id, "key_mgmt",
					"FT-SAE FT-PSK") < 0)
				return -2;
		} else if (!akm) {
			if (set_network(ifname, id, "key_mgmt",
					"SAE WPA-PSK") < 0)
				return -2;
		}
		if (wpa_command(ifname, "SET sae_groups ") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear sae_groups to default");
			return -2;
		}
		if (!pmf) {
			dut->sta_pmf = STA_PMF_OPTIONAL;
			if (set_network(ifname, id, "ieee80211w", "1") < 0)
				return -2;
		}
	} else if (alg && strcasecmp(alg, "SHA-256") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK-SHA256") < 0)
			return -2;
	} else if (alg && strcasecmp(alg, "SHA-1") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK") < 0)
			return -2;
	} else if (val && strcasecmp(val, "wpa2-ft") == 0) {
		if (set_network(ifname, id, "key_mgmt", "FT-PSK") < 0)
			return -2;
	} else if (!akm &&
		   ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
		    dut->sta_pmf == STA_PMF_REQUIRED)) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-PSK WPA-PSK-SHA256") < 0)
			return -2;
	} else if (!akm && dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-PSK WPA-PSK-SHA256") < 0)
			return -2;
	} else if (!akm) {
		if (set_network(ifname, id, "key_mgmt", "WPA-PSK") < 0)
			return -2;
	}

	val = get_param(cmd, "passPhrase");
	if (val == NULL)
		return -1;
	if (type && strcasecmp(type, "SAE") == 0) {
		if (set_network_quoted(ifname, id, "sae_password", val) < 0)
			return -2;
	} else {
		if (set_network_quoted(ifname, id, "psk", val) < 0)
			return -2;
	}

	val = get_param(cmd, "PasswordId");
	if (val && set_network_quoted(ifname, id, "sae_password_id", val) < 0)
		return ERROR_SEND_STATUS;

	val = get_param(cmd, "ECGroupID");
	if (val) {
		snprintf(buf, sizeof(buf), "SET sae_groups %s", val);
		if (wpa_command(ifname, buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to clear sae_groups");
			return -2;
		}
	}

	val = get_param(cmd, "InvalidSAEElement");
	if (val) {
		free(dut->sae_commit_override);
		dut->sae_commit_override = strdup(val);
	}

	val = get_param(cmd, "PMKID_Include");
	if (val) {
		snprintf(buf, sizeof(buf), "SET sae_pmkid_in_assoc %d",
			 get_enable_disable(val));
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "IgnoreH2E_RSNXE_BSSMemSel");
	if (val) {
		snprintf(buf, sizeof(buf), "SET ignore_sae_h2e_only %d",
			 get_enable_disable(val));
		wpa_command(intf, buf);
		if (get_driver_type(dut) == DRIVER_WCN)
			wcn_set_ignore_h2e_rsnxe(dut, intf,
						 get_enable_disable(val));
	}

	val = get_param(cmd, "ECGroupID_RGE");
	if (val) {
		snprintf(buf, sizeof(buf), "SET extra_sae_rejected_groups %s",
			 val);
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "RSNXE_Content");
	if (val) {
		const char *param;

		if (strncasecmp(val, "AssocReq:", 9) == 0) {
			val += 9;
			param = "rsnxe_override_assoc";
		} else if (strncasecmp(val, "EapolM2:", 8) == 0) {
			val += 8;
			param = "rsnxe_override_eapol";
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported RSNXE_Content value");
			return STATUS_SENT_ERROR;
		}
		snprintf(buf, sizeof(buf), "SET %s %s", param, val);
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "sae_pwe");
	if (val) {
		if (strcasecmp(val, "h2e") == 0) {
			dut->sae_pwe = SAE_PWE_H2E;
		} else if (strcasecmp(val, "loop") == 0 ||
			   strcasecmp(val, "looping") == 0) {
			dut->sae_pwe = SAE_PWE_LOOP;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported sae_pwe value");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "Clear_RSNXE");
	if (val && strcmp(val, "1") == 0 &&
	    (wpa_command(intf, "SET rsnxe_override_assoc ") ||
	     wpa_command(intf, "SET rsnxe_override_eapol "))) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to clear RSNXE");
		return STATUS_SENT_ERROR;
	}

	if (dut->sae_pwe == SAE_PWE_LOOP && get_param(cmd, "PasswordId"))
		sae_pwe = 3;
	else if (dut->sae_pwe == SAE_PWE_LOOP)
		sae_pwe = 0;
	else if (dut->sae_pwe == SAE_PWE_H2E)
		sae_pwe = 1;
	else if (dut->sae_h2e_default)
		sae_pwe = 2;
	snprintf(buf, sizeof(buf), "SET sae_pwe %d", sae_pwe);
	if (sae_pwe >= 0 && wpa_command(ifname, buf) != 0)
		return ERROR_SEND_STATUS;

	val = get_param(cmd, "sae_pk");
	if (val && strcmp(val, "0") == 0 &&
	    set_network(ifname, id, "sae_pk", "2") < 0)
		return ERROR_SEND_STATUS;

	val = get_param(cmd, "sae_pk_only");
	if (val && strcmp(val, "1") == 0 &&
	    set_network(ifname, id, "sae_pk", "1") < 0)
		return ERROR_SEND_STATUS;

	if (dut->program == PROGRAM_60GHZ && network_mode &&
	    strcasecmp(network_mode, "PBSS") == 0 &&
	    set_network(ifname, id, "pbss", "1") < 0)
		return -2;

	val = get_param(cmd, "ProfileConnect");
	if (dut->program == PROGRAM_LOCR2 &&
	    val && strcasecmp(val, "disable") == 0) {
		snprintf(buf, sizeof(buf), "ENABLE_NETWORK %d no-connect", id);
		wpa_command(ifname, buf);
	}

	return 1;
}


static enum sigma_cmd_result set_trust_root_system(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   const char *ifname, int id)
{
	char buf[200];

	snprintf(buf, sizeof(buf), "%s/certs", sigma_cert_path);
	if (!file_exists(buf))
		strlcpy(buf, "/system/etc/security/cacerts", sizeof(buf));
	if (!file_exists(buf))
		strlcpy(buf, "/etc/ssl/certs", sizeof(buf));
	if (!file_exists(buf)) {
		char msg[300];

		snprintf(msg, sizeof(msg),
			 "ErrorCode,trustedRootCA system store (%s) not found",
			 buf);
		send_resp(dut, conn, SIGMA_ERROR, msg);
		return STATUS_SENT_ERROR;
	}

	if (set_network_quoted(ifname, id, "ca_path", buf) < 0)
		return ERROR_SEND_STATUS;

	return SUCCESS_SEND_STATUS;
}


static enum sigma_cmd_result set_trust_root(struct sigma_dut *dut,
					    struct sigma_conn *conn,
					    const char *ifname, int id,
					    const char *val)
{
	char buf[200];
#ifdef ANDROID
	unsigned char kvalue[KEYSTORE_MESSAGE_SIZE];
	int length;
#endif /* ANDROID */

	if (strcmp(val, "DEFAULT") == 0)
		return set_trust_root_system(dut, conn, ifname, id);

#ifdef ANDROID
	snprintf(buf, sizeof(buf), "CACERT_%s", val);
	length = android_keystore_get(ANDROID_KEYSTORE_GET, buf, kvalue);
	if (length > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Use Android keystore [%s]",
				buf);
		snprintf(buf, sizeof(buf), "keystore://CACERT_%s", val);
		goto ca_cert_selected;
	}
#endif /* ANDROID */

	snprintf(buf, sizeof(buf), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
	if (!file_exists(buf)) {
		char msg[300];

		snprintf(msg, sizeof(msg),
			 "ErrorCode,trustedRootCA file (%s) not found", buf);
		send_resp(dut, conn, SIGMA_ERROR, msg);
		return STATUS_SENT_ERROR;
	}
#endif /* __linux__ */
#ifdef ANDROID
ca_cert_selected:
#endif /* ANDROID */
	if (set_network_quoted(ifname, id, "ca_cert", buf) < 0)
		return ERROR_SEND_STATUS;

	return SUCCESS_SEND_STATUS;
}


static int set_eap_common(struct sigma_dut *dut, struct sigma_conn *conn,
			  const char *ifname, int username_identity,
			  struct sigma_cmd *cmd)
{
	const char *val, *alg, *akm, *trust_root, *domain, *domain_suffix;
	int id;
	char buf[200], buf2[300];
	int erp = 0;
	enum sigma_cmd_result res;

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	val = get_param(cmd, "keyMgmtType");
	alg = get_param(cmd, "micAlg");
	akm = get_param(cmd, "AKMSuiteType");
	trust_root = get_param(cmd, "trustedRootCA");
	domain = get_param(cmd, "Domain");
	domain_suffix = get_param(cmd, "DomainSuffix");

	if (val && strcasecmp(val, "SuiteB") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP-SUITE-B-192") <
		    0)
			return -2;
	} else if (alg && strcasecmp(alg, "SHA-256") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP-SHA256") < 0)
			return -2;
	} else if (alg && strcasecmp(alg, "SHA-1") == 0) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP") < 0)
			return -2;
	} else if (val && strcasecmp(val, "wpa2-ft") == 0) {
		if (set_network(ifname, id, "key_mgmt", "FT-EAP") < 0)
			return -2;
	} else if (!akm &&
		   ((val && strcasecmp(val, "wpa2-sha256") == 0) ||
		    dut->sta_pmf == STA_PMF_REQUIRED)) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-EAP WPA-EAP-SHA256") < 0)
			return -2;
	} else if (akm && atoi(akm) == 14) {
		if (dut->sta_pmf == STA_PMF_OPTIONAL ||
		    dut->sta_pmf == STA_PMF_REQUIRED) {
			if (set_network(ifname, id, "key_mgmt",
					"WPA-EAP-SHA256 FILS-SHA256") < 0)
				return -2;
		} else {
			if (set_network(ifname, id, "key_mgmt",
					"WPA-EAP FILS-SHA256") < 0)
				return -2;
		}

		erp = 1;
	} else if (akm && atoi(akm) == 15) {
		if (dut->sta_pmf == STA_PMF_OPTIONAL ||
		    dut->sta_pmf == STA_PMF_REQUIRED) {
			if (set_network(ifname, id, "key_mgmt",
					"WPA-EAP-SHA256 FILS-SHA384") < 0)
				return -2;
		} else {
			if (set_network(ifname, id, "key_mgmt",
					"WPA-EAP FILS-SHA384") < 0)
				return -2;
		}

		erp = 1;
	} else if (!akm && dut->sta_pmf == STA_PMF_OPTIONAL) {
		if (set_network(ifname, id, "key_mgmt",
				"WPA-EAP WPA-EAP-SHA256") < 0)
			return -2;
	} else if (!akm) {
		if (set_network(ifname, id, "key_mgmt", "WPA-EAP") < 0)
			return -2;
	}

	if (trust_root) {
		if (strcmp(trust_root, "DEFAULT") == 0 && !domain &&
		    !domain_suffix) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,trustRootCA DEFAULT used without specifying Domain or DomainSuffix");
			return STATUS_SENT_ERROR;
		}
		res = set_trust_root(dut, conn, ifname, id, trust_root);
		if (res != SUCCESS_SEND_STATUS)
			return res;
	}

	val = get_param(cmd, "ServerCert");
	if (val) {
		FILE *f;
		char *result = NULL, *pos;

		snprintf(buf, sizeof(buf), "%s/%s.sha256", sigma_cert_path,
			 val);
		f = fopen(buf, "r");
		if (f) {
			result = fgets(buf, sizeof(buf), f);
			fclose(f);
		}
		if (!result) {
			snprintf(buf2, sizeof(buf2),
				 "ErrorCode,ServerCert hash could not be read from %s",
				 buf);
			send_resp(dut, conn, SIGMA_ERROR, buf2);
			return STATUS_SENT_ERROR;
		}
		pos = strchr(buf, '\n');
		if (pos)
			*pos = '\0';
		pos = strchr(buf, '\r');
		if (pos)
			*pos = '\0';
		snprintf(buf2, sizeof(buf2), "hash://server/sha256/%s", buf);
		if (set_network_quoted(ifname, id, "ca_cert", buf2) < 0)
			return ERROR_SEND_STATUS;

		snprintf(buf, sizeof(buf), "%s/%s.tod", sigma_cert_path, val);
		if (file_exists(buf)) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"TOD policy enabled for the configured ServerCert hash");
			dut->sta_tod_policy = 1;
		}
	}

	if (domain &&
	    set_network_quoted(ifname, id, "domain_match", domain) < 0)
		return ERROR_SEND_STATUS;

	if (domain_suffix &&
	    set_network_quoted(ifname, id, "domain_suffix_match",
			       domain_suffix) < 0)
		return ERROR_SEND_STATUS;

	if (username_identity) {
		val = get_param(cmd, "username");
		if (val) {
			if (set_network_quoted(ifname, id, "identity", val) < 0)
				return -2;
		}

		val = get_param(cmd, "password");
		if (val) {
			if (set_network_quoted(ifname, id, "password", val) < 0)
				return -2;
		}
	}

	val = get_param(cmd, "imsiPrivacyCert");
	if (val) {
		snprintf(buf, sizeof(buf), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(buf)) {
			char msg[300];

			snprintf(msg, sizeof(msg),
				 "ErrorCode,imsiPrivacyCert file (%s) not found",
				 buf);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return STATUS_SENT_ERROR;
		}
#endif /* __linux__ */
		if (set_network_quoted(ifname, id, "imsi_privacy_cert",
				       buf) < 0)
			return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "imsiPrivacyCertID");
	if (val && set_network_quoted(ifname, id, "imsi_privacy_attr",
				      val) < 0)
		return ERROR_SEND_STATUS;

	if (dut->akm_values &
	    ((1 << AKM_FILS_SHA256) |
	     (1 << AKM_FILS_SHA384) |
	     (1 << AKM_FT_FILS_SHA256) |
	     (1 << AKM_FT_FILS_SHA384)))
		erp = 1;
	if (erp && set_network(ifname, id, "erp", "1") < 0)
		return ERROR_SEND_STATUS;

	dut->sta_associate_wait_connect = 1;

	return id;
}


static int set_tls_cipher(const char *ifname, int id, const char *cipher)
{
	const char *val;

	if (!cipher)
		return 0;

	if (strcasecmp(cipher, "TLS_ECDHE_ECDSA_WITH_AES_256_GCM_SHA384") == 0)
		val = "ECDHE-ECDSA-AES256-GCM-SHA384";
	else if (strcasecmp(cipher,
			    "TLS_ECDHE_RSA_WITH_AES_256_GCM_SHA384") == 0)
		val = "ECDHE-RSA-AES256-GCM-SHA384";
	else if (strcasecmp(cipher, "TLS_DHE_RSA_WITH_AES_256_GCM_SHA384") == 0)
		val = "DHE-RSA-AES256-GCM-SHA384";
	else if (strcasecmp(cipher,
			    "TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256") == 0)
		val = "ECDHE-ECDSA-AES128-GCM-SHA256";
	else
		return -1;

	/* Need to clear phase1="tls_suiteb=1" to allow cipher enforcement */
	set_network_quoted(ifname, id, "phase1", "");

	return set_network_quoted(ifname, id, "openssl_ciphers", val);
}


static enum sigma_cmd_result cmd_sta_set_eaptls(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *type = get_param(cmd, "Type");
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[200];
#ifdef ANDROID
	unsigned char kvalue[KEYSTORE_MESSAGE_SIZE];
	int length;
	int jb_or_newer = 0;
	char prop[PROPERTY_VALUE_MAX];
#endif /* ANDROID */

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TLS") < 0)
		return -2;

	if (!get_param(cmd, "username") &&
	    set_network_quoted(ifname, id, "identity",
			       "wifi-user@wifilabs.local") < 0)
		return -2;

	val = get_param(cmd, "clientCertificate");
	if (val == NULL)
		return -1;
#ifdef ANDROID
	snprintf(buf, sizeof(buf), "USRPKEY_%s", val);
	length = android_keystore_get(ANDROID_KEYSTORE_GET, buf, kvalue);
	if (length < 0) {
		/*
		 * JB started reporting keystore type mismatches, so retry with
		 * the GET_PUBKEY command if the generic GET fails.
		 */
		length = android_keystore_get(ANDROID_KEYSTORE_GET_PUBKEY,
					      buf, kvalue);
	}

	if (property_get("ro.build.version.release", prop, NULL) != 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Android release %s", prop);
		if (strncmp(prop, "4.0", 3) != 0)
			jb_or_newer = 1;
	} else
		jb_or_newer = 1; /* assume newer */

	if (jb_or_newer && length > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Use Android keystore [%s]", buf);
		if (set_network(ifname, id, "engine", "1") < 0)
			return -2;
		if (set_network_quoted(ifname, id, "engine_id", "keystore") < 0)
			return -2;
		snprintf(buf, sizeof(buf), "USRPKEY_%s", val);
		if (set_network_quoted(ifname, id, "key_id", buf) < 0)
			return -2;
		snprintf(buf, sizeof(buf), "keystore://USRCERT_%s", val);
		if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
			return -2;
		return 1;
	} else if (length > 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Use Android keystore [%s]", buf);
		snprintf(buf, sizeof(buf), "keystore://USRPKEY_%s", val);
		if (set_network_quoted(ifname, id, "private_key", buf) < 0)
			return -2;
		snprintf(buf, sizeof(buf), "keystore://USRCERT_%s", val);
		if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
			return -2;
		return 1;
	}
#endif /* ANDROID */

	snprintf(buf, sizeof(buf), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
	if (!file_exists(buf)) {
		char msg[300];
		snprintf(msg, sizeof(msg), "ErrorCode,clientCertificate file "
			 "(%s) not found", buf);
		send_resp(dut, conn, SIGMA_ERROR, msg);
		return -3;
	}
#endif /* __linux__ */
	if (set_network_quoted(ifname, id, "private_key", buf) < 0)
		return -2;
	if (set_network_quoted(ifname, id, "client_cert", buf) < 0)
		return -2;

	if (set_network_quoted(ifname, id, "private_key_passwd", "wifi") < 0)
		return -2;

	val = get_param(cmd, "keyMgmtType");
	if (val && strcasecmp(val, "SuiteB") == 0) {
		val = get_param(cmd, "CertType");
		if (val && strcasecmp(val, "RSA") == 0) {
			if (set_network_quoted(ifname, id, "phase1",
					       "tls_suiteb=1") < 0)
				return -2;
		} else {
			if (set_network_quoted(ifname, id, "openssl_ciphers",
					       "SUITEB192") < 0)
				return -2;
		}

		val = get_param(cmd, "TLSCipher");
		if (set_tls_cipher(ifname, id, val) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported TLSCipher value");
			return -3;
		}
	}

	if (set_network(ifname, id, "ocsp", "1") < 0)
		return ERROR_SEND_STATUS;

	if (type && strcasecmp(type, "EAPTLS_1_3") == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Enable only TLS v1.3");
		if (set_network_quoted(ifname, id, "phase1",
				       "tls_disable_tlsv1_0=1 tls_disable_tlsv1_1=1 tls_disable_tlsv1_2=1 tls_disable_tlsv1_3=0") < 0)
			return ERROR_SEND_STATUS;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_eapttls(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "TTLS") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set TTLS method");
		return 0;
	}

	if (set_network_quoted(ifname, id, "phase2", "auth=MSCHAPV2") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set MSCHAPv2 for TTLS Phase 2");
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_eapsim(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "SIM") < 0)
		return -2;

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_peap(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "PEAP") < 0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPv2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "peapVersion");
	if (val) {
		int ver = atoi(val);
		if (ver < 0 || ver > 1)
			return -1;
		snprintf(buf, sizeof(buf), "peapver=%d", ver);
		if (set_network_quoted(ifname, id, "phase1", buf) < 0)
			return -2;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_eapfast(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;
	char buf[100];

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, 1, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "FAST") < 0)
		return -2;

	val = get_param(cmd, "innerEAP");
	if (val) {
		if (strcasecmp(val, "MSCHAPV2") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=MSCHAPV2") < 0)
				return -2;
		} else if (strcasecmp(val, "GTC") == 0) {
			if (set_network_quoted(ifname, id, "phase2",
					       "auth=GTC") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "validateServer");
	if (val) {
		/* TODO */
		sigma_dut_print(dut, DUT_MSG_INFO, "Ignored EAP-FAST "
				"validateServer=%s", val);
	}

	val = get_param(cmd, "pacFile");
	if (val) {
		snprintf(buf, sizeof(buf), "blob://%s", val);
		if (set_network_quoted(ifname, id, "pac_file", buf) < 0)
			return -2;
	}

	if (set_network_quoted(ifname, id, "phase1", "fast_provisioning=2") <
	    0)
		return -2;

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_eapaka(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *username = get_param(cmd, "Username");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	/* RFC 5448: EAP-AKA' MUST use the leading character "6" (ASCII 36
	 * hexadecimal).
	 */
	if (username && username[0] == '6') {
		if (set_network(ifname, id, "eap", "AKA'") < 0)
			return -2;
	} else if (set_network(ifname, id, "eap", "AKA") < 0) {
		return -2;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_eapakaprime(struct sigma_dut *dut,
						     struct sigma_conn *conn,
						     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname;
	int id;

	if (intf == NULL)
		return -1;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_eap_common(dut, conn, ifname, !dut->sim_no_username, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "eap", "AKA'") < 0)
		return -2;

	return 1;
}


static int sta_set_open(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *network_mode = get_param(cmd, "network_mode");
	const char *ifname;
	int id;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = add_network_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "NONE") < 0)
		return -2;

	if (dut->program == PROGRAM_60GHZ && network_mode &&
	    strcasecmp(network_mode, "PBSS") == 0 &&
	    set_network(ifname, id, "pbss", "1") < 0)
		return -2;

	return 1;
}


static enum sigma_cmd_result sta_set_owe(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ifname, *val;
	int id;

	if (intf == NULL)
		return INVALID_SEND_STATUS;

	if (strcmp(intf, get_main_ifname(dut)) == 0)
		ifname = get_station_ifname(dut);
	else
		ifname = intf;

	id = set_wpa_common(dut, conn, ifname, cmd);
	if (id < 0)
		return id;

	if (set_network(ifname, id, "key_mgmt", "OWE") < 0)
		return ERROR_SEND_STATUS;

	if (dut->owe_ptk_workaround &&
	    set_network(ifname, id, "owe_ptk_workaround", "1") < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set owe_ptk_workaround to 1");
		return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "ECGroupID");
	if (val && strcmp(val, "0") == 0) {
		if (wpa_command(ifname,
				"VENDOR_ELEM_ADD 13 ff23200000783590fb7440e03d5b3b33911f86affdcc6b4411b707846ac4ff08ddc8831ccd") != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set OWE DH Param element override");
			return ERROR_SEND_STATUS;
		}
	} else if (val && set_network(ifname, id, "owe_group", val) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set owe_group");
		return ERROR_SEND_STATUS;
	}

	return SUCCESS_SEND_STATUS;
}


static enum sigma_cmd_result cmd_sta_set_security(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	const char *type = get_param(cmd, "Type");

	if (type == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Type argument");
		return 0;
	}

	if (strcasecmp(type, "OPEN") == 0)
		return sta_set_open(dut, conn, cmd);
	if (strcasecmp(type, "OWE") == 0)
		return sta_set_owe(dut, conn, cmd);
	if (strcasecmp(type, "PSK") == 0 ||
	    strcasecmp(type, "PSK-SAE") == 0 ||
	    strcasecmp(type, "SAE") == 0)
		return cmd_sta_set_psk(dut, conn, cmd);
	if (strcasecmp(type, "EAPTLS") == 0 ||
	    strcasecmp(type, "EAPTLS_1_3") == 0)
		return cmd_sta_set_eaptls(dut, conn, cmd);
	if (strcasecmp(type, "EAPTTLS") == 0)
		return cmd_sta_set_eapttls(dut, conn, cmd);
	if (strcasecmp(type, "EAPPEAP") == 0)
		return cmd_sta_set_peap(dut, conn, cmd);
	if (strcasecmp(type, "EAPSIM") == 0)
		return cmd_sta_set_eapsim(dut, conn, cmd);
	if (strcasecmp(type, "EAPFAST") == 0)
		return cmd_sta_set_eapfast(dut, conn, cmd);
	if (strcasecmp(type, "EAPAKA") == 0)
		return cmd_sta_set_eapaka(dut, conn, cmd);
	if (strcasecmp(type, "EAPAKAPRIME") == 0)
		return cmd_sta_set_eapakaprime(dut, conn, cmd);
	if (strcasecmp(type, "wep") == 0)
		return cmd_sta_set_encryption(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR,
		  "ErrorCode,Unsupported Type value");
	return 0;
}


int ath6kl_client_uapsd(struct sigma_dut *dut, const char *intf, int uapsd)
{
#ifdef __linux__
	/* special handling for ath6kl */
	char path[128], fname[128], *pos;
	ssize_t res;
	FILE *f;

	res = snprintf(fname, sizeof(fname), "/sys/class/net/%s/phy80211",
		       intf);
	if (res < 0 || res >= sizeof(fname))
		return 0;
	res = readlink(fname, path, sizeof(path));
	if (res < 0)
		return 0; /* not ath6kl */

	if (res >= (int) sizeof(path))
		res = sizeof(path) - 1;
	path[res] = '\0';
	pos = strrchr(path, '/');
	if (pos == NULL)
		pos = path;
	else
		pos++;
	res = snprintf(fname, sizeof(fname),
		       "/sys/kernel/debug/ieee80211/%s/ath6kl/"
		       "create_qos", pos);
	if (res < 0 || res >= sizeof(fname) || !file_exists(fname))
		return 0; /* not ath6kl */

	if (uapsd) {
		f = fopen(fname, "w");
		if (f == NULL)
			return -1;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Use ath6kl create_qos");
		fprintf(f, "4 2 2 1 2 9999999 9999999 9999999 7777777 0 4 "
			"45000 200 56789000 56789000 5678900 0 0 9999999 "
			"20000 0\n");
		fclose(f);
	} else {
		res = snprintf(fname, sizeof(fname),
			       "/sys/kernel/debug/ieee80211/%s/ath6kl/"
			       "delete_qos", pos);
		if (res < 0 || res >= sizeof(fname))
			return -1;

		f = fopen(fname, "w");
		if (f == NULL)
			return -1;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "Use ath6kl delete_qos");
		fprintf(f, "2 4\n");
		fclose(f);
	}
#endif /* __linux__ */

	return 0;
}


static enum sigma_cmd_result cmd_sta_set_uapsd(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	/* const char *ssid = get_param(cmd, "ssid"); */
	const char *val;
	int max_sp_len = 4;
	int ac_be = 1, ac_bk = 1, ac_vi = 1, ac_vo = 1;
	char buf[100];
	int ret1, ret2;

	val = get_param(cmd, "maxSPLength");
	if (val) {
		max_sp_len = atoi(val);
		if (max_sp_len != 0 && max_sp_len != 1 && max_sp_len != 2 &&
		    max_sp_len != 4)
			return -1;
	}

	val = get_param(cmd, "acBE");
	if (val)
		ac_be = atoi(val);

	val = get_param(cmd, "acBK");
	if (val)
		ac_bk = atoi(val);

	val = get_param(cmd, "acVI");
	if (val)
		ac_vi = atoi(val);

	val = get_param(cmd, "acVO");
	if (val)
		ac_vo = atoi(val);

	dut->client_uapsd = ac_be || ac_bk || ac_vi || ac_vo;

	snprintf(buf, sizeof(buf), "P2P_SET client_apsd %d,%d,%d,%d;%d",
		 ac_be, ac_bk, ac_vi, ac_vo, max_sp_len);
	ret1 = wpa_command(intf, buf);

	snprintf(buf, sizeof(buf), "SET uapsd %d,%d,%d,%d;%d",
		 ac_be, ac_bk, ac_vi, ac_vo, max_sp_len);
	ret2 = wpa_command(intf, buf);

	if (ret1 && ret2) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to set client mode "
				"UAPSD parameters.");
		return -2;
	}

	if (ath6kl_client_uapsd(dut, intf, dut->client_uapsd) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set ath6kl QoS parameters");
		return 0;
	}

	return 1;
}

#ifdef NL80211_SUPPORT

struct tspec {
	u8 handle;
	u8 tid;
	u8 direction;
	u8 psb_ts;
	u8 up;
	u8 ack;
	u16 nomsize;
	u16 msize;
	u32 minsi;
	u32 maxsi;
	u32 inact;
	u32 sussi;
	u32 mindr;
	u32 meandr;
	u32 peakdr;
	u32 burstsize;
	u32 phyrate;
	u16 sba;
};

static int sta_send_add_tspec_params(struct sigma_dut *dut, const char *intf,
				     struct tspec *tspec_info)
{
	struct nl_msg *msg;
	int ret;
	struct nlattr *params;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0, NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TSPEC))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_OPERATION,
		       QCA_WLAN_TSPEC_ADD) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_TSID,
		       tspec_info->tid) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_DIRECTION,
		       tspec_info->direction) ||
	    (tspec_info->psb_ts &&
	     nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_APSD)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_USER_PRIORITY,
		       tspec_info->up) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_ACK_POLICY,
		       tspec_info->ack) ||
	    nla_put_u16(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_NOMINAL_MSDU_SIZE,
			tspec_info->nomsize) ||
	    nla_put_u16(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MAXIMUM_MSDU_SIZE,
			tspec_info->msize) ||
	    nla_put_u32(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MIN_SERVICE_INTERVAL,
			tspec_info->minsi) ||
	    nla_put_u32(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MAX_SERVICE_INTERVAL,
			tspec_info->maxsi) ||
	    nla_put_u32(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_INACTIVITY_INTERVAL,
			tspec_info->inact) ||
	    nla_put_u32(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_SUSPENSION_INTERVAL,
			tspec_info->sussi) ||
	    nla_put_u32(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MINIMUM_DATA_RATE,
			tspec_info->mindr) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MEAN_DATA_RATE,
			tspec_info->meandr) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_PEAK_DATA_RATE,
			tspec_info->peakdr) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_BURST_SIZE,
			tspec_info->burstsize) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_MINIMUM_PHY_RATE,
			tspec_info->phyrate) ||
	    nla_put_u16(msg,
			QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_SURPLUS_BANDWIDTH_ALLOWANCE,
			tspec_info->sba))
		goto fail;

	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		return ret;
	}

	return 0;

fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
		"%s: err in adding vendor_cmd and vendor_data",
		__func__);
	nlmsg_free(msg);
	return -1;
}


static int sta_send_del_tspec_params(struct sigma_dut *dut, const char *intf,
				     uint8_t tid)
{
	struct nl_msg *msg;
	int ret;
	struct nlattr *params;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0, NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TSPEC))
		goto fail;

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_OPERATION,
		       QCA_WLAN_TSPEC_DEL) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TSPEC_TSID, tid))
		goto fail;
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		return ret;
	}

	return 0;

fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
		"%s: err in adding vendor_cmd and vendor_data",
		__func__);
	nlmsg_free(msg);
	return -1;
}

#endif /* NL80211_SUPPORT */


static enum sigma_cmd_result cmd_sta_set_wmm(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	char buf[1000];
	const char *intf = get_param(cmd, "Interface");
	const char *grp = get_param(cmd, "Group");
	const char *act = get_param(cmd, "Action");
	const char *tid = get_param(cmd, "Tid");
	const char *dir = get_param(cmd, "Direction");
	const char *psb = get_param(cmd, "Psb");
	const char *up = get_param(cmd, "Up");
	const char *fixed = get_param(cmd, "Fixed");
	const char *size = get_param(cmd, "Size");
	const char *msize = get_param(cmd, "Maxsize");
	const char *minsi = get_param(cmd, "Min_srvc_intrvl");
	const char *maxsi = get_param(cmd, "Max_srvc_intrvl");
	const char *inact = get_param(cmd, "Inactivity");
	const char *sus = get_param(cmd, "Suspension");
	const char *mindr = get_param(cmd, "Mindatarate");
	const char *meandr = get_param(cmd, "Meandatarate");
	const char *peakdr = get_param(cmd, "Peakdatarate");
	const char *phyrate = get_param(cmd, "Phyrate");
	const char *burstsize = get_param(cmd, "Burstsize");
	const char *sba = get_param(cmd, "Sba");
	int direction;
	int handle;
	float sba_fv = 0;
	unsigned int sba_val = 0;
	int fixed_int;
	int psb_ts;
#ifdef NL80211_SUPPORT
	int tspec_status;
	enum qca_wlan_tspec_direction tspec_dir;
	struct tspec *tspec_info;
#endif /* NL80211_SUPPORT */

	if (intf == NULL || grp == NULL || act == NULL )
		return -1;

	if (strcasecmp(act, "addts") == 0) {
		if (tid == NULL || dir == NULL || psb == NULL ||
		    up == NULL || fixed == NULL || size == NULL)
			return -1;

		/*
		 * Note: Sigma CAPI spec lists uplink, downlink, and bidi as the
		 * possible values, but WMM-AC and V-E test scripts use "UP,
		 * "DOWN", and "BIDI".
		 */
		if (strcasecmp(dir, "uplink") == 0 ||
		    strcasecmp(dir, "up") == 0) {
			direction = 0;
#ifdef NL80211_SUPPORT
			tspec_dir = QCA_WLAN_TSPEC_DIRECTION_UPLINK;
#endif /* NL80211_SUPPORT */
		} else if (strcasecmp(dir, "downlink") == 0 ||
			   strcasecmp(dir, "down") == 0) {
			direction = 1;
#ifdef NL80211_SUPPORT
			tspec_dir = QCA_WLAN_TSPEC_DIRECTION_DOWNLINK;
#endif /* NL80211_SUPPORT */
		} else if (strcasecmp(dir, "bidi") == 0) {
			direction = 2;
#ifdef NL80211_SUPPORT
			tspec_dir = QCA_WLAN_TSPEC_DIRECTION_BOTH;
#endif /* NL80211_SUPPORT */
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Direction %s not supported", dir);
			return -1;
		}

		if (strcasecmp(psb, "legacy") == 0) {
			psb_ts = 0;
		} else if (strcasecmp(psb, "uapsd") == 0) {
			psb_ts = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"PSB %s not supported", psb);
			return -1;
		}

		if (atoi(tid) < 0 || atoi(tid) > 7) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TID %s not supported", tid);
			return -1;
		}

		if (strcasecmp(fixed, "true") == 0) {
			fixed_int = 1;
		} else {
			fixed_int = 0;
		}

		if (sba)
			sba_fv = atof(sba);

		dut->dialog_token++;
		handle = 7000 + dut->dialog_token;

		/*
		 * size: convert to hex
		 * maxsi: convert to hex
		 * mindr: convert to hex
		 * meandr: convert to hex
		 * peakdr: convert to hex
		 * burstsize: convert to hex
		 * phyrate: convert to hex
		 * sba: convert to hex with modification
		 * minsi: convert to integer
		 * sus: convert to integer
		 * inact: convert to integer
		 * maxsi: convert to integer
		 */

		/*
		 * The Nominal MSDU Size field is 2 octets long and contains an
		 * unsigned integer that specifies the nominal size, in octets,
		 * of MSDUs belonging to the traffic under this traffic
		 * specification and is defined in Figure 16. If the Fixed
		 * subfield is set to 1, then the size of the MSDU is fixed and
		 * is indicated by the Size Subfield. If the Fixed subfield is
		 * set to 0, then the size of the MSDU might not be fixed and
		 * the Size indicates the nominal MSDU size.
		 *
		 * The Surplus Bandwidth Allowance Factor field is 2 octets long
		 * and specifies the excess allocation of time (and bandwidth)
		 * over and above the stated rates required to transport an MSDU
		 * belonging to the traffic in this TSPEC. This field is
		 * represented as an unsigned binary number with an implicit
		 * binary point after the leftmost 3 bits. For example, an SBA
		 * of 1.75 is represented as 0x3800. This field is included to
		 * account for retransmissions. As such, the value of this field
		 * must be greater than unity.
		 */

		if (sba)
			sba_val = (unsigned int)
				(((int) sba_fv << 13) |
				 (int) ((sba_fv - (int) sba_fv) * 8192));

#ifdef NL80211_SUPPORT
		tspec_info = malloc(sizeof(struct tspec));
		if (!tspec_info)
			return ERROR_SEND_STATUS;

		tspec_info->handle = handle;
		tspec_info->tid = tid ? atoi(tid) : 0;
		tspec_info->direction = tspec_dir;
		tspec_info->psb_ts = psb_ts;
		tspec_info->up = up ? atoi(up) : 0;
		tspec_info->ack = 0;
		tspec_info->nomsize = (unsigned int) ((fixed_int << 15) |
						      atoi(size));
		tspec_info->msize = msize ? atoi(msize) : 0;
		tspec_info->minsi = minsi ? atoi(minsi) : 0;
		tspec_info->maxsi = maxsi ? atoi(maxsi) : 0;
		tspec_info->inact = inact ? atoi(inact) : 0;
		tspec_info->sussi = sus ? atoi(sus) : 0;
		tspec_info->mindr = mindr ? atoi(mindr) : 0;
		tspec_info->meandr =  meandr ? atoi(meandr) : 0;
		tspec_info->peakdr =  peakdr ? atoi(peakdr) : 0;
		tspec_info->burstsize = burstsize ? atoi(burstsize) : 0;
		tspec_info->phyrate =  phyrate ? atoi(phyrate) : 0;
		tspec_info->sba = sba_val;

		tspec_status = sta_send_add_tspec_params(dut, intf, tspec_info);
		free(tspec_info);
		if (!tspec_status) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"addtspec request netlink command sent");
			dut->tid_to_handle[atoi(tid)] = handle;
			return SUCCESS_SEND_STATUS;
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"addtspec request netlink command cmd failed - try iwpriv");
#endif /* NL80211_SUPPORT */

		snprintf(buf, sizeof(buf),
			 "iwpriv %s addTspec %d %s %d %d %s 0x%X"
			 " 0x%X 0x%X 0x%X"
			 " 0x%X 0x%X 0x%X"
			 " 0x%X %d %d %d %d"
			 " %d %d",
			 intf, handle, tid, direction, psb_ts, up,
			 (unsigned int) ((fixed_int << 15) | atoi(size)),
			 msize ? atoi(msize) : 0,
			 mindr ? atoi(mindr) : 0,
			 meandr ? atoi(meandr) : 0,
			 peakdr ? atoi(peakdr) : 0,
			 burstsize ? atoi(burstsize) : 0,
			 phyrate ? atoi(phyrate) : 0,
			 sba_val,
			 minsi ? atoi(minsi) : 0,
			 sus ? atoi(sus) : 0,
			 0, 0,
			 inact ? atoi(inact) : 0,
			 maxsi ? atoi(maxsi) : 0);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv addtspec request failed");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to execute addTspec command");
			return 0;
		}

		sigma_dut_print(dut, DUT_MSG_INFO,
				"iwpriv addtspec request send");

		/* Mapping handle to a TID */
		dut->tid_to_handle[atoi(tid)] = handle;
	} else if (strcasecmp(act, "delts") == 0) {
		if (tid == NULL)
			return -1;

		if (atoi(tid) < 0 || atoi(tid) > 7) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TID %s not supported", tid);
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported TID");
			return 0;
		}

		handle = dut->tid_to_handle[atoi(tid)];

		if (handle < 7000 || handle > 7255) {
			/* Invalid handle ie no mapping for that TID */
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"handle-> %d not found", handle);
		}

#ifdef NL80211_SUPPORT
		tspec_status = sta_send_del_tspec_params(dut, intf, atoi(tid));
		if (!tspec_status) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"deltspec request netlink command sent");
			dut->tid_to_handle[atoi(tid)] = 0;
			return SUCCESS_SEND_STATUS;
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"deltspec request netlink command failed - try iwpriv");
#endif /* NL80211_SUPPORT */

		snprintf(buf, sizeof(buf), "iwpriv %s delTspec %d",
			 intf, handle);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv deltspec request failed");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to execute delTspec command");
			return 0;
		}

		sigma_dut_print(dut, DUT_MSG_INFO,
				"iwpriv deltspec request send");

		dut->tid_to_handle[atoi(tid)] = 0;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Action type %s not supported", act);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported Action");
		return 0;
	}

	return 1;
}


static int find_network(struct sigma_dut *dut, const char *ssid)
{
	char list[4096];
	char *pos;

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Search for profile based on SSID: '%s'", ssid);
	if (wpa_command_resp(get_station_ifname(dut), "LIST_NETWORKS",
			     list, sizeof(list)) < 0)
		return -1;
	pos = strstr(list, ssid);
	if (!pos || pos == list || pos[-1] != '\t' || pos[strlen(ssid)] != '\t')
		return -1;

	while (pos > list && pos[-1] != '\n')
		pos--;
	dut->infra_network_id = atoi(pos);
	snprintf(dut->infra_ssid, sizeof(dut->infra_ssid), "%s", ssid);
	return 0;
}


/**
 * enum qca_sta_helper_config_params - This helper enum defines the config
 * parameters which can be delivered to sta.
 */
enum qca_sta_helper_config_params {
	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_RSN_IE */
	STA_SET_RSNIE,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_LDPC */
	STA_SET_LDPC,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_TX_STBC */
	STA_SET_TX_STBC,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_RX_STBC */
	STA_SET_RX_STBC,

	/* For the attributes QCA_WLAN_VENDOR_ATTR_CONFIG_TX_MSDU_AGGREGATION
	 * and QCA_WLAN_VENDOR_ATTR_CONFIG_RX_MSDU_AGGREGATION */
	STA_SET_AMSDU,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_CHANNEL_WIDTH */
	STA_SET_CHAN_WIDTH,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_FT_OVER_DS */
	STA_SET_FT_DS,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_EML_CAPABILITY */
	STA_SET_EHT_EML_CAPABILITY,

	/* For the attribute
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MAX_SIMULTANEOUS_LINKS */
	STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS,

	/* For the attribute
	 * QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MAX_NUM_LINKS */
	STA_SET_EHT_MLO_MAX_NUM_LINKS,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MODE */
	STA_SET_EHT_MLO_MODE,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_DYNAMIC_BW */
	STA_SET_DYN_BW,

	/* For the attributes QCA_WLAN_VENDOR_ATTR_CONFIG_TX_MPDU_AGGREGATION
	 * and QCA_WLAN_VENDOR_ATTR_CONFIG_RX_MPDU_AGGREGATION */
	STA_SET_AMPDU,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_NSS */
	STA_SET_NSS,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_LISTEN_INTERVAL */
	STA_SET_LISTEN_INTERVAL,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_EMLSR_MODE_SWITCH */
	STA_SET_EMLSR_MODE_SWITCH,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_EPCS_CAPABILITY */
	STA_SET_EPCS_CAPABILITY,

	/* For the attribute QCA_WLAN_VENDOR_ATTR_CONFIG_EPCS_FUNCTION */
	STA_SET_EPCS_FUNCTION,
};


static int sta_config_params(struct sigma_dut *dut, const char *intf,
			     enum qca_sta_helper_config_params config_cmd,
			     int value)
{
#ifdef NL80211_SUPPORT
	struct nl_msg *msg;
	int ret;
	struct nlattr *params;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Interface %s does not exist",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION) ||
	    !(params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)))
		goto fail;

	switch (config_cmd) {
	case STA_SET_RSNIE:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_RSN_IE, value))
			goto fail;
		break;
	case STA_SET_LDPC:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_LDPC, value))
			goto fail;
		break;
	case STA_SET_TX_STBC:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TX_STBC, value))
			goto fail;
		break;
	case STA_SET_RX_STBC:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_RX_STBC, value))
			goto fail;
		break;
	case STA_SET_AMSDU:
		/* The driver expects both Tx and Rx aggregation parameters to
		 * set A-MSDU configuration. Include both Tx and Rx MSDU
		 * aggregation parameters in the command.
		 */
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_TX_MSDU_AGGREGATION,
			       value) ||
		    nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_RX_MSDU_AGGREGATION,
			       value))
			goto fail;
		break;
	case STA_SET_CHAN_WIDTH:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_CHANNEL_WIDTH,
			       value))
			goto fail;
		break;
	case STA_SET_FT_DS:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_FT_OVER_DS,
			       value))
			goto fail;
		break;
	case STA_SET_EHT_EML_CAPABILITY:
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_EML_CAPABILITY,
			       value))
			goto fail;
		break;
	case STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS:
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MAX_SIMULTANEOUS_LINKS,
			       value))
			goto fail;
		break;
	case STA_SET_EHT_MLO_MAX_NUM_LINKS:
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MAX_NUM_LINKS,
			       value))
			goto fail;
		break;
	case STA_SET_EHT_MLO_MODE:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_EHT_MLO_MODE,
			       value))
			goto fail;
		break;
	case STA_SET_DYN_BW:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_DYNAMIC_BW,
			       value))
			goto fail;
		break;
	case STA_SET_AMPDU:
		/* The driver expects both Tx and Rx aggregation parameters to
		 * set A-MPDU configuration. Include both Tx and Rx MPDU
		 * aggregation parameters in the command.
		 */
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_TX_MPDU_AGGREGATION,
			       value) ||
		    nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_RX_MPDU_AGGREGATION,
			       value))
			goto fail;
		break;
	case STA_SET_NSS:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_NSS, value))
			goto fail;
		break;
	case STA_SET_LISTEN_INTERVAL:
		if (nla_put_u32(msg,
				QCA_WLAN_VENDOR_ATTR_CONFIG_LISTEN_INTERVAL,
				value))
			goto fail;
		break;
	case STA_SET_EMLSR_MODE_SWITCH:
		if (nla_put_u8(msg,
			       QCA_WLAN_VENDOR_ATTR_CONFIG_EMLSR_MODE_SWITCH,
			       value))
			goto fail;
		break;
	case STA_SET_EPCS_CAPABILITY:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_EPCS_CAPABILITY,
			       value))
			goto fail;
		break;
	case STA_SET_EPCS_FUNCTION:
		if (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_EPCS_FUNCTION,
			       value))
			goto fail;
		break;
	}
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		return ret;
	}

	return 0;

fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"%s: err in adding vendor_cmd and vendor_data",
			__func__);
	nlmsg_free(msg);
#endif /* NL80211_SUPPORT */
	return -1;
}


void free_dscp_policy_table(struct sigma_dut *dut)
{
	struct dscp_policy_data *dscp_policy;

	while (dut->dscp_policy_table) {
		dscp_policy = dut->dscp_policy_table;
		dut->dscp_policy_table = dscp_policy->next;
		free(dscp_policy);
	}
}


static char * protocol_to_str(int proto)
{
	switch (proto) {
	case 6:
		return "tcp";
	case 17:
		return "udp";
	case 50:
		return "esp";
	default:
		return "unknown";
	}
}


static int delete_nft_table(struct sigma_dut *dut, const char *table,
			   const char *ip_type)
{
	int res;
	char cmd[200];

	res = snprintf(cmd, sizeof(cmd), "nft delete table %s %s_%s", ip_type,
		      table, ip_type);
	if (snprintf_error(sizeof(cmd), res)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to create delete table command");
		return -1;
	}

	if (system(cmd) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run %s", cmd);
		return -1;
	}

	return 0;
}


static int remove_nft_rule(struct sigma_dut *dut, int policy_id,
			   enum ip_version ip_ver)
{
	int res;
	char table[50];

	res = snprintf(table, sizeof(table), "wifi_%s_dscp_policy_%d",
		       dut->station_ifname, policy_id);
	if (snprintf_error(sizeof(table), res)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to create table name for policy %d",
				policy_id);
		return -1;
	}


	if (ip_ver == IPV6)
		return delete_nft_table(dut, table, "ip6");
	else
		return delete_nft_table(dut, table, "ip");
}


static int remove_iptable_rule(struct sigma_dut *dut,
			       struct dscp_policy_data *dscp_policy)
{
	char ip_cmd[1000];
	char *pos;
	int ret, len;
	enum ip_version ip_ver = dscp_policy->ip_version;

	pos = ip_cmd;
	len = sizeof(ip_cmd);

	ret = snprintf(pos, len,
		       "%s -t mangle -D OUTPUT -o %s",
#ifdef ANDROID
		       ip_ver == IPV6 ? "/system/bin/ip6tables" : "/system/bin/iptables",
#else /* ANDROID */
		       ip_ver == IPV6 ? "ip6tables" : "iptables",
#endif /* ANDROID */
		       dut->station_ifname);
	if (snprintf_error(len, ret)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to create delete iptables command %s",
				ip_cmd);
		return -1;
	}

	pos += ret;
	len -= ret;

	if (strlen(dscp_policy->src_ip)) {
		ret = snprintf(pos, len, " -s %s", dscp_policy->src_ip);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding src_ip %s in delete command",
					dscp_policy->src_ip);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (strlen(dscp_policy->dst_ip)) {
		ret = snprintf(pos, len, " -d %s",
			       dscp_policy->dst_ip);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding dst_ip %s in delete cmd",
					dscp_policy->dst_ip);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->src_port || dscp_policy->dst_port ||
	    (dscp_policy->start_port && dscp_policy->end_port)) {
		ret = snprintf(pos, len, " -p %s",
			       protocol_to_str(dscp_policy->protocol));
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding protocol %d in delete command",
					dscp_policy->protocol);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->src_port) {
		ret = snprintf(pos, len, " --sport %d",
			       dscp_policy->src_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding src_port %d in delete command",
					 dscp_policy->src_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->dst_port) {
		ret = snprintf(pos, len, " --dport %d",
			       dscp_policy->dst_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding dst_port %d in delete command",
					 dscp_policy->dst_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->start_port && dscp_policy->end_port) {
		ret = snprintf(pos, len, " --match multiport --dports %d:%d",
			       dscp_policy->start_port,
			       dscp_policy->end_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding start:end port %d:%d in delete command",
					 dscp_policy->start_port,
					 dscp_policy->end_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	ret = snprintf(pos, len, " -j DSCP --set-dscp 0x%0x",
		       dscp_policy->dscp);
	if (snprintf_error(len, ret)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Error in adding dscp %0x in delete command",
				dscp_policy->dscp);
		return -1;
	}
	ret = system(ip_cmd);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "iptables rule: %s err: %d",
			ip_cmd, ret);

	return ret;
}


static int remove_dscp_policy_rule(struct sigma_dut *dut,
				   struct dscp_policy_data *dscp_policy)
{
	return dut->dscp_use_iptables ? remove_iptable_rule(dut, dscp_policy) :
		remove_nft_rule(dut, dscp_policy->policy_id,
				dscp_policy->ip_version);
}


static int create_nft_table(struct sigma_dut *dut, int policy_id,
			    const char *table_name, enum ip_version ip_ver)
{
	char cmd[200];
	int res;

	res = snprintf(cmd, sizeof(cmd), "nft add table %s %s",
		       ip_ver == IPV6 ? "ip6" : "ip", table_name);
	if (snprintf_error(sizeof(cmd), res)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to add rule to create table for policy id %d",
				policy_id);
		return -1;
	}

	if (system(cmd) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run %s", cmd);
		return -1;
	}

	res = snprintf(cmd, sizeof(cmd),
		       "nft add chain %s %s OUTPUT { type filter hook output priority 0 \\; }",
		       ip_ver == IPV6 ? "ip6" : "ip", table_name);
	if (snprintf_error(sizeof(cmd), res)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to add rule to create chain for table = %s",
				table_name);
		return -1;
	}

	if (system(cmd) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run %s", cmd);
		return -1;
	}

	return 0;
}


static int remove_dscp_policy(struct sigma_dut *dut, u8 policy_id)
{
	struct dscp_policy_data *dscp_policy = dut->dscp_policy_table;
	struct dscp_policy_data *prev = NULL;

	while (dscp_policy) {
		if (dscp_policy->policy_id == policy_id)
			break;

		prev = dscp_policy;
		dscp_policy = dscp_policy->next;
	}

	/*
	 * Consider remove request for a policy id which does not exist as
	 * success.
	 */
	if (!dscp_policy)
		return 0;

	if (strlen(dscp_policy->domain_name) == 0 &&
	    remove_dscp_policy_rule(dut, dscp_policy))
		return -1;

	if (prev)
		prev->next = dscp_policy->next;
	else
		dut->dscp_policy_table = dscp_policy->next;

	free(dscp_policy);
	return 0;
}


static int add_nft_rule(struct sigma_dut *dut,
			struct dscp_policy_data *dscp_policy)
{
	char nft_cmd[1000], ip[4], table_name[100];
	char *pos;
	int ret, len, policy_id = dscp_policy->policy_id;
	enum ip_version ip_ver = dscp_policy->ip_version;

	if (ip_ver == IPV6)
		strlcpy(ip, "ip6", sizeof(ip));
	else
		strlcpy(ip, "ip", sizeof(ip));

	ret = snprintf(table_name, sizeof(table_name),
		       "wifi_%s_dscp_policy_%d_%s",
		       dut->station_ifname, policy_id, ip);
	if (snprintf_error(sizeof(table_name), ret))
		return -1;

	if (create_nft_table(dut, policy_id, table_name, ip_ver)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to create nft table");
		return -1;
	}

	pos = nft_cmd;
	len = sizeof(nft_cmd);

	ret = snprintf(pos, len,
		       "nft add rule %s %s OUTPUT oifname \"%s\"",
		       ip, table_name, dut->station_ifname);
	if (snprintf_error(len, ret)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to create nft cmd %s", nft_cmd);
		return -1;
	}

	pos += ret;
	len -= ret;

	if (strlen(dscp_policy->src_ip)) {
		ret = snprintf(pos, len, " %s saddr %s", ip,
			       dscp_policy->src_ip);
		if (snprintf_error(len, ret))
			return -1;

		pos += ret;
		len -= ret;
	}

	if (strlen(dscp_policy->dst_ip)) {
		ret = snprintf(pos, len, " %s daddr %s", ip,
			       dscp_policy->dst_ip);
		if (snprintf_error(len, ret))
			return -1;

		pos += ret;
		len -= ret;
	}

	if (dscp_policy->src_port) {
		ret = snprintf(pos, len, " %s sport %d",
			       protocol_to_str(dscp_policy->protocol),
			       dscp_policy->src_port);
		if (snprintf_error(len, ret))
			return -1;

		pos += ret;
		len -= ret;
	}

	if (dscp_policy->dst_port) {
		ret = snprintf(pos, len, " %s dport %d",
			       protocol_to_str(dscp_policy->protocol),
			       dscp_policy->dst_port);
		if (snprintf_error(len, ret))
			return -1;

		pos += ret;
		len -= ret;
	}

	if (dscp_policy->start_port && dscp_policy->end_port) {
		ret = snprintf(pos, len, " %s dport %d-%d",
			       protocol_to_str(dscp_policy->protocol),
			       dscp_policy->start_port,
			       dscp_policy->end_port);
		if (snprintf_error(len, ret))
			return -1;

		pos += ret;
		len -= ret;
	}

	ret = snprintf(pos, len, " counter %s dscp set 0x%0x", ip,
		       dscp_policy->dscp);
	if (snprintf_error(len, ret))
		return -1;

	ret = system(nft_cmd);
	sigma_dut_print(dut, DUT_MSG_INFO, "nft rule: %s err: %d",
			nft_cmd, ret);

	return ret;
}


static int add_iptable_rule(struct sigma_dut *dut,
			    struct dscp_policy_data *dscp_policy)
{
	char ip_cmd[1000];
	char *pos;
	int ret, len;
	enum ip_version ip_ver = dscp_policy->ip_version;
	struct dscp_policy_data *active_policy = dut->dscp_policy_table;
	int ipv4_rule_num = 1, ipv6_rule_num = 1;

	pos = ip_cmd;
	len = sizeof(ip_cmd);

	/*
	 * DSCP target in the mangle table doesn't stop processing of rules
	 * so to make sure the most granular rule is applied last, add the new
	 * rules in granularity increasing order.
	 */
	while (active_policy) {
		/*
		 * Domain name rules are managed in sigma_dut thus don't count
		 * them while counting the number of active rules.
		 */
		if (strlen(active_policy->domain_name)) {
			active_policy = active_policy->next;
			continue;
		}

		if (active_policy->granularity_score >
		    dscp_policy->granularity_score)
			break;

		if (active_policy->ip_version == IPV6)
			ipv6_rule_num++;
		else
			ipv4_rule_num++;

		active_policy = active_policy->next;
	}

	ret = snprintf(pos, len,
		       "%s -t mangle -I OUTPUT %d -o %s",
#ifdef ANDROID
		       ip_ver == IPV6 ? "/system/bin/ip6tables" : "/system/bin/iptables",
#else /* ANDROID */
		       ip_ver == IPV6 ? "ip6tables" : "iptables",
#endif /* ANDROID */
		       ip_ver == IPV6 ? ipv6_rule_num : ipv4_rule_num,
		       dut->station_ifname);
	if (snprintf_error(len, ret)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to create iptables command %s", ip_cmd);
		return -1;
	}

	pos += ret;
	len -= ret;

	if (strlen(dscp_policy->src_ip)) {
		ret = snprintf(pos, len, " -s %s", dscp_policy->src_ip);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding src_ip %s",
					dscp_policy->src_ip);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (strlen(dscp_policy->dst_ip)) {
		ret = snprintf(pos, len, " -d %s", dscp_policy->dst_ip);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding dst_ip %s",
					dscp_policy->dst_ip);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->src_port || dscp_policy->dst_port ||
	    (dscp_policy->start_port && dscp_policy->end_port)) {
		ret = snprintf(pos, len, " -p %s",
			       protocol_to_str(dscp_policy->protocol));
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding protocol %d in add command",
					 dscp_policy->protocol);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->src_port) {
		ret = snprintf(pos, len, " --sport %d", dscp_policy->src_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding src_port %d",
					 dscp_policy->src_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->dst_port) {
		ret = snprintf(pos, len, " --dport %d", dscp_policy->dst_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding dst_port %d",
					dscp_policy->dst_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	if (dscp_policy->start_port && dscp_policy->end_port) {
		ret = snprintf(pos, len, " --match multiport --dports %d:%d",
			       dscp_policy->start_port, dscp_policy->end_port);
		if (snprintf_error(len, ret)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Error in adding start:end port %d:%d",
					dscp_policy->start_port,
					dscp_policy->end_port);
			return -1;
		}
		pos += ret;
		len -= ret;
	}

	ret = snprintf(pos, len, " -j DSCP --set-dscp 0x%0x",
		       dscp_policy->dscp);
	if (snprintf_error(len, ret)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Error in adding dscp %0x", dscp_policy->dscp);
		return -1;
	}
	ret = system(ip_cmd);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "iptables rule: %s err: %d",
			ip_cmd, ret);

	return ret;
}


static int add_dscp_policy_rule(struct sigma_dut *dut,
				struct dscp_policy_data *dscp_policy)
{
	return dut->dscp_use_iptables ? add_iptable_rule(dut, dscp_policy) :
		add_nft_rule(dut, dscp_policy);
}


static void clear_all_dscp_policies(struct sigma_dut *dut)
{
	free_dscp_policy_table(dut);

	if (dut->dscp_use_iptables) {
#ifdef ANDROID
		if (system("/system/bin/iptables -t mangle -F && /system/bin/iptables -t mangle -X") != 0 ||
		    system("/system/bin/ip6tables -t mangle -F && /system/bin/ip6tables -t mangle -X") != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iptables: Failed to flush DSCP policy");
#else /* ANDROID */
		if (system("iptables -t mangle -F && iptables -t mangle -X") != 0 ||
		    system("ip6tables -t mangle -F && ip6tables -t mangle -X") != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iptables: Failed to flush DSCP policy");
#endif /* ANDROID */
	} else {
		if (system("nft flush ruleset") != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nftables: Failed to flush DSCP policy");
	}
}


static int send_dscp_response(struct sigma_dut *dut,
			      struct dscp_policy_status *status_list,
			      int num_status)
{
	int rem_len, ret;
	char buf[200] = "", *pos, cmd[256];

	pos = buf;
	rem_len = sizeof(buf);

	ret = snprintf(pos, rem_len, " solicited");
	if (snprintf_error(rem_len, ret)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to write DSCP policy response command");
		return -1;
	}
	pos += ret;
	rem_len -= ret;

	for (int i = 0; i < num_status; i++) {
		ret = snprintf(pos, rem_len, " policy_id=%d status=%d",
			       status_list[i].id, status_list[i].status);
		if (snprintf_error(rem_len, ret)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to wite DSCP policy response");
			return -1;
		}

		pos += ret;
		rem_len -= ret;
	}

	ret = snprintf(cmd, sizeof(cmd), "DSCP_RESP%s", buf);
	if (snprintf_error(sizeof(cmd), ret)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to create DSCP Policy Response frame");
		return -1;
	}

	if (wpa_command(dut->station_ifname, cmd) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to send DSCP Policy Response frame");
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"DSCP Policy Response frame sent: %s", cmd);
	return 0;
}


#ifdef ANDROID
static void thread_cancel_handler(int sig)
{
	if (sig == SIGUSR1)
		pthread_exit(0);
}
#endif /* ANDROID */


static void * mon_dscp_policies(void *ptr)
{
	struct sigma_dut *dut = ptr;
	int ret, policy_id;
	struct wpa_ctrl *ctrl;
	char buf[4096], *pos, *end;
	struct dscp_policy_data *policy = NULL, *current_policy, *prev_policy;
	struct dscp_policy_status status_list[10];
	int num_status = 0;
	const char *events[] = {
		"CTRL-EVENT-DISCONNECTED",
		"CTRL-EVENT-DSCP-POLICY",
		NULL
	};
#ifdef ANDROID
	struct sigaction actions;
#endif /* ANDROID */

	ctrl = open_wpa_mon(get_station_ifname(dut));
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return NULL;
	}

#ifdef ANDROID
	memset(&actions, 0, sizeof(actions));
	sigemptyset(&actions.sa_mask);
	actions.sa_flags = 0;
	actions.sa_handler = thread_cancel_handler;
	if (sigaction(SIGUSR1, &actions, NULL) == -1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to register exit handler for %s",
				__func__);
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return NULL;
	}
#endif /* ANDROID */

	while (1) {
		ret = get_wpa_cli_events_timeout(dut, ctrl, events,
						 buf, sizeof(buf), 0);

		if (ret || strlen(buf) == 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Did not receive any event");
			continue;
		}

		if (strstr(buf, "CTRL-EVENT-DISCONNECTED")) {
			clear_all_dscp_policies(dut);
			break;
		}

		if (strstr(buf, "request_start")) {
			num_status = 0;
			if (strstr(buf, "clear_all"))
				clear_all_dscp_policies(dut);
			continue;
		}

		if (strstr(buf, "request_end")) {
			send_dscp_response(dut, status_list, num_status);
			continue;
		}

		if (!strstr(buf, "add") && !strstr(buf, "remove") &&
		    !strstr(buf, "reject")) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Ignore event: %s",
					buf);
			continue;
		}

		pos = strstr(buf, "policy_id=");
		if (!pos) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Policy id not present");
			continue;
		}
		policy_id = atoi(pos + 10);

		if (num_status >= ARRAY_SIZE(status_list)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Max policies allowed per DSCP request reached. Drop policy id %d request",
					policy_id);
			continue;
		}
		status_list[num_status].id = policy_id;

		if (dut->reject_dscp_policies) {
			status_list[num_status].status =
				dut->dscp_reject_resp_code;
			num_status++;
			continue;
		}

		if (strstr(buf, "reject"))
			goto reject;

		/*
		 * In case of "add" also if policy with same policy id exist it
		 * shall be removed. So always call remove_dscp_policy().
		 */
		if (remove_dscp_policy(dut, policy_id))
			goto reject;

		if (strstr(buf, "remove"))
			goto success;

		policy = malloc(sizeof(*policy));
		if (!policy)
			goto reject;

		memset(policy, 0, sizeof(*policy));

		policy->policy_id = policy_id;

		pos = strstr(buf, "dscp=");
		if (!pos) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"DSCP info not present");
			goto reject;
		}
		policy->dscp = atoi(pos + 5);

		pos = strstr(buf, "ip_version=");
		if (!pos) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"IP version info not present");
			goto reject;
		}
		policy->ip_version = atoi(pos + 11);
		if (policy->ip_version)
			policy->granularity_score++;

		pos = strstr(buf, "domain_name=");
		if (pos) {
			pos += 12;
			end = strchr(pos, ' ');
			if (!end)
				end = pos + strlen(pos);

			if (end - pos >= (int) sizeof(policy->domain_name))
				goto reject;

			memcpy(policy->domain_name, pos, end - pos);
			policy->domain_name[end - pos] = '\0';
			policy->granularity_score++;
		}

		pos = strstr(buf, "start_port=");
		if (pos) {
			pos += 11;
			policy->start_port = atoi(pos);
		}

		pos = strstr(buf, "end_port=");
		if (pos) {
			pos += 9;
			policy->end_port = atoi(pos);
		}

		if (policy->start_port && policy->end_port)
			policy->granularity_score++;

		pos = strstr(buf, "src_ip=");
		if (pos) {
			pos += 7;
			end = strchr(pos, ' ');
			if (!end)
				end = pos + strlen(pos);

			if (end - pos >= (int) sizeof(policy->src_ip))
				goto reject;

			memcpy(policy->src_ip, pos, end - pos);
			policy->src_ip[end - pos] = '\0';
			policy->granularity_score++;
		}

		pos = strstr(buf, "dst_ip=");
		if (pos) {
			pos += 7;
			end = strchr(pos, ' ');
			if (!end)
				end = pos + strlen(pos);

			if (end - pos >= (int) sizeof(policy->dst_ip))
				goto reject;

			memcpy(policy->dst_ip, pos, end - pos);
			policy->dst_ip[end - pos] = '\0';
			policy->granularity_score++;
		}

		pos = strstr(buf, "src_port=");
		if (pos) {
			pos += 9;
			policy->src_port = atoi(pos);
			policy->granularity_score++;
		}

		pos = strstr(buf, "dst_port=");
		if (pos) {
			pos += 9;
			policy->dst_port = atoi(pos);
			policy->granularity_score++;
		}

		pos = strstr(buf, "protocol=");
		if (pos) {
			pos += 9;
			policy->protocol = atoi(pos);
			policy->granularity_score++;
		}

		/*
		 * Skip adding nft rules for doman name policies.
		 * Domain name rules are applied in sigma_dut itself.
		 */
		if (!strlen(policy->domain_name) &&
		    add_dscp_policy_rule(dut, policy))
			goto reject;

		/*
		 * Add the new policy in policy table in granularity increasing
		 * order.
		 */
		current_policy = dut->dscp_policy_table;
		prev_policy = NULL;

		while (current_policy) {
			if (current_policy->granularity_score >
			    policy->granularity_score)
				break;

			prev_policy = current_policy;
			current_policy = current_policy->next;
		}

		if (prev_policy)
			prev_policy->next = policy;
		else
			dut->dscp_policy_table = policy;

		policy->next = current_policy;

success:
		status_list[num_status].status = DSCP_POLICY_SUCCESS;
		num_status++;
		policy = NULL;
		continue;
reject:
		status_list[num_status].status = DSCP_POLICY_REJECT;
		num_status++;
		free(policy);
		policy = NULL;
	}

	free_dscp_policy_table(dut);
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	pthread_exit(0);
	return NULL;
}


static void start_dscp_policy_mon_thread(struct sigma_dut *dut)
{
	/* Create event thread */
	pthread_create(&dut->dscp_policy_mon_thread, NULL, &mon_dscp_policies,
		       (void *) dut);
}


void stop_dscp_policy_mon_thread(struct sigma_dut *dut)
{
	if (dut->dscp_policy_mon_thread) {
#ifdef ANDROID
		/* pthread_cancel not supported in Android */
		pthread_kill(dut->dscp_policy_mon_thread, SIGUSR1);
#else /* ANDROID */
		pthread_cancel(dut->dscp_policy_mon_thread);
#endif /* ANDROID */
		dut->dscp_policy_mon_thread = 0;
	}
}


static enum sigma_cmd_result cmd_sta_associate(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *ssid = get_param(cmd, "ssid");
	const char *wps_param = get_param(cmd, "WPS");
	const char *bssid = get_param(cmd, "bssid");
	const char *ap_link_mac = get_param(cmd, "AP_Link_MAC");
	const char *chan = get_param(cmd, "channel");
	const char *multi_link = get_param(cmd, "MultiLink");
	const char *network_mode = get_param(cmd, "network_mode");
	const char *ifname = get_station_ifname(dut);
	int wps = 0;
	char buf[1000], extra[50];
	int e;
	enum sigma_cmd_result ret = SUCCESS_SEND_STATUS;
	struct wpa_ctrl *ctrl = NULL;
	int num_network_not_found = 0;
	int num_disconnected = 0;
	int tod = -1;

	if (ssid == NULL)
		return -1;

	dut->server_cert_tod = 0;

	if ((dut->program == PROGRAM_EHT || dut->device_mode == MODE_11BE) &&
	    get_driver_type(dut) == DRIVER_WCN) {
		if (multi_link) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"multi link %s", multi_link);
			if (strcasecmp(multi_link, "Enable") == 0)
				sta_config_params(dut, intf,
						  STA_SET_EHT_MLO_MAX_NUM_LINKS,
						  2);
			else
				sta_config_params(dut, intf,
						  STA_SET_EHT_MLO_MAX_NUM_LINKS,
						  1);
		} else {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"multi link config is not present");
			sta_config_params(dut, intf,
					  STA_SET_EHT_MLO_MAX_NUM_LINKS, 1);
		}
	}

	if (dut->rsne_override) {
#ifdef NL80211_SUPPORT
		if (get_driver_type(dut) == DRIVER_WCN) {
			sta_config_params(dut, intf, STA_SET_RSNIE, 1);
			dut->config_rsnie = 1;
		}
#endif /* NL80211_SUPPORT */
		snprintf(buf, sizeof(buf), "TEST_ASSOC_IE %s",
			 dut->rsne_override);
		if (wpa_command(get_station_ifname(dut), buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set DEV_CONFIGURE_IE RSNE override");
			return 0;
		}
	}

	if (dut->sae_commit_override) {
		snprintf(buf, sizeof(buf), "SET sae_commit_override %s",
			 dut->sae_commit_override);
		if (wpa_command(get_station_ifname(dut), buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set SAE commit override");
			return 0;
		}
	}
#ifdef ANDROID
	if (dut->fils_hlp)
		process_fils_hlp(dut);
#endif /* ANDROID */

	if (wps_param &&
	    (strcmp(wps_param, "1") == 0 || strcasecmp(wps_param, "On") == 0))
		wps = 1;

	if (wps) {
		if (dut->program == PROGRAM_60GHZ && network_mode &&
		    strcasecmp(network_mode, "PBSS") == 0 &&
		    set_network(get_station_ifname(dut), dut->infra_network_id,
				"pbss", "1") < 0)
			return -2;

		if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS "
				  "parameters not yet set");
			return 0;
		}
		if (dut->wps_method == WFA_CS_WPS_PBC) {
			if (wpa_command(get_station_ifname(dut), "WPS_PBC") < 0)
				return -2;
		} else {
			snprintf(buf, sizeof(buf), "WPS_PIN any %s",
				 dut->wps_pin);
			if (wpa_command(get_station_ifname(dut), buf) < 0)
				return -2;
		}
	} else {
		if (strcmp(ssid, dut->infra_ssid) == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"sta_associate for the most recently added network");
		} else if (find_network(dut, ssid) < 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"sta_associate for a previously stored network profile");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Profile not found");
			return 0;
		}

		if (bssid &&
		    set_network(get_station_ifname(dut), dut->infra_network_id,
				"bssid", bssid) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "Invalid bssid argument");
			return 0;
		} else if (ap_link_mac &&
			   set_network(get_station_ifname(dut),
				       dut->infra_network_id,
				       "bssid", ap_link_mac) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid bssid argument");
			return 0;
		}

		if ((dut->program == PROGRAM_WPA3 &&
		     dut->sta_associate_wait_connect) ||
		    dut->program == PROGRAM_QM || ap_link_mac ||
		    (dut->dhcp_client_running && dut->client_privacy)) {
			ctrl = open_wpa_mon(get_station_ifname(dut));
			if (!ctrl)
				return ERROR_SEND_STATUS;
		}

		extra[0] = '\0';
		if (chan)
			snprintf(extra, sizeof(extra), " freq=%u",
				 channel_to_freq(dut, atoi(chan)));
		snprintf(buf, sizeof(buf), "SELECT_NETWORK %d%s",
			 dut->infra_network_id, extra);
		if (wpa_command(get_station_ifname(dut), buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to select "
					"network id %d on %s",
					dut->infra_network_id,
					get_station_ifname(dut));
			ret = ERROR_SEND_STATUS;
			goto done;
		}
	}

	if (!ctrl)
		return SUCCESS_SEND_STATUS;

	/* Wait for connection result to be able to store server certificate
	 * hash for trust root override testing
	 * (dev_exec_action,ServerCertTrust). */

	for (e = 0; e < 20; e++) {
		const char *events[] = {
			"CTRL-EVENT-EAP-PEER-CERT",
			"CTRL-EVENT-EAP-TLS-CERT-ERROR",
			"CTRL-EVENT-DISCONNECTED",
			"CTRL-EVENT-CONNECTED",
			"CTRL-EVENT-NETWORK-NOT-FOUND",
			NULL
		};
		char buf[1024];
		int res;

		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "Result,Association did not complete");
			ret = STATUS_SENT_ERROR;
			break;
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Connection event: %s",
				buf);

		if (strstr(buf, "CTRL-EVENT-EAP-PEER-CERT") &&
		    strstr(buf, " depth=0")) {
			char *pos = strstr(buf, " hash=");

			if (pos) {
				char *end;

				if (strstr(buf, " tod=1"))
					tod = 1;
				else if (strstr(buf, " tod=2"))
					tod = 2;
				else
					tod = 0;
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Server certificate TOD policy: %d",
						tod);
				dut->server_cert_tod = tod;

				pos += 6;
				end = strchr(pos, ' ');
				if (end)
					*end = '\0';
				strlcpy(dut->server_cert_hash, pos,
					sizeof(dut->server_cert_hash));
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Server certificate hash: %s",
						dut->server_cert_hash);
			}
		}

		if (strstr(buf, "CTRL-EVENT-EAP-TLS-CERT-ERROR")) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "Result,TLS server certificate validation failed");
			ret = STATUS_SENT_ERROR;
			break;
		}

		if (strstr(buf, "CTRL-EVENT-NETWORK-NOT-FOUND")) {
			num_network_not_found++;

			if (num_network_not_found > 2) {
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "Result,Network not found");
				ret = STATUS_SENT_ERROR;
				break;
			}
		}

		if (strstr(buf, "CTRL-EVENT-DISCONNECTED")) {
			num_disconnected++;

			if (num_disconnected > 2) {
				send_resp(dut, conn, SIGMA_COMPLETE,
					  "Result,Connection failed");
				ret = STATUS_SENT_ERROR;
				break;
			}
		}

		if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
			if (dut->dhcp_client_running && dut->client_privacy) {
				/*
				 * Interface MAC address will be changed by
				 * wpa_supplicant before connection attempt when
				 * client privacy enabled. Restart DHCP client
				 * to make sure DHCP frames use the correct
				 * source MAC address.
				 * */
				kill_dhcp_client(dut, ifname);
				if (start_dhcp_client(dut, ifname) < 0) {
					send_resp(dut, conn, SIGMA_COMPLETE,
						  "Result,DHCP client start failed");
					ret = STATUS_SENT_ERROR;
					break;
				}
			}
			if (tod >= 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Network profile TOD policy update: %d -> %d",
						dut->sta_tod_policy, tod);
				dut->sta_tod_policy = tod;
			}
			if (dut->program == PROGRAM_QM) {
				unsigned char iface_mac_addr[ETH_ALEN];
				char ipv6[100];

				if (get_hwaddr(ifname, iface_mac_addr) < 0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"%s: get_hwaddr %s failed",
							__func__, ifname);
					ret = ERROR_SEND_STATUS;
					break;
				}

				convert_mac_addr_to_ipv6_lladdr(iface_mac_addr,
								ipv6,
								sizeof(ipv6));

				if (set_ipv6_addr(dut, ipv6, "64", ifname) !=
				    0) {
					ret = ERROR_SEND_STATUS;
					break;
				}
				start_dscp_policy_mon_thread(dut);
			}
			if (ap_link_mac &&
			    set_network(intf, dut->infra_network_id, "bssid",
					"any") < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set bssid to any after connecting with AP link MAC");
				ret = ERROR_SEND_STATUS;
			}
			break;
		}
	}
done:
	if (ctrl) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
	}
	return ret;
}


static int run_hs20_osu(struct sigma_dut *dut, const char *params)
{
	char buf[500], cmd[200];
	int res;

	/* Use hs20-osu-client file at the current dir, if found; otherwise use
	 * default path */
	res = snprintf(cmd, sizeof(cmd),
		       "%s -w \"%s\" -r hs20-osu-client.res %s%s -dddKt -f Logs/hs20-osu-client.txt",
		       file_exists("./hs20-osu-client") ?
		       "./hs20-osu-client" : "hs20-osu-client",
		       sigma_wpas_ctrl,
		       dut->summary_log ? "-s " : "",
		       dut->summary_log ? dut->summary_log : "");
	if (res < 0 || res >= (int) sizeof(cmd))
		return -1;

	res = snprintf(buf, sizeof(buf), "%s %s", cmd, params);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run: %s", buf);
		return -1;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Completed hs20-osu-client operation");

	return 0;
}


static int download_ppsmo(struct sigma_dut *dut,
			  struct sigma_conn *conn,
			  const char *intf,
			  struct sigma_cmd *cmd)
{
	const char *name, *path, *val;
	char url[500], buf[600], fbuf[100];
	char *fqdn = NULL;

	name = get_param(cmd, "FileName");
	path = get_param(cmd, "FilePath");
	if (name == NULL || path == NULL)
		return -1;

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(url, sizeof(url), "PPS/%s", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured PPS MO "
				"from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "PPS MO file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s pps-tnds.xml", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy PPS MO");
			return 0;
		}
	} else if (strncasecmp(path, "http:", 5) != 0 &&
		   strncasecmp(path, "https:", 6) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
			  "Unsupported FilePath value");
		return 0;
	} else {
		snprintf(url, sizeof(url), "%s/%s", path, name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Downloading PPS MO from %s",
				url);
		snprintf(buf, sizeof(buf), "wget -T 10 -t 3 -O pps-tnds.xml '%s'", url);
		remove("pps-tnds.xml");
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download PPS MO");
			return 0;
		}
	}

	if (run_hs20_osu(dut, "from_tnds pps-tnds.xml pps.xml") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to parse downloaded PPSMO");
		return 0;
	}
	unlink("pps-tnds.xml");

	val = get_param(cmd, "managementTreeURI");
	if (val) {
		const char *pos, *end;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "managementTreeURI: %s",
				val);
		if (strncmp(val, "./Wi-Fi/", 8) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid managementTreeURI prefix");
			return 0;
		}
		pos = val + 8;
		end = strchr(pos, '/');
		if (end == NULL ||
		    strcmp(end, "/PerProviderSubscription") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid managementTreeURI postfix");
			return 0;
		}
		if (end - pos >= (int) sizeof(fbuf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Too long FQDN in managementTreeURI");
			return 0;
		}
		memcpy(fbuf, pos, end - pos);
		fbuf[end - pos] = '\0';
		fqdn = fbuf;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"FQDN from managementTreeURI: %s", fqdn);
	} else if (run_hs20_osu(dut, "get_fqdn pps.xml") == 0) {
		FILE *f = fopen("pps-fqdn", "r");
		if (f) {
			if (fgets(fbuf, sizeof(fbuf), f)) {
				fbuf[sizeof(fbuf) - 1] = '\0';
				fqdn = fbuf;
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Use FQDN %s", fqdn);
			}
			fclose(f);
		}
	}

	if (fqdn == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,No FQDN specified");
		return 0;
	}

	mkdir("SP", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	snprintf(buf, sizeof(buf), "SP/%s", fqdn);
	mkdir(buf, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);

	snprintf(buf, sizeof(buf), "SP/%s/pps.xml", fqdn);
	if (rename("pps.xml", buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not move PPS MO");
		return 0;
	}

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(buf, sizeof(buf), "cp Certs/ca.pem SP/%s/ca.pem",
			 fqdn);
		if (system(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy OSU CA cert");
			return 0;
		}

		snprintf(buf, sizeof(buf),
			 "cp Certs/aaa-ca.pem SP/%s/aaa-ca.pem",
			 fqdn);
		if (system(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy AAA CA cert");
			return 0;
		}
	} else {
		snprintf(buf, sizeof(buf),
			 "dl_osu_ca SP/%s/pps.xml SP/%s/ca.pem",
			 fqdn, fqdn);
		if (run_hs20_osu(dut, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download OSU CA cert");
			return 0;
		}

		snprintf(buf, sizeof(buf),
			 "dl_aaa_ca SP/%s/pps.xml SP/%s/aaa-ca.pem",
			 fqdn, fqdn);
		if (run_hs20_osu(dut, buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to download AAA CA cert");
		}
	}

	if (file_exists("next-client-cert.pem")) {
		snprintf(buf, sizeof(buf), "SP/%s/client-cert.pem", fqdn);
		if (rename("next-client-cert.pem", buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not move client certificate");
			return 0;
		}
	}

	if (file_exists("next-client-key.pem")) {
		snprintf(buf, sizeof(buf), "SP/%s/client-key.pem", fqdn);
		if (rename("next-client-key.pem", buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not move client key");
			return 0;
		}
	}

	snprintf(buf, sizeof(buf), "set_pps SP/%s/pps.xml", fqdn);
	if (run_hs20_osu(dut, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to configure credential from "
			  "PPSMO");
		return 0;
	}

	return 1;
}


static int download_cert(struct sigma_dut *dut,
			 struct sigma_conn *conn,
			 const char *intf,
			 struct sigma_cmd *cmd)
{
	const char *name, *path;
	char url[500], buf[600];

	name = get_param(cmd, "FileName");
	path = get_param(cmd, "FilePath");
	if (name == NULL || path == NULL)
		return -1;

	if (strcasecmp(path, "VendorSpecific") == 0) {
		snprintf(url, sizeof(url), "Certs/%s-cert.pem", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured client "
				"certificate from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "certificate file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s next-client-cert.pem", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client "
				  "certificate");
			return 0;
		}

		snprintf(url, sizeof(url), "Certs/%s-key.pem", name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Use pre-configured client "
				"private key from the device (%s)", url);
		if (!file_exists(url)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Requested "
				  "private key file does not exist");
			return 0;
		}
		snprintf(buf, sizeof(buf), "cp %s next-client-key.pem", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client key");
			return 0;
		}
	} else if (strncasecmp(path, "http:", 5) != 0 &&
		   strncasecmp(path, "https:", 6) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
			  "Unsupported FilePath value");
		return 0;
	} else {
		snprintf(url, sizeof(url), "%s/%s.pem", path, name);
		sigma_dut_print(dut, DUT_MSG_INFO, "Downloading client "
				"certificate/key from %s", url);
		snprintf(buf, sizeof(buf),
			 "wget -T 10 -t 3 -O next-client-cert.pem '%s'", url);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to download client "
				  "certificate");
			return 0;
		}

		if (system("cp next-client-cert.pem next-client-key.pem") != 0)
		{
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to copy client key");
			return 0;
		}
	}

	return 1;
}


static int cmd_sta_preset_testparameters_60ghz(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	const char *val;
	const char *intf = get_param(cmd, "interface");

	if (!intf)
		return -1;

	val = get_param(cmd, "WscIEFragment");
	if (val && strcasecmp(val, "enable") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Enable WSC IE fragmentation");

		dut->wsc_fragment = 1;
		/* set long attributes to force fragmentation */
		if (wpa_command(intf, "SET device_name "
				WPS_LONG_DEVICE_NAME) < 0)
			return -2;
		if (wpa_command(intf, "SET manufacturer "
				WPS_LONG_MANUFACTURER) < 0)
			return -2;
		if (wpa_command(intf, "SET model_name "
				WPS_LONG_MODEL_NAME) < 0)
			return -2;
		if (wpa_command(intf, "SET model_number "
				WPS_LONG_MODEL_NUMBER) < 0)
			return -2;
		if (wpa_command(intf, "SET serial_number "
				WPS_LONG_SERIAL_NUMBER) < 0)
			return -2;
	}

	val = get_param(cmd, "RSN_IE");
	if (val) {
		if (strcasecmp(val, "disable") == 0)
			dut->force_rsn_ie = FORCE_RSN_IE_REMOVE;
		else if (strcasecmp(val, "enable") == 0)
			dut->force_rsn_ie = FORCE_RSN_IE_ADD;
	}

	val = get_param(cmd, "WpsVersion");
	if (val)
		dut->wps_forced_version = get_wps_forced_version(dut, val);

	val = get_param(cmd, "WscEAPFragment");
	if (val && strcasecmp(val, "enable") == 0)
		dut->eap_fragment = 1;

	return 1;
}


static int cmd_sta_preset_testparameters_hs2_r2(struct sigma_dut *dut,
						struct sigma_conn *conn,
						const char *intf,
						struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "FileType");
	if (val && strcasecmp(val, "PPSMO") == 0)
		return download_ppsmo(dut, conn, intf, cmd);
	if (val && strcasecmp(val, "CERT") == 0)
		return download_cert(dut, conn, intf, cmd);
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported FileType");
		return 0;
	}

	return 1;
}


static int cmd_sta_preset_testparameters_oce(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     const char *intf,
					     struct sigma_cmd *cmd)
{
	const char *val;
	char buf[1000];
	char text[20];
	unsigned char addr[ETH_ALEN];

	val = get_param(cmd, "OCESupport");
	if (val && strcasecmp(val, "Disable") == 0) {
		if (wpa_command(intf, "SET oce 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable OCE");
			return 0;
		}
	} else if (val && strcasecmp(val, "Enable") == 0) {
		if (wpa_command(intf, "SET oce 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable OCE");
			return 0;
		}
	}

	val = get_param(cmd, "FILScap");
	if (val && (atoi(val) == 1)) {
		if (wpa_command(intf, "SET disable_fils 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable FILS");
			return 0;
		}
	} else if (val && (atoi(val) == 0)) {
		if (wpa_command(intf, "SET disable_fils 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable FILS");
			return 0;
		}
	}

	val = get_param(cmd, "FILSHLP");
	if (val && strcasecmp(val, "Enable") == 0) {
		if (get_wpa_status(get_station_ifname(dut), "address", text,
				   sizeof(text)) < 0)
			return -2;
		hwaddr_aton(text, addr);
		snprintf(buf, sizeof(buf),
			 "FILS_HLP_REQ_ADD ff:ff:ff:ff:ff:ff "
			 "080045100140000040004011399e00000000ffffffff00440043"
			 "012cb30001010600fd4f46410000000000000000000000000000"
			 "000000000000"
			 "%02x%02x%02x%02x%02x%02x"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000000000000000"
			 "0000000000000000000000000000000000000000638253633501"
			 "013d0701000af549d29b390205dc3c12616e64726f69642d6468"
			 "63702d382e302e30370a0103060f1a1c333a3b2b5000ff00",
			 addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to add HLP");
			return 0;
		}
		dut->fils_hlp = 1;
	}

	return 1;
}


static void ath_sta_set_noack(struct sigma_dut *dut, const char *intf,
			      const char *val)
{
	int counter = 0;
	char token[50];
	char *result;
	char *saveptr;

	strlcpy(token, val, sizeof(token));
	token[sizeof(token) - 1] = '\0';
	result = strtok_r(token, ":", &saveptr);
	while (result) {
		if (strcmp(result, "disable") == 0)
			run_iwpriv(dut, intf, "noackpolicy %d 1 0", counter);
		else
			run_iwpriv(dut, intf, "noackpolicy %d 1 1", counter);
		result = strtok_r(NULL, ":", &saveptr);
		counter++;
	}
}


static void ath_sta_set_rts(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	char buf[100];

	snprintf(buf, sizeof(buf), "iwconfig %s rts %s", intf, val);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwconfig RTS failed");
	}
}


static void ath_sta_set_wmm(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	if (strcasecmp(val, "off") == 0) {
		run_iwpriv(dut, intf, "wmm 0");
	}
}


static int wcn_sta_set_wmm(struct sigma_dut *dut, const char *intf,
			   const char *val)
{
#ifdef NL80211_SUPPORT
	int wmmenable = 1;

	if (val &&
	    (strcasecmp(val, "off") == 0 || strcmp(val, "0") == 0))
		wmmenable = 0;

	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_WMM_ENABLE,
		wmmenable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"WMM cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static void ath_sta_set_sgi(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	int sgi20;

	sgi20 = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;

	run_iwpriv(dut, intf, "shortgi %d", sgi20);
}


static void ath_sta_set_11nrates(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	int rate_code, v;

	/* Disable Tx Beam forming when using a fixed rate */
	ath_disable_txbf(dut, intf);

	v = atoi(val);
	if (v < 0 || v > 32) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid Fixed MCS rate: %d", v);
		return;
	}
	rate_code = 0x80 + v;

	run_iwpriv(dut, intf, "set11NRates 0x%x", rate_code);

	/* Channel width gets messed up, fix this */
	run_iwpriv(dut, intf, "chwidth %d", dut->chwidth);
}


static void iwpriv_sta_set_amsdu(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	int ret;
	int value;

	if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)
		value = 2;
	else
		value = 1;

#ifdef NL80211_SUPPORT
	ret = sta_config_params(dut, intf, STA_SET_AMSDU, value);
	if (!ret)
		return;
	sigma_dut_print(dut, DUT_MSG_ERROR, "set amsdu nl cmd failed");
	/* Try to use the old iwpriv command instead. */
#endif /* NL80211_SUPPORT */

	ret = run_iwpriv(dut, intf, "amsdu %d", value);
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv amsdu failed");
}


static int iwpriv_sta_set_ampdu(struct sigma_dut *dut, const char *intf,
				int ampdu)
{
	char buf[60];
	int maxaggregation = 63;

	if (ampdu)
		ampdu = maxaggregation;
	snprintf(buf, sizeof(buf), "iwpriv %s ampdu %d", intf, ampdu);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv ampdu failed");
		return -1;
	}

	return 0;
}


static int wcn_sta_set_ampdu(struct sigma_dut *dut, const char *intf,
			     int ampdu)
{
	int maxaggregation = 63;

	if (ampdu)
		ampdu = maxaggregation;

#ifdef NL80211_SUPPORT
	if (!sta_config_params(dut, intf, STA_SET_AMPDU, ampdu))
		return 0;
	sigma_dut_print(dut, DUT_MSG_ERROR, "set ampdu nl cmd failed");
#endif /* NL80211_SUPPORT */

	return iwpriv_sta_set_ampdu(dut, intf, ampdu);

}


static void ath_sta_set_stbc(struct sigma_dut *dut, const char *intf,
			     const char *val)
{
	run_iwpriv(dut, intf, "tx_stbc %s", val);
	run_iwpriv(dut, intf, "rx_stbc %s", val);
}


static int wcn_sta_set_cts_width(struct sigma_dut *dut, const char *intf,
				  const char *val)
{
	char buf[60];

	if (strcmp(val, "160") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 5", intf);
	} else if (strcmp(val, "80") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 3", intf);
	} else if (strcmp(val, "40") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 2", intf);
	} else if (strcmp(val, "20") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s cts_cbw 1", intf);
	} else if (strcasecmp(val, "Auto") == 0) {
		buf[0] = '\0';
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"WIDTH/CTS_WIDTH value not supported");
		return -1;
	}

	if (buf[0] != '\0' && system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set WIDTH/CTS_WIDTH");
		return -1;
	}

	return 0;
}


int ath_set_width(struct sigma_dut *dut, struct sigma_conn *conn,
		  const char *intf, const char *val)
{
	if (strcasecmp(val, "Auto") == 0) {
		run_iwpriv(dut, intf, "chwidth 0");
		dut->chwidth = 0;
	} else if (strcasecmp(val, "20") == 0) {
		run_iwpriv(dut, intf, "chwidth 0");
		dut->chwidth = 0;
	} else if (strcasecmp(val, "40") == 0) {
		run_iwpriv(dut, intf, "chwidth 1");
		dut->chwidth = 1;
	} else if (strcasecmp(val, "80") == 0) {
		run_iwpriv(dut, intf, "chwidth 2");
		dut->chwidth = 2;
	} else if (strcasecmp(val, "160") == 0) {
		run_iwpriv(dut, intf, "chwidth 3");
		dut->chwidth = 3;
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,WIDTH not supported");
		return -1;
	}

	return 0;
}


static int wcn_sta_set_sp_stream(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	int sta_nss;

	if (strcmp(val, "1SS") == 0 || strcmp(val, "1") == 0) {
		sta_nss = 1;
	} else if (strcmp(val, "2SS") == 0 || strcmp(val, "2") == 0) {
		sta_nss = 2;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"SP_STREAM value not supported");
		return -1;
	}

#ifdef NL80211_SUPPORT
	if (!sta_config_params(dut, intf, STA_SET_NSS, sta_nss)) {
		dut->sta_nss = sta_nss;
		return 0;
	}
	sigma_dut_print(dut, DUT_MSG_ERROR, "set Nss nl cmd failed");
#endif /* NL80211_SUPPORT */

	if (run_iwpriv(dut, intf, "nss %d", sta_nss) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set SP_STREAM");
		return -1;
	}

	dut->sta_nss = sta_nss;

	return 0;
}


static void wcn_sta_set_stbc(struct sigma_dut *dut, const char *intf,
			     const char *val)
{
	char buf[60];
	int ret;

	snprintf(buf, sizeof(buf), "iwpriv %s tx_stbc %s", intf, val);
	ret = system(buf);
#ifdef NL80211_SUPPORT
	if (ret)
		ret = sta_config_params(dut, intf, STA_SET_TX_STBC,
					strcmp(val, "0") == 0 ? 0 : 1);
#endif /* NL80211_SUPPORT */
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv tx_stbc failed");

	snprintf(buf, sizeof(buf), "iwpriv %s rx_stbc %s", intf, val);
	ret = system(buf);
#ifdef NL80211_SUPPORT
	if (ret)
		ret = sta_config_params(dut, intf, STA_SET_RX_STBC,
					strcmp(val, "0") == 0 ? 0 : 1);
#endif /* NL80211_SUPPORT */
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv rx_stbc failed");
}


static int mbo_set_cellular_data_capa(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      const char *intf, int capa)
{
	char buf[32];

	if (capa > 0 && capa < 4) {
		snprintf(buf, sizeof(buf), "SET mbo_cell_capa %d", capa);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode, Failed to set cellular data capability");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid Cellular data capability: %d", capa);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Invalid cellular data capability");
	return 0;
}


static int mbo_set_roaming(struct sigma_dut *dut, struct sigma_conn *conn,
			   const char *intf, const char *val)
{
	if (strcasecmp(val, "Disable") == 0) {
		if (wpa_command(intf, "SET roaming 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable roaming");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Enable") == 0) {
		if (wpa_command(intf, "SET roaming 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable roaming");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for roaming: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for Roaming");
	return 0;
}


static int mbo_set_assoc_disallow(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *intf, const char *val)
{
	if (strcasecmp(val, "Disable") == 0) {
		if (wpa_command(intf, "SET ignore_assoc_disallow 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to disable Assoc_disallow");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Enable") == 0) {
		if (wpa_command(intf, "SET ignore_assoc_disallow 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable Assoc_disallow");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for Assoc_disallow: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for Assoc_disallow");
	return 0;
}


static int mbo_set_bss_trans_req(struct sigma_dut *dut, struct sigma_conn *conn,
				 const char *intf, const char *val)
{
	if (strcasecmp(val, "Reject") == 0) {
		if (wpa_command(intf, "SET reject_btm_req_reason 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to Reject BTM Request");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "Accept") == 0) {
		if (wpa_command(intf, "SET reject_btm_req_reason 0") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to Accept BTM Request");
			return 0;
		}
		return 1;
	}

	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Invalid value provided for BSS_Transition: %s", val);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,Unknown value provided for BSS_Transition");
	return 0;
}


static int mbo_set_non_pref_ch_list(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    const char *intf,
				    struct sigma_cmd *cmd)
{
	const char *ch, *pref, *op_class, *reason;
	char buf[120];
	int len, ret;

	pref = get_param(cmd, "Ch_Pref");
	if (!pref)
		return 1;

	if (strcasecmp(pref, "clear") == 0) {
		free(dut->non_pref_ch_list);
		dut->non_pref_ch_list = NULL;
	} else {
		op_class = get_param(cmd, "Ch_Op_Class");
		if (!op_class) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Op_Class not provided");
			return 0;
		}

		ch = get_param(cmd, "Ch_Pref_Num");
		if (!ch) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Pref_Num not provided");
			return 0;
		}

		reason = get_param(cmd, "Ch_Reason_Code");
		if (!reason) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Ch_Reason_Code not provided");
			return 0;
		}

		if (!dut->non_pref_ch_list) {
			dut->non_pref_ch_list =
				calloc(1, NON_PREF_CH_LIST_SIZE);
			if (!dut->non_pref_ch_list) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to allocate memory for non_pref_ch_list");
				return 0;
			}
		}
		len = strlen(dut->non_pref_ch_list);
		ret = snprintf(dut->non_pref_ch_list + len,
			       NON_PREF_CH_LIST_SIZE - len,
			       " %s:%s:%s:%s", op_class, ch, pref, reason);
		if (ret > 0 && ret < NON_PREF_CH_LIST_SIZE - len) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "non_pref_list: %s",
					dut->non_pref_ch_list);
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"snprintf failed for non_pref_list, ret = %d",
					ret);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,snprintf failed");
			free(dut->non_pref_ch_list);
			dut->non_pref_ch_list = NULL;
			return 0;
		}
	}

	ret = snprintf(buf, sizeof(buf), "SET non_pref_chan%s",
		       dut->non_pref_ch_list ? dut->non_pref_ch_list : " ");
	if (ret < 0 || ret >= (int) sizeof(buf)) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"snprintf failed for set non_pref_chan, ret: %d",
				ret);
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,snprint failed");
		return 0;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set non-preferred channel list");
		return 0;
	}

	return 1;
}


#ifdef NL80211_SUPPORT

static int sta_set_he_htc_supp(struct sigma_dut *dut, const char *intf,
			       uint8_t cfg)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_HTC_HE_SUPP,
		cfg);
}


static int sta_set_he_fragmentation(struct sigma_dut *dut, const char *intf,
				    enum he_fragmentation_val frag)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_FRAGMENTATION, frag);
}


int wcn_set_he_ltf(struct sigma_dut *dut, const char *intf,
		   enum qca_wlan_he_ltf_cfg ltf)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_LTF, ltf);
}


static int nlvendor_sta_set_noack(struct sigma_dut *dut, const char *intf,
				  int noack, enum qca_wlan_ac_type ac)
{
	struct nl_msg *msg;
	int ret = 0;
	struct nlattr *params;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION) ||
	    !(params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ENABLE_NO_ACK,
		       noack) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_NO_ACK_AC,
		       ac)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}
	return ret;
}


static void wcn_sta_set_noack(struct sigma_dut *dut, const char *intf,
			      const char *val)
{
	int noack, ret;
	char token[100];
	char *result;
	char *saveptr;
	enum qca_wlan_ac_type ac = QCA_WLAN_AC_BE;

	strlcpy(token, val, sizeof(token));
	token[sizeof(token) - 1] = '\0';
	result = strtok_r(token, ":", &saveptr);
	while (result) {
		noack = strcasecmp(result, "Disable") != 0;
		ret = nlvendor_sta_set_noack(dut, intf, noack, ac);
		if (ret) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nlvendor_sta_set_noack failed for ac:%d, ret:%d",
					ac, ret);
		}
		result = strtok_r(NULL, ":", &saveptr);
		ac++;
	}
}


static int nlvendor_sta_set_phymode(struct sigma_dut *dut, const char *intf,
				    enum qca_wlan_vendor_phy_mode val)
{
	struct nl_msg *msg;
	struct nlattr *params;
	int ifindex, ret;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s not found",
				__func__, intf);
		return -1;
	}

	msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
			      NL80211_CMD_VENDOR);
	if (!msg) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd", __func__);
		return -1;
	}

	if (nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_SET_WIFI_CONFIGURATION)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_attr", __func__);
		nlmsg_free(msg);
		return -1;
	}

	params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!params || nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_PHY_MODE,
				   val)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_data", __func__);
		nlmsg_free(msg);
		return -1;
	}

	nla_nest_end(msg, params);
	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d (%s)",
				__func__, ret, strerror(-ret));
		return ret;
	}

	return 0;
}


static enum qca_wlan_vendor_phy_mode get_qca_vendor_phymode(const char *val)
{
	if (strcmp(val, "11a") == 0) {
		/* IEEE80211_MODE_11A */
		return QCA_WLAN_VENDOR_PHY_MODE_11A;
	}

	if (strcmp(val, "11g") == 0) {
		/* IEEE80211_MODE_11G */
		return QCA_WLAN_VENDOR_PHY_MODE_11G;
	}

	if (strcmp(val, "11b") == 0) {
		/* IEEE80211_MODE_11B */
		return QCA_WLAN_VENDOR_PHY_MODE_11B;
	}

	if (strcmp(val, "11n") == 0 ||
		   strcmp(val, "11nl") == 0 ||
		   strcmp(val, "11nl(nabg)") == 0) {
		/* IEEE80211_MODE_11AGN */
		return QCA_WLAN_VENDOR_PHY_MODE_11AGN;
	}

	if (strcmp(val, "11ng") == 0) {
		/* IEEE80211_MODE_11NG_HT40 */
		return QCA_WLAN_VENDOR_PHY_MODE_11NG_HT40;
	}

	if (strcmp(val, "AC") == 0 ||
		   strcasecmp(val, "11AC") == 0) {
		/* IEEE80211_MODE_11AC_VHT80 */
		return QCA_WLAN_VENDOR_PHY_MODE_11AC_VHT80;
	}

	if (strcmp(val, "11na") == 0 ||
		   strcasecmp(val, "11an") == 0) {
		/* IEEE80211_MODE_11NA_HT40 */
		return QCA_WLAN_VENDOR_PHY_MODE_11NA_HT40;
	}

	if (strcmp(val, "11ax") == 0 ||
		   strcmp(val, "auto") == 0) {
		/* IEEE80211_MODE_AUTO */
		return QCA_WLAN_VENDOR_PHY_MODE_AUTO;
	}

	return -1;
}

#endif /* NL80211_SUPPORT */


static int get_phymode(const char *val)
{
	if (strcmp(val, "11a") == 0)
		return 1; /* IEEE80211_MODE_11A */
	if (strcmp(val, "11g") == 0)
		return 3; /* IEEE80211_MODE_11G */
	if (strcmp(val, "11b") == 0)
		return 2; /* IEEE80211_MODE_11B */
	if (strcmp(val, "11n") == 0 ||
	    strcmp(val, "11nl") == 0 ||
	    strcmp(val, "11nl(nabg)") == 0)
		return 22; /* IEEE80211_MODE_11AGN */
	if (strcmp(val, "11ng") == 0)
		return 13; /* IEEE80211_MODE_11NG_HT40 */
	if (strcmp(val, "AC") == 0 ||
	    strcasecmp(val, "11AC") == 0)
		return 19; /* IEEE80211_MODE_11AC_VHT80 */
	if (strcmp(val, "11na") == 0 ||
	    strcasecmp(val, "11an") == 0)
		return 14; /* IEEE80211_MODE_11NA_HT40 */
	if (strcmp(val, "11ax") == 0 ||
	    strcmp(val, "auto") == 0)
		return 0; /* IEEE80211_MODE_AUTO */
	return -1;
}


static void sta_set_phymode(struct sigma_dut *dut, const char *intf,
			    const char *val)
{
	char buf[100];
	int len, phymode;
#ifdef NL80211_SUPPORT
	enum qca_wlan_vendor_phy_mode qca_phymode;

	qca_phymode = get_qca_vendor_phymode(val);
	if (qca_phymode == -1) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Ignoring mode change for mode: %s",
				val);
		return;
	}

	if (nlvendor_sta_set_phymode(dut, intf, qca_phymode) == 0)
		return;
#endif /* NL80211_SUPPORT */

	phymode = get_phymode(val);
	if (phymode == -1) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Ignoring mode change for mode: %s",
				val);
		return;
	}

	len = snprintf(buf, sizeof(buf), "iwpriv %s setphymode %d", intf,
		       phymode);
	if (len < 0 || len >= sizeof(buf)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set phymode");
		return;
	}

	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"iwpriv setting of phymode failed");
}


static enum sigma_cmd_result
cmd_sta_preset_testparameters(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	val = get_param(cmd, "FT_DS");
	if (val) {
		int sta_ft_ds;

		if (strcasecmp(val, "Enable") == 0) {
			sta_ft_ds = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			sta_ft_ds = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported value for FT_DS");
			return STATUS_SENT_ERROR;
		}

		if (dut->sta_ft_ds != sta_ft_ds &&
		    get_driver_type(dut) == DRIVER_WCN &&
		    sta_config_params(dut, intf, STA_SET_FT_DS,
				      sta_ft_ds) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to enable/disable FT_DS");
			return STATUS_SENT_ERROR;
		}

		dut->sta_ft_ds = sta_ft_ds;
	}

	val = get_param(cmd, "Program");
	if (val && (strcasecmp(val, "HS2-R2") == 0 ||
		    strcasecmp(val, "HS2-R3") == 0 ||
		    strcasecmp(val, "HS2-2022") == 0 ||
		    strcasecmp(val, "HS2-R4") == 0))
		return cmd_sta_preset_testparameters_hs2_r2(dut, conn, intf,
							    cmd);

	if (val && strcasecmp(val, "LOC") == 0)
		return loc_cmd_sta_preset_testparameters(dut, conn, cmd);
	if (val && strcasecmp(val, "60GHZ") == 0) {
		val = get_param(cmd, "WPS");
		if (val && strcasecmp(val, "disable") == 0) {
			dut->wps_disable = 1;
			sigma_dut_print(dut, DUT_MSG_INFO, "WPS disabled");
		} else {
			/* wps_disable can have other value from the previous
			 * test, so make sure it has the correct value.
			 */
			dut->wps_disable = 0;
		}

		val = get_param(cmd, "P2P");
		if (val && strcasecmp(val, "disable") == 0)
			sigma_dut_print(dut, DUT_MSG_INFO, "P2P disabled");
	}

	if (dut->program == PROGRAM_WPS && dut->band == WPS_BAND_60G)
		return cmd_sta_preset_testparameters_60ghz(dut, conn, cmd);

#ifdef ANDROID_NAN
	if (val && strcasecmp(val, "NAN") == 0)
		return nan_cmd_sta_preset_testparameters(dut, conn, cmd);
#endif /* ANDROID_NAN */
#ifdef MIRACAST
	if (val && (strcasecmp(val, "WFD") == 0 ||
		    strcasecmp(val, "DisplayR2") == 0))
		return miracast_preset_testparameters(dut, conn, cmd);
#endif /* MIRACAST */

	if (val &&
	    (strcasecmp(val, "MBO") == 0 || strcasecmp(val, "HE") == 0 ||
	     strcasecmp(val, "EHT") == 0)) {
		bool eht = strcasecmp(val, "EHT") == 0;

		val = get_param(cmd, "Cellular_Data_Cap");
		if (val &&
		    mbo_set_cellular_data_capa(dut, conn, intf, atoi(val)) == 0)
			return 0;

		val = get_param(cmd, "Ch_Pref");
		if (val && mbo_set_non_pref_ch_list(dut, conn, intf, cmd) == 0)
			return 0;

		val = get_param(cmd, "BSS_Transition");
		if (val && mbo_set_bss_trans_req(dut, conn, intf, val) == 0)
			return 0;

		val = get_param(cmd, "Assoc_Disallow");
		if (val && mbo_set_assoc_disallow(dut, conn, intf, val) == 0)
			return 0;

		val = get_param(cmd, "Roaming");
		if (val && mbo_set_roaming(dut, conn, intf, val) == 0)
			return 0;

		if (!eht)
			return 1;
		val = get_param(cmd, "Program");
	}

	if (val && strcasecmp(val, "OCE") == 0)
		return cmd_sta_preset_testparameters_oce(dut, conn, intf, cmd);

#if 0
	val = get_param(cmd, "Supplicant");
	if (val && strcasecmp(val, "Default") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Only default(Vendor) supplicant "
			  "supported");
		return 0;
	}
#endif

	val = get_param(cmd, "RTS");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_rts(dut, intf, val);
			break;
		default:
#if 0
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting RTS not supported");
			return 0;
#else
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Setting RTS not supported");
			break;
#endif
		}
	}

#if 0
	val = get_param(cmd, "FRGMNT");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting FRGMNT not supported");
		return 0;
	}
#endif

#if 0
	val = get_param(cmd, "Preamble");
	if (val) {
		/* TODO: Long/Short */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Setting Preamble not supported");
		return 0;
	}
#endif

	val = get_param(cmd, "Mode");
	if (val) {
		if (strcmp(val, "11b") == 0 ||
		    strcmp(val, "11g") == 0 ||
		    strcmp(val, "11a") == 0 ||
		    strcmp(val, "11n") == 0 ||
		    strcmp(val, "11ng") == 0 ||
		    strcmp(val, "11nl") == 0 ||
		    strcmp(val, "11nl(nabg)") == 0 ||
		    strcmp(val, "AC") == 0 ||
		    strcmp(val, "11AC") == 0 ||
		    strcmp(val, "11ac") == 0 ||
		    strcmp(val, "11na") == 0 ||
		    strcmp(val, "11an") == 0 ||
		    strcmp(val, "11ax") == 0 ||
		    strcmp(val, "11be") == 0) {
			/* STA supports all modes by default */
			dut->device_mode = dev_mode_to_enum(val);
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting Mode not supported");
			return 0;
		}

		/* Change the mode only in case of testbed for HE program
		 * and for 11a and 11g modes only. */
		if (dut->program == PROGRAM_HE &&
		    dut->device_type == STA_testbed) {
			sta_set_phymode(dut, intf, val);
		}
	}

	val = get_param(cmd, "wmm");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_wmm(dut, intf, val);
			break;
		case DRIVER_WCN:
			wcn_sta_set_wmm(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Setting wmm not supported");
			break;
		}
	}

	val = get_param(cmd, "Powersave");
	if (val) {
		if (strcmp(val, "0") == 0 || strcasecmp(val, "off") == 0) {
			if (get_driver_type(dut) == DRIVER_WCN) {
				if (set_power_save_wcn(dut, intf, 2) < 0)
					return 0;
			}

			if (wpa_command(get_station_ifname(dut),
					"P2P_SET ps 0") < 0)
				return -2;
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(dut), "P2P_SET ps 98");
			wpa_command(get_station_ifname(dut), "P2P_SET ps 96");
		} else if (strcmp(val, "1") == 0 ||
			   strcasecmp(val, "PSPoll") == 0 ||
			   strcasecmp(val, "on") == 0) {
			if (get_driver_type(dut) == DRIVER_WCN) {
				if (set_power_save_wcn(dut, intf, 1) < 0)
					return 0;
			}
			/* Disable default power save mode */
			wpa_command(get_station_ifname(dut), "P2P_SET ps 0");
			/* Enable PS-Poll test mode */
			if (wpa_command(get_station_ifname(dut),
					"P2P_SET ps 97") < 0 ||
			    wpa_command(get_station_ifname(dut),
					"P2P_SET ps 99") < 0)
				return -2;
		} else if (strcmp(val, "2") == 0 ||
			   strcasecmp(val, "Fast") == 0) {
			/* TODO */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Powersave=Fast not supported");
			return 0;
		} else if (strcmp(val, "3") == 0 ||
			   strcasecmp(val, "PSNonPoll") == 0) {
			/* Make sure test modes are disabled */
			wpa_command(get_station_ifname(dut), "P2P_SET ps 98");
			wpa_command(get_station_ifname(dut), "P2P_SET ps 96");

			/* Enable default power save mode */
			if (wpa_command(get_station_ifname(dut),
					"P2P_SET ps 1") < 0)
				return -2;
		} else
			return -1;
	}

	val = get_param(cmd, "NoAck");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_noack(dut, intf, val);
			break;
#ifdef NL80211_SUPPORT
		case DRIVER_WCN:
			wcn_sta_set_noack(dut, intf, val);
			break;
#endif /* NL80211_SUPPORT */
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Setting NoAck not supported");
			return 0;
		}
	}

	val = get_param(cmd, "IgnoreChswitchProhibit");
	if (val) {
		/* TODO: Enabled/disabled */
		if (strcasecmp(val, "Enabled") == 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Enabling IgnoreChswitchProhibit "
				  "not supported");
			return 0;
		}
	}

	val = get_param(cmd, "TDLS");
	if (val) {
		if (strcasecmp(val, "Disabled") == 0) {
			if (wpa_command(intf, "SET tdls_disabled 1")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to disable TDLS");
				return 0;
			}
		} else if (strcasecmp(val, "Enabled") == 0) {
			if (wpa_command(intf, "SET tdls_disabled 0")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable TDLS");
				return 0;
			}
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported TDLS value");
			return 0;
		}
	}

	val = get_param(cmd, "TDLSmode");
	if (val) {
		if (strcasecmp(val, "Default") == 0) {
			wpa_command(intf, "SET tdls_testing 0");
		} else if (strcasecmp(val, "APProhibit") == 0) {
			if (wpa_command(intf, "SET tdls_testing 0x400")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable ignore "
					  "APProhibit TDLS mode");
				return 0;
			}
		} else if (strcasecmp(val, "HiLoMac") == 0) {
			/* STA should respond with TDLS setup req for a TDLS
			 * setup req */
			if (wpa_command(intf, "SET tdls_testing 0x80")) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable HiLoMac "
					  "TDLS mode");
				return 0;
			}
		} else if (strcasecmp(val, "WeakSecurity") == 0) {
			/*
			 * Since all security modes are enabled by default when
			 * Sigma control is used, there is no need to do
			 * anything here.
			 */
		} else if (strcasecmp(val, "ExistLink") == 0) {
			/*
			 * Since we allow new TDLS Setup Request even if there
			 * is an existing link, nothing needs to be done for
			 * this.
			 */
		} else {
			/* TODO:
			 * ExistLink: STA should send TDLS setup req even if
			 * direct link already exists
			 */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported TDLSmode value");
			return 0;
		}
	}

	val = get_param(cmd, "FakePubKey");
	if (val && atoi(val) && wpa_command(intf, "SET wps_corrupt_pkhash 1")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable FakePubKey");
		return 0;
	}

#ifdef NL80211_SUPPORT
	val = get_param(cmd, "FrgmntSupport");
	if (val) {
		if (strcasecmp(val, "Enable") == 0) {
			if (sta_set_he_fragmentation(dut, intf,
						     HE_FRAG_LEVEL1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable HE Fragmentation");
				return 0;
			}
		} else if (strcasecmp(val, "Disable") == 0) {
			if (sta_set_he_fragmentation(dut, intf,
						     HE_FRAG_DISABLE)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to disable HE Fragmentation");
				return 0;
			}
		}
	}
#endif /* NL80211_SUPPORT */

	val = get_param(cmd, "IncludeMSCSDescriptor");
	if (val && strcasecmp(val, "1") == 0) {
		char buf[128];
		int len;

		len = snprintf(buf, sizeof(buf),
			       "MSCS add up_bitmap=F0 up_limit=7 stream_timeout=60000 frame_classifier=045F%032x",
			       0);

		if (len < 0 || len >= sizeof(buf)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to build MSCS Descriptor IE");
			return ERROR_SEND_STATUS;
		}

		if (wpa_command(intf, buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				"ErrorCode,Failed to include MSCS descriptor");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "SCSSupport");
	if (val) {
		char buf[30];
		int disable_scs, len;

		if (strcasecmp(val, "Enable") == 0) {
			disable_scs = 0;
		} else if (strcasecmp(val, "Disable") == 0) {
			disable_scs = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid SCSsupport parameter");
			return INVALID_SEND_STATUS;
		}

		if (disable_scs || dut->prev_disable_scs_support) {
			len = snprintf(buf, sizeof(buf),
				       "SET disable_scs_support %d",
				       disable_scs);
			if (len < 0 || len >= sizeof(buf) ||
			    wpa_command(intf, buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to update SCS support");
				return STATUS_SENT_ERROR;
			}
			dut->prev_disable_scs_support = disable_scs;
		}
	}

	val = get_param(cmd, "MSCSSupport");
	if (val) {
		char buf[30];
		int disable_mscs, len;

		if (strcasecmp(val, "Enable") == 0) {
			disable_mscs = 0;
		} else if (strcasecmp(val, "Disable") == 0) {
			disable_mscs = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid MSCSsupport parameter");
			return INVALID_SEND_STATUS;
		}

		if (disable_mscs || dut->prev_disable_mscs_support) {
			len = snprintf(buf, sizeof(buf),
				       "SET disable_mscs_support %d",
				       disable_mscs);
			if (len < 0 || len >= sizeof(buf) ||
			    wpa_command(intf, buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to update MSCS support");
				return STATUS_SENT_ERROR;
			}
			dut->prev_disable_mscs_support = disable_mscs;
		}
	}

	val = get_param(cmd, "DSCPPolicyCapability");
	if (val) {
		char buf[35];
		int len;

		if (strcasecmp(val, "Enable") == 0) {
			len = snprintf(buf, sizeof(buf),
				       "SET enable_dscp_policy_capa 1");
		} else if (strcasecmp(val, "Disable") == 0) {
			len = snprintf(buf, sizeof(buf),
				       "SET enable_dscp_policy_capa 0");
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid DSCP policy parameter");
			return INVALID_SEND_STATUS;
		}

		if (len < 0 || len >= sizeof(buf) ||
		    wpa_command(intf, buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to update DSCP policy capability");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "DSCPPolicyRespParams");
	if (val) {
		if (strcasecmp(val, "RejectAll") == 0) {
			dut->reject_dscp_policies = 1;
			dut->dscp_reject_resp_code = DSCP_POLICY_REJECT;
		} else if (strcasecmp(val, "AcceptAll") == 0) {
			dut->reject_dscp_policies = 0;
		}
	}

	val = get_param(cmd, "DSCPPolicyResp_StatusCode");
	if (val)
		dut->dscp_reject_resp_code = atoi(val);

	val = get_param(cmd, "Deauth_Reconnect_Policy");
	if (val) {
		char buf[35];
		int len;

		if (strcasecmp(val, "0") == 0) {
			len = snprintf(buf, sizeof(buf),
				       "STA_AUTOCONNECT %d",
				       dut->autoconnect_default);
		} else if (strcasecmp(val, "1") == 0) {
			len = snprintf(buf, sizeof(buf),
				       "STA_AUTOCONNECT 0");
		} else if (strcasecmp(val, "2") == 0) {
			len = snprintf(buf, sizeof(buf),
				       "STA_AUTOCONNECT 1");
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid Deauth_Reconnect_Policy");
			return INVALID_SEND_STATUS;
		}

		if (len < 0 || len >= sizeof(buf) ||
		    wpa_command(intf, buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to update Deauth_Reconnect_Policy");
			return STATUS_SENT_ERROR;
		}
	}

	return 1;
}


static const char * ath_get_radio_name(const char *radio_name)
{
	if (radio_name == NULL)
		return "wifi0";
	if (strcmp(radio_name, "wifi1") == 0)
		return "wifi1";
	if (strcmp(radio_name, "wifi2") == 0)
		return "wifi2";
	return "wifi0";
}


static void ath_sta_set_txsp_stream(struct sigma_dut *dut, const char *intf,
				    const char *val)
{
	unsigned int vht_mcsmap = 0;
	int txchainmask = 0;
	const char *basedev = ath_get_radio_name(sigma_radio_ifname[0]);

	if (strcasecmp(val, "1") == 0 || strcasecmp(val, "1SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xfffc;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xfffe;
		}
		txchainmask = 1;
	} else if (strcasecmp(val, "2") == 0 || strcasecmp(val, "2SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xfff0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xfffa;
		}
		txchainmask = 3;
	} else if (strcasecmp(val, "3") == 0 || strcasecmp(val, "3SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
		txchainmask = 7;
	} else if (strcasecmp(val, "4") == 0 || strcasecmp(val, "4SS") == 0) {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xff00;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffaa;
		}
		txchainmask = 15;
	} else {
		if (dut->testbed_flag_txsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_txsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
	}

	if (txchainmask)
		run_iwpriv(dut, basedev, "txchainmask %d", txchainmask);

	run_iwpriv(dut, intf, "vht_mcsmap 0x%04x", vht_mcsmap);
}


static void ath_sta_set_rxsp_stream(struct sigma_dut *dut, const char *intf,
				    const char *val)
{
	unsigned int vht_mcsmap = 0;
	int rxchainmask = 0;
	const char *basedev = ath_get_radio_name(sigma_radio_ifname[0]);

	if (strcasecmp(val, "1") == 0 || strcasecmp(val, "1SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xfffc;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xfffe;
		}
		rxchainmask = 1;
	} else if (strcasecmp(val, "2") == 0 || strcasecmp(val, "2SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xfff0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xfffa;
		}
		rxchainmask = 3;
	} else if (strcasecmp(val, "3") == 0 || strcasecmp(val, "3SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
		rxchainmask = 7;
	} else if (strcasecmp(val, "4") == 0 || strcasecmp(val, "4SS") == 0) {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xff00;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffaa;
		}
		rxchainmask = 15;
	} else {
		if (dut->testbed_flag_rxsp == 1) {
			vht_mcsmap = 0xffc0;
			dut->testbed_flag_rxsp = 0;
		} else {
			vht_mcsmap = 0xffea;
		}
	}

	if (rxchainmask)
		run_iwpriv(dut, basedev, "rxchainmask %d", rxchainmask);

	run_iwpriv(dut, intf, "vht_mcsmap 0x%04x", vht_mcsmap);
}


void ath_set_zero_crc(struct sigma_dut *dut, const char *val)
{
	if (strcasecmp(val, "enable") == 0) {
		if (system("athdiag --set --address=0x2a204 --and=0xbfffffff")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable BB_VHTSIGB_CRC_CALC failed");
		}

		if (system("athdiag --set --address=0x2a204 --or=0x80000000")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable FORCE_VHT_SIGB_CRC_VALUE_ZERO failed");
		}
	} else {
		if (system("athdiag --set --address=0x2a204 --and=0x7fffffff")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable FORCE_VHT_SIGB_CRC_VALUE_ZERO failed");
		}

		if (system("athdiag --set --address=0x2a204 --or=0x40000000")
		    != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable BB_VHTSIGB_CRC_CALC failed");
		}
	}
}


static int wcn_sta_set_width(struct sigma_dut *dut, const char *intf,
			     const char *val)
{
	char buf[60];

#ifdef NL80211_SUPPORT
	enum nl80211_chan_width qca_channel_width;

	if (strcmp(val, "20") == 0) {
		qca_channel_width = NL80211_CHAN_WIDTH_20;
		dut->chwidth = 0;
	} else if (strcmp(val, "40") == 0) {
		qca_channel_width = NL80211_CHAN_WIDTH_40;
		dut->chwidth = 1;
	} else if (strcmp(val, "80") == 0) {
		qca_channel_width = NL80211_CHAN_WIDTH_80;
		dut->chwidth = 2;
	} else if (strcmp(val, "160") == 0) {
		qca_channel_width = NL80211_CHAN_WIDTH_160;
		dut->chwidth = 3;
	} else if (strcmp(val, "320") == 0) {
		qca_channel_width = NL80211_CHAN_WIDTH_320;
		dut->chwidth = 4;
	} else if (strcasecmp(val, "Auto") == 0) {
		return 0;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "WIDTH %s not supported",
				val);
		return -1;
	}
	if (sta_config_params(dut, intf, STA_SET_CHAN_WIDTH,
			      qca_channel_width) == 0)
		return 0;
#endif /* NL80211_SUPPORT */

	if (strcmp(val, "20") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 0", intf);
		dut->chwidth = 0;
	} else if (strcmp(val, "40") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 1", intf);
		dut->chwidth = 1;
	} else if (strcmp(val, "80") == 0) {
		snprintf(buf, sizeof(buf), "iwpriv %s chwidth 2", intf);
		dut->chwidth = 2;
	} else if (strcasecmp(val, "Auto") == 0) {
		buf[0] = '\0';
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "WIDTH %s not supported",
				val);
		return -1;
	}

	if (buf[0] != '\0' && system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "iwpriv chwidth failed");
		return -1;
	}

	return 0;
}


static int nlvendor_sta_set_addba_reject(struct sigma_dut *dut,
					 const char *intf, int addbareject)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ACCEPT_ADDBA_REQ,
		!addbareject);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"ADDBA_REJECT cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_addba_reject(struct sigma_dut *dut, const char *intf,
				int addbareject)
{
	int ret;

	switch (get_driver_type(dut)) {
	case DRIVER_WCN:
		ret = nlvendor_sta_set_addba_reject(dut, intf, addbareject);
		if (ret) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nlvendor_sta_set_addba_reject failed, ret:%d",
					ret);
			return ret;
		}
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"errorCode,Unsupported ADDBA_REJECT with the current driver");
		ret = -1;
		break;
	}

	return ret;
}


static int nlvendor_config_send_addba(struct sigma_dut *dut, const char *intf,
				      int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_SEND_ADDBA_REQ,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Disable addba not possible without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


#ifdef NL80211_SUPPORT
static int nl80211_sta_set_rts(struct sigma_dut *dut, const char *intf, int val)
{
	struct nl_msg *msg;
	int ret = 0;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_SET_WIPHY)) ||
	    nla_put_u32(msg, NL80211_ATTR_WIPHY_RTS_THRESHOLD, val)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding RTS threshold",
				__func__);
		nlmsg_free(msg);
		return -1;
	}

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}
	return ret;
}
#endif /* NL80211_SUPPORT */


static int sta_set_rts(struct sigma_dut *dut, const char *intf, int val)
{
	char buf[100];

#ifdef NL80211_SUPPORT
	if (nl80211_sta_set_rts(dut, intf, val) == 0)
		return 0;
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Fall back to using iwconfig for setting RTS threshold");
#endif /* NL80211_SUPPORT */

	snprintf(buf, sizeof(buf), "iwconfig %s rts %d", intf, val);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set RTS threshold %d", val);
		return -1;
	}
	return 0;
}


static int wcn_config_dyn_bw_sig(struct sigma_dut *dut, const char *intf,
				 const char *val)
{
	int set_val = -1;
	int res;

	if (strcasecmp(val, "enable") == 0)
		set_val = 1;
	else if (strcasecmp(val, "disable") == 0)
		set_val = 0;
	else
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported DYN_BW_SGL");
	if (set_val >= 0) {
		res = sta_config_params(dut, intf, STA_SET_DYN_BW, set_val);
		if (res) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Set DYN_BW_SGL using NL failed");
			if (run_iwpriv(dut, intf,
				       "cwmenable %d", set_val) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv cwmenable %d failed",
						set_val);
				return -1;
			}
		}
	}

	if (run_iwpriv(dut, intf, "cts_cbw 3") < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set cts_cbw in DYN_BW_SGNL");
		return -1;
	}

	return 0;
}


static enum sigma_cmd_result
cmd_sta_set_wireless_common(const char *intf, struct sigma_dut *dut,
			    struct sigma_conn *conn, struct sigma_cmd *cmd)
{
	const char *val;
	int ampdu = -1, addbareject = -1;
	char buf[128];
	int res;

	val = get_param(cmd, "40_INTOLERANT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* TODO: iwpriv ht40intol through wpa_supplicant */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,40_INTOLERANT not supported");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "ADDBA_REJECT");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* reject any ADDBA with status "decline" */
			ampdu = 0;
			addbareject = 1;
		} else {
			/* accept ADDBA */
			ampdu = 1;
			addbareject = 0;
		}
	}

	if (addbareject >= 0 &&
	    sta_set_addba_reject(dut, intf, addbareject) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,set addba_reject failed");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* enable AMPDU Aggregation */
			if (ampdu == 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return STATUS_SENT_ERROR;
			}
			ampdu = 1;
		} else {
			/* disable AMPDU Aggregation */
			if (ampdu == 1) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Mismatch in "
					  "addba_reject/ampdu - "
					  "not supported");
				return STATUS_SENT_ERROR;
			}
			ampdu = 0;
		}
	}

	if (ampdu >= 0) {
		int ret;

		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s A-MPDU aggregation",
				ampdu ? "Enabling" : "Disabling");
		snprintf(buf, sizeof(buf), "SET ampdu %d", ampdu);
		if (wpa_command(intf, buf) < 0 &&
		    wcn_sta_set_ampdu(dut, intf, ampdu)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,set aggr failed");
			return STATUS_SENT_ERROR;
		}

		if (ampdu == 0) {
			/* Disable sending of addba using nl vendor command */
			ret = nlvendor_config_send_addba(dut, intf, 0);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to disable addba, ret:%d",
						ret);
			}
		}
	}

	val = get_param(cmd, "AMSDU");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
		case DRIVER_WCN:
			iwpriv_sta_set_amsdu(dut, intf, val);
			break;
		default:
			if (strcmp(val, "1") == 0 ||
			    strcasecmp(val, "Enable") == 0) {
				/* Enable AMSDU Aggregation */
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,AMSDU aggregation not supported");
				return STATUS_SENT_ERROR;
			}
			break;
		}
	}

	val = get_param(cmd, "STBC_RX");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_stbc(dut, intf, val);
			break;
		case DRIVER_WCN:
			wcn_sta_set_stbc(dut, intf, val);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,STBC_RX not supported");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "WIDTH");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_sta_set_width(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set WIDTH");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			if (ath_set_width(dut, conn, intf, val) < 0)
				return STATUS_SENT_ERROR;
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting WIDTH not supported");
			break;
		}
	}

	val = get_param(cmd, "SMPS");
	if (val) {
		/* TODO: Dynamic/0, Static/1, No Limit/2 */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,SMPS not supported");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "TXSP_STREAM");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set TXSP_STREAM");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			ath_sta_set_txsp_stream(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting TXSP_STREAM not supported");
			break;
		}
	}

	val = get_param(cmd, "RXSP_STREAM");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set RXSP_STREAM");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			ath_sta_set_rxsp_stream(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting RXSP_STREAM not supported");
			break;
		}
	}

	val = get_param(cmd, "DYN_BW_SGNL");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_config_dyn_bw_sig(dut, intf, val) < 0)
				return ERROR_SEND_STATUS;
			break;
		case DRIVER_ATHEROS:
			novap_reset(dut, intf, 1);
			ath_config_dyn_bw_sig(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set DYN_BW_SGNL");
			break;
		}
	}

	val = get_param(cmd, "RTS_FORCE");
	if (val) {
		novap_reset(dut, intf, 1);
		if (strcasecmp(val, "Enable") == 0) {
			if (sta_set_rts(dut, intf, 64) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set RTS_FORCE 64");
			}
			res = snprintf(buf, sizeof(buf),
				       "wifitool %s beeliner_fw_test 100 1",
				       intf);
			if (res < 0 || res >= sizeof(buf) || system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"wifitool beeliner_fw_test 100 1 failed");
			}
		} else if (strcasecmp(val, "Disable") == 0) {
			if (sta_set_rts(dut, intf, 2347) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set RTS_FORCE 2347");
			}
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,RTS_FORCE value not supported");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "CTS_WIDTH");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_sta_set_cts_width(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set CTS_WIDTH");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			ath_set_cts_width(dut, intf, val);
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting CTS_WIDTH not supported");
			break;
		}
	}

	val = get_param(cmd, "BW_SGNL");
	if (val) {
		int set_val;

		if (strcasecmp(val, "Enable") == 0) {
			set_val = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			set_val = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,BW_SGNL value not supported");
			return STATUS_SENT_ERROR;
		}

		if (get_driver_type(dut) == DRIVER_WCN) {
			res = sta_config_params(dut, intf, STA_SET_DYN_BW,
						set_val);
			if (res) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Set DYN_BW_SGL using NL failed");
				if (run_iwpriv(dut, intf, "cwmenable %d") < 0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"iwpriv cwmenable %d failed",
							set_val);
					return ERROR_SEND_STATUS;
				}
			}
		} else {
			run_iwpriv(dut, intf, "cwmenable %d", set_val);
		}
	}

	val = get_param(cmd, "Band");
	if (val) {
		if (strcmp(val, "2.4") == 0 || strcmp(val, "5") == 0 ||
		    strcmp(val, "24G") == 0 || strcmp(val, "5G") == 0 ||
		    strcmp(val, "6G") == 0) {
			/* STA supports all bands by default */
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported Band");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "zero_crc");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_set_zero_crc(dut, val);
			break;
		default:
			break;
		}
	}

	val = get_param(cmd, "Listen_Interval");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (sta_config_params(dut, intf,
					      STA_SET_LISTEN_INTERVAL,
					      atoi(val))) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting listen interval failed");
			}
			break;
		default:
			break;
		}
	}

	return SUCCESS_SEND_STATUS;
}


static int sta_set_force_mcs(struct sigma_dut *dut, int force, int mcs)
{
	switch (get_driver_type(dut)) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_set_force_mcs(dut, force, mcs);
#endif /* __linux__ */
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Unsupported sta_set_force_mcs with the current driver");
		return -1;
	}
}


static int sta_60g_force_rsn_ie(struct sigma_dut *dut, int state)
{
	switch (get_driver_type(dut)) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_force_rsn_ie(dut, state);
#endif /* __linux__ */
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Unsupported sta_60g_force_rsn_ie with the current driver");
		return -1;
	}
}


static int sta_set_60g_common(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *val;
	char buf[100];

	val = get_param(cmd, "MSDUSize");
	if (val) {
		int mtu;

		dut->amsdu_size = atoi(val);
		if (dut->amsdu_size > IEEE80211_MAX_DATA_LEN_DMG ||
		    dut->amsdu_size < IEEE80211_SNAP_LEN_DMG) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MSDUSize %d is above max %d or below min %d",
					dut->amsdu_size,
					IEEE80211_MAX_DATA_LEN_DMG,
					IEEE80211_SNAP_LEN_DMG);
			dut->amsdu_size = 0;
			return ERROR_SEND_STATUS;
		}

		mtu = dut->amsdu_size - IEEE80211_SNAP_LEN_DMG;
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Setting amsdu_size to %d", mtu);
		snprintf(buf, sizeof(buf), "ifconfig %s mtu %d",
			 get_station_ifname(dut), mtu);

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set %s",
					buf);
			return ERROR_SEND_STATUS;
		}
	}

	val = get_param(cmd, "BAckRcvBuf");
	if (val) {
		dut->back_rcv_buf = atoi(val);
		if (dut->back_rcv_buf == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to convert %s or value is 0",
					val);
			return ERROR_SEND_STATUS;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Setting BAckRcvBuf to %s", val);
	}

	val = get_param(cmd, "MCS_FixedRate");
	if (val) {
		if (sta_set_force_mcs(dut, 1, atoi(val))) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to force MCS");
			return ERROR_SEND_STATUS;
		}
	}

	return SUCCESS_SEND_STATUS;
}


static int sta_pcp_start(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct sigma_cmd *cmd)
{
	int net_id;
	const char *ifname;
	const char *val;
	char buf[100];

	dut->mode = SIGMA_MODE_STATION;
	ifname = get_main_ifname(dut);
	if (wpa_command(ifname, "PING") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Supplicant not running");
		return ERROR_SEND_STATUS;
	}

	wpa_command(ifname, "FLUSH");
	net_id = add_network_common(dut, conn, ifname, cmd);
	if (net_id < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add network");
		return net_id;
	}

	/* TODO: mode=2 for the AP; in the future, replace for mode PCP */
	if (set_network(ifname, net_id, "mode", "2") < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set supplicant network mode");
		return ERROR_SEND_STATUS;
	}

	if (set_network(ifname, net_id, "pbss", "1") < 0)
		return -2;

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Supplicant set network with mode 2. network_id %d",
			net_id);

	if (set_network(ifname, net_id, "wps_disabled", "0") < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to set supplicant to WPS ENABLE");
		return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "Security");
	if (val && strcasecmp(val, "OPEN") == 0) {
		dut->ap_key_mgmt = AP_OPEN;
		if (set_network(ifname, net_id, "key_mgmt", "NONE") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to %s security",
					val);
			return ERROR_SEND_STATUS;
		}
	} else if (val && strcasecmp(val, "WPA2-PSK") == 0) {
		dut->ap_key_mgmt = AP_WPA2_PSK;
		if (set_network(ifname, net_id, "key_mgmt", "WPA-PSK") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to %s security",
					val);
			return ERROR_SEND_STATUS;
		}

		if (set_network(ifname, net_id, "proto", "RSN") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to proto RSN");
			return ERROR_SEND_STATUS;
		}
	} else if (val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Requested Security %s is not supported on 60GHz",
				val);
		return INVALID_SEND_STATUS;
	}

	val = get_param(cmd, "Encrypt");
	if (val && strcasecmp(val, "AES-GCMP") == 0) {
		if (set_network(ifname, net_id, "pairwise", "GCMP") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to pairwise GCMP");
			return ERROR_SEND_STATUS;
		}
		if (set_network(ifname, net_id, "group", "GCMP") < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set supplicant to group GCMP");
			return ERROR_SEND_STATUS;
		}
	} else if (val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Requested Encrypt %s is not supported on 60 GHz",
				val);
		return INVALID_SEND_STATUS;
	}

	val = get_param(cmd, "PSK");
	if (val && set_network_quoted(ifname, net_id, "psk", val) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set psk %s",
				val);
		return ERROR_SEND_STATUS;
	}

	/* Convert 60G channel to freq */
	switch (dut->ap_channel) {
	case 1:
		val = "58320";
		break;
	case 2:
		val = "60480";
		break;
	case 3:
		val = "62640";
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to configure channel %d. Not supported",
				dut->ap_channel);
		return ERROR_SEND_STATUS;
	}

	if (set_network(ifname, net_id, "frequency", val) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set supplicant network frequency");
		return ERROR_SEND_STATUS;
	}

	if (dut->eap_fragment) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Set EAP fragment size to 128 bytes.");
		if (set_network(ifname, net_id, "fragment_size", "128") < 0)
			return ERROR_SEND_STATUS;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Supplicant set network with frequency");

	snprintf(buf, sizeof(buf), "SELECT_NETWORK %d", net_id);
	if (wpa_command(ifname, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to select network id %d on %s",
				net_id, ifname);
		return ERROR_SEND_STATUS;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Selected network");

	return SUCCESS_SEND_STATUS;
}


static int wil6210_set_abft_len(struct sigma_dut *dut, int abft_len)
{
	char buf[128], fname[128];
	FILE *f;
	int res;

	if (wil6210_get_debugfs_dir(dut, buf, sizeof(buf))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	res = snprintf(fname, sizeof(fname), "%s/abft_len", buf);
	if (res < 0 || res >= sizeof(fname))
		return -1;
	f = fopen(fname, "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", fname);
		return -1;
	}

	fprintf(f, "%d\n", abft_len);
	fclose(f);

	return 0;
}


int sta_set_60g_abft_len(struct sigma_dut *dut, struct sigma_conn *conn,
			 int abft_len)
{
	switch (get_driver_type(dut)) {
	case DRIVER_WIL6210:
		return wil6210_set_abft_len(dut, abft_len);
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"set abft_len not supported");
		return -1;
	}
}


static int sta_set_60g_pcp(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *val;
	unsigned int abft_len = 1; /* default is one slot */

	if (dut->dev_role != DEVROLE_PCP) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid DevRole");
		return 0;
	}

	val = get_param(cmd, "SSID");
	if (val) {
		if (strlen(val) > sizeof(dut->ap_ssid) - 1) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,Invalid SSID");
			return -1;
		}

		strlcpy(dut->ap_ssid, val, sizeof(dut->ap_ssid));
	}

	val = get_param(cmd, "CHANNEL");
	if (val) {
		const char *pos;

		dut->ap_channel = atoi(val);
		pos = strchr(val, ';');
		if (pos) {
			pos++;
			dut->ap_channel_1 = atoi(pos);
		}
	}

	switch (dut->ap_channel) {
	case 1:
	case 2:
	case 3:
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Channel %d is not supported", dut->ap_channel);
		send_resp(dut, conn, SIGMA_ERROR,
			  "Requested channel is not supported");
		return -1;
	}

	val = get_param(cmd, "BCNINT");
	if (val)
		dut->ap_bcnint = atoi(val);

	val = get_param(cmd, "AllocType");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,AllocType is not supported yet");
		return -1;
	}

	val = get_param(cmd, "PercentBI");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,PercentBI is not supported yet");
		return -1;
	}

	val = get_param(cmd, "CBAPOnly");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,CBAPOnly is not supported yet");
		return -1;
	}

	val = get_param(cmd, "AMPDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_ampdu = 1;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_ampdu = 2;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,AMPDU value is not Enable nor Disabled");
			return -1;
		}
	}

	val = get_param(cmd, "AMSDU");
	if (val) {
		if (strcasecmp(val, "Enable") == 0)
			dut->ap_amsdu = 1;
		else if (strcasecmp(val, "Disable") == 0)
			dut->ap_amsdu = 2;
	}

	val = get_param(cmd, "NumMSDU");
	if (val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, NumMSDU is not supported yet");
		return -1;
	}

	val = get_param(cmd, "ABFTLRang");
	if (val) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"ABFTLRang parameter %s", val);
		if (strcmp(val, "Gt1") == 0)
			abft_len = 2; /* 2 slots in this case */
	}

	if (sta_set_60g_abft_len(dut, conn, abft_len)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, Can't set ABFT length");
		return -1;
	}

	if (sta_pcp_start(dut, conn, cmd) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, Can't start PCP role");
		return -1;
	}

	return sta_set_60g_common(dut, conn, cmd);
}


static int sta_set_60g_sta(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "DiscoveryMode");

	if (dut->dev_role != DEVROLE_STA) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid DevRole");
		return 0;
	}

	if (val) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Discovery: %s", val);
		/* Ignore Discovery mode till Driver expose API. */
#if 0
		if (strcasecmp(val, "1") == 0) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,DiscoveryMode 1 not supported");
			return 0;
		}

		if (strcasecmp(val, "0") == 0) {
			/* OK */
		} else {
			send_resp(dut, conn, SIGMA_INVALID,
				  "ErrorCode,DiscoveryMode not supported");
			return 0;
		}
#endif
	}

	if (start_sta_mode(dut) != 0)
		return ERROR_SEND_STATUS;
	return sta_set_60g_common(dut, conn, cmd);
}


static enum sigma_cmd_result cmd_sta_disconnect(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val = get_param(cmd, "maintain_profile");

	if (dut->program == PROGRAM_OCE ||
	    dut->program == PROGRAM_HE ||
	    dut->program == PROGRAM_EHT ||
	    dut->program == PROGRAM_LOCR2 ||
	    (val && atoi(val) == 1)) {
		wpa_command(intf, "DISCONNECT");
		return 1;
	}

	disconnect_station(dut);
	/* Try to ignore old scan results to avoid HS 2.0R2 test case failures
	 * due to cached results. */
	wpa_command(intf, "SET ignore_old_scan_res 1");
	wpa_command(intf, "BSS_FLUSH");
	return 1;
}


static enum sigma_cmd_result cmd_sta_reassoc(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "bssid");
	const char *val = get_param(cmd, "CHANNEL");
	const char *freq_val = get_param(cmd, "ChnlFreq");
	struct wpa_ctrl *ctrl;
	char buf[1000];
	char result[32];
	int res;
	int chan = 0;
	int freq = 0;
	enum sigma_cmd_result status = STATUS_SENT;
	int fastreassoc = 1;
	int ft_ds = 0;

	if (bssid == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Missing bssid "
			  "argument");
		return STATUS_SENT_ERROR;
	}

	if (val)
		chan = atoi(val);

	if (freq_val)
		freq = atoi(freq_val);

	if (wifi_chip_type != DRIVER_WCN && wifi_chip_type != DRIVER_AR6003) {
		/* The current network may be from sta_associate or
		 * sta_hs2_associate
		 */
		if (set_network(intf, dut->infra_network_id, "bssid", bssid) <
		    0 ||
		    set_network(intf, 0, "bssid", bssid) < 0)
			return ERROR_SEND_STATUS;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	if (get_wpa_status(get_station_ifname(dut), "wpa_state", result,
			   sizeof(result)) < 0 ||
	    strncmp(result, "COMPLETED", 9) != 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"sta_reassoc: Not connected");
		fastreassoc = 0;
	} else if (dut->sta_ft_ds) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"sta_reassoc: Use FT-over-DS");
		ft_ds = 1;
	}

	if (dut->rsne_override) {
#ifdef NL80211_SUPPORT
		if (get_driver_type(dut) == DRIVER_WCN &&
		    dut->config_rsnie == 0) {
			sta_config_params(dut, intf, STA_SET_RSNIE, 1);
			dut->config_rsnie = 1;
		}
#endif /* NL80211_SUPPORT */
		snprintf(buf, sizeof(buf), "TEST_ASSOC_IE %s",
			 dut->rsne_override);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set DEV_CONFIGURE_IE RSNE override");
			return 0;
		}
	}

	if (ft_ds && get_driver_type(dut) != DRIVER_WCN) {
		if (chan || freq) {
			if (!freq)
				freq = channel_to_freq(dut, chan);
			if (!freq) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Invalid channel number provided: %d",
						chan);
				send_resp(dut, conn, SIGMA_INVALID,
					  "ErrorCode,Invalid channel number");
				goto close_mon_conn;
			}
			res = snprintf(buf, sizeof(buf),
				       "SCAN TYPE=ONLY freq=%d", freq);
		} else {
			res = snprintf(buf, sizeof(buf), "SCAN TYPE=ONLY");
		}
		if (res < 0 || res >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,snprintf failed");
			goto close_mon_conn;
		}
		if (wpa_command(intf, buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to start scan");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan failed");
			goto close_mon_conn;
		}

		res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
					buf, sizeof(buf));
		if (res < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Scan did not complete");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan did not complete");
			goto close_mon_conn;
		}

		res = snprintf(buf, sizeof(buf), "FT_DS %s", bssid);
		if (res > 0 && res < (int) sizeof(buf))
			res = wpa_command(intf, buf);

		if (res < 0 || res >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,FT_DS command failed");
			status = STATUS_SENT_ERROR;
			goto close_mon_conn;
		}
	} else if (wifi_chip_type == DRIVER_WCN && fastreassoc) {
		if (chan || freq) {
			if (!freq)
				freq = channel_to_freq(dut, chan);
			if (!freq) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Invalid channel number provided: %d",
						chan);
				send_resp(dut, conn, SIGMA_INVALID,
					  "ErrorCode,Invalid channel number");
				goto close_mon_conn;
			}
			res = snprintf(buf, sizeof(buf),
				       "SCAN TYPE=ONLY freq=%d", freq);
		} else {
			res = snprintf(buf, sizeof(buf), "SCAN TYPE=ONLY");
		}
		if (res < 0 || res >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,snprintf failed");
			goto close_mon_conn;
		}
		if (wpa_command(intf, buf) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to start scan");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan failed");
			goto close_mon_conn;
		}

		res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
					buf, sizeof(buf));
		if (res < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Scan did not complete");
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,scan did not complete");
			goto close_mon_conn;
		}

		if (set_network(intf, dut->infra_network_id, "bssid", "any")
		    < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
					"bssid to any during FASTREASSOC");
			status = ERROR_SEND_STATUS;
			goto close_mon_conn;
		}
		res = snprintf(buf, sizeof(buf), "FASTREASSOC %s %d",
			       bssid, is_6ghz_freq(freq) ? freq : chan);
		if (res < 0 || res >= (int) sizeof(buf) ||
		    wcn_driver_cmd(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to run FASTREASSOC");
			goto close_mon_conn;
		}
		sigma_dut_print(dut, DUT_MSG_INFO,
				"sta_reassoc: Run %s successful", buf);
	} else if (wpa_command(intf, "REASSOCIATE")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "request reassociation");
		goto close_mon_conn;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
				buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Connection did not complete");
		status = STATUS_SENT_ERROR;
		goto close_mon_conn;
	}
	status = SUCCESS_SEND_STATUS;

close_mon_conn:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	return status;
}


static void hs2_clear_credentials(const char *intf)
{
	wpa_command(intf, "REMOVE_CRED all");
}


#ifdef __linux__
static int wil6210_get_aid(struct sigma_dut *dut, const char *bssid,
			   unsigned int *aid)
{
	const char *pattern = "AID[ \t]+([0-9]+)";

	return wil6210_get_sta_info_field(dut, bssid, pattern, aid);
}
#endif /* __linux__ */


static int sta_get_aid_60g(struct sigma_dut *dut, const char *bssid,
			   unsigned int *aid)
{
	switch (get_driver_type(dut)) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_get_aid(dut, bssid, aid);
#endif /* __linux__ */
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR, "get AID not supported");
		return -1;
	}
}


static int sta_get_parameter_60g(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	char buf[MAX_CMD_LEN];
	char bss_list[MAX_CMD_LEN];
	const char *parameter = get_param(cmd, "Parameter");

	if (parameter == NULL)
		return -1;

	if (strcasecmp(parameter, "AID") == 0) {
		unsigned int aid = 0;
		char bssid[20];

		if (get_wpa_status(get_station_ifname(dut), "bssid",
				   bssid, sizeof(bssid)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"could not get bssid");
			return -2;
		}

		if (sta_get_aid_60g(dut, bssid, &aid))
			return -2;

		snprintf(buf, sizeof(buf), "aid,%d", aid);
		sigma_dut_print(dut, DUT_MSG_INFO, "%s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	if (strcasecmp(parameter, "DiscoveredDevList") == 0) {
		char *bss_line;
		char *bss_id = NULL;
		const char *ifname = get_param(cmd, "Interface");
		char *saveptr;

		if (ifname == NULL) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"For get DiscoveredDevList need Interface name.");
			return -1;
		}

		/*
		 * Use "BSS RANGE=ALL MASK=0x2" which provides a list
		 * of BSSIDs in "bssid=<BSSID>\n"
		 */
		if (wpa_command_resp(ifname, "BSS RANGE=ALL MASK=0x2",
				     bss_list,
				     sizeof(bss_list)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to get bss list");
			return -1;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"bss list for ifname:%s is:%s",
				ifname, bss_list);

		snprintf(buf, sizeof(buf), "DeviceList");
		bss_line = strtok_r(bss_list, "\n", &saveptr);
		while (bss_line) {
			if (sscanf(bss_line, "bssid=%ms", &bss_id) > 0 &&
			    bss_id) {
				int len;

				len = snprintf(buf + strlen(buf),
					       sizeof(buf) - strlen(buf),
					       ",%s", bss_id);
				free(bss_id);
				bss_id = NULL;
				if (len < 0) {
					sigma_dut_print(dut,
							DUT_MSG_ERROR,
							"Failed to read BSSID");
					send_resp(dut, conn, SIGMA_ERROR,
						  "ErrorCode,Failed to read BSS ID");
					return 0;
				}

				if ((size_t) len >= sizeof(buf) - strlen(buf)) {
					sigma_dut_print(dut,
							DUT_MSG_ERROR,
							"Response buf too small for list");
					send_resp(dut, conn,
						  SIGMA_ERROR,
						  "ErrorCode,Response buf too small for list");
					return 0;
				}
			}

			bss_line = strtok_r(NULL, "\n", &saveptr);
		}

		sigma_dut_print(dut, DUT_MSG_INFO, "DiscoveredDevList is %s",
				buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int sta_get_parameter_he(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	char buf[MAX_CMD_LEN];
	const char *parameter = get_param(cmd, "Parameter");

	if (!parameter)
		return -1;

	if (strcasecmp(parameter, "RSSI") == 0) {
		char rssi[10];

		if (get_wpa_signal_poll(dut, get_station_ifname(dut), "RSSI",
					rssi, sizeof(rssi)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get RSSI");
			return -2;
		}

		snprintf(buf, sizeof(buf), "rssi,%s", rssi);
		sigma_dut_print(dut, DUT_MSG_INFO, "RSSI %s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


#ifdef NL80211_SUPPORT

struct station_info {
	uint64_t filled;
	uint32_t beacon_mic_error_count;
	uint32_t beacon_replay_count;
};


static int qca_get_sta_info_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct station_info *data = arg;
	struct nlattr *info[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_MAX + 1];
	static struct nla_policy info_policy[
		QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_MAX + 1] = {
		[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_MIC_ERROR_COUNT] = {
			.type = NLA_U32
		},
		[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_REPLAY_COUNT] = {
			.type = NLA_U32
		},
	};

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_VENDOR_DATA])
		return NL_SKIP;

	if (nla_parse_nested(info, QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_MAX,
			     tb[NL80211_ATTR_VENDOR_DATA], info_policy)) {
		return NL_SKIP;
	}

	if (info[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_MIC_ERROR_COUNT]) {
		data->filled |=
			BIT_ULL(QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_MIC_ERROR_COUNT);
		data->beacon_mic_error_count =
			nla_get_u32(info[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_MIC_ERROR_COUNT]);
	}

	if (info[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_REPLAY_COUNT]) {
		data->filled |=
			BIT_ULL(QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_REPLAY_COUNT);
		data->beacon_replay_count =
			nla_get_u32(info[QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_REPLAY_COUNT]);
	}

	return NL_SKIP;
}


static int qca_nl80211_get_sta_info(struct sigma_dut *dut, const char *intf,
				    struct station_info *sta_data)
{
	struct nl_msg *msg;
	int ifindex, ret;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s not found",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_GET_STA_INFO)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd", __func__);
		nlmsg_free(msg);
		return -1;
	}

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg,
				 qca_get_sta_info_handler, sta_data);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}
	return ret;
}
#endif /* NL80211_SUPPORT */


static int get_bip_mic_error_count(struct sigma_dut *dut,
				   const char *ifname,
				   unsigned int *count)
{
#ifdef NL80211_SUPPORT
	struct station_info sta_data;
#endif /* NL80211_SUPPORT */

	if (get_driver_type(dut) != DRIVER_WCN) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"BIP MIC error count not supported");
		return -1;
	}

#ifdef NL80211_SUPPORT
	if (qca_nl80211_get_sta_info(dut, ifname, &sta_data) != 0 ||
	    !(sta_data.filled &
	      BIT_ULL(QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_MIC_ERROR_COUNT))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"BIP MIC error count fetching failed");
		return -1;
	}

	*count = sta_data.beacon_mic_error_count;
	return 0;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"BIP MIC error count cannot be fetched without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int get_cmac_replay_count(struct sigma_dut *dut, const char *ifname,
				 unsigned int *count)
{
#ifdef NL80211_SUPPORT
	struct station_info sta_data;
#endif /* NL80211_SUPPORT */

	if (get_driver_type(dut) != DRIVER_WCN) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"CMAC reply count not supported");
		return -1;
	}

#ifdef NL80211_SUPPORT
	if (qca_nl80211_get_sta_info(dut, ifname, &sta_data) != 0 ||
	    !(sta_data.filled &
	      BIT_ULL(QCA_WLAN_VENDOR_ATTR_GET_STA_INFO_BEACON_REPLAY_COUNT))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"CMAC replay count fetching failed");
		return -1;
	}

	*count = sta_data.beacon_replay_count;
	return 0;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"CMAC replay count cannot be fetched without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static enum sigma_cmd_result sta_get_parameter_wpa3(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	char buf[MAX_CMD_LEN];
	const char *ifname = get_param(cmd, "interface");
	const char *parameter = get_param(cmd, "Parameter");
	unsigned int val;

	if (!ifname || !parameter)
		return INVALID_SEND_STATUS;

	if (strcasecmp(parameter, "BIPMICErrors") == 0) {
		if (get_bip_mic_error_count(dut, ifname, &val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to get BIPMICErrors");
			return STATUS_SENT_ERROR;
		}
		snprintf(buf, sizeof(buf), "BIPMICErrors,%d", val);
		sigma_dut_print(dut, DUT_MSG_INFO, "BIPMICErrors %s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return STATUS_SENT;
	}

	if (strcasecmp(parameter, "CMACReplays") == 0) {
		if (get_cmac_replay_count(dut, ifname, &val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to get CMACReplays");
			return STATUS_SENT_ERROR;
		}
		snprintf(buf, sizeof(buf), "CMACReplays,%d", val);
		sigma_dut_print(dut, DUT_MSG_INFO, "CMACReplays %s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return STATUS_SENT;
	}

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return STATUS_SENT_ERROR;
}


static enum sigma_cmd_result sta_get_pmk(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	char buf[4096], bssid[20], resp[200], *pos, *tmp;
	const char *ap_mld_mac = get_param(cmd, "APMLD_MAC_Address");

	snprintf(buf, sizeof(buf), "PMKSA_GET %d", dut->infra_network_id);
	if (wpa_command_resp(intf, buf, buf, sizeof(buf)) < 0 ||
	    strncmp(buf, "UNKNOWN COMMAND", 15) == 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,PMKSA_GET not supported");
		return STATUS_SENT_ERROR;
	}

	if (strncmp(buf, "FAIL", 4) == 0 ||
	    get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not find current network");
		return STATUS_SENT_ERROR;
	}
	if (ap_mld_mac)
		strlcpy(bssid, ap_mld_mac, sizeof(bssid));

	pos = buf;
	while (pos) {
		if (strncasecmp(pos, bssid, 17) == 0) {
			pos = strchr(pos, ' ');
			if (!pos)
				break;
			pos++;
			pos = strchr(pos, ' ');
			if (!pos)
				break;
			pos++;
			tmp = strchr(pos, ' ');
			if (!tmp)
				break;
			*tmp = '\0';
			break;
		}
		pos = strchr(pos, '\n');
		if (pos)
			pos++;
	}

	if (!pos) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,PMK not available");
		return STATUS_SENT_ERROR;
	}

	snprintf(resp, sizeof(resp), "PMK,%s", pos);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


static enum sigma_cmd_result cmd_sta_get_parameter(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");
	const char *parameter = get_param(cmd, "Parameter");

	if (!parameter)
		return INVALID_SEND_STATUS;

	if (strcasecmp(parameter, "PMK") == 0)
		return sta_get_pmk(dut, conn, cmd);

	if (!program)
		return INVALID_SEND_STATUS;

	if (strcasecmp(program, "P2PNFC") == 0)
		return p2p_cmd_sta_get_parameter(dut, conn, cmd);

	if (strcasecmp(program, "60ghz") == 0)
		return sta_get_parameter_60g(dut, conn, cmd);

	if (strcasecmp(program, "he") == 0 ||
	    strcasecmp(program, "eht") == 0)
		return sta_get_parameter_he(dut, conn, cmd);

#ifdef ANDROID_NAN
	if (strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_get_parameter(dut, conn, cmd);
#endif /* ANDROID_NAN */

#ifdef MIRACAST
	if (strcasecmp(program, "WFD") == 0 ||
	    strcasecmp(program, "DisplayR2") == 0)
		return miracast_cmd_sta_get_parameter(dut, conn, cmd);
#endif /* MIRACAST */
	if (strcasecmp(program, "WPA3") == 0)
		return sta_get_parameter_wpa3(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static void sta_reset_default_ath(struct sigma_dut *dut, const char *intf,
				  const char *type)
{
	char buf[100];

	if (dut->program == PROGRAM_VHT) {
		run_iwpriv(dut, intf, "chwidth 2");
		run_iwpriv(dut, intf, "mode 11ACVHT80");
		run_iwpriv(dut, intf, "vhtmcs -1");
	}

	if (dut->program == PROGRAM_HT) {
		run_iwpriv(dut, intf, "chwidth 0");
		run_iwpriv(dut, intf, "mode 11naht40");
		run_iwpriv(dut, intf, "set11NRates 0");
	}

	if (dut->program == PROGRAM_VHT || dut->program == PROGRAM_HT) {
		run_iwpriv(dut, intf, "powersave 0");

		/* Reset CTS width */
		snprintf(buf, sizeof(buf), "wifitool %s beeliner_fw_test 54 0",
			 intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"wifitool %s beeliner_fw_test 54 0 failed",
					intf);
		}

		/* Enable Dynamic Bandwidth signalling by default */
		run_iwpriv(dut, intf, "cwmenable 1");

		snprintf(buf, sizeof(buf), "iwconfig %s rts 2347", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv rts failed");
		}
	}

	if (type && strcasecmp(type, "Testbed") == 0) {
		dut->testbed_flag_txsp = 1;
		dut->testbed_flag_rxsp = 1;
		/* STA has to set spatial stream to 2 per Appendix H */
		run_iwpriv(dut, intf, "vht_mcsmap 0xfff0");

		/* Disable LDPC per Appendix H */
		run_iwpriv(dut, intf, "ldpc 0");

		run_iwpriv(dut, intf, "amsdu 1");

		/* TODO: Disable STBC 2x1 transmit and receive */
		run_iwpriv(dut, intf, "tx_stbc 0");
		run_iwpriv(dut, intf, "rx_stbc 0");

		/* STA has to disable Short GI per Appendix H */
		run_iwpriv(dut, intf, "shortgi 0");
	}

	if (type && strcasecmp(type, "DUT") == 0) {
		run_iwpriv(dut, intf, "nss 3");
		dut->sta_nss = 3;

		run_iwpriv(dut, intf, "shortgi 1");
	}
}


#ifdef NL80211_SUPPORT

static int sta_set_he_mcs(struct sigma_dut *dut, const char *intf,
			  enum he_mcs_config mcs)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MCS, mcs);
}


static int sta_set_eht_mcs(struct sigma_dut *dut, const char *intf, uint8_t mcs)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_MCS, mcs);
}

#endif /* NL80211_SUPPORT */


static int sta_set_inactive_period(struct sigma_dut *dut,
				   const char *intf, int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_FORCE_MLO_POWER_SAVE_BCN_PERIOD,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"EHT inactivate link cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_action_tx_in_he_tb_ppdu(struct sigma_dut *dut,
					   const char *intf, int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_ACTION_TX_TB_PPDU,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"HE action Tx TB PPDU cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_heconfig_and_wep_tkip(struct sigma_dut *dut,
					 const char *intf, int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_WEP_TKIP_IN_HE,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"HE config enablement cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_testbed_def(struct sigma_dut *dut, const char *intf,
				   int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_SET_EHT_TESTBED_DEFAULTS,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_beamformee_ss_80(struct sigma_dut *dut,
					const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_BEAMFORMEE_SS_80MHZ,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_beamformee_ss_160(struct sigma_dut *dut,
					 const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_BEAMFORMEE_SS_160MHZ,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_beamformee_ss_320(struct sigma_dut *dut,
					 const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_BEAMFORMEE_SS_320MHZ,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_mlo_str_tx(struct sigma_dut *dut, const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_MLO_STR_TX,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_emlsr_padding_delay(struct sigma_dut *dut,
					 const char *intf, u8 val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EMLSR_PADDING_DELAY,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_tb_sounding_fb_rl(struct sigma_dut *dut,
					 const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_TB_SOUNDING_FB_RL,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_exclude_sta_prof_ml_ie(struct sigma_dut *dut,
					  const char *intf, int cfg)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EXCLUDE_STA_PROF_IN_PROBE_REQ,
		cfg);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR, "NL80211_SUPPORT is not defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_eht_om_ctrl_supp(struct sigma_dut *dut, const char *intf,
				    int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_EHT_OM_CTRL_SUPPORT, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"EHT OM ctrl cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


#ifdef NL80211_SUPPORT

static int sta_set_he_testbed_def(struct sigma_dut *dut,
				  const char *intf, int cfg)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_SET_HE_TESTBED_DEFAULTS,
		cfg);
}


static int sta_set_2g_vht_supp(struct sigma_dut *dut, const char *intf, int cfg)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ENABLE_2G_VHT,
		cfg);
}

#endif /* NL80211_SUPPORT */


int sta_set_addba_buf_size(struct sigma_dut *dut,
			   const char *intf, int bufsize)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u16(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ADDBA_BUFF_SIZE, bufsize);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"AddBA bufsize cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_scan_unicast_probe(struct sigma_dut *dut,
				      const char *intf, int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_USE_BSSID_IN_PROBE_REQ_RA,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Unicast RA in Probe Request frame cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_rx_ctrl_multi_bss(struct sigma_dut *dut, const char *intf,
				     int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_RX_CTRL_FRAME_TO_MBSS,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Rx ctrl frame to Multi-BSS cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_bcast_twt_support(struct sigma_dut *dut, const char *intf,
				     int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_BCAST_TWT_SUPPORT,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"BCAST TWT cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_tx_beamformee(struct sigma_dut *dut, const char *intf,
				 int enable)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ENABLE_TX_BEAMFORMEE,
		enable);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"tx beamformee cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_beamformee_sts(struct sigma_dut *dut, const char *intf,
				  int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_TX_BEAMFORMEE_NSTS,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"beamformee sts cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


#ifdef NL80211_SUPPORT
static int sta_set_mac_padding_duration(struct sigma_dut *dut, const char *intf,
					enum qca_wlan_he_mac_padding_dur val)
{
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_MAC_PADDING_DUR, val);
}
#endif /* NL80211_SUPPORT */


static int sta_set_tx_su_ppdu_cfg(struct sigma_dut *dut, const char *intf,
				  int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_TX_SUPPDU,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Tx SU PPDU cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_mgmt_data_tx_disable_cfg(struct sigma_dut *dut,
					    const char *intf, int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_DISABLE_DATA_MGMT_RSP_TX,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Tx disable config cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_keep_alive_data_cfg(struct sigma_dut *dut, const char *intf,
				       int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_KEEP_ALIVE_FRAME_TYPE,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Keep alive data type cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


#ifdef NL80211_SUPPORT
static int sta_set_he_om_ctrl_reset(struct sigma_dut *dut, const char *intf)
{
	return wcn_wifi_test_config_set_flag(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_CLEAR_HE_OM_CTRL_CONFIG);
}
#endif /* NL80211_SUPPORT */


static int sta_set_mu_edca_override(struct sigma_dut *dut, const char *intf,
				    int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_OVERRIDE_MU_EDCA, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"MU EDCA override cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_er_su_ppdu_type_tx(struct sigma_dut *dut, const char *intf,
				      int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ER_SU_PPDU_TYPE, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"ER-SU PPDU type cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_ru_242_tone_tx(struct sigma_dut *dut, const char *intf,
				  int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_RU_242_TONE_TX, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"RU 242 tone cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_om_ctrl_supp(struct sigma_dut *dut, const char *intf,
				int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OM_CTRL_SUPP, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"HE OM ctrl cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


#ifdef NL80211_SUPPORT

struct features_info {
	unsigned char flags[8];
	size_t flags_len;
};

static int features_info_handler(struct nl_msg *msg, void *arg)
{
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct features_info *info = arg;
	struct nlattr *nl_vend, *attr;

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	nl_vend = tb[NL80211_ATTR_VENDOR_DATA];
	if (nl_vend) {
		struct nlattr *tb_vendor[QCA_WLAN_VENDOR_ATTR_MAX + 1];

		nla_parse(tb_vendor, QCA_WLAN_VENDOR_ATTR_MAX,
			  nla_data(nl_vend), nla_len(nl_vend), NULL);

		attr = tb_vendor[QCA_WLAN_VENDOR_ATTR_FEATURE_FLAGS];
		if (attr) {
			int len = nla_len(attr);

			if (info && len <= sizeof(info->flags)) {
				memcpy(info->flags, nla_data(attr), len);
				info->flags_len = len;
			}
		}
	}

	return NL_SKIP;
}


static int check_feature(enum qca_wlan_vendor_features feature,
			 struct features_info *info)
{
	size_t idx = feature / 8;

	if (!info)
		return 0;

	return (idx < info->flags_len) &&
		(info->flags[idx] & BIT(feature % 8));
}

#endif /* NL80211_SUPPORT */


static void sta_get_twt_feature_async_supp(struct sigma_dut *dut,
					   const char *intf)
{
#ifdef NL80211_SUPPORT
	struct nl_msg *msg;
	struct features_info info = { 0 };
	int ifindex, ret;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_GET_FEATURES)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return;
	}

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, features_info_handler,
				 &info);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		return;
	}

	if (check_feature(QCA_WLAN_VENDOR_FEATURE_TWT_ASYNC_SUPPORT, &info))
		dut->sta_async_twt_supp = 1;
	else
		dut->sta_async_twt_supp = 0;

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"%s: sta_async_twt_supp %d",
			__func__, dut->sta_async_twt_supp);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_INFO,
			"TWT async supp get cannot be done without NL80211_SUPPORT defined");
	dut->sta_async_twt_supp = 0;
#endif /* NL80211_SUPPORT */
}


static int sta_set_twt_req_support(struct sigma_dut *dut, const char *intf,
				   int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_TWT_REQ_SUPPORT, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT Request cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_bss_max_idle_period(struct sigma_dut *dut, const char *intf,
				       int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u16(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_BSS_MAX_IDLE_PERIOD, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"BSS max idle period cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_bss_max_idle_support(struct sigma_dut *dut, const char *intf,
					int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_BSS_MAX_IDLE_PERIOD_ENABLE,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"BSS max idle support cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_fullbw_ulmumimo(struct sigma_dut *dut, const char *intf,
				   int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_FULL_BW_UL_MU_MIMO, val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Full BW UL MU MIMO cannot be changed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_punctured_preamble_rx(struct sigma_dut *dut,
					 const char *intf, int val)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_PUNCTURED_PREAMBLE_RX,
		val);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Punctured preamble Rx cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_set_he_tx_rate(struct sigma_dut *dut, const char *intf,
			      u16 tx_rate, u8 nss)
{
 #ifdef NL80211_SUPPORT
	struct nlattr *attr;
	struct nlattr *attr1;
	int ifindex, ret;
	struct nl_msg *msg;
	struct nl80211_txrate_he he_rate;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_SET_TX_BITRATE_MASK)) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_TX_RATES))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_CMD_SET_TX_BITRATE_MASK msg failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	memset(&he_rate, 0, sizeof(he_rate));
	sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: Set HE Tx rate %0X for Nss %d",
			__func__, tx_rate, nss);

	he_rate.mcs[nss - 1] = tx_rate;

	attr1 = nla_nest_start(msg, NL80211_BAND_2GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_2GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	if (nla_put(msg, NL80211_TXRATE_HE, sizeof(he_rate), &he_rate)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_TXRATE_HE for 2.4 GHz failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, attr1);

	attr1 = nla_nest_start(msg, NL80211_BAND_5GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_5GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	if (nla_put(msg, NL80211_TXRATE_HE, sizeof(he_rate), &he_rate)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_TXRATE_HE for 5 GHz failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, attr1);

	attr1 = nla_nest_start(msg, NL80211_BAND_6GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_6GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	if (nla_put(msg, NL80211_TXRATE_HE, sizeof(he_rate), &he_rate)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_TXRATE_HE for 6 GHz failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, attr1);

	nla_nest_end(msg, attr);
	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: send_and_recv_msgs failed, ret=%d",
				__func__, ret);
	}
	return ret;
#else /* NL80211_SUPPORT */
	return -1;
#endif /* NL80211_SUPPORT */
}


int wcn_set_he_gi(struct sigma_dut *dut, const char *intf, u8 gi_val)
{
 #ifdef NL80211_SUPPORT
	struct nlattr *attr;
	struct nlattr *attr1;
	int ifindex, ret;
	struct nl_msg *msg;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_SET_TX_BITRATE_MASK)) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_TX_RATES))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_CMD_SET_TX_BITRATE_MASK msg failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: Setting HE GI %d",
			__func__, gi_val);

	attr1 = nla_nest_start(msg, NL80211_BAND_2GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_2GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_put_u8(msg, NL80211_TXRATE_HE_GI, gi_val);
	nla_nest_end(msg, attr1);

	attr1 = nla_nest_start(msg, NL80211_BAND_5GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_5GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_put_u8(msg, NL80211_TXRATE_HE_GI, gi_val);
	nla_nest_end(msg, attr1);

	nla_nest_end(msg, attr);
	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: send_and_recv_msgs failed, ret=%d",
				__func__, ret);
	}
	return ret;
#else /* NL80211_SUPPORT */
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_set_vht_gi(struct sigma_dut *dut, const char *intf, u8 gi_val)
{
 #ifdef NL80211_SUPPORT
	struct nlattr *attr;
	struct nlattr *attr1;
	int ifindex, ret;
	struct nl_msg *msg;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_SET_TX_BITRATE_MASK)) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_TX_RATES))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: NL80211_CMD_SET_TX_BITRATE_MASK msg failed",
				__func__);
		nlmsg_free(msg);
		return -1;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: Setting VHT GI %d",
			__func__, gi_val);

	attr1 = nla_nest_start(msg, NL80211_BAND_2GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_2GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_put_u8(msg, NL80211_TXRATE_GI, gi_val);
	nla_nest_end(msg, attr1);

	attr1 = nla_nest_start(msg, NL80211_BAND_5GHZ);
	if (!attr1) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Netlink nest start failed for NL80211_BAND_5GHZ",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_put_u8(msg, NL80211_TXRATE_GI, gi_val);
	nla_nest_end(msg, attr1);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: send_and_recv_msgs failed, ret=%d",
				__func__, ret);
	}
	return ret;
#else /* NL80211_SUPPORT */
	return -1;
#endif /* NL80211_SUPPORT */
}


static void sta_reset_default_wcn(struct sigma_dut *dut, const char *intf,
				  const char *type)
{
	char buf[60];
#ifdef NL80211_SUPPORT
	int ret;
#endif /* NL80211_SUPPORT */

	if (dut->program == PROGRAM_HE || dut->program == PROGRAM_EHT ||
	    dut->device_mode == MODE_11BE) {
		/* resetting phymode to auto in case of HE program */
		sta_set_phymode(dut, intf, "auto");

		/* reset the rate to Auto rate */
		if (wcn_set_he_tx_rate(dut, intf, 0x0FFF, 2)) {
			/* 0xFFF value for setting MCS 0-12 */
			sigma_dut_print(dut, DUT_MSG_INFO,
					"wcn_set_he_tx_rate failed, using iwpriv");
			snprintf(buf, sizeof(buf),
				 "iwpriv %s set_11ax_rate 0xff", intf);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv %s set_11ax_rate 0xff failed",
						intf);
			}
		}

		/* reset the LDPC setting */
		snprintf(buf, sizeof(buf), "iwpriv %s ldpc 1", intf);
		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"iwpriv %s ldpc 1 failed", intf);
		}

		/* reset the power save setting */
		set_power_save_wcn(dut, intf, 2);

		/* remove all network profiles */
		remove_wpa_networks(intf);

		/* Configure ADDBA Req/Rsp buffer size to be 64 */
		sta_set_addba_buf_size(dut, intf, 64);

		if (dut->sta_async_twt_supp == -1)
			sta_get_twt_feature_async_supp(dut, intf);

		sta_set_scan_unicast_probe(dut, intf, 0);

#ifdef NL80211_SUPPORT
		nl80211_close_event_sock(dut);

		/* Reset the device HE capabilities to its default supported
		 * configuration. */
		sta_set_he_testbed_def(dut, intf, 0);

		/* Disable noackpolicy for all AC */
		if (nlvendor_sta_set_noack(dut, intf, 0, QCA_WLAN_AC_ALL)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable of noackpolicy for all AC failed");
		}
#endif /* NL80211_SUPPORT */

		/* Enable WMM by default */
		if (wcn_sta_set_wmm(dut, intf, "on")) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable of WMM in sta_reset_default_wcn failed");
		}

		/* Disable ADDBA_REJECT by default */
		if (nlvendor_sta_set_addba_reject(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Disable of addba_reject in sta_reset_default_wcn failed");
		}

		/* Enable sending of ADDBA by default */
		if (nlvendor_config_send_addba(dut, intf, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Enable sending of ADDBA in sta_reset_default_wcn failed");
		}

		/* Enable AMPDU by default */
		wcn_sta_set_ampdu(dut, intf, 1);

#ifdef NL80211_SUPPORT
		if (wcn_set_he_ltf(dut, intf, QCA_WLAN_HE_LTF_AUTO)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Set LTF config to default in sta_reset_default_wcn failed");
		}

		/* set the beamformee NSTS(maximum number of
		 * space-time streams) to default DUT config
		 */
		if (sta_set_beamformee_sts(dut, intf, 7)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set BeamformeeSTS");
		}

		if (sta_set_mgmt_data_tx_disable_cfg(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to reset mgmt/data Tx disable config");
		}

		if (sta_set_mac_padding_duration(
			    dut, intf,
			    QCA_WLAN_HE_NO_ADDITIONAL_PROCESS_TIME)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set MAC padding duration");
		}

		if (sta_set_mu_edca_override(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"ErrorCode,Failed to set MU EDCA override disable");
		}

		if (sta_set_ru_242_tone_tx(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				  "Failed to set RU 242 tone Tx");
		}

		if (sta_set_er_su_ppdu_type_tx(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				  "Failed to set ER-SU PPDU type Tx");
		}

		if (sta_set_om_ctrl_supp(dut, intf, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set OM ctrl supp");
		}

		if (sta_set_tx_su_ppdu_cfg(dut, intf, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set Tx SU PPDU enable");
		}

		if (sta_set_action_tx_in_he_tb_ppdu(dut, intf, 0)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"failed to send TB PPDU Tx cfg");
		}

		if (sta_set_he_om_ctrl_reset(dut, intf)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set OM ctrl reset");
		}

		/* +HTC-HE support default on */
		if (sta_set_he_htc_supp(dut, intf, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting of +HTC-HE support failed");
		}
#endif /* NL80211_SUPPORT */

		if (sta_set_tx_beamformee(dut, intf, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Set tx beamformee enable by default in sta_reset_default_wcn failed");
		}

		wpa_command(intf, "SET oce 1");

		/* Set nss to 1 and MCS 0-7 in case of testbed */
		if (type && strcasecmp(type, "Testbed") == 0) {

			wpa_command(intf, "SET oce 0");

			if (wcn_sta_set_sp_stream(dut, intf, "1SS") < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"set nss to 1SS failed");
			}

#ifdef NL80211_SUPPORT
			ret = sta_set_he_mcs(dut, intf, HE_80_MCS0_7);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting of MCS failed, ret:%d",
						ret);
			}

			ret = sta_set_he_mcs(dut, intf, HE_160_MCS0_7);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"160M MCS set failed, ret:%d",
						ret);
			}
#endif /* NL80211_SUPPORT */

			/* Disable STBC as default */
			wcn_sta_set_stbc(dut, intf, "0");

			/* Disable AMSDU as default */
			iwpriv_sta_set_amsdu(dut, intf, "0");

#ifdef NL80211_SUPPORT
			/* HE fragmentation default off */
			if (sta_set_he_fragmentation(dut, intf,
						     HE_FRAG_DISABLE)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting of HE fragmentation failed");
			}

			/* set the beamformee NSTS(maximum number of
			 * space-time streams) to default testbed config
			 */
			if (sta_set_beamformee_sts(dut, intf, 3)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to set BeamformeeSTS");
			}

			if (sta_set_punctured_preamble_rx(dut, intf, 0)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to reset PreamblePunctRx support");
			}

			if (sta_set_bss_max_idle_period(dut, intf, 0)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to reset BSS max idle period");
			}

			/* +HTC-HE support default off */
			if (sta_set_he_htc_supp(dut, intf, 0)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting of +HTC-HE support failed");
			}

			/* Set device HE capabilities to testbed default
			 * configuration. */
			if (sta_set_he_testbed_def(dut, intf, 1)) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Failed to set HE defaults");
			}

			/* Disable VHT support in 2.4 GHz for testbed */
			sta_set_2g_vht_supp(dut, intf, 0);
#endif /* NL80211_SUPPORT */

			/* Enable WEP/TKIP with HE capability in testbed */
			if (sta_set_heconfig_and_wep_tkip(dut, intf, 1)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Enabling HE config with WEP/TKIP failed");
			}
		}

		/* Defaults in case of DUT */
		if (type && strcasecmp(type, "DUT") == 0) {
			/* Enable STBC by default */
			wcn_sta_set_stbc(dut, intf, "1");

			/* set nss to 2 */
			if (wcn_sta_set_sp_stream(dut, intf, "2SS") < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"set nss to 2SS failed");
			}
#ifdef NL80211_SUPPORT
			/* Set HE_MCS to 0-11 */
			if (sta_set_he_mcs(dut, intf, HE_80_MCS0_11)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting of MCS failed");
			}
#endif /* NL80211_SUPPORT */

			/* Disable WEP/TKIP with HE capability in DUT */
			if (sta_set_heconfig_and_wep_tkip(dut, intf, 0)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Enabling HE config with WEP/TKIP failed");
			}
		}
	}

	if (dut->program == PROGRAM_EHT || dut->device_mode == MODE_11BE) {
		/* Reset the device EHT capabilities to its default supported
		 * configuration. */
		sta_set_eht_testbed_def(dut, intf, 0);
		sta_config_params(dut, intf,
				  STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS, 0);
		sta_config_params(dut, intf, STA_SET_EHT_EML_CAPABILITY, 0);
		sta_config_params(dut, intf, STA_SET_EHT_MLO_MAX_NUM_LINKS, 1);
		sta_config_params(dut, intf, STA_SET_EHT_MLO_MODE,
				  QCA_WLAN_EHT_MLSR);

		if (type && strcasecmp(type, "Testbed") == 0) {
			sta_set_eht_testbed_def(dut, intf, 1);
#ifdef NL80211_SUPPORT
			ret = sta_set_he_mcs(dut, intf, HE_80_MCS0_9);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Setting of MCS failed, ret:%d",
						ret);
			}

			ret = sta_set_he_mcs(dut, intf, HE_160_MCS0_9);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"160M MCS set failed, ret:%d",
						ret);
			}
#endif /* NL80211_SUPPORT */
		}
	}
}


static int sta_set_client_privacy(struct sigma_dut *dut,
				  struct sigma_conn *conn, const char *intf,
				  int enable)
{
	if (enable &&
	    (wpa_command(intf, "SET mac_addr 1") < 0 ||
	     wpa_command(intf, "SET rand_addr_lifetime 1") < 0 ||
	     (wpa_command(intf, "MAC_RAND_SCAN enable=1 all") < 0 &&
	      wpa_command(intf, "SET preassoc_mac_addr 1") < 0) ||
	     wpa_command(intf, "SET gas_rand_mac_addr 1") < 0 ||
	     wpa_command(intf, "SET gas_rand_addr_lifetime 1") < 0))
		return -1;

	if (!enable &&
	    (wpa_command(intf, "SET mac_addr 0") < 0 ||
	     (wpa_command(intf, "MAC_RAND_SCAN enable=0 all") < 0 &&
	      wpa_command(intf, "SET preassoc_mac_addr 0") < 0) ||
	     wpa_command(intf, "SET gas_rand_mac_addr 0") < 0))
		return -1;

	dut->client_privacy = enable;
	return 0;
}


static enum sigma_cmd_result cmd_sta_reset_default(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *band = get_param(cmd, "band");
	const char *type;
	const char *program = get_param(cmd, "program");
	const char *dev_role = get_param(cmd, "DevRole");
	char resp[20];
	char buf[100];
	int ret;

#ifdef ANDROID
	kill_pid(dut, concat_sigma_tmpdir(dut, "/sigma_dut-dnsmasq.pid",
					  buf, sizeof(buf)));
#endif /* ANDROID */

	if (dut->station_ifname_2g &&
	    strcmp(dut->station_ifname_2g, intf) == 0)
		dut->use_5g = 0;
	else if (dut->station_ifname_5g &&
		 strcmp(dut->station_ifname_5g, intf) == 0)
		dut->use_5g = 1;

	if (!program)
		program = get_param(cmd, "prog");
	dut->program = sigma_program_to_enum(program);

	if (dut->program == PROGRAM_WFD && dut->user_config_timeout)
		dut->default_timeout = dut->user_config_timeout;

	dut->device_mode = MODE_UNKNOWN;

	dut->device_type = STA_unknown;
	type = get_param(cmd, "type");
	if (type && strcasecmp(type, "Testbed") == 0)
		dut->device_type = STA_testbed;
	if (type && strcasecmp(type, "DUT") == 0)
		dut->device_type = STA_dut;

	if (dut->program == PROGRAM_TDLS) {
		/* Clear TDLS testing mode */
		wpa_command(intf, "SET tdls_disabled 0");
		wpa_command(intf, "SET tdls_testing 0");
		dut->no_tpk_expiration = 0;
		if (get_driver_type(dut) == DRIVER_WCN) {
			/* Enable the WCN driver in TDLS Explicit trigger mode
			 */
			wpa_command(intf, "SET tdls_external_control 0");
			wpa_command(intf, "SET tdls_trigger_control 0");
		}
	}

#ifdef MIRACAST
	if (dut->program == PROGRAM_WFD ||
	    dut->program == PROGRAM_DISPLAYR2)
		miracast_sta_reset_default(dut, conn, cmd);
#endif /* MIRACAST */

	switch (get_driver_type(dut)) {
	case DRIVER_ATHEROS:
		sta_reset_default_ath(dut, intf, type);
		break;
	case DRIVER_WCN:
		sta_reset_default_wcn(dut, intf, type);
		break;
	default:
		break;
	}

#ifdef ANDROID_NAN
	if (dut->program == PROGRAM_NAN)
		nan_cmd_sta_reset_default(dut, conn, cmd);
#endif /* ANDROID_NAN */

	if ((dut->program == PROGRAM_LOC || dut->program == PROGRAM_LOCR2) &&
	    lowi_cmd_sta_reset_default(dut, conn, cmd) < 0)
		return ERROR_SEND_STATUS;

	if (is_passpoint_r2_or_newer(dut->program)) {
		unlink("SP/wi-fi.org/pps.xml");
		if (system("rm -r SP/*") != 0) {
		}
		unlink("next-client-cert.pem");
		unlink("next-client-key.pem");
	}

	/* For WPS program of the 60 GHz band the band type needs to be saved */
	if (dut->program == PROGRAM_WPS) {
		if (band && strcasecmp(band, "60GHz") == 0) {
			dut->band = WPS_BAND_60G;
			/* For 60 GHz enable WPS for WPS TCs */
			dut->wps_disable = 0;
		} else {
			dut->band = WPS_BAND_NON_60G;
		}
	} else if (dut->program == PROGRAM_60GHZ) {
		/* For 60 GHz MAC/PHY TCs WPS must be disabled */
		dut->wps_disable = 1;
	}

	if (is_60g_sigma_dut(dut)) {
		const char *dev_role = get_param(cmd, "DevRole");
		char buf[256];

		sigma_dut_print(dut, DUT_MSG_INFO,
				"WPS 60 GHz program, wps_disable = %d",
				dut->wps_disable);

		if (!dev_role) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Missing DevRole argument");
			return 0;
		}

		if (strcasecmp(dev_role, "STA") == 0)
			dut->dev_role = DEVROLE_STA;
		else if (strcasecmp(dev_role, "PCP") == 0)
			dut->dev_role = DEVROLE_PCP;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unknown DevRole");
			return 0;
		}

		if (dut->device_type == STA_unknown) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Device type is not STA testbed or DUT");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unknown device type");
			return 0;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Setting msdu_size to MAX: 7912");
		snprintf(buf, sizeof(buf), "ifconfig %s mtu 7912",
			 get_station_ifname(dut));

		if (system(buf) != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set %s",
					buf);
			return ERROR_SEND_STATUS;
		}

		if (sta_set_force_mcs(dut, 0, 1)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to reset force MCS");
			return ERROR_SEND_STATUS;
		}
	}

	wpa_command(intf, "WPS_ER_STOP");
	wpa_command(intf, "FLUSH");
	wpa_command(intf, "ERP_FLUSH");
	wpa_command(intf, "SET radio_disabled 0");

	dut->wps_forced_version = 0;

	if (dut->wsc_fragment) {
		dut->wsc_fragment = 0;
		wpa_command(intf, "SET device_name Test client");
		wpa_command(intf, "SET manufacturer ");
		wpa_command(intf, "SET model_name ");
		wpa_command(intf, "SET model_number ");
		wpa_command(intf, "SET serial_number ");
	}
	if (is_60g_sigma_dut(dut) && dut->force_rsn_ie) {
		dut->force_rsn_ie = FORCE_RSN_IE_NONE;
		sta_60g_force_rsn_ie(dut, FORCE_RSN_IE_NONE);
	}

	if (dut->tmp_mac_addr && dut->set_macaddr) {
		dut->tmp_mac_addr = 0;
		if (system(dut->set_macaddr) != 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Failed to clear "
					"temporary MAC address");
		}
	}

	set_ps(intf, dut, 0);

	if (is_passpoint(dut->program)) {
		wpa_command(intf, "SET interworking 1");
		wpa_command(intf, "SET hs20 1");
	}

	if (is_passpoint_r2_or_newer(dut->program) ||
	    dut->program == PROGRAM_OCE || dut->program == PROGRAM_WPA3) {
		wpa_command(intf, "SET pmf 1");
	} else {
		wpa_command(intf, "SET pmf 0");
	}

	hs2_clear_credentials(intf);
	wpa_command(intf, "SET hessid 00:00:00:00:00:00");
	wpa_command(intf, "SET access_network_type 15");

	static_ip_file(0, NULL, NULL, NULL);
	kill_dhcp_client(dut, intf);
	clear_ip_addr(dut, intf);

	dut->er_oper_performed = 0;
	dut->er_oper_bssid[0] = '\0';

	if (dut->program == PROGRAM_LOC) {
		/* Disable Interworking by default */
		wpa_command(get_station_ifname(dut), "SET interworking 0");
	}

	if (dut->program == PROGRAM_MBO || dut->program == PROGRAM_HE ||
	    dut->program == PROGRAM_EHT) {
		free(dut->non_pref_ch_list);
		dut->non_pref_ch_list = NULL;
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
		wpa_command(intf, "SET reject_btm_req_reason 0");
		wpa_command(intf, "SET ignore_assoc_disallow 0");
		wpa_command(intf, "SET gas_address3 0");
		wpa_command(intf, "SET roaming 1");
		wpa_command(intf, "SET interworking 1");
	}

	free(dut->rsne_override);
	dut->rsne_override = NULL;

	free(dut->sae_commit_override);
	dut->sae_commit_override = NULL;
	wpa_command(intf, "SET sae_pmkid_in_assoc 0");
	dut->sae_pwe = SAE_PWE_DEFAULT;

	dut->sta_associate_wait_connect = 0;
	dut->server_cert_hash[0] = '\0';
	dut->server_cert_tod = 0;
	dut->sta_tod_policy = 0;

	dut->dpp_conf_id = -1;
	free(dut->dpp_peer_uri);
	dut->dpp_peer_uri = NULL;
	dut->dpp_local_bootstrap = -1;
	wpa_command(intf, "SET dpp_config_processing 2");
	wpa_command(intf, "SET dpp_mud_url ");
	wpa_command(intf, "SET dpp_extra_conf_req_name ");
	wpa_command(intf, "SET dpp_extra_conf_req_value ");
	wpa_command(intf, "SET dpp_connector_privacy_default 0");
	dpp_mdns_stop(dut);
	unlink("/tmp/dpp-rest-server.uri");
	unlink("/tmp/dpp-rest-server.id");

	wpa_command(intf, "VENDOR_ELEM_REMOVE 13 *");

	if (dut->program == PROGRAM_OCE) {
		wpa_command(intf, "SET oce 1");
		wpa_command(intf, "SET disable_fils 0");
		wpa_command(intf, "FILS_HLP_REQ_FLUSH");
		dut->fils_hlp = 0;
#ifdef ANDROID
		hlp_thread_cleanup(dut);
#endif /* ANDROID */
	}

	if (dut->program == PROGRAM_QM) {
		wpa_command(intf, "SET interworking 1");
		wpa_command(intf, "SET enable_dscp_policy_capa 1");
		dut->qm_domain_name[0] = '\0';
		dut->reject_dscp_policies = 0;
		dut->num_dscp_status = 0;
		snprintf(buf, sizeof(buf),
			 "ip -6 route replace fe80::/64 dev %s table local",
			 intf);
		if (system(buf) != 0)
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to run: %s",
					buf);

		stop_dscp_policy_mon_thread(dut);
		clear_all_dscp_policies(dut);
	}

	dut->akm_values = 0;

#ifdef NL80211_SUPPORT
	if (get_driver_type(dut) == DRIVER_WCN)
		sta_config_params(dut, intf, STA_SET_FT_DS, 0);
#endif /* NL80211_SUPPORT */

	dut->sta_ft_ds = 0;

#ifdef NL80211_SUPPORT
	if (get_driver_type(dut) == DRIVER_WCN &&
	    dut->config_rsnie == 1) {
		dut->config_rsnie = 0;
		sta_config_params(dut, intf, STA_SET_RSNIE, 0);
	}
#endif /* NL80211_SUPPORT */

	if (dev_role && strcasecmp(dev_role, "STA-CFON") == 0) {
		dut->dev_role = DEVROLE_STA_CFON;
		return sta_cfon_reset_default(dut, conn, cmd);
	}

	wpa_command(intf, "SET setband AUTO");

	ret = wpa_command_resp(intf, "GET_CAPABILITY ocv", resp, sizeof(resp));
	dut->ocvc = ret == 0 && strncmp(resp, "supported", 9) == 0;

	ret = wpa_command_resp(intf, "GET_CAPABILITY beacon_prot", resp,
			       sizeof(resp));
	dut->beacon_prot = ret == 0 && strncmp(resp, "supported", 9) == 0;

	if (sta_set_client_privacy(dut, conn, intf,
				   dut->device_type == STA_dut &&
				   dut->client_privacy_default)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set client privacy functionality");
		/* sta_reset_default command is not really supposed to fail,
		 * so allow this to continue. */
	}

	if (get_driver_type(dut) == DRIVER_WCN)
		wcn_set_ignore_h2e_rsnxe(dut, intf, 0);

	dut->saquery_oci_freq = 0;
	dut->prev_disable_scs_support = 0;
	dut->prev_disable_mscs_support = 0;

	if (dut->autoconnect_default)
		wpa_command(intf, "STA_AUTOCONNECT 1");
	else
		wpa_command(intf, "STA_AUTOCONNECT 0");

	if (dut->program != PROGRAM_VHT)
		return cmd_sta_p2p_reset(dut, conn, cmd);

	return 1;
}


static enum sigma_cmd_result cmd_sta_get_events(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Program");

	if (program == NULL)
		return -1;
#ifdef ANDROID_NAN
	if (strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_get_events(dut, conn, cmd);
#endif /* ANDROID_NAN */
	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int sta_exec_action_url(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	const char *url = get_param(cmd, "url");
	const char *method = get_param(cmd, "method");
	pid_t pid;
	int status;

	if (!url || !method)
		return -1;

	/* TODO: Add support for method,post */
	if (strcasecmp(method, "get") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported method");
		return 0;
	}

	pid = fork();
	if (pid < 0) {
		perror("fork");
		return -1;
	}

	if (pid == 0) {
		char * argv[5] = { "wget", "-O", "/dev/null",
				   (char *) url, NULL };

		execv("/usr/bin/wget", argv);
		perror("execv");
		exit(0);
		return -1;
	}

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid");
		return -1;
	}

	if (WIFEXITED(status)) {
		const char *errmsg;

		if (WEXITSTATUS(status) == 0)
			return 1;
		sigma_dut_print(dut, DUT_MSG_INFO, "wget exit status %d",
				WEXITSTATUS(status));
		switch (WEXITSTATUS(status)) {
		case 4:
			errmsg = "errmsg,Network failure";
			break;
		case 8:
			errmsg = "errmsg,Server issued an error response";
			break;
		default:
			errmsg = "errmsg,Unknown failure from wget";
			break;
		}
		send_resp(dut, conn, SIGMA_ERROR, errmsg);
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR, "errmsg,Unknown failure");
	return 0;
}


static enum sigma_cmd_result cmd_sta_exec_action(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *program = get_param(cmd, "Prog");

	if (program && !get_param(cmd, "interface"))
		return -1;
#ifdef ANDROID_NAN
	if (program && strcasecmp(program, "NAN") == 0)
		return nan_cmd_sta_exec_action(dut, conn, cmd);
#endif /* ANDROID_NAN */

	if (program && strcasecmp(program, "Loc") == 0)
		return loc_cmd_sta_exec_action(dut, conn, cmd);

	if (program && strcasecmp(program, "LOCR2") == 0)
		return loc_r2_cmd_sta_exec_action(dut, conn, cmd);

	if (get_param(cmd, "url"))
		return sta_exec_action_url(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static enum sigma_cmd_result cmd_sta_set_11n(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *mcs32, *rate;

	val = get_param(cmd, "GREENFIELD");
	if (val) {
		if (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0) {
			/* Enable GD */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,GF not supported");
			return 0;
		}
	}

	val = get_param(cmd, "SGI20");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_sgi(dut, intf, val);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,SGI20 not supported");
			return 0;
		}
	}

	mcs32 = get_param(cmd, "MCS32"); /* HT Duplicate Mode Enable/Disable */
	rate = get_param(cmd, "MCS_FIXEDRATE"); /* Fixed MCS rate (0..31) */
	if (mcs32 && rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32,MCS_FIXEDRATE not supported");
		return 0;
	} else if (mcs32 && !rate) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,MCS32 not supported");
		return 0;
	} else if (!mcs32 && rate) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			novap_reset(dut, intf, 1);
			ath_sta_set_11nrates(dut, intf, rate);
			break;
		default:
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,MCS32_FIXEDRATE not supported");
			return 0;
		}
	}

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static void cmd_set_max_he_mcs(struct sigma_dut *dut, const char *intf,
			       int mcs_config)
{
#ifdef NL80211_SUPPORT
	int ret;

	switch (mcs_config) {
	case HE_80_MCS0_7:
	case HE_80_MCS0_9:
	case HE_80_MCS0_11:
		ret = sta_set_he_mcs(dut, intf, mcs_config);
		if (ret) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"cmd_set_max_he_mcs: Setting of MCS:%d failed, ret:%d",
					mcs_config, ret);
		}
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"cmd_set_max_he_mcs: Invalid mcs %d",
				mcs_config);
		break;
	}
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"max HE MCS cannot be changed without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */
}


struct wait_event {
	struct sigma_dut *dut;
	int cmd;
	unsigned int twt_op;
};

#ifdef NL80211_SUPPORT

static int twt_event_handler(struct nl_msg *msg, void *arg)
{
	struct wait_event *wait = arg;
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *tb[NL80211_ATTR_MAX + 1];
	uint32_t subcmd;
	uint8_t *data = NULL;
	size_t len = 0;
	struct nlattr *twt_rsp[QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_MAX + 1];
	struct nlattr *twt_status[QCA_WLAN_VENDOR_ATTR_TWT_SETUP_MAX + 1];
	int cmd_id;
	unsigned char val;

	if (!wait)
		return NL_SKIP;

	if (gnlh->cmd != NL80211_CMD_VENDOR) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"%s: NL cmd is not vendor %d", __func__,
				gnlh->cmd);
		return NL_SKIP;
	}

	nla_parse(tb, NL80211_ATTR_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!tb[NL80211_ATTR_VENDOR_ID] || !tb[NL80211_ATTR_VENDOR_SUBCMD]) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"%s: vendor ID not found", __func__);
		return NL_SKIP;
	}
	subcmd = nla_get_u32(tb[NL80211_ATTR_VENDOR_SUBCMD]);

	if (subcmd != QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"%s: Not a TWT_cmd %d", __func__, subcmd);
		return NL_SKIP;
	}
	if (tb[NL80211_ATTR_VENDOR_DATA]) {
		data = nla_data(tb[NL80211_ATTR_VENDOR_DATA]);
		len = nla_len(tb[NL80211_ATTR_VENDOR_DATA]);
	} else {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"%s: vendor data not present", __func__);
		return NL_SKIP;
	}
	if (!data || !len) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"Invalid vendor data or len");
		return NL_SKIP;
	}
	sigma_dut_print(wait->dut, DUT_MSG_DEBUG,
			"event data len %ld", len);
	hex_dump(wait->dut, data, len);
	if (nla_parse(twt_rsp, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_MAX,
		      (struct nlattr *) data, len, NULL)) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"vendor data parse error");
		return NL_SKIP;
	}

	val = nla_get_u8(twt_rsp[QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION]);
	if (val != wait->twt_op) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"Invalid TWT operation, expected %d, rcvd %d",
				wait->twt_op, val);
		return NL_SKIP;
	}
	if (nla_parse_nested(twt_status, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_MAX,
			     twt_rsp[QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS],
			     NULL)) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"nla_parse failed for TWT event");
		return NL_SKIP;
	}

	cmd_id = QCA_WLAN_VENDOR_ATTR_TWT_SETUP_STATUS;
	if (!twt_status[cmd_id]) {
		sigma_dut_print(wait->dut, DUT_MSG_ERROR,
				"%s TWT resp status missing", __func__);
		wait->cmd = -1;
	} else {
		val = nla_get_u8(twt_status[cmd_id]);
		if (val != QCA_WLAN_VENDOR_TWT_STATUS_OK) {
			sigma_dut_print(wait->dut, DUT_MSG_ERROR,
					"%s TWT resp status %d", __func__, val);
			wait->cmd = -1;
		} else {
			wait->cmd = 1;
		}
	}

	return NL_SKIP;
}


static int wait_on_nl_socket(struct nl_sock *sock, struct sigma_dut *dut,
			     unsigned int timeout)
{
	fd_set read_fd_set;
	int retval;
	int sock_fd;
	struct timeval time_out;

	time_out.tv_sec = timeout;
	time_out.tv_usec = 0;

	FD_ZERO(&read_fd_set);

	if (!sock)
		return -1;

	sock_fd = nl_socket_get_fd(sock);
	FD_SET(sock_fd, &read_fd_set);

	retval = select(sock_fd + 1, &read_fd_set, NULL, NULL, &time_out);

	if (retval == 0)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: TWT event response timedout", __func__);

	if (retval < 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s:no NL msgs, ret=%d",
				__func__, retval);

	return retval;
}


#define TWT_ASYNC_EVENT_WAIT_TIME_SEC   6

static int twt_async_event_wait(struct sigma_dut *dut, unsigned int twt_op)
{
	struct nl_cb *cb = NULL;
	int err_code = 0, select_retval = 0;
	struct wait_event wait_info;

	if (dut->nl_ctx->event_sock)
		cb = nl_socket_get_cb(dut->nl_ctx->event_sock);
	if (!cb) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"event callback not found");
		return ERROR_SEND_STATUS;
	}

	wait_info.cmd = 0;
	wait_info.dut = dut;
	wait_info.twt_op = twt_op;

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, twt_event_handler, &wait_info);

	while (!wait_info.cmd) {
		select_retval = wait_on_nl_socket(
			dut->nl_ctx->event_sock, dut,
			TWT_ASYNC_EVENT_WAIT_TIME_SEC);

		if (select_retval > 0) {
			err_code = nl_recvmsgs(dut->nl_ctx->event_sock, cb);
			if (err_code < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"%s: nl rcv failed, err_code %d",
						__func__, err_code);
				break;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s: wait on socket failed %d",
					__func__, select_retval);
			err_code = 1;
			break;
		}

	}
	nl_cb_put(cb);

	if (wait_info.cmd < 0)
		err_code = 1;

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"%s: rcvd cmd %d, err_code %d, s_ret %d",
			__func__, wait_info.cmd, err_code, select_retval);

	return err_code;
}

#endif /* NL80211_SUPPORT */


static int sta_twt_send_suspend(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
#ifdef NL80211_SUPPORT
	struct nlattr *attr, *attr1;
	struct nl_msg *msg;
	int ifindex, ret;
	const char *intf = get_param(cmd, "Interface");

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return ERROR_SEND_STATUS;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_SUSPEND) ||
	    !(attr1 = nla_nest_start(msg,
				     QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return ERROR_SEND_STATUS;
	}
	nla_nest_end(msg, attr1);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	if (!dut->sta_async_twt_supp)
		return ret;

	return twt_async_event_wait(dut, QCA_WLAN_TWT_SUSPEND);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT suspend cannot be done without NL80211_SUPPORT defined");
	return ERROR_SEND_STATUS;
#endif /* NL80211_SUPPORT */
}


static int sta_twt_send_nudge(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd,
			      unsigned int suspend_duration)
{
#ifdef NL80211_SUPPORT
	struct nlattr *attr, *attr1;
	struct nl_msg *msg;
	int ifindex, ret;
	const char *intf = get_param(cmd, "Interface");
	int next_twt_size = 1;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return ERROR_SEND_STATUS;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_NUDGE) ||
	    !(attr1 = nla_nest_start(msg,
				     QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS)) ||
	    (suspend_duration &&
	     nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_NUDGE_WAKE_TIME,
			 suspend_duration)) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_NUDGE_NEXT_TWT_SIZE,
			next_twt_size) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_NUDGE_FLOW_ID, 0)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return ERROR_SEND_STATUS;
	}
	nla_nest_end(msg, attr1);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	if (!dut->sta_async_twt_supp)
		return ret;

	return twt_async_event_wait(dut, QCA_WLAN_TWT_NUDGE);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT suspend cannot be done without NL80211_SUPPORT defined");
	return ERROR_SEND_STATUS;
#endif /* NL80211_SUPPORT */
}


static int sta_twt_suspend_or_nudge(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "TWT_SuspendDuration");
	if (val) {
		unsigned int suspend_duration;

		suspend_duration = atoi(val);
		suspend_duration = suspend_duration * 1000 * 1000;
		return sta_twt_send_nudge(dut, conn, cmd, suspend_duration);
	}

	return sta_twt_send_suspend(dut, conn, cmd);
}


static int sta_twt_resume(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
#ifdef NL80211_SUPPORT
	struct nlattr *attr, *attr1;
	struct nl_msg *msg;
	int ifindex, ret;
	const char *intf = get_param(cmd, "Interface");
	int next2_twt_size = 1;
	unsigned int resume_duration = 0;
	const char *val;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "TWT_ResumeDuration");
	if (val) {
		resume_duration = atoi(val);
		resume_duration = resume_duration * 1000 * 1000;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_RESUME) ||
	    !(attr1 = nla_nest_start(msg,
				     QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS)) ||
	    (resume_duration &&
	     nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT2_TWT,
			 resume_duration)) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_RESUME_NEXT_TWT_SIZE,
			next2_twt_size)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return ERROR_SEND_STATUS;
	}
	nla_nest_end(msg, attr1);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	if (!dut->sta_async_twt_supp)
		return ret;

	return twt_async_event_wait(dut, QCA_WLAN_TWT_RESUME);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT resume cannot be done without NL80211_SUPPORT defined");
	return ERROR_SEND_STATUS;
#endif /* NL80211_SUPPORT */
}


#define TWT_REQUEST_CMD     0
#define TWT_SUGGEST_CMD     1
#define TWT_DEMAND_CMD      2

static int sta_twt_request(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
#ifdef NL80211_SUPPORT
	struct nlattr *params;
	struct nlattr *attr;
	struct nl_msg *msg;
	int ifindex, ret;
	const char *val;
	const char *intf = get_param(cmd, "Interface");
	int wake_interval_exp = 10, nominal_min_wake_dur = 255,
		wake_interval_mantissa = 512;
	int flow_type = 0, twt_trigger = 0, target_wake_time = 0,
		protection = 0, cmd_type = QCA_WLAN_VENDOR_TWT_SETUP_SUGGEST;
	int bcast_twt = 0;
	int bcast_twt_id = 0, bcast_twt_recommdn = 0, bcast_twt_persis = 0;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	val = get_param(cmd, "FlowType");
	if (val) {
		flow_type = atoi(val);
		if (flow_type != 0 && flow_type != 1) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TWT: Invalid FlowType %d", flow_type);
			return -1;
		}
	}

	val = get_param(cmd, "TWT_Trigger");
	if (val) {
		twt_trigger = atoi(val);
		if (twt_trigger != 0 && twt_trigger != 1) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TWT: Invalid TWT_Trigger %d",
					twt_trigger);
			return -1;
		}
	}

	val = get_param(cmd, "Protection");
	if (val) {
		protection = atoi(val);
		if (protection != 0 && protection != 1) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"TWT: Invalid Protection %d",
					protection);
			return -1;
		}
	}

	val = get_param(cmd, "SetupCommand");
	if (val) {
		cmd_type = atoi(val);
		if (cmd_type == TWT_REQUEST_CMD)
			cmd_type = QCA_WLAN_VENDOR_TWT_SETUP_REQUEST;
		else if (cmd_type == TWT_SUGGEST_CMD)
			cmd_type = QCA_WLAN_VENDOR_TWT_SETUP_SUGGEST;
		else if (cmd_type == TWT_DEMAND_CMD)
			cmd_type = QCA_WLAN_VENDOR_TWT_SETUP_DEMAND;
		else
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Default suggest is used for cmd %d",
					cmd_type);
	}

	val = get_param(cmd, "TargetWakeTime");
	if (val)
		target_wake_time = atoi(val);

	val = get_param(cmd, "WakeIntervalMantissa");
	if (val)
		wake_interval_mantissa = atoi(val);

	val = get_param(cmd, "WakeIntervalExp");
	if (val)
		wake_interval_exp = atoi(val);

	val = get_param(cmd, "NominalMinWakeDur");
	if (val)
		nominal_min_wake_dur = atoi(val);

	val = get_param(cmd, "BTWT_ID");
	if (val) {
		bcast_twt_id = atoi(val);
		bcast_twt = 1;
	}

	val = get_param(cmd, "BTWT_Persistence");
	if (val) {
		bcast_twt_persis = atoi(val);
		bcast_twt = 1;
	}

	val = get_param(cmd, "BTWT_Recommendation");
	if (val) {
		bcast_twt_recommdn = atoi(val);
		bcast_twt = 1;
	}

	if (bcast_twt)
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"BCAST_TWT: ID %d, RECOMM %d, PERSIS %d",
				bcast_twt_id, bcast_twt_recommdn,
				bcast_twt_persis);

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_SET) ||
	    !(params = nla_nest_start(
		      msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_EXP,
		       wake_interval_exp) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_REQ_TYPE, cmd_type) ||
	    (twt_trigger &&
	     nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_TRIGGER)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_TYPE,
		       flow_type) ||
	    (protection &&
	     nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_PROTECTION)) ||
	    (bcast_twt &&
	     nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST)) ||
	    (bcast_twt &&
	     nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST_ID,
			bcast_twt_id)) ||
	    (bcast_twt &&
	     nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST_PERSISTENCE,
			bcast_twt_persis)) ||
	    (bcast_twt &&
	     nla_put_u8(msg,
			QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST_RECOMMENDATION,
			bcast_twt_recommdn)) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_TIME,
			target_wake_time) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_DURATION,
			nominal_min_wake_dur) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_WAKE_INTVL_MANTISSA,
			wake_interval_mantissa)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, params);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	if (!dut->sta_async_twt_supp)
		return ret;

	return twt_async_event_wait(dut, QCA_WLAN_TWT_SET);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT request cannot be done without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_twt_teardown(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
 #ifdef NL80211_SUPPORT
	struct nlattr *params;
	struct nlattr *attr;
	int ifindex, ret;
	struct nl_msg *msg;
	const char *intf = get_param(cmd, "Interface");
	int bcast_twt = 0;
	int bcast_twt_id = 0;
	const char *val;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	val = get_param(cmd, "BTWT_ID");
	if (val) {
		bcast_twt_id = atoi(val);
		bcast_twt = 1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_TERMINATE) ||
	    !(params = nla_nest_start(
		      msg,
		      QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_FLOW_TYPE, 0) ||
	    (bcast_twt &&
	     nla_put_flag(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST)) ||
	    (bcast_twt &&
	     nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_TWT_SETUP_BCAST_ID,
			bcast_twt_id))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, params);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	if (!dut->sta_async_twt_supp)
		return ret;

	return twt_async_event_wait(dut, QCA_WLAN_TWT_TERMINATE);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT teardown cannot be done without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int sta_transmit_omi(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
#ifdef NL80211_SUPPORT
	struct nlattr *params;
	struct nlattr *attr;
	struct nlattr *attr1;
	struct nl_msg *msg;
	int ifindex, ret;
	const char *val;
	const char *intf = get_param(cmd, "Interface");
	uint8_t rx_nss = 0xFF, ch_bw = 0xFF, tx_nsts = 0xFF, ulmu_dis = 0,
		ulmu_data_dis = 0, ch_bw_extn = 0, rx_nss_extn = 0,
		tx_nsts_extn = 0;
	bool eht_omi = false;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}
	val = get_param(cmd, "OMCtrl_RxNSS");
	if (val)
		rx_nss = atoi(val);

	val = get_param(cmd, "OMCtrl_ChnlWidth");
	if (val)
		ch_bw = atoi(val);

	val = get_param(cmd, "OMCtrl_ULMUDisable");
	if (val)
		ulmu_dis = atoi(val);

	val = get_param(cmd, "OMCtrl_TxNSTS");
	if (val)
		tx_nsts = atoi(val);

	val = get_param(cmd, "OMCtrl_ULMUDataDisable");
	if (val)
		ulmu_data_dis = atoi(val);

	val = get_param(cmd, "EHT_OMCtrl_RxNSS_Ext");
	if (val) {
		eht_omi = true;
		rx_nss_extn = atoi(val);
	}

	val = get_param(cmd, "EHT_OMCtrl_ChnlWidth_Ext");
	if (val) {
		eht_omi = true;
		ch_bw_extn = atoi(val);
	}

	val = get_param(cmd, "EHT_OMCtrl_TxNSTS_Ext");
	if (val) {
		eht_omi = true;
		tx_nsts_extn = atoi(val);
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    !(params = nla_nest_start(
		      msg, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_HE_OMI_TX)) ||
	    !(attr1 = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_HE_OMI_RX_NSS, rx_nss) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_HE_OMI_CH_BW, ch_bw) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_HE_OMI_TX_NSTS, tx_nsts) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_HE_OMI_ULMU_DATA_DISABLE,
		       ulmu_data_dis) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_HE_OMI_ULMU_DISABLE,
		       ulmu_dis) ||
	    (eht_omi &&
	     (nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_EHT_OMI_RX_NSS_EXTN,
			 rx_nss_extn) ||
	      nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_EHT_OMI_CH_BW_EXTN,
			 ch_bw_extn) ||
	      nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_EHT_OMI_TX_NSS_EXTN,
			 tx_nsts_extn)))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, attr1);
	nla_nest_end(msg, params);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}

	return ret;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"OMI TX cannot be processed without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static enum sigma_cmd_result
cmd_sta_set_wireless_vht(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	const char *program;
	int tkip = -1;
	int wep = -1;
	int iwpriv_status;

	program = get_param(cmd, "Program");
	val = get_param(cmd, "SGI80");
	if (val) {
		int sgi80;
		enum nl80211_txrate_gi gi_val;

		sgi80 = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;
		if (sgi80)
			gi_val = NL80211_TXRATE_FORCE_LGI;
		else
			gi_val = NL80211_TXRATE_FORCE_SGI;
		if (sta_set_vht_gi(dut, intf, (u8) gi_val)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"sta_set_vht_gi failed, using iwpriv");
			run_iwpriv(dut, intf, "shortgi %d", sgi80);
		}
	}

	val = get_param(cmd, "TxBF");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (sta_set_tx_beamformee(dut, intf, 1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set TX beamformee enable");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			if (run_iwpriv(dut, intf, "vhtsubfee 1") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Setting vhtsubfee failed");
				return STATUS_SENT_ERROR;
			}
			if (run_iwpriv(dut, intf, "vhtsubfer 1") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Setting vhtsubfer failed");
				return STATUS_SENT_ERROR;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported driver type");
			break;
		}
	}

	val = get_param(cmd, "MU_TxBF");
	if (val && (strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0)) {
		switch (get_driver_type(dut)) {
		case DRIVER_ATHEROS:
			ath_sta_set_txsp_stream(dut, intf, "1SS");
			ath_sta_set_rxsp_stream(dut, intf, "1SS");
			run_iwpriv(dut, intf, "vhtmubfee 1");
			run_iwpriv(dut, intf, "vhtmubfer 1");
			break;
		case DRIVER_WCN:
			if (wcn_sta_set_sp_stream(dut, intf, "1SS") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set RX/TXSP_STREAM");
				return STATUS_SENT_ERROR;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting SP_STREAM not supported");
			break;
		}
	}

	val = get_param(cmd, "LDPC");
	if (val) {
		int ldpc;

		ldpc = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;
		iwpriv_status = run_iwpriv(dut, intf, "ldpc %d", ldpc);
		if (iwpriv_status)
			sta_config_params(dut, intf, STA_SET_LDPC, ldpc);
	}

	val = get_param(cmd, "BCC");
	if (val) {
		int bcc;

		bcc = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;
		/* use LDPC iwpriv itself to set bcc coding, bcc coding
		 * is mutually exclusive to bcc */
		iwpriv_status = run_iwpriv(dut, intf, "ldpc %d", !bcc);
		if (iwpriv_status)
			sta_config_params(dut, intf, STA_SET_LDPC, !bcc);
	}

	val = get_param(cmd, "MaxHE-MCS_1SS_RxMapLTE80");
	if (val && dut->sta_nss == 1)
		cmd_set_max_he_mcs(dut, intf, atoi(val));

	val = get_param(cmd, "MaxHE-MCS_2SS_RxMapLTE80");
	if (val && dut->sta_nss == 2)
		cmd_set_max_he_mcs(dut, intf, atoi(val));

	val = get_param(cmd, "MCS_FixedRate");
	if (val) {
#ifdef NL80211_SUPPORT
		int mcs, ratecode = 0;
		enum he_mcs_config mcs_config;
		int ret;
		char buf[60];
		u16 tx_rate = 0;

		ratecode = (0x07 & dut->sta_nss) << 5;
		mcs = atoi(val);
		/* Add the MCS to the ratecode */
		if (mcs >= 0 && mcs <= 11) {
			ratecode += mcs;
			tx_rate = 1 << mcs;
			if (dut->device_type == STA_testbed &&
			    mcs > 7 && mcs <= 11) {
				if (mcs <= 9)
					mcs_config = HE_80_MCS0_9;
				else
					mcs_config = HE_80_MCS0_11;
				ret = sta_set_he_mcs(dut, intf, mcs_config);
				if (ret) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"MCS_FixedRate: mcs setting failed, mcs:%d, mcs_config %d, ret:%d",
							mcs, mcs_config, ret);
				}
			}

			if (wcn_set_he_tx_rate(dut, intf, tx_rate,
					       dut->sta_nss)) {
				sigma_dut_print(dut, DUT_MSG_INFO,
						"wcn_set_he_tx_rate failed, using iwpriv");
				snprintf(buf, sizeof(buf),
					 "iwpriv %s set_11ax_rate 0x%03x",
					 intf, ratecode);
				if (system(buf) != 0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"MCS_FixedRate: iwpriv setting of 11ax rates 0x%03x failed",
							ratecode);
				}
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MCS_FixedRate: HE MCS %d not supported",
					mcs);
		}
#else /* NL80211_SUPPORT */
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MCS_FixedRate cannot be changed without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */
	}

	val = get_param(cmd, "opt_md_notif_ie");
	if (val) {
		char *result = NULL;
		char delim[] = ";";
		char token[30];
		int value, config_val = 0;
		char *saveptr;

		strlcpy(token, val, sizeof(token));
		result = strtok_r(token, delim, &saveptr);

		/* Extract the NSS information */
		if (result) {
			value = atoi(result);
			switch (value) {
			case 1:
				config_val = 1;
				break;
			case 2:
				config_val = 3;
				break;
			case 3:
				config_val = 7;
				break;
			case 4:
				config_val = 15;
				break;
			default:
				config_val = 3;
				break;
			}

			run_iwpriv(dut, intf, "rxchainmask %d", config_val);
			run_iwpriv(dut, intf, "txchainmask %d", config_val);

		}

		/* Extract the channel width information */
		result = strtok_r(NULL, delim, &saveptr);
		if (result) {
			value = atoi(result);
			switch (value) {
			case 20:
				config_val = 0;
				break;
			case 40:
				config_val = 1;
				break;
			case 80:
				config_val = 2;
				break;
			case 160:
				config_val = 3;
				break;
			default:
				config_val = 2;
				break;
			}

			dut->chwidth = config_val;

			run_iwpriv(dut, intf, "chwidth %d", config_val);
		}

		run_iwpriv(dut, intf, "opmode_notify 1");
	}

	val = get_param(cmd, "nss_mcs_cap");
	if (val) {
		int nss, mcs;
		char token[20];
		char *result = NULL;
		unsigned int vht_mcsmap = 0;
		char *saveptr;

		strlcpy(token, val, sizeof(token));
		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"NSS not specified");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,NSS not specified");
			return STATUS_SENT_ERROR;
		}
		nss = atoi(result);

		if (wcn_sta_set_sp_stream(dut, intf, result) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"set nss to %s failed", result);
		}

		result = strtok_r(NULL, ";", &saveptr);
		if (result == NULL) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MCS not specified");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,MCS not specified");
			return STATUS_SENT_ERROR;
		}
		result = strtok_r(result, "-", &saveptr);
		result = strtok_r(NULL, "-", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"MCS not specified");
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,MCS not specified");
			return STATUS_SENT_ERROR;
		}
		mcs = atoi(result);

		if (program && strcasecmp(program, "EHT") == 0) {
#ifdef NL80211_SUPPORT
			uint8_t mcs_config;
			int ret;

			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"nss_mcs_cap: mcs: %d, nss %d",
					mcs, nss);

			if (mcs >= 0 && mcs <= 9) {
				mcs_config = EHT_MCS0_9;
			} else if (mcs > 9 && mcs <= 11) {
				mcs_config = EHT_MCS0_11;
			} else if (mcs > 11 && mcs <= 13) {
				mcs_config = EHT_MCS0_13;
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Invalid MCS");
				return STATUS_SENT_ERROR;
			}

			ret = sta_set_eht_mcs(dut, intf, mcs_config);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"nss_mcs_cap: EHT: Setting of MCS failed, mcs_config: %d, ret: %d",
						mcs_config, ret);
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set MCS");
				return STATUS_SENT_ERROR;
			}
#else /* NL80211_SUPPORT */
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nss_mcs_cap: EHT: MCS cannot be changed without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */
		}

		if (program && (strcasecmp(program, "HE") == 0 ||
				strcasecmp(program, "EHT")) == 0) {
#ifdef NL80211_SUPPORT
			enum he_mcs_config mcs_config;
			int ret;

			if (mcs >= 0 && mcs <= 7) {
				mcs_config = HE_80_MCS0_7;
			} else if (mcs > 7 && mcs <= 9) {
				mcs_config = HE_80_MCS0_9;
			} else if (mcs > 9 && mcs <= 11) {
				mcs_config = HE_80_MCS0_11;
			} else if (strcasecmp(program, "EHT") == 0 &&
				   mcs == 13) {
				mcs_config = HE_80_MCS0_11;
			} else {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"nss_mcs_cap: HE: Invalid mcs: %d",
						mcs);
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Invalid MCS");
				return STATUS_SENT_ERROR;
			}

			ret = sta_set_he_mcs(dut, intf, mcs_config);
			if (ret) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"nss_mcs_cap: HE: Setting of MCS failed, mcs_config: %d, ret: %d",
						mcs_config, ret);
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set MCS");
				return STATUS_SENT_ERROR;
			}
#else /* NL80211_SUPPORT */
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"nss_mcs_cap: HE: MCS cannot be changed without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */
		} else {
			run_iwpriv(dut, intf, "vhtmcs %d", mcs);

			switch (nss) {
			case 1:
				switch (mcs) {
				case 7:
					vht_mcsmap = 0xfffc;
					break;
				case 8:
					vht_mcsmap = 0xfffd;
					break;
				case 9:
					vht_mcsmap = 0xfffe;
					break;
				default:
					vht_mcsmap = 0xfffe;
					break;
				}
				break;
			case 2:
				switch (mcs) {
				case 7:
					vht_mcsmap = 0xfff0;
					break;
				case 8:
					vht_mcsmap = 0xfff5;
					break;
				case 9:
					vht_mcsmap = 0xfffa;
					break;
				default:
					vht_mcsmap = 0xfffa;
					break;
				}
				break;
			case 3:
				switch (mcs) {
				case 7:
					vht_mcsmap = 0xffc0;
					break;
				case 8:
					vht_mcsmap = 0xffd5;
					break;
				case 9:
					vht_mcsmap = 0xffea;
					break;
				default:
					vht_mcsmap = 0xffea;
					break;
				}
				break;
			default:
				vht_mcsmap = 0xffea;
				break;
			}
			run_iwpriv(dut, intf, "vht_mcsmap 0x%04x", vht_mcsmap);
		}
	}

	/* UNSUPPORTED: val = get_param(cmd, "Tx_lgi_rate"); */

	val = get_param(cmd, "Vht_tkip");
	if (val)
		tkip = strcmp(val, "1") == 0 ||	strcasecmp(val, "Enable") == 0;

	val = get_param(cmd, "Vht_wep");
	if (val)
		wep = strcmp(val, "1") == 0 || strcasecmp(val, "Enable") == 0;

	if (tkip != -1 || wep != -1) {
		if ((tkip == 1 && wep != 0) || (wep == 1 && tkip != 0)) {
			run_iwpriv(dut, intf, "htweptkip 1");
		} else if ((tkip == 0 && wep != 1) || (wep == 0 && tkip != 1)) {
			run_iwpriv(dut, intf, "htweptkip 0");
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,mixed mode of VHT TKIP/WEP not supported");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "TWTSchedSTASupport");
	if (!val)
		val = get_param(cmd, "BroadcastTWT");
	if (val) {
		int set_val;

		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (strcasecmp(val, "Enable") == 0) {
				set_val = 1;
			} else if (strcasecmp(val, "Disable") == 0) {
				set_val = 0;
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Invalid TWTSchedSTASupport");
				return STATUS_SENT_ERROR;
			}

			if (sta_set_bcast_twt_support(dut, intf, set_val)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set TWTSchedSTASupport");
				return STATUS_SENT_ERROR;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting TWTSchedSTASupport not supported");
			break;
		}
	}

	val = get_param(cmd, "MBSSID_RxCtrl");
	if (val) {
		int set_val;

		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (strcasecmp(val, "Enable") == 0) {
				set_val = 1;
			} else if (strcasecmp(val, "Disable") == 0) {
				set_val = 0;
			} else {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Invalid MBSSID_RxCtrl");
				return STATUS_SENT_ERROR;
			}

			if (sta_set_rx_ctrl_multi_bss(dut, intf, set_val)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set MBSSID_RxCtrl");
				return STATUS_SENT_ERROR;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting MBSSID_RxCtrl not supported");
			break;
		}
	}

	val = get_param(cmd, "txBandwidth");
	if (val) {
		switch (get_driver_type(dut)) {
		case DRIVER_WCN:
			if (wcn_sta_set_width(dut, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set txBandwidth");
				return STATUS_SENT_ERROR;
			}
			break;
		case DRIVER_ATHEROS:
			if (ath_set_width(dut, conn, intf, val) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set txBandwidth");
				return STATUS_SENT_ERROR;
			}
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Setting txBandwidth not supported");
			break;
		}
	}

	val = get_param(cmd, "BeamformeeSTS");
	if (val) {
		if (sta_set_tx_beamformee(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
					"ErrorCode,Failed to set TX beamformee enable");
			return STATUS_SENT_ERROR;
		}

		if (sta_set_beamformee_sts(dut, intf, atoi(val))) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set BeamformeeSTS");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "Trig_MAC_Padding_Dur");
	if (val) {
#ifdef NL80211_SUPPORT
		enum qca_wlan_he_mac_padding_dur set_val;

		switch (atoi(val)) {
		case 16:
			set_val = QCA_WLAN_HE_16US_OF_PROCESS_TIME;
			break;
		case 8:
			set_val = QCA_WLAN_HE_8US_OF_PROCESS_TIME;
			break;
		default:
			set_val = QCA_WLAN_HE_NO_ADDITIONAL_PROCESS_TIME;
			break;
		}
		if (sta_set_mac_padding_duration(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set MAC padding duration");
			return STATUS_SENT_ERROR;
		}
#else /* NL80211_SUPPORT */
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"MAC padding duration cannot be changed without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */
	}

	val = get_param(cmd, "TWT_ReqSupport");
	if (val) {
		int set_val;

		if (strcasecmp(val, "Enable") == 0) {
			set_val = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			set_val = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid TWT_ReqSupport");
			return STATUS_SENT_ERROR;
		}

		if (sta_set_twt_req_support(dut, intf, set_val)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set TWT req support %d",
					set_val);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set TWT_ReqSupport");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "PreamblePunctRx");
	if (val && get_driver_type(dut) == DRIVER_WCN) {
		int set_val;

		if (strcasecmp(val, "Enable") == 0) {
			set_val = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			set_val = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid PreamblePunctRx");
			return STATUS_SENT_ERROR;
		}

		if (sta_set_punctured_preamble_rx(dut, intf, set_val)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set PreamblePunctRx support %d",
					set_val);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set PreamblePunctRx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "FullBW_ULMUMIMO");
	if (val) {
		int set_val;

		if (strcasecmp(val, "Enable") == 0) {
			set_val = 1;
		} else if (strcasecmp(val, "Disable") == 0) {
			set_val = 0;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid FullBW_ULMUMIMO");
			return STATUS_SENT_ERROR;
		}

		if (sta_set_fullbw_ulmumimo(dut, intf, set_val)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to set FullBW_ULMUMIMO %d",
					set_val);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set FullBW_ULMUMIMO");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "TWTInfoFrameTx");
	if (val) {
		if (strcasecmp(val, "Enable") == 0) {
			/* No-op */
		} else if (strcasecmp(val, "Disable") == 0) {
			/* No-op */
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid TWTInfoFrameTx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "MU_EDCA");
	if (val && (strcasecmp(val, "Override") == 0)) {
		if (sta_set_mu_edca_override(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set MU EDCA override");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "PPDUTxType");
	if (val && strcasecmp(val, "ER-SU") == 0) {
		if (sta_set_er_su_ppdu_type_tx(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set ER-SU PPDU type Tx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "RUAllocTone");
	if (val && strcasecmp(val, "242") == 0) {
		if (sta_set_ru_242_tone_tx(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set RU 242 tone Tx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "OMControl");
	if (val) {
		int set_val = 1;

		if (strcasecmp(val, "Enable") == 0)
			set_val = 1;
		else if (strcasecmp(val, "Disable") == 0)
			set_val = 0;

		if (sta_set_om_ctrl_supp(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set OM ctrl supp");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "BSSMaxIdlePeriod");
	if (val && sta_set_bss_max_idle_period(dut, intf, atoi(val))) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set BSS max idle period");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "BSS_max_idle");
	if (val) {
		int set_val = 0;

		if (strcasecmp(val, "Enable") == 0)
			set_val = 1;
		else if (strcasecmp(val, "Disable") == 0)
			set_val = 0;
		if (sta_set_bss_max_idle_support(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set BSS max idle support");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "ADDBAResp_BufSize");
	if (val) {
		int buf_size;

		if (strcasecmp(val, "gt512") == 0)
			buf_size = 1024;
		else if (strcasecmp(val, "gt256") == 0 ||
			 strcasecmp(val, "eq512"))
			buf_size = 512;
		else if (strcasecmp(val, "gt64") == 0 ||
			 strcasecmp(val, "eq256"))
			buf_size = 256;
		else
			buf_size = 64;
		if (get_driver_type(dut) == DRIVER_WCN &&
		    sta_set_addba_buf_size(dut, intf, buf_size)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,set addbaresp_buff_size failed");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "ADDBAReq_BufSize");
	if (val) {
		int buf_size;

		if (strcasecmp(val, "gt512") == 0)
			buf_size = 1024;
		else if (strcasecmp(val, "gt256") == 0 ||
			 strcasecmp(val, "eq512"))
			buf_size = 512;
		else if (strcasecmp(val, "gt64") == 0 ||
			 strcasecmp(val, "eq256"))
			buf_size = 256;
		else
			buf_size = 64;
		if (get_driver_type(dut) == DRIVER_WCN &&
		    sta_set_addba_buf_size(dut, intf, buf_size)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,set addbareq_buff_size failed");
			return STATUS_SENT_ERROR;
		}
	}

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static enum sigma_cmd_result
cmd_sta_set_wireless_eht(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	val = get_param(cmd, "BeamformeeSS_80");
	if (val)
		sta_set_eht_beamformee_ss_80(dut, intf, (u8) atoi(val));

	val = get_param(cmd, "BeamformeeSS_160");
	if (val)
		sta_set_eht_beamformee_ss_160(dut, intf, (u8) atoi(val));

	val = get_param(cmd, "BeamformeeSS_320");
	if (val)
		sta_set_eht_beamformee_ss_320(dut, intf, (u8) atoi(val));

	val = get_param(cmd, "MLOmode");
	if (val) {
		if (strcasecmp(val, "MLSR") == 0) {
			sta_config_params(dut, intf, STA_SET_EHT_MLO_MODE,
					  QCA_WLAN_EHT_MLSR);
		} else if (strcasecmp(val, "STR") == 0) {
			sta_config_params(dut, intf, STA_SET_EHT_MLO_MODE,
					  QCA_WLAN_EHT_STR_MLMR);
			sta_config_params(
				dut, intf,
				STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS, 1);
			sta_config_params(dut, intf,
					  STA_SET_EHT_MLO_MAX_NUM_LINKS, 2);
			/* Configure STR Tx for testbed. The configuration
			 * gets reset with disconnection.
			 */
			if (dut->device_type == STA_testbed)
				sta_set_mlo_str_tx(dut, intf, 1);
		} else if (strcasecmp(val, "EMLSR") == 0) {
			sta_config_params(dut, intf, STA_SET_EHT_MLO_MODE,
					  QCA_WLAN_EHT_EMLSR);
			sta_config_params(
				dut, intf,
				STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS, 0);
			sta_config_params(dut, intf,
					  STA_SET_EHT_MLO_MAX_NUM_LINKS, 2);
		} else if (strcasecmp(val, "NSTR") == 0) {
			sta_config_params(dut, intf, STA_SET_EHT_MLO_MODE,
					  QCA_WLAN_EHT_NON_STR_MLMR);
			sta_config_params(
				dut, intf,
				STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS, 1);
			sta_config_params(dut, intf,
					  STA_SET_EHT_MLO_MAX_NUM_LINKS, 2);
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid MLO mode %s", val);
		}
	}

	val = get_param(cmd, "EMLSR_Padding_Delay");
	if (val) {
		u8 set_val = 0;
		int ival = atoi(val);

		if (ival == 32)
			set_val = 1;
		else if (ival == 64)
			set_val = 2;
		else if (ival == 128)
			set_val = 3;
		else if (ival == 256)
			set_val = 4;
		else
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Unsupported EMLSR_Padding_Delay value '%s'",
					val);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"EMLSR padding %s us, set val %d",
				val, set_val);
		sta_set_eht_emlsr_padding_delay(dut, intf, set_val);
	}

	val = get_param(cmd, "TBSoundingFBRateLimit");
	if (val)
		sta_set_eht_tb_sounding_fb_rl(dut, intf, (u8) atoi(val));

	val = get_param(cmd, "MaxNumofSimultaneousLinks");
	if (val)
		sta_config_params(dut, intf,
				  STA_SET_EHT_MLO_MAX_SIMULTANEOUS_LINKS,
				  (u8) atoi(val));

	val = get_param(cmd, "EMLCapaPresent");
	if (val)
		sta_config_params(dut, intf, STA_SET_EHT_EML_CAPABILITY,
				  (u8) atoi(val));

	val = get_param(cmd, "EHT_OMControl");
	if (val) {
		if (get_enable_disable(val))
			sta_set_eht_om_ctrl_supp(dut, intf, 1);
		else
			sta_set_eht_om_ctrl_supp(dut, intf, 0);
	}

	val = get_param(cmd, "EPCS");
	if (val)
		sta_config_params(dut, intf, STA_SET_EPCS_CAPABILITY,
				  get_enable_disable(val) ? 1 : 0);

	return cmd_sta_set_wireless_vht(dut, conn, cmd);
}


static int sta_set_wireless_60g(struct sigma_dut *dut,
				struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *dev_role = get_param(cmd, "DevRole");

	if (!dev_role) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,DevRole not specified");
		return 0;
	}

	if (strcasecmp(dev_role, "PCP") == 0)
		return sta_set_60g_pcp(dut, conn, cmd);
	if (strcasecmp(dev_role, "STA") == 0)
		return sta_set_60g_sta(dut, conn, cmd);
	send_resp(dut, conn, SIGMA_INVALID,
		  "ErrorCode,DevRole not supported");
	return 0;
}


static int sta_set_wireless_oce(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	int status;
	const char *intf = get_param(cmd, "Interface");
	const char *val = get_param(cmd, "DevRole");

	if (val && strcasecmp(val, "STA-CFON") == 0) {
		status = sta_cfon_set_wireless(dut, conn, cmd);
		if (status)
			return status;
	}
	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static enum sigma_cmd_result
sta_set_wireless_wpa3(struct sigma_dut *dut, struct sigma_conn *conn,
		      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	val = get_param(cmd, "ocvc");
	if (val)
		dut->ocvc = atoi(val);

	val = get_param(cmd, "ClientPrivacy");
	if (val && dut->client_privacy != atoi(val) &&
	    sta_set_client_privacy(dut, conn, intf, atoi(val))) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to configure random MAC address use");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "GKH_G2_Tx");
	if (val) {
		char buf[50];

		snprintf(buf, sizeof(buf), "SET disable_eapol_g2_tx %d",
			 strcasecmp(val, "disable") == 0);

		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to enable/disable G2 transmit");
			return STATUS_SENT_ERROR;
		}
	}

	if (dut->device_mode == MODE_11BE)
		return cmd_sta_set_wireless_eht(dut, conn, cmd);

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static enum sigma_cmd_result
sta_set_wireless_loc_r2(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	const char *i2rlmr_iftmr = get_param(cmd, "I2RLMRIFTMR");
	const char *session_terminate = get_param(cmd, "FTMSessionTerminate");

	if (i2rlmr_iftmr) {
		dut->i2rlmr_iftmr = atoi(i2rlmr_iftmr);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"i2rlmr_iftmr value is %d", dut->i2rlmr_iftmr);
	}

	if (session_terminate) {
		int val = atoi(session_terminate);

		sigma_dut_print(dut, DUT_MSG_INFO,
				"session_terminate value is %d", val);
		if (val)
			dut->i2rlmrpolicy =
				LOC_ABORT_ON_I2R_LMR_POLICY_MISMATCH;
	}

	return SUCCESS_SEND_STATUS;
}


static int sta_set_wireless_qm(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");

	if (dut->device_mode == MODE_11BE)
		return cmd_sta_set_wireless_eht(dut, conn, cmd);

	return cmd_sta_set_wireless_common(intf, dut, conn, cmd);
}


static enum sigma_cmd_result cmd_sta_set_wireless(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "Program");
	if (!val)
		val = get_param(cmd, "Prog");
	if (val) {
		if (strcasecmp(val, "11n") == 0)
			return cmd_sta_set_11n(dut, conn, cmd);
		if (strcasecmp(val, "VHT") == 0 || strcasecmp(val, "HE") == 0)
			return cmd_sta_set_wireless_vht(dut, conn, cmd);
		if (strcasecmp(val, "60ghz") == 0)
			return sta_set_wireless_60g(dut, conn, cmd);
		if (strcasecmp(val, "OCE") == 0)
			return sta_set_wireless_oce(dut, conn, cmd);
		/* sta_set_wireless in WPS program is only used for 60G */
		if (is_60g_sigma_dut(dut))
			return sta_set_wireless_60g(dut, conn, cmd);
		if (strcasecmp(val, "WPA3") == 0)
			return sta_set_wireless_wpa3(dut, conn, cmd);
		if (strcasecmp(val, "LOCR2") == 0)
			return sta_set_wireless_loc_r2(dut, conn, cmd);
		if (strcasecmp(val, "EHT") == 0)
			return cmd_sta_set_wireless_eht(dut, conn, cmd);
		if (strcasecmp(val, "QM") == 0)
			return sta_set_wireless_qm(dut, conn, cmd);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Program value not supported");
	} else {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Program argument not available");
	}

	return 0;
}


static void ath_sta_inject_frame(struct sigma_dut *dut, const char *intf,
				 int tid)
{
	char buf[100];
	int tid_to_dscp [] = { 0x00, 0x20, 0x40, 0x60, 0x80, 0xa0, 0xc0, 0xe0 };

	if (tid < 0 ||
	    tid >= (int) (sizeof(tid_to_dscp) / sizeof(tid_to_dscp[0]))) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Unsupported TID: %d", tid);
		return;
	}

	/*
	 * Two ways to ensure that addba request with a
	 * non zero TID could be sent out. EV 117296
	 */
	snprintf(buf, sizeof(buf),
		 "ping -c 8 -Q %d `arp -a | grep wlan0 | awk '{print $2}' | tr -d '()'`",
		 tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Ping did not send out");
	}

	snprintf(buf, sizeof(buf),
		 "iwconfig %s | grep Access | awk '{print $6}' > %s",
		 intf, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		return;

	snprintf(buf, sizeof(buf),
		 "ifconfig %s | grep HWaddr | cut -b 39-56 >> %s",
		 intf, VI_QOS_TMP_FILE);
	if (system(buf) != 0)
		sigma_dut_print(dut, DUT_MSG_ERROR, "HWaddr matching failed");

	snprintf(buf,sizeof(buf), "sed -n '3,$p' %s >> %s",
		 VI_QOS_REFFILE, VI_QOS_TMP_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_TEMP_FILE generation error failed");
	}
	snprintf(buf, sizeof(buf), "sed '5 c %x' %s > %s",
		 tid_to_dscp[tid], VI_QOS_TMP_FILE, VI_QOS_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_FILE generation failed");
	}

	snprintf(buf, sizeof(buf), "sed '5 c %x' %s > %s",
		 tid_to_dscp[tid], VI_QOS_TMP_FILE, VI_QOS_FILE);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"VI_QOS_FILE generation failed");
	}

	snprintf(buf, sizeof(buf), "ethinject %s %s", intf, VI_QOS_FILE);
	if (system(buf) != 0) {
	}
}


static int ath_sta_send_addba(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	int tid = 0;
	char buf[100];

	val = get_param(cmd, "TID");
	if (val) {
		tid = atoi(val);
		if (tid)
			ath_sta_inject_frame(dut, intf, tid);
	}

	/* Command sequence for ADDBA request on Peregrine based devices */
	run_iwpriv(dut, intf, "setaddbaoper 1");

	snprintf(buf, sizeof(buf), "wifitool %s senddelba 1 %d 1 4", intf, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool senddelba failed");
	}

	snprintf(buf, sizeof(buf), "wifitool %s sendaddba 1 %d 64", intf, tid);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"wifitool sendaddba failed");
	}

	/* UNSUPPORTED: val = get_param(cmd, "Dest_mac"); */

	return 1;
}


#ifdef __linux__

static int wil6210_send_addba(struct sigma_dut *dut, const char *dest_mac,
			      int agg_size)
{
	char dir[128], buf[128];
	FILE *f;
	regex_t re;
	regmatch_t m[2];
	int rc, ret = -1, vring_id, found, res;

	if (wil6210_get_debugfs_dir(dut, dir, sizeof(dir))) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to get wil6210 debugfs dir");
		return -1;
	}

	res = snprintf(buf, sizeof(buf), "%s/vrings", dir);
	if (res < 0 || res >= sizeof(buf))
		return -1;
	f = fopen(buf, "r");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "failed to open: %s", buf);
		/* newer wil6210 driver renamed file to "rings" */
		res = snprintf(buf, sizeof(buf), "%s/rings", dir);
		if (res < 0 || res >= sizeof(buf))
			return -1;
		f = fopen(buf, "r");
		if (!f) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"failed to open: %s", buf);
			return -1;
		}
	}

	/* can be either VRING tx... or RING... */
	if (regcomp(&re, "RING tx_[ \t]*([0-9]+)", REG_EXTENDED)) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "regcomp failed");
		goto out;
	}

	/* find TX VRING for the mac address */
	found = 0;
	while (fgets(buf, sizeof(buf), f)) {
		if (strcasestr(buf, dest_mac)) {
			found = 1;
			break;
		}
	}

	if (!found) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no TX VRING for %s", dest_mac);
		goto out;
	}

	/* extract VRING ID, "VRING tx_<id> = {" */
	if (!fgets(buf, sizeof(buf), f)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no VRING start line for %s", dest_mac);
		goto out;
	}

	rc = regexec(&re, buf, 2, m, 0);
	regfree(&re);
	if (rc || m[1].rm_so < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"no VRING TX ID for %s", dest_mac);
		goto out;
	}
	buf[m[1].rm_eo] = 0;
	vring_id = atoi(&buf[m[1].rm_so]);

	/* send the addba command */
	fclose(f);
	res = snprintf(buf, sizeof(buf), "%s/back", dir);
	if (res < 0 || res >= sizeof(buf))
		return -1;
	f = fopen(buf, "w");
	if (!f) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"failed to open: %s", buf);
		return -1;
	}

	fprintf(f, "add %d %d\n", vring_id, agg_size);

	ret = 0;

out:
	fclose(f);

	return ret;
}


int send_addba_60g(struct sigma_dut *dut, struct sigma_conn *conn,
		   struct sigma_cmd *cmd, const char *mac_param)
{
	const char *val;
	int tid = 0;

	val = get_param(cmd, "TID");
	if (val) {
		tid = atoi(val);
		if (tid != 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Ignore TID %d for send_addba use TID 0 for 60g since only 0 required on TX",
					tid);
		}
	}

	val = get_param(cmd, mac_param);
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Currently not supporting addba for 60G without Dest_mac");
		return ERROR_SEND_STATUS;
	}

	if (wil6210_send_addba(dut, val, dut->back_rcv_buf))
		return -1;

	return 1;
}

#endif /* __linux__ */


static int wcn_sta_send_addba(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
#ifdef NL80211_SUPPORT
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	int tid = -1;
	int bufsize = 64;
	struct nl_msg *msg;
	int ret = 0;
	struct nlattr *params;
	int ifindex;

	val = get_param(cmd, "TID");
	if (val)
		tid = atoi(val);

	if (tid == -1) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,sta_send_addba tid invalid");
		return 0;
	}

	/* UNSUPPORTED: val = get_param(cmd, "Dest_mac"); */

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,sta_send_addba interface invalid");
		return 0;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION) ||
	    !(params = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    nla_put_u8(msg,
		       QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ADD_DEL_BA_SESSION,
		       QCA_WLAN_ADD_BA) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_BA_TID,
		       tid) ||
	    nla_put_u16(msg,
			QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_ADDBA_BUFF_SIZE,
			bufsize)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,sta_send_addba err in adding vendor_cmd and vendor_data");
		return 0;
	}
	nla_nest_end(msg, params);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
		if (ret == -EOPNOTSUPP)
			return 1;
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,sta_send_addba err in send_and_recv_msgs");
		return 0;
	}
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"sta_send_addba not supported without NL80211_SUPPORT defined");
#endif /* NL80211_SUPPORT */

	return 1;
}


static enum sigma_cmd_result cmd_sta_send_addba(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
	case DRIVER_ATHEROS:
		return ath_sta_send_addba(dut, conn, cmd);
	case DRIVER_WCN:
		return wcn_sta_send_addba(dut, conn, cmd);
#ifdef __linux__
	case DRIVER_WIL6210:
		return send_addba_60g(dut, conn, cmd, "Dest_mac");
#endif /* __linux__ */
	default:
		/*
		 * There is no driver specific implementation for other drivers.
		 * Ignore the command and report COMPLETE since the following
		 * throughput test operation will end up sending ADDBA anyway.
		 */
		return 1;
	}
}


int inject_eth_frame(int s, const void *data, size_t len,
		     unsigned short ethtype, char *dst, char *src)
{
	struct iovec iov[4] = {
		{
			.iov_base = dst,
			.iov_len = ETH_ALEN,
		},
		{
			.iov_base = src,
			.iov_len = ETH_ALEN,
		},
		{
			.iov_base = &ethtype,
			.iov_len = sizeof(unsigned short),
		},
		{
			.iov_base = (void *) data,
			.iov_len = len,
		}
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = iov,
		.msg_iovlen = 4,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	return sendmsg(s, &msg, 0);
}

#if defined(__linux__) || defined(__QNXNTO__)

int inject_frame(int s, const void *data, size_t len, int encrypt)
{
#define	IEEE80211_RADIOTAP_F_WEP	0x04
#define	IEEE80211_RADIOTAP_F_FRAG	0x08
	unsigned char rtap_hdr[] = {
		0x00, 0x00, /* radiotap version */
		0x0e, 0x00, /* radiotap length */
		0x02, 0xc0, 0x00, 0x00, /* bmap: flags, tx and rx flags */
		IEEE80211_RADIOTAP_F_FRAG, /* F_FRAG (fragment if required) */
		0x00,       /* padding */
		0x00, 0x00, /* RX and TX flags to indicate that */
		0x00, 0x00, /* this is the injected frame directly */
	};
	struct iovec iov[2] = {
		{
			.iov_base = &rtap_hdr,
			.iov_len = sizeof(rtap_hdr),
		},
		{
			.iov_base = (void *) data,
			.iov_len = len,
		}
	};
	struct msghdr msg = {
		.msg_name = NULL,
		.msg_namelen = 0,
		.msg_iov = iov,
		.msg_iovlen = 2,
		.msg_control = NULL,
		.msg_controllen = 0,
		.msg_flags = 0,
	};

	if (encrypt)
		rtap_hdr[8] |= IEEE80211_RADIOTAP_F_WEP;

	return sendmsg(s, &msg, 0);
}


int open_monitor(const char *ifname)
{
#ifdef __QNXNTO__
	struct sockaddr_dl ll;
	int s;

	memset(&ll, 0, sizeof(ll));
	ll.sdl_family = AF_LINK;
	ll.sdl_index = if_nametoindex(ifname);
	if (ll.sdl_index == 0) {
		perror("if_nametoindex");
		return -1;
	}
	s = socket(PF_INET, SOCK_RAW, 0);
#else /* __QNXNTO__ */
	struct sockaddr_ll ll;
	int s;

	memset(&ll, 0, sizeof(ll));
	ll.sll_family = AF_PACKET;
	ll.sll_ifindex = if_nametoindex(ifname);
	if (ll.sll_ifindex == 0) {
		perror("if_nametoindex");
		return -1;
	}
	s = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
#endif /* __QNXNTO__ */
	if (s < 0) {
		perror("socket[PF_PACKET,SOCK_RAW]");
		return -1;
	}

	if (bind(s, (struct sockaddr *) &ll, sizeof(ll)) < 0) {
		perror("monitor socket bind");
		close(s);
		return -1;
	}

	return s;
}


static int hex2num(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}


int hwaddr_aton(const char *txt, unsigned char *addr)
{
	int i;

	for (i = 0; i < 6; i++) {
		int a, b;

		a = hex2num(*txt++);
		if (a < 0)
			return -1;
		b = hex2num(*txt++);
		if (b < 0)
			return -1;
		*addr++ = (a << 4) | b;
		if (i < 5 && *txt++ != ':')
			return -1;
	}

	return 0;
}

#endif /* defined(__linux__) || defined(__QNXNTO__) */


#ifdef NL80211_SUPPORT
static int nl80211_send_frame_cmd(struct sigma_dut *dut, const char *intf,
				  const u8 *data, size_t data_len, int freq)
{
	struct nl_msg *msg;
	int ret = 0;
	int ifindex;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_FRAME)) ||
	    (freq && nla_put_u32(msg, NL80211_ATTR_WIPHY_FREQ, freq)) ||
	    nla_put(msg, NL80211_ATTR_FRAME, data_len, data)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Error in adding NL80211_CMD_FRAME",
				__func__);
		nlmsg_free(msg);
		return -1;
	}

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"nl80211: Frame command failed: ret=%d (%s) req=%u",
				ret, strerror(-ret), freq);
		return -1;
	}

	return 0;
}
#endif /* NL80211_SUPPORT */


enum send_frame_type {
	DISASSOC, DEAUTH, SAQUERY, AUTH, ASSOCREQ, REASSOCREQ, DLS_REQ
};
enum send_frame_protection {
	CORRECT_KEY, INCORRECT_KEY, UNPROTECTED
};


static int sta_inject_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			    const char *intf, enum send_frame_type frame,
			    enum send_frame_protection protected,
			    const char *dest, int use_monitor)
{
#ifdef __linux__
	unsigned char buf[1000], *pos;
	int s, res;
	char bssid[20], addr[20];
	char result[32], ssid[100];
	size_t ssid_len;

	if (get_wpa_status(get_station_ifname(dut), "wpa_state", result,
			   sizeof(result)) < 0 ||
	    strncmp(result, "COMPLETED", 9) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Not connected");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(dut), "bssid",
			   bssid, sizeof(bssid)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current BSSID");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(dut), "address",
			   addr, sizeof(addr)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "own MAC address");
		return 0;
	}

	if (get_wpa_status(get_station_ifname(dut), "ssid", ssid, sizeof(ssid))
	    < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not get "
			  "current SSID");
		return 0;
	}
	ssid_len = strlen(ssid);

	pos = buf;

	/* Frame Control */
	switch (frame) {
	case DISASSOC:
		*pos++ = 0xa0;
		break;
	case DEAUTH:
		*pos++ = 0xc0;
		break;
	case SAQUERY:
		*pos++ = 0xd0;
		break;
	case AUTH:
		*pos++ = 0xb0;
		break;
	case ASSOCREQ:
		*pos++ = 0x00;
		break;
	case REASSOCREQ:
		*pos++ = 0x20;
		break;
	case DLS_REQ:
		*pos++ = 0xd0;
		break;
	}

	if (protected == INCORRECT_KEY)
		*pos++ = 0x40; /* Set Protected field to 1 */
	else
		*pos++ = 0x00;

	/* Duration */
	*pos++ = 0x00;
	*pos++ = 0x00;

	/* addr1 = DA (current AP) */
	hwaddr_aton(bssid, pos);
	pos += 6;
	/* addr2 = SA (own address) */
	hwaddr_aton(addr, pos);
	pos += 6;
	/* addr3 = BSSID (current AP) */
	hwaddr_aton(bssid, pos);
	pos += 6;

	/* Seq# (to be filled by driver/mac80211) */
	*pos++ = 0x00;
	*pos++ = 0x00;

	if (protected == INCORRECT_KEY) {
		/* CCMP parameters */
		memcpy(pos, "\x61\x01\x00\x20\x00\x10\x00\x00", 8);
		pos += 8;
	}

	if (protected == INCORRECT_KEY) {
		switch (frame) {
		case DEAUTH:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case DISASSOC:
			/* Reason code (encrypted) */
			memcpy(pos, "\xa7\x39", 2);
			pos += 2;
			break;
		case SAQUERY:
			/* Category|Action|TransID (encrypted) */
			memcpy(pos, "\x6f\xbd\xe9\x4d", 4);
			pos += 4;
			break;
		default:
			return -1;
		}

		/* CCMP MIC */
		memcpy(pos, "\xc8\xd8\x3b\x06\x5d\xb7\x25\x68", 8);
		pos += 8;
	} else {
		switch (frame) {
		case DEAUTH:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case DISASSOC:
			/* reason code = 8 */
			*pos++ = 0x08;
			*pos++ = 0x00;
			break;
		case SAQUERY:
			/* Category - SA Query */
			*pos++ = 0x08;
			/* SA query Action - Request */
			*pos++ = 0x00;
			/* Transaction ID */
			*pos++ = 0x12;
			*pos++ = 0x34;
			if (dut->saquery_oci_freq) {
				/* OCI IE - Extended ID */
				*pos++ = 0xFF;
				*pos++ = 0x04;
				*pos++ = 0x36;
				/* Operating Class */
				*pos++ = 0x74;
				/* Primary Channel */
				*pos++ = freq_to_channel(dut->saquery_oci_freq);
				/* Frequency Segment 1 Channel Number */
				*pos++ = 0x00;
			}
			break;
		case AUTH:
			/* Auth Alg (Open) */
			*pos++ = 0x00;
			*pos++ = 0x00;
			/* Seq# */
			*pos++ = 0x01;
			*pos++ = 0x00;
			/* Status code */
			*pos++ = 0x00;
			*pos++ = 0x00;
			break;
		case ASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSN */
			memcpy(pos, "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00"
			       "\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x02\xc0"
			       "\x00\x00\x00\x00\x0f\xac\x06", 28);
			pos += 28;
			break;
		case REASSOCREQ:
			/* Capability Information */
			*pos++ = 0x31;
			*pos++ = 0x04;
			/* Listen Interval */
			*pos++ = 0x0a;
			*pos++ = 0x00;
			/* Current AP */
			hwaddr_aton(bssid, pos);
			pos += 6;
			/* SSID */
			*pos++ = 0x00;
			*pos++ = ssid_len;
			memcpy(pos, ssid, ssid_len);
			pos += ssid_len;
			/* Supported Rates */
			memcpy(pos, "\x01\x08\x02\x04\x0b\x16\x0c\x12\x18\x24",
			       10);
			pos += 10;
			/* Extended Supported Rates */
			memcpy(pos, "\x32\x04\x30\x48\x60\x6c", 6);
			pos += 6;
			/* RSNE - Group and Pairwise ciphers */
			memcpy(pos,
			       "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04",
			       14);
			pos += 14;
			/* RSNE - AKM Suite count */
			*pos++ = 0x01;
			*pos++ = 0x00;
			/* RSNE - AKM Suites */
			if (dut->program == PROGRAM_WPA3)
				memcpy(pos, "\x00\x0f\xac\x08", 4);
			else
				memcpy(pos, "\x00\x0f\xac\x02", 4);
			pos += 4;
			/* RSNE - Capabilities */
			*pos++ = 0xc0;
			if (dut->ocvc)
				*pos++ = 0x40;
			else
				*pos++ = 0x00;
			/* RSNE - PMKID list and Group Management Ciphers */
			memcpy(pos, "\x00\x00\x00\x0f\xac\x06", 6);
			pos += 6;
			break;
		case DLS_REQ:
			/* Category - DLS */
			*pos++ = 0x02;
			/* DLS Action - Request */
			*pos++ = 0x00;
			/* Destination MACAddress */
			if (dest)
				hwaddr_aton(dest, pos);
			else
				memset(pos, 0, 6);
			pos += 6;
			/* Source MACAddress */
			hwaddr_aton(addr, pos);
			pos += 6;
			/* Capability Information */
			*pos++ = 0x10; /* Privacy */
			*pos++ = 0x06; /* QoS */
			/* DLS Timeout Value */
			*pos++ = 0x00;
			*pos++ = 0x01;
			/* Supported rates */
			*pos++ = 0x01;
			*pos++ = 0x08;
			*pos++ = 0x0c; /* 6 Mbps */
			*pos++ = 0x12; /* 9 Mbps */
			*pos++ = 0x18; /* 12 Mbps */
			*pos++ = 0x24; /* 18 Mbps */
			*pos++ = 0x30; /* 24 Mbps */
			*pos++ = 0x48; /* 36 Mbps */
			*pos++ = 0x60; /* 48 Mbps */
			*pos++ = 0x6c; /* 54 Mbps */
			/* TODO: Extended Supported Rates */
			/* TODO: HT Capabilities */
			break;
		}
	}

	if (use_monitor) {
		s = open_monitor("sigmadut");
		if (s < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to open monitor socket");
			return 0;
		}

		res = inject_frame(s, buf, pos - buf, protected == CORRECT_KEY);
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to inject frame");
			close(s);
			return 0;
		}
		if (res < pos - buf) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Only partial frame sent");
			close(s);
			return 0;
		}

		close(s);
	} else {
#ifdef NL80211_SUPPORT
		int freq;
		char freq_str[10];

		if (get_wpa_status(get_station_ifname(dut), "freq",
				   freq_str, sizeof(freq_str)) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Could not get current operating frequency");
			return 0;
		}
		freq = atoi(freq_str);

		if (nl80211_send_frame_cmd(dut, intf, buf, pos - buf, freq)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to inject frame");
			return 0;
		}
#else /* NL80211_SUPPORT */
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to inject frame (no NL80211_SUPPORT)");
		return 0;
#endif /* NL80211_SUPPORT */
	}

	return 1;
#else /* __linux__ */
	send_resp(dut, conn, SIGMA_ERROR, "errorCode,sta_send_frame not "
		  "yet supported");
	return 0;
#endif /* __linux__ */
}


static int cmd_sta_send_frame_tdls(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *sta, *val;
	unsigned char addr[ETH_ALEN];
	char buf[100];

	if (!intf)
		return -1;

	sta = get_param(cmd, "peer");
	if (sta == NULL)
		sta = get_param(cmd, "station");
	if (sta == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing peer address");
		return 0;
	}
	if (hwaddr_aton(sta, addr) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Invalid peer address");
		return 0;
	}

	val = get_param(cmd, "type");
	if (val == NULL)
		return -1;

	if (strcasecmp(val, "DISCOVERY") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "SETUP") == 0) {
		int status = 0, timeout = 0;

		val = get_param(cmd, "Status");
		if (val)
			status = atoi(val);

		val = get_param(cmd, "Timeout");
		if (val)
			timeout = atoi(val);

		if (status != 0 && status != 37) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported status value");
			return 0;
		}

		if (timeout != 0 && timeout != 301) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported timeout value");
			return 0;
		}

		if (status && timeout) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported timeout+status "
				  "combination");
			return 0;
		}

		if (status == 37 &&
		    wpa_command(intf, "SET tdls_testing 0x200")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable "
				  "decline setup response test mode");
			return 0;
		}

		if (timeout == 301) {
			int res;
			if (dut->no_tpk_expiration)
				res = wpa_command(intf,
						  "SET tdls_testing 0x108");
			else
				res = wpa_command(intf,
						  "SET tdls_testing 0x8");
			if (res) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set short TPK "
					  "lifetime");
				return 0;
			}
		}

		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS setup");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(val, "TEARDOWN") == 0) {
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", sta);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS teardown");
			return 0;
		}
		return 1;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "ErrorCode,Unsupported TDLS frame");
	return 0;
}


static int sta_ap_known(const char *ifname, const char *bssid)
{
	char buf[4096];

	snprintf(buf, sizeof(buf), "BSS MASK=1 %s", bssid);
	if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0)
		return 0;
	if (strncmp(buf, "id=", 3) != 0)
		return 0;
	return 1;
}


static int sta_scan_ap(struct sigma_dut *dut, const char *ifname,
		       const char *bssid)
{
	int res;
	struct wpa_ctrl *ctrl;
	char buf[256];

	if (sta_ap_known(ifname, bssid))
		return 0;
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"AP not in BSS table - start scan");

	ctrl = open_wpa_mon(ifname);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -1;
	}

	if (wpa_command(ifname, "SCAN") < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to start scan");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Scan did not complete");
		return -1;
	}

	if (sta_ap_known(ifname, bssid))
		return 0;
	sigma_dut_print(dut, DUT_MSG_INFO, "AP not in BSS table");
	return -1;
}


static int cmd_sta_send_frame_hs2_neighadv(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *intf)
{
	char buf[200];

	snprintf(buf, sizeof(buf), "ndsend 2001:DB8::1 %s", intf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to run "
			  "ndsend");
		return 0;
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_neighsolreq(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd,
					      const char *intf)
{
	char buf[200];
	const char *ip = get_param(cmd, "SenderIP");

	if (!ip)
		return 0;

	snprintf(buf, sizeof(buf), "ndisc6 -nm %s %s -r 4", ip, intf);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Neighbor Solicitation got a response "
				"for %s@%s", ip, intf);
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpprobe(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *ifname)
{
	char buf[200];
	const char *ip = get_param(cmd, "SenderIP");

	if (ip == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing SenderIP parameter");
		return 0;
	}
	snprintf(buf, sizeof(buf), "arping -I %s -D %s -c 4", ifname, ip);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "arping DAD got a response "
				"for %s@%s", ip, ifname);
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpannounce(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd,
					      const char *ifname)
{
	char buf[200];
	char ip[16];
	int s;
	struct ifreq ifr;
	struct sockaddr_in saddr;

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0) {
		perror("socket");
		return -1;
	}

	memset(&ifr, 0, sizeof(ifr));
	strlcpy(ifr.ifr_name, ifname, sizeof(ifr.ifr_name));
	if (ioctl(s, SIOCGIFADDR, &ifr) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to get %s IP address: %s",
				ifname, strerror(errno));
		close(s);
		return -1;
	}
	close(s);

	memcpy(&saddr, &ifr.ifr_addr, sizeof(struct sockaddr_in));
	strlcpy(ip, inet_ntoa(saddr.sin_addr), sizeof(ip));

	snprintf(buf, sizeof(buf), "arping -I %s -s %s %s -c 4", ifname, ip,
		 ip);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
	}

	return 1;
}


static int cmd_sta_send_frame_hs2_arpreply(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd,
					   const char *ifname)
{
	char buf[200], addr[20];
	char dst[ETH_ALEN], src[ETH_ALEN];
	short ethtype = htons(ETH_P_ARP);
	char *pos;
	int s, res;
	const char *val;
	struct sockaddr_in taddr;

	val = get_param(cmd, "dest");
	if (val)
		hwaddr_aton(val, (unsigned char *) dst);

	val = get_param(cmd, "DestIP");
	if (val)
		inet_aton(val, &taddr.sin_addr);
	else
		return -2;

	if (get_wpa_status(get_station_ifname(dut), "address", addr,
			   sizeof(addr)) < 0)
		return -2;
	hwaddr_aton(addr, (unsigned char *) src);

	pos = buf;
	*pos++ = 0x00;
	*pos++ = 0x01;
	*pos++ = 0x08;
	*pos++ = 0x00;
	*pos++ = 0x06;
	*pos++ = 0x04;
	*pos++ = 0x00;
	*pos++ = 0x02;
	memcpy(pos, src, ETH_ALEN);
	pos += ETH_ALEN;
	memcpy(pos, &taddr.sin_addr, 4);
	pos += 4;
	memcpy(pos, dst, ETH_ALEN);
	pos += ETH_ALEN;
	memcpy(pos, &taddr.sin_addr, 4);
	pos += 4;

	s = open_monitor(get_station_ifname(dut));
	if (s < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to open "
			  "monitor socket");
		return 0;
	}

	res = inject_eth_frame(s, buf, pos - buf, ethtype, dst, src);
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to "
			  "inject frame");
		close(s);
		return 0;
	}

	close(s);

	return 1;
}


static int cmd_sta_send_frame_hs2_dls_req(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd,
					  const char *intf, const char *dest)
{
	char buf[100];

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(buf, sizeof(buf),
			 "iw dev %s interface add sigmadut type monitor",
			 get_station_ifname(dut));
		if (system(buf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", buf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	return sta_inject_frame(dut, conn, intf, DLS_REQ, UNPROTECTED, dest, 1);
}


static int cmd_sta_send_frame_hs2(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *dest = get_param(cmd, "Dest");
	const char *type = get_param(cmd, "FrameName");
	const char *val;
	char buf[200], *pos, *end;
	int count, count2;

	if (type == NULL)
		type = get_param(cmd, "Type");

	if (intf == NULL || dest == NULL || type == NULL)
		return -1;

	if (strcasecmp(type, "NeighAdv") == 0)
		return cmd_sta_send_frame_hs2_neighadv(dut, conn, cmd, intf);

	if (strcasecmp(type, "NeighSolicitReq") == 0)
		return cmd_sta_send_frame_hs2_neighsolreq(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPProbe") == 0)
		return cmd_sta_send_frame_hs2_arpprobe(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPAnnounce") == 0)
		return cmd_sta_send_frame_hs2_arpannounce(dut, conn, cmd, intf);

	if (strcasecmp(type, "ARPReply") == 0)
		return cmd_sta_send_frame_hs2_arpreply(dut, conn, cmd, intf);

	if (strcasecmp(type, "DLS-request") == 0 ||
	    strcasecmp(type, "DLSrequest") == 0)
		return cmd_sta_send_frame_hs2_dls_req(dut, conn, cmd, intf,
						      dest);

	if (strcasecmp(type, "ANQPQuery") != 0 &&
	    strcasecmp(type, "Query") != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported HS 2.0 send frame type");
		return 0;
	}

	if (sta_scan_ap(dut, intf, dest) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not find "
			  "the requested AP");
		return 0;
	}

	pos = buf;
	end = buf + sizeof(buf);
	count = 0;
	pos += snprintf(pos, end - pos, "ANQP_GET %s ", dest);

	val = get_param(cmd, "ANQP_CAP_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s257", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "VENUE_NAME");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s258", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "NETWORK_AUTH_TYPE");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s260", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "ROAMING_CONS");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s261", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "IP_ADDR_TYPE_AVAILABILITY");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s262", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "NAI_REALM_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s263", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "3GPP_INFO");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s264", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "DOMAIN_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s268", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "Venue_URL");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s277", count > 0 ? "," : "");
		count++;
	}

	val = get_param(cmd, "Advice_Of_Charge");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s278", count > 0 ? "," : "");
		count++;
	}

	if (count && wpa_command(intf, buf)) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,ANQP_GET failed");
		return 0;
	}

	pos = buf;
	end = buf + sizeof(buf);
	count2 = 0;
	pos += snprintf(pos, end - pos, "HS20_ANQP_GET %s ", dest);

	val = get_param(cmd, "HS_CAP_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s2", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OPER_NAME");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s3", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "WAN_METRICS");
	if (!val)
		val = get_param(cmd, "WAN_MAT");
	if (!val)
		val = get_param(cmd, "WAN_MET");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s4", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "CONNECTION_CAPABILITY");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s5", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OP_CLASS");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s7", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OSU_PROVIDER_LIST");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s8", count2 > 0 ? "," : "");
		count2++;
	}

	val = get_param(cmd, "OPER_ICON_METADATA");
	if (!val)
		val = get_param(cmd, "OPERATOR_ICON_METADATA");
	if (val && atoi(val)) {
		pos += snprintf(pos, end - pos, "%s12", count2 > 0 ? "," : "");
		count2++;
	}

	if (count && count2) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before sending out "
				"second query");
		sleep(1);
	}

	if (count2 && wpa_command(intf, buf)) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,HS20_ANQP_GET "
			  "failed");
		return 0;
	}

	val = get_param(cmd, "NAI_HOME_REALM_LIST");
	if (val) {
		if (count || count2) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before "
					"sending out second query");
			sleep(1);
		}

		if (strcmp(val, "1") == 0)
			val = "mail.example.com";
		snprintf(buf, end - pos,
			 "HS20_GET_NAI_HOME_REALM_LIST %s realm=%s",
			 dest, val);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,HS20_GET_NAI_HOME_REALM_LIST "
				  "failed");
			return 0;
		}
	}

	val = get_param(cmd, "ICON_REQUEST");
	if (val) {
		if (count || count2) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Wait before "
					"sending out second query");
			sleep(1);
		}

		snprintf(buf, end - pos,
			 "HS20_ICON_REQUEST %s %s", dest, val);
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,HS20_ICON_REQUEST failed");
			return 0;
		}
	}

	return 1;
}


static int ath_sta_send_frame_vht(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *val;
	const char *ifname;
	int chwidth, nss;

	val = get_param(cmd, "framename");
	if (!val)
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "framename is %s", val);

	/* Command sequence to generate Op mode notification */
	if (val && strcasecmp(val, "Op_md_notif_frm") == 0) {
		ifname = get_station_ifname(dut);

		/* Disable STBC */
		run_iwpriv(dut, ifname, "tx_stbc 0");

		/* Extract Channel width */
		val = get_param(cmd, "Channel_width");
		if (val) {
			switch (atoi(val)) {
			case 20:
				chwidth = 0;
				break;
			case 40:
				chwidth = 1;
				break;
			case 80:
				chwidth = 2;
				break;
			case 160:
				chwidth = 3;
				break;
			default:
				chwidth = 2;
				break;
			}

			run_iwpriv(dut, ifname, "chwidth %d", chwidth);
		}

		/* Extract NSS */
		val = get_param(cmd, "NSS");
		if (val) {
			switch (atoi(val)) {
			case 1:
				nss = 1;
				break;
			case 2:
				nss = 3;
				break;
			case 3:
				nss = 7;
				break;
			default:
				/* We do not support NSS > 3 */
				nss = 3;
				break;
			}
			run_iwpriv(dut, ifname, "rxchainmask %d", nss);
		}

		/* Opmode notify */
		run_iwpriv(dut, ifname, "opmode_notify 1");
	}

	return 1;
}


static int wcn_sta_set_pmf_config(struct sigma_dut *dut, const char *intf,
				  enum send_frame_protection protected)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_PMF_PROTECTION,
		protected);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"PMF config cannot be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int cmd_sta_send_frame_vht(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
	case DRIVER_ATHEROS:
		return ath_sta_send_frame_vht(dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_frame(VHT) with the current driver");
		return 0;
	}
}


static int wcn_sta_send_disassoc(struct sigma_dut *dut, const char *intf)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_flag(
		dut, intf, QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_DISASSOC_TX);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Disassoc Tx cannot be done without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_sta_send_frame_he(struct sigma_dut *dut, struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	const char *val;
	const char *intf = get_param(cmd, "Interface");
	enum send_frame_protection protected;
	const char *pmf;

	if (!intf)
		return -1;

	val = get_param(cmd, "framename");
	if (!val)
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "framename is %s", val);

	pmf = get_param(cmd, "PMFProtected");
	if (!pmf)
		pmf = get_param(cmd, "Protected");
	if (pmf) {
		if (strcasecmp(pmf, "Correct-key") == 0 ||
		    strcasecmp(pmf, "CorrectKey") == 0) {
			protected = CORRECT_KEY;
		} else if (strcasecmp(pmf, "IncorrectKey") == 0) {
			protected = INCORRECT_KEY;
		} else if (strcasecmp(pmf, "Unprotected") == 0) {
			protected = UNPROTECTED;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported PMFProtected");
			return STATUS_SENT_ERROR;
		}
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Config PMF protection %d",
				protected);
		wcn_sta_set_pmf_config(dut, intf, protected);
	}

	/* Command sequence to generate Op mode notification */
	if (val && strcasecmp(val, "action") == 0) {
		val = get_param(cmd, "PPDUTxType");
		if (val && strcasecmp(val, "TB") == 0) {
			if (sta_set_action_tx_in_he_tb_ppdu(dut, intf, 1)) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"failed to send TB PPDU Tx cfg");
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,set TB PPDU Tx cfg failed");
				return 0;
			}
			return 1;
		}

		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Action Tx type is not defined");

		return SUCCESS_SEND_STATUS;
	}

	if (strcasecmp(val, "disassoc") == 0)
		wcn_sta_send_disassoc(dut, intf);

	return 1;
}


static int cmd_sta_send_frame_he(struct sigma_dut *dut,
				 struct sigma_conn *conn,
				 struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
	case DRIVER_WCN:
		return wcn_sta_send_frame_he(dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_frame(HE) with the current driver");
		return 0;
	}
}


#ifdef __linux__

static int
wil6210_send_p2p_frame_60g(struct sigma_dut *dut, struct sigma_cmd *cmd,
			   const char *frame_name, const char *dest_mac)
{
	int isprobereq = strcasecmp(frame_name, "probereq") == 0;
	const char *ssid = get_param(cmd, "ssid");
	const char *countstr = get_param(cmd, "count");
	const char *channelstr = get_param(cmd, "channel");
	const char *group_id = get_param(cmd, "groupid");
	const char *client_id = get_param(cmd, "clientmac");
	int count, channel, freq, i;
	const char *fname;
	char frame[1024], src_mac[20], group_id_attr[25],
		device_macstr[3 * ETH_ALEN], client_mac[ETH_ALEN];
	const char *group_ssid;
	const int group_ssid_prefix_len = 9;
	struct ieee80211_hdr_3addr *hdr = (struct ieee80211_hdr_3addr *) frame;
	size_t framelen = sizeof(frame);
	struct template_frame_tag tags[2];
	size_t tags_total = ARRAY_SIZE(tags);
	int tag_index, len, dst_len;

	if (!countstr || !channelstr) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Missing argument: count, channel");
		return -1;
	}
	if (isprobereq && !ssid) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Missing argument: ssid");
		return -1;
	}
	if (!isprobereq && (!group_id || !client_id)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Missing argument: group_id, client_id");
		return -1;
	}

	count = atoi(countstr);
	channel = atoi(channelstr);
	freq = channel_to_freq(dut, channel);

	if (!freq) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"invalid channel: %s", channelstr);
		return -1;
	}

	if (isprobereq) {
		if (strcasecmp(ssid, "wildcard") == 0) {
			fname = "probe_req_wildcard.txt";
		} else if (strcasecmp(ssid, "P2P_Wildcard") == 0) {
			fname = "probe_req_P2P_Wildcard.txt";
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"invalid probe request type");
			return -1;
		}
	} else {
		fname = "P2P_device_discovery_req.txt";
	}

	if (parse_template_frame_file(dut, fname, frame, &framelen,
				      tags, &tags_total)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"invalid frame template: %s", fname);
		return -1;
	}

	if (get_wpa_status(get_station_ifname(dut), "address",
			   src_mac, sizeof(src_mac)) < 0 ||
	    parse_mac_address(dut, src_mac, &hdr->addr2[0]) ||
	    parse_mac_address(dut, dest_mac, &hdr->addr1[0]))
		return -1;
	/* Use wildcard BSSID, since we are in PBSS */
	memset(&hdr->addr3, 0xFF, ETH_ALEN);

	if (!isprobereq) {
		tag_index = find_template_frame_tag(tags, tags_total, 1);
		if (tag_index < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"can't find device id attribute");
			return -1;
		}
		if (parse_mac_address(dut, client_id,
				      (unsigned char *) client_mac)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"invalid client_id: %s", client_id);
			return -1;
		}
		if (replace_p2p_attribute(dut, &frame[tags[tag_index].offset],
					  framelen - tags[tag_index].offset,
					  IEEE80211_P2P_ATTR_DEVICE_ID,
					  client_mac, ETH_ALEN)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"fail to replace device id attribute");
			return -1;
		}

		/*
		 * group_id arg contains device MAC address followed by
		 * space and SSID (DIRECT-somessid).
		 * group id attribute contains device address (6 bytes)
		 * followed by SSID prefix DIRECT-XX (9 bytes)
		 */
		if (strlen(group_id) < sizeof(device_macstr)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"group_id arg too short");
			return -1;
		}
		memcpy(device_macstr, group_id, sizeof(device_macstr));
		device_macstr[sizeof(device_macstr) - 1] = '\0';
		if (parse_mac_address(dut, device_macstr,
				      (unsigned char *) group_id_attr)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"fail to parse device address from group_id");
			return -1;
		}
		group_ssid = strchr(group_id, ' ');
		if (!group_ssid) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"invalid group_id arg, no ssid");
			return -1;
		}
		group_ssid++;
		len = strlen(group_ssid);
		if (len < group_ssid_prefix_len) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"group_id SSID too short");
			return -1;
		}
		dst_len = sizeof(group_id_attr) - ETH_ALEN;
		if (len > dst_len) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"group_id SSID (%s) too long",
					group_ssid);
			return -1;
		}

		memcpy(group_id_attr + ETH_ALEN, group_ssid, len);
		tag_index = find_template_frame_tag(tags, tags_total, 2);
		if (tag_index < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"can't find group id attribute");
			return -1;
		}
		if (replace_p2p_attribute(dut, &frame[tags[tag_index].offset],
					  framelen - tags[tag_index].offset,
					  IEEE80211_P2P_ATTR_GROUP_ID,
					  group_id_attr,
					  sizeof(group_id_attr))) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"fail to replace group id attribute");
			return -1;
		}
	}

	for (i = 0; i < count; i++) {
		if (wil6210_transmit_frame(dut, freq,
					   WIL_TRANSMIT_FRAME_DEFAULT_ROC,
					   frame, framelen)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"fail to transmit probe request frame");
			return -1;
		}
	}

	return 0;
}


int wil6210_send_frame_60g(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd)
{
	const char *frame_name = get_param(cmd, "framename");
	const char *mac = get_param(cmd, "dest_mac");

	if (!frame_name || !mac) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"framename and dest_mac must be provided");
		return -1;
	}

	if (strcasecmp(frame_name, "brp") == 0) {
		const char *l_rx = get_param(cmd, "L-RX");
		int l_rx_i;

		if (!l_rx) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"L-RX must be provided");
			return -1;
		}
		l_rx_i = atoi(l_rx);

		sigma_dut_print(dut, DUT_MSG_INFO,
				"dev_send_frame: BRP-RX, dest_mac %s, L-RX %s",
				mac, l_rx);
		if (l_rx_i != 16) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"unsupported L-RX: %s", l_rx);
			return -1;
		}

		if (wil6210_send_brp_rx(dut, mac, l_rx_i))
			return -1;
	} else if (strcasecmp(frame_name, "ssw") == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"dev_send_frame: SLS, dest_mac %s", mac);
		if (wil6210_send_sls(dut, mac))
			return -1;
	} else if ((strcasecmp(frame_name, "probereq") == 0) ||
		   (strcasecmp(frame_name, "devdiscreq") == 0)) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"dev_send_frame: %s, dest_mac %s", frame_name,
				mac);
		if (wil6210_send_p2p_frame_60g(dut, cmd, frame_name, mac))
			return -1;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"unsupported frame type: %s", frame_name);
		return -1;
	}

	return 1;
}

#endif /* __linux__ */


static int cmd_sta_send_frame_60g(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
#ifdef __linux__
	case DRIVER_WIL6210:
		return wil6210_send_frame_60g(dut, conn, cmd);
#endif /* __linux__ */
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_frame(60G) with the current driver");
		return 0;
	}
}


static int mbo_send_anqp_query(struct sigma_dut *dut, struct sigma_conn *conn,
			       const char *intf, struct sigma_cmd *cmd)
{
	const char *val, *addr;
	char buf[100];

	addr = get_param(cmd, "DestMac");
	if (!addr) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,AP MAC address is missing");
		return 0;
	}

	val = get_param(cmd, "ANQPQuery_ID");
	if (!val) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Missing ANQPQuery_ID");
		return 0;
	}

	if (strcasecmp(val, "NeighborReportReq") == 0) {
		snprintf(buf, sizeof(buf), "ANQP_GET %s 272", addr);
	} else if (strcasecmp(val, "QueryListWithCellPref") == 0) {
		snprintf(buf, sizeof(buf), "ANQP_GET %s 272,mbo:2", addr);
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Invalid ANQPQuery_ID: %s",
				val);
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,Invalid ANQPQuery_ID");
		return 0;
	}

	/* Set gas_address3 field to IEEE 802.11-2012 standard compliant form
	 * (Address3 = Wildcard BSSID when sent to not-associated AP;
	 * if associated, AP BSSID).
	 */
	if (wpa_command(intf, "SET gas_address3 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set gas_address3");
		return 0;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send ANQP query");
		return 0;
	}

	return 1;
}


static int mbo_cmd_sta_send_frame(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *intf,
				  struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "FrameName");

	if (val && strcasecmp(val, "ANQPQuery") == 0)
		return mbo_send_anqp_query(dut, conn, intf, cmd);

	return 2;
}


static enum sigma_cmd_result cmd_sta_send_frame_wpa3(struct sigma_dut *dut,
						     struct sigma_conn *conn,
						     const char *intf,
						     struct sigma_cmd *cmd)
{
	const char *val = get_param(cmd, "framename");

	if (!val)
		return INVALID_SEND_STATUS;

	if (strcasecmp(val, "SAQueryReq") == 0) {
		val = get_param(cmd, "OCIChannel");

		if (!val) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,OCIChannel not present");
			return STATUS_SENT_ERROR;
		}

		dut->saquery_oci_freq = channel_to_freq(dut, atoi(val));
		if (!dut->saquery_oci_freq) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid OCIChannel number");
			return STATUS_SENT_ERROR;
		}

		return sta_inject_frame(dut, conn, intf, SAQUERY, CORRECT_KEY,
					NULL, 0);
	}

	if (strcasecmp(val, "reassocreq") == 0)
		return sta_inject_frame(dut, conn, intf, REASSOCREQ,
					CORRECT_KEY, NULL, 0);

	if (strcasecmp(val, "ANQPQuery") == 0) {
		char buf[50];
		const char *dest = get_param(cmd, "DestMac");
		const char *chan = get_param(cmd, "channel");
		const char *freq_str = get_param(cmd, "ChnlFreq");
		int len, freq;

		if (freq_str)
			freq = atoi(freq_str);
		else
			freq = chan ? channel_to_freq(dut, atoi(chan)) : 0;

		if (!dest || !freq)
			return INVALID_SEND_STATUS;

		len = snprintf(buf, sizeof(buf), "ANQP_GET %s freq=%d 257",
			       dest, freq);
		if (len < 0 || len >= sizeof(buf)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to allocate buf");
			return ERROR_SEND_STATUS;
		}

		if (wpa_command(intf, buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send ANQP Query frame");
			return STATUS_SENT_ERROR;
		}

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"ANQP Query sent: %s", buf);

		return SUCCESS_SEND_STATUS;
	}

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported framename");
	return STATUS_SENT_ERROR;
}


static int
get_type4_frame_classifier(struct sigma_dut *dut, struct sigma_cmd *cmd,
			   char *pos, int rem_len, int num_of_scs_desc,
			   int num_of_tclas_elem)
{
	const char *val;
	int ipv6;
	int len, total_len = 0;

	val = get_param_fmt(cmd, "TCLASElem_Version_%d_%d", num_of_scs_desc,
			    num_of_tclas_elem);
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s: ip_version empty",
				__func__);
		return -1;
	}

	if (strcmp(val, "6") == 0) {
		ipv6 = 1;
	} else if (strcmp(val, "4") == 0) {
		ipv6 = 0;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s: ip_version invalid",
				__func__);
		return -1;
	}

	len = snprintf(pos, rem_len, " ip_version=%s", ipv6 ? "ipv6" : "ipv4");
	if (len < 0 || len >= rem_len)
		return -1;

	pos += len;
	rem_len -= len;
	total_len += len;

	val = get_param_fmt(cmd, "TCLASElem_SourceIPAddr_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " src_ip=%s", val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_DestinationIPAddr_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " dst_ip=%s", val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_SourcePort_%d_%d", num_of_scs_desc,
			    num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " src_port=%s", val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_DestinationPort_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " dst_port=%s", val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_DSCP_%d_%d", num_of_scs_desc,
			    num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " dscp=%s", val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_ProtocolNxtHeader_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		char *prot;

		switch (atoi(val)) {
		case 17:
			prot = "udp";
			break;
		case 6:
			prot = "tcp";
			break;
		case 50:
			prot = "esp";
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid protocol %d", atoi(val));
			return -1;
		}

		if (ipv6)
			len = snprintf(pos, rem_len, " next_header=%s", prot);
		else
			len = snprintf(pos, rem_len, " protocol=%s", prot);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	return total_len;
}


static int
get_type10_frame_classifier(struct sigma_dut *dut, struct sigma_cmd *cmd,
			    char *pos, int rem_len, int num_of_scs_desc,
			    int num_of_tclas_elem)
{
	const char *val;
	int len, total_len = 0;

	val = get_param_fmt(cmd, "TCLASElem_ProtoInstance_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " prot_instance=%s",
			       val);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_ProtoNumNextHeader_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		char *prot;

		switch (atoi(val)) {
		case 17:
			prot = "udp";
			break;
		case 6:
			prot = "tcp";
			break;
		case 50:
			prot = "esp";
			break;
		default:
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid protocol %d",
					atoi(val));
			return -1;
		}

		len = snprintf(pos, rem_len, " prot_number=%s", prot);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_FilterValue_%d_%d",
			    num_of_scs_desc, num_of_tclas_elem);
	if (val) {
		len = snprintf(pos, rem_len, " filter_value=%s", (val + 2));
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	val = get_param_fmt(cmd, "TCLASElem_FilterMask_%d_%d", num_of_scs_desc,
			    num_of_tclas_elem);
	if (val && strlen(val) >= 2) {
		len = snprintf(pos, rem_len, " filter_mask=%s", val + 2);
		if (len < 0 || len >= rem_len)
			return -1;

		pos += len;
		rem_len -= len;
		total_len += len;
	}

	return total_len;
}


static enum sigma_cmd_result
cmd_sta_send_frame_scs(struct sigma_dut *dut, struct sigma_conn *conn,
		       const char *intf, struct sigma_cmd *cmd)
{
	char buf[4096], *pos;
	const char *val, *scs_id, *classifier_type;
	int len, rem_len, total_bytes;
	int num_of_scs_desc = 0, num_of_tclas_elem = 0;
	bool qos_char_up = false;

	scs_id = get_param(cmd, "SCSDescrElem_SCSID_1");
	if (!scs_id) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "SCS ID empty");
		return INVALID_SEND_STATUS;
	}

	rem_len = sizeof(buf);
	pos = buf;

	len = snprintf(buf, sizeof(buf), "SCS");
	if (len < 0 || len > rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	while (scs_id) {
		num_of_scs_desc++;

		val = get_param_fmt(cmd, "SCSDescrElem_RequestType_%d",
				    num_of_scs_desc);
		if (!val)
			return INVALID_SEND_STATUS;

		if (strcasecmp(val, "Add") == 0) {
			len = snprintf(pos, rem_len, " scs_id=%s add",
				       scs_id);
		} else if (strcasecmp(val, "Change") == 0) {
			len = snprintf(pos, rem_len, " scs_id=%s change",
				       scs_id);
		} else if (strcasecmp(val, "Remove") == 0) {
			len = snprintf(pos, rem_len, " scs_id=%s remove",
				       scs_id);
			if (len < 0 || len >= rem_len)
				goto fail;

			pos += len;
			rem_len -= len;
			goto scs_desc_end;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"%s: request type - %s is invalid",
					__func__, val);
			return INVALID_SEND_STATUS;
		}

		if (len < 0 || len >= rem_len)
			goto fail;

		pos += len;
		rem_len -= len;

		val = get_param_fmt(cmd, "IntraAccessCatElem_UP_%d",
				    num_of_scs_desc);
		if (!val) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"IntraAccess Priority empty. Check QoSChar_UserPriority");
			val = get_param_fmt(cmd, "QoSChar_UserPriority_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Both IntraAccess and QoS Characteristics Priorities are empty");
				return INVALID_SEND_STATUS;
			}
			qos_char_up = true;
		}

		len = snprintf(pos, rem_len, " scs_up=%s", val);
		if (len < 0 || len >= rem_len)
			goto fail;

		pos += len;
		rem_len -= len;

		if (qos_char_up)
			goto qos_char;

		classifier_type = get_param_fmt(cmd,
						"TCLASElem_ClassifierType_%d_1",
						num_of_scs_desc);
		if (!classifier_type) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"classifier type missing");
			return INVALID_SEND_STATUS;
		}

		while (classifier_type) {
			num_of_tclas_elem++;

			len = snprintf(pos, rem_len, " classifier_type=%s",
				       classifier_type);
			if (len < 0 || len >= rem_len)
				goto fail;

			pos += len;
			rem_len -= len;

			if (strcmp(classifier_type, "10") == 0) {
				total_bytes = get_type10_frame_classifier(
					dut, cmd, pos, rem_len,
					num_of_scs_desc,
					num_of_tclas_elem);
			} else if (strcmp(classifier_type, "4") == 0) {
				total_bytes = get_type4_frame_classifier(
					dut, cmd, pos, rem_len,
					num_of_scs_desc,
					num_of_tclas_elem);
			} else {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"classifier_type invalid");
				goto fail;
			}

			if (total_bytes < 0)
				goto fail;

			pos += total_bytes;
			rem_len -= total_bytes;

			classifier_type = get_param_fmt(
				cmd, "TCLASElem_ClassifierType_%d_%d",
				num_of_scs_desc, num_of_tclas_elem + 1);
		}

		if (num_of_tclas_elem > 1) {
			val = get_param_fmt(cmd,
					    "TCLASProcessingElem_Processing_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Tclas_processing element %d empty",
						num_of_scs_desc);
				goto fail;
			}

			len = snprintf(pos, rem_len,
				       " tclas_processing=%s", val);
			if (len < 0 || len >= rem_len)
				goto fail;

			pos += len;
			rem_len -= len;
		}

	qos_char:
		/* QoS Characteristics mandatory elements parsing */
		val = get_param_fmt(cmd, "QoSChar_Direction_%d",
				    num_of_scs_desc);
		if (val) {
			char *direction_str[] = { "up", "down", "direct" };
			int direction = atoi(val);
			int min_si, max_si, min_data_rate, delay_bound;

			if (direction < 0 || direction > 2) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Invalid QoSChar_Direction_%d=%d",
						num_of_scs_desc, direction);
				goto fail;
			}

			val = get_param_fmt(cmd,
					    "QoSChar_MinService_Interval_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"QoSChar_MinService_Interval_%d empty",
						num_of_scs_desc);
				goto fail;
			}
			min_si = atoi(val);

			val = get_param_fmt(cmd,
					    "QoSChar_MaxService_Interval_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"QoSChar_MaxService_Interval_%d empty",
						num_of_scs_desc);
				goto fail;
			}
			max_si = atoi(val);

			val = get_param_fmt(cmd,
					    "QoSChar_MinDataRate_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"QoSChar_MinDataRate_%d empty",
						num_of_scs_desc);
				goto fail;
			}
			min_data_rate = atoi(val);

			val = get_param_fmt(cmd,
					    "QoSChar_DelayBound_%d",
					    num_of_scs_desc);
			if (!val) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"QoSChar_DelayBound_%d empty",
						num_of_scs_desc);
				goto fail;
			}
			delay_bound = atoi(val);

			len = snprintf(pos, rem_len,
				       " qos_characteristics %s min_si=%d"
				       " max_si=%d min_data_rate=%d"
				       " delay_bound=%d",
				       direction_str[direction],
				       min_si, max_si, min_data_rate,
				       delay_bound);
			if (len < 0 || len >= rem_len)
				goto fail;

			pos += len;
			rem_len -= len;

			/* Optional QoS Charateristics parameters */
			val = get_param_fmt(cmd,
					    "QoSChar_ServiceStartTime_%d",
					    num_of_scs_desc);
			if (val) {
				len = snprintf(pos, rem_len,
					       " service_start_time=%d",
					       atoi(val));
				if (len < 0 || len >= rem_len)
					goto fail;

				pos += len;
				rem_len -= len;
			}

			val = get_param_fmt(cmd,
					    "QoSChar_ServiceStartTime_LinkID_%d",
					    num_of_scs_desc);
			if (val) {
				len = snprintf(pos, rem_len,
					       " service_start_time_link_id=%d",
					       atoi(val));
				if (len < 0 || len >= rem_len)
					goto fail;

				pos += len;
				rem_len -= len;
			}

			val = get_param_fmt(cmd,
					    "QoSChar_MSDUDeliveryInfo_%d",
					    num_of_scs_desc);
			if (val) {
				if (strlen(val) != 4)
					goto fail;
				len = snprintf(pos, rem_len,
					       " msdu_delivery_info=%d",
					       (int) strtol(val + 2, NULL,
							    0x10));
				if (len < 0 || len >= rem_len)
					goto fail;

				pos += len;
				rem_len -= len;
			}
		}

scs_desc_end:
		num_of_tclas_elem = 0;
		scs_id = get_param_fmt(cmd, "SCSDescrElem_SCSID_%d",
				       num_of_scs_desc + 1);
	}

	if (wpa_command(intf, buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send SCS frame request");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"SCS frame request sent: %s", buf);

	return SUCCESS_SEND_STATUS;
fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Failed to create SCS frame request");
	return ERROR_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sta_send_frame_mscs(struct sigma_dut *dut, struct sigma_conn *conn,
			const char *intf, struct sigma_cmd *cmd)
{
	char buf[128], *pos;
	const char *val, *classifier_type = "04", *type;
	int len, rem_len;
	u8 up_bitmap;

	type = get_param(cmd, "Request_Type");
	if (!type) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: type not valid", __func__);
		return INVALID_SEND_STATUS;
	}

	rem_len = sizeof(buf);
	pos = buf;
	if (strcasecmp(type, "add") == 0) {
		len = snprintf(pos, rem_len, "MSCS add");
	} else if (strcasecmp(type, "update") == 0) {
		len = snprintf(pos, rem_len, "MSCS change");
	} else if (strcasecmp(type, "remove") == 0) {
		if (wpa_command(intf, "MSCS remove") != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send MSCS frame req");
			return STATUS_SENT_ERROR;
		}
		return SUCCESS_SEND_STATUS;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: request type invalid", __func__);
		return INVALID_SEND_STATUS;
	}

	if (len < 0 || len >= rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "User_Priority_Bitmap");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: user priority bitmap empty", __func__);
		return INVALID_SEND_STATUS;
	}

	switch (atoi(val)) {
	case 0:
		up_bitmap = 0x00;
		break;
	case 1:
		up_bitmap = 0xF0;
		break;
	case 2:
		up_bitmap = 0xF6;
		break;
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Unknown User_Priority_Bitmap value %d",
				__func__, atoi(val));
		return INVALID_SEND_STATUS;
	}

	len = snprintf(pos, rem_len, " up_bitmap=%02x", up_bitmap);
	if (len < 0 || len >= rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "User_Priority_Limit");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: invalid user priority limit", __func__);
		return INVALID_SEND_STATUS;
	}

	len = snprintf(pos, rem_len, " up_limit=%s", val);
	if (len < 0 || len >= rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "Stream_Timeout");
	if (!val)
		val = get_param(cmd, "Stream_Timtout");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: invalid stream timeout", __func__);
		return INVALID_SEND_STATUS;
	}

	len = snprintf(pos, rem_len, " stream_timeout=%s", val);
	if (len < 0 || len >= rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "TCLAS_Mask");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: invalid tclas mask", __func__);
		return INVALID_SEND_STATUS;
	}

	len = snprintf(pos, rem_len, " frame_classifier=%s%lx%032x",
		       classifier_type, strtol(val, NULL, 2), 0);
	if (len < 0 || len >= rem_len)
		goto fail;

	if (wpa_command(intf, buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send MSCS frame req");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"MSCS frame request sent: %s", buf);

	return SUCCESS_SEND_STATUS;
fail:
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Failed to create MSCS frame req");
	return ERROR_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sta_send_frame_dscp_query(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *intf, struct sigma_cmd *cmd)
{
	char buf[150], *pos;
	const char *val;
	int len, rem_len;

	rem_len = sizeof(buf);
	pos = buf;

	len = snprintf(pos, rem_len, "DSCP_QUERY");
	if (len < 0 || len >= rem_len)
		goto fail;

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "Wildcard");
	if (val && strcasecmp(val, "Yes") == 0) {
		len = snprintf(pos, rem_len, " wildcard");
		if (len < 0 || len >= rem_len)
			goto fail;
	} else if (strlen(dut->qm_domain_name)) {
		len = snprintf(pos, rem_len, " domain_name=%s",
			       dut->qm_domain_name);
		if (len < 0 || len >= rem_len)
			goto fail;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid DSCP Query configuration");
		return INVALID_SEND_STATUS;
	}

	if (wpa_command(intf, buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send DSCP policy query frame");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"DSCP policy query frame sent: %s", buf);
	return SUCCESS_SEND_STATUS;
fail:
	sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to send DSCP query");
	return ERROR_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sta_send_frame_dscp_response(struct sigma_dut *dut, struct sigma_conn *conn,
				 const char *intf, struct sigma_cmd *cmd)
{
	char buf[256], *pos, *item, *list, *saveptr;
	const char *val;
	int len, rem_len;

	pos = buf;
	rem_len = sizeof(buf);

	len = snprintf(pos, rem_len, "DSCP_RESP");
	if (snprintf_error(rem_len, len)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to create DSCP Policy Response command");
		return ERROR_SEND_STATUS;
	}

	pos += len;
	rem_len -= len;

	val = get_param(cmd, "PolicyID_List");
	if (!val) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"DSCP policy ID list missing");
		return INVALID_SEND_STATUS;
	}

	list = strdup(val);
	if (!list)
		return ERROR_SEND_STATUS;

	item = strtok_r(list, "_", &saveptr);
	while (item) {
		unsigned int i;
		int policy_id = atoi(item);

		for (i = 0; i < dut->num_dscp_status; i++)
			if (dut->dscp_status[i].id == policy_id)
				break;

		if (i == dut->num_dscp_status) {
			free(list);
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,DSCP policy id not found in status list");
			return STATUS_SENT_ERROR;
		}

		len = snprintf(pos, rem_len, " policy_id=%d status=%d",
			       policy_id, dut->dscp_status[i].status);
		if (snprintf_error(rem_len, len)) {
			free(list);
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to write DSCP policy list");
			return ERROR_SEND_STATUS;
		}

		pos += len;
		rem_len -= len;

		if (dut->dscp_status[i].status)
			remove_dscp_policy(dut, policy_id);

		item = strtok_r(NULL, "_", &saveptr);
	}

	free(list);

	if (wpa_command(intf, buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to send DSCP Policy Response frame");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"DSCP Policy Response frame sent: %s", buf);
	return SUCCESS_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sta_send_frame_qm(struct sigma_dut *dut, struct sigma_conn *conn,
		      const char *intf, struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "FrameName");
	if (val) {
		if (strcasecmp(val, "MSCSReq") == 0)
			return cmd_sta_send_frame_mscs(dut, conn, intf, cmd);
		if (strcasecmp(val, "SCSReq") == 0)
			return cmd_sta_send_frame_scs(dut, conn, intf, cmd);
		if (strcasecmp(val, "DSCPPolicyQuery") == 0)
			return cmd_sta_send_frame_dscp_query(dut, conn, intf,
							     cmd);
		if (strcasecmp(val, "DSCPPolicyResponse") == 0)
			return cmd_sta_send_frame_dscp_response(dut, conn, intf,
								cmd);

		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: frame name - %s is invalid",
				__func__, val);
	}

	return INVALID_SEND_STATUS;
}


enum sigma_cmd_result cmd_sta_send_frame(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;
	enum send_frame_type frame;
	enum send_frame_protection protected;
	char buf[100];
	unsigned char addr[ETH_ALEN];
	int res;

	if (!intf)
		return -1;

	val = get_param(cmd, "program");
	if (val == NULL)
		val = get_param(cmd, "frame");
	if (val && strcasecmp(val, "TDLS") == 0)
		return cmd_sta_send_frame_tdls(dut, conn, cmd);
	if (val && (strcasecmp(val, "HS2") == 0 ||
		    strcasecmp(val, "HS2-R2") == 0 ||
		    strcasecmp(val, "HS2-R3") == 0 ||
		    strcasecmp(val, "HS2-2022") == 0 ||
		    strcasecmp(val, "HS2-R4") == 0))
		return cmd_sta_send_frame_hs2(dut, conn, cmd);
	if (val && strcasecmp(val, "VHT") == 0)
		return cmd_sta_send_frame_vht(dut, conn, cmd);
	if (val && strcasecmp(val, "HE") == 0)
		return cmd_sta_send_frame_he(dut, conn, cmd);
	if (val && strcasecmp(val, "LOC") == 0)
		return loc_cmd_sta_send_frame(dut, conn, cmd);
	if (val && strcasecmp(val, "60GHz") == 0)
		return cmd_sta_send_frame_60g(dut, conn, cmd);
	if (val && strcasecmp(val, "MBO") == 0) {
		res = mbo_cmd_sta_send_frame(dut, conn, intf, cmd);
		if (res != 2)
			return res;
	}
	if (val && strcasecmp(val, "WPA3") == 0)
		return cmd_sta_send_frame_wpa3(dut, conn, intf, cmd);
	if (val && strcasecmp(val, "QM") == 0)
		return cmd_sta_send_frame_qm(dut, conn, intf, cmd);

	val = get_param(cmd, "TD_DISC");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_DISCOVER %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to send TDLS discovery");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_Setup");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_SETUP %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to start TDLS setup");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_TearDown");
	if (val) {
		if (hwaddr_aton(val, addr) < 0)
			return -1;
		snprintf(buf, sizeof(buf), "TDLS_TEARDOWN %s", val);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to tear down TDLS link");
			return 0;
		}
		return 1;
	}

	val = get_param(cmd, "TD_ChannelSwitch");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_ChannelSwitch not yet supported");
		return 0;
	}

	val = get_param(cmd, "TD_NF");
	if (val) {
		/* TODO */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,TD_NF not yet supported");
		return 0;
	}

	val = get_param(cmd, "PMFFrameType");
	if (val == NULL)
		val = get_param(cmd, "FrameName");
	if (val == NULL)
		val = get_param(cmd, "Type");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "disassoc") == 0)
		frame = DISASSOC;
	else if (strcasecmp(val, "deauth") == 0)
		frame = DEAUTH;
	else if (strcasecmp(val, "saquery") == 0)
		frame = SAQUERY;
	else if (strcasecmp(val, "auth") == 0)
		frame = AUTH;
	else if (strcasecmp(val, "assocreq") == 0)
		frame = ASSOCREQ;
	else if (strcasecmp(val, "reassocreq") == 0)
		frame = REASSOCREQ;
	else if (strcasecmp(val, "neigreq") == 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Got neighbor request");

		val = get_param(cmd, "ssid");
		if (val == NULL)
			return -1;

		res = send_neighbor_request(dut, intf, val);
		if (res) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Failed to send neighbor report request");
			return 0;
		}

		return 1;
	} else if (strcasecmp(val, "transmgmtquery") == 0 ||
		   strcasecmp(val, "BTMQuery") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Got Transition Management Query");

		res = send_trans_mgmt_query(dut, intf, cmd);
		if (res) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Failed to send Transition Management Query");
			return 0;
		}

		return 1;
	} else if (strcasecmp(val, "SCSReq") == 0) {
		return cmd_sta_send_frame_scs(dut, conn, intf, cmd);
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFFrameType");
		return 0;
	}

	val = get_param(cmd, "PMFProtected");
	if (val == NULL)
		val = get_param(cmd, "Protected");
	if (val == NULL)
		return -1;
	if (strcasecmp(val, "Correct-key") == 0 ||
	    strcasecmp(val, "CorrectKey") == 0)
		protected = CORRECT_KEY;
	else if (strcasecmp(val, "IncorrectKey") == 0)
		protected = INCORRECT_KEY;
	else if (strcasecmp(val, "Unprotected") == 0)
		protected = UNPROTECTED;
	else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "PMFProtected");
		return 0;
	}

	if (protected != UNPROTECTED &&
	    (frame == AUTH || frame == ASSOCREQ || frame == REASSOCREQ)) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Impossible "
			  "PMFProtected for auth/assocreq/reassocreq");
		return 0;
	}

	if (if_nametoindex("sigmadut") == 0) {
		snprintf(buf, sizeof(buf),
			 "iw dev %s interface add sigmadut type monitor",
			 get_station_ifname(dut));
		if (system(buf) != 0 ||
		    if_nametoindex("sigmadut") == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to add "
					"monitor interface with '%s'", buf);
			return -2;
		}
	}

	if (system("ifconfig sigmadut up") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to set "
				"monitor interface up");
		return -2;
	}

	return sta_inject_frame(dut, conn, intf, frame, protected, NULL, 1);
}


static int cmd_sta_set_parameter_hs2(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd,
				     const char *ifname)
{
	char buf[200];
	const char *val;

	val = get_param(cmd, "ClearARP");
	if (val && atoi(val) == 1) {
		snprintf(buf, sizeof(buf), "ip neigh flush dev %s", ifname);
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to clear ARP cache");
			return 0;
		}
	}

	return 1;
}


int cmd_sta_set_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	if (intf == NULL)
		return -1;

	val = get_param(cmd, "program");
	if (val && (strcasecmp(val, "HS2") == 0 ||
		    strcasecmp(val, "HS2-R2") == 0 ||
		    strcasecmp(val, "HS2-R3") == 0 ||
		    strcasecmp(val, "HS2-2022") == 0 ||
		    strcasecmp(val, "HS2-R4") == 0))
		return cmd_sta_set_parameter_hs2(dut, conn, cmd, intf);

	return -1;
}


static enum sigma_cmd_result cmd_sta_set_macaddr(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mac = get_param(cmd, "MAC");

	if (intf == NULL || mac == NULL)
		return -1;

	sigma_dut_print(dut, DUT_MSG_INFO, "Change local MAC address for "
			"interface %s to %s", intf, mac);

	if (dut->set_macaddr) {
		char buf[128];
		int res;
		if (strcasecmp(mac, "default") == 0) {
			res = snprintf(buf, sizeof(buf), "%s",
				       dut->set_macaddr);
			dut->tmp_mac_addr = 0;
		} else {
			res = snprintf(buf, sizeof(buf), "%s %s",
				       dut->set_macaddr, mac);
			dut->tmp_mac_addr = 1;
		}
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		if (system(buf) != 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set MAC "
				  "address");
			return 0;
		}
		return 1;
	}

	if (strcasecmp(mac, "default") == 0)
		return 1;

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
		  "command");
	return 0;
}


static int iwpriv_tdlsoffchnmode(struct sigma_dut *dut,
				 struct sigma_conn *conn, const char *intf,
				 int val)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsoffchnmode %d",
		       intf, val);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to configure offchannel mode");
		return 0;
	}

	return 1;
}


static int off_chan_val(enum sec_ch_offset off)
{
	switch (off) {
	case SEC_CH_NO:
		return 0;
	case SEC_CH_40ABOVE:
		return 40;
	case SEC_CH_40BELOW:
		return -40;
	}

	return 0;
}


static int iwpriv_set_offchan(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *intf, int off_ch_num,
			      enum sec_ch_offset sec)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsoffchan %d",
		       intf, off_ch_num);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set offchan");
		return 0;
	}

	res = snprintf(buf, sizeof(buf), "iwpriv %s tdlsecchnoffst %d",
		       intf, off_chan_val(sec));
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);
	if (system(buf) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set sec chan offset");
		return 0;
	}

	return 1;
}


static int tdls_set_offchannel_offset(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      const char *intf, int off_ch_num,
				      enum sec_ch_offset sec)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "DRIVER TDLSOFFCHANNEL %d",
		       off_ch_num);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set offchan");
		return 0;
	}
	res = snprintf(buf, sizeof(buf), "DRIVER TDLSSECONDARYCHANNELOFFSET %d",
		       off_chan_val(sec));
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set sec chan offset");
		return 0;
	}

	return 1;
}


static int tdls_set_offchannel_mode(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    const char *intf, int val)
{
	char buf[200];
	int res;

	res = snprintf(buf, sizeof(buf), "DRIVER TDLSOFFCHANNELMODE %d",
		       val);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Run: %s", buf);

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			"ErrorCode,Failed to configure offchannel mode");
		return 0;
	}

	return 1;
}


static int cmd_sta_set_rfeature_tdls(const char *intf, struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *val;
	enum {
		CHSM_NOT_SET,
		CHSM_ENABLE,
		CHSM_DISABLE,
		CHSM_REJREQ,
		CHSM_UNSOLRESP
	} chsm = CHSM_NOT_SET;
	int off_ch_num = -1;
	enum sec_ch_offset sec_ch = SEC_CH_NO;
	int res;

	val = get_param(cmd, "Uapsd");
	if (val) {
		char buf[100];
		if (strcasecmp(val, "Enable") == 0)
			snprintf(buf, sizeof(buf), "SET ps 99");
		else if (strcasecmp(val, "Disable") == 0)
			snprintf(buf, sizeof(buf), "SET ps 98");
		else {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,"
				  "Unsupported uapsd parameter value");
			return 0;
		}
		if (wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to change U-APSD "
				  "powersave mode");
			return 0;
		}
	}

	val = get_param(cmd, "TPKTIMER");
	if (val && strcasecmp(val, "DISABLE") == 0) {
		if (wpa_command(intf, "SET tdls_testing 0x100")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to enable no TPK "
				  "expiration test mode");
			return 0;
		}
		dut->no_tpk_expiration = 1;
	}

	val = get_param(cmd, "ChSwitchMode");
	if (val) {
		if (strcasecmp(val, "Enable") == 0 ||
		    strcasecmp(val, "Initiate") == 0)
			chsm = CHSM_ENABLE;
		else if (strcasecmp(val, "Disable") == 0 ||
		    strcasecmp(val, "passive") == 0)
			chsm = CHSM_DISABLE;
		else if (strcasecmp(val, "RejReq") == 0)
			chsm = CHSM_REJREQ;
		else if (strcasecmp(val, "UnSolResp") == 0)
			chsm = CHSM_UNSOLRESP;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unknown ChSwitchMode value");
			return 0;
		}
	}

	val = get_param(cmd, "OffChNum");
	if (val) {
		off_ch_num = atoi(val);
		if (off_ch_num == 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid OffChNum");
			return 0;
		}
	}

	val = get_param(cmd, "SecChOffset");
	if (val) {
		if (strcmp(val, "20") == 0)
			sec_ch = SEC_CH_NO;
		else if (strcasecmp(val, "40above") == 0)
			sec_ch = SEC_CH_40ABOVE;
		else if (strcasecmp(val, "40below") == 0)
			sec_ch = SEC_CH_40BELOW;
		else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unknown SecChOffset value");
			return 0;
		}
	}

	if (chsm == CHSM_NOT_SET) {
		/* no offchannel changes requested */
		return 1;
	}

	if (strcmp(intf, get_main_ifname(dut)) != 0 &&
	    strcmp(intf, get_station_ifname(dut)) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unknown interface");
		return 0;
	}

	switch (chsm) {
	case CHSM_NOT_SET:
		res = 1;
		break;
	case CHSM_ENABLE:
		if (off_ch_num < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Missing OffChNum argument");
			return 0;
		}
		if (wifi_chip_type == DRIVER_WCN) {
			res = tdls_set_offchannel_offset(dut, conn, intf,
							 off_ch_num, sec_ch);
		} else {
			res = iwpriv_set_offchan(dut, conn, intf, off_ch_num,
						 sec_ch);
		}
		if (res != 1)
			return res;
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 1);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 1);
		break;
	case CHSM_DISABLE:
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 2);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 2);
		break;
	case CHSM_REJREQ:
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 3);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 3);
		break;
	case CHSM_UNSOLRESP:
		if (off_ch_num < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Missing OffChNum argument");
			return 0;
		}
		if (wifi_chip_type == DRIVER_WCN) {
			res = tdls_set_offchannel_offset(dut, conn, intf,
							 off_ch_num, sec_ch);
		} else {
			res = iwpriv_set_offchan(dut, conn, intf, off_ch_num,
						 sec_ch);
		}
		if (res != 1)
			return res;
		if (wifi_chip_type == DRIVER_WCN)
			res = tdls_set_offchannel_mode(dut, conn, intf, 4);
		else
			res = iwpriv_tdlsoffchnmode(dut, conn, intf, 4);
		break;
	}

	return res;
}


static int ath_sta_set_rfeature_vht(const char *intf, struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *val;
	char *token = NULL, *result;

	novap_reset(dut, intf, 1);

	val = get_param(cmd, "nss_mcs_opt");
	if (val) {
		/* String (nss_operating_mode; mcs_operating_mode) */
		int nss, mcs;
		char *saveptr;

		token = strdup(val);
		if (!token)
			return 0;
		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT NSS not specified");
			goto failed;
		}
		if (strcasecmp(result, "def") != 0) {
			nss = atoi(result);
			if (nss == 4)
				ath_disable_txbf(dut, intf);
			if (run_iwpriv(dut, intf, "nss %d", nss) < 0)
				goto failed;

		}

		result = strtok_r(NULL, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
				"VHT MCS not specified");
			goto failed;
		}
		if (strcasecmp(result, "def") == 0) {
			if (run_iwpriv(dut, intf, "set11NRates 0") < 0)
				goto failed;
		} else {
			mcs = atoi(result);
			if (run_iwpriv(dut, intf, "vhtmcs %d", mcs) < 0)
				goto failed;
		}
		/* Channel width gets messed up, fix this */
		run_iwpriv(dut, intf, "chwidth %d", dut->chwidth);
	}

	free(token);
	return 1;
failed:
	free(token);
	return 0;
}


static int cmd_sta_set_rfeature_vht(const char *intf, struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
	case DRIVER_ATHEROS:
		return ath_sta_set_rfeature_vht(intf, dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_rfeature(VHT) with the current driver");
		return 0;
	}
}


static enum sigma_cmd_result
wcn_sta_set_rfeature_he(const char *intf, struct sigma_dut *dut,
			struct sigma_conn *conn, struct sigma_cmd *cmd)
{
	const char *val;
	char *token = NULL, *result;
	char buf[60];
	u16 tx_rate = 0;

	val = get_param(cmd, "nss_mcs_opt");
	if (val) {
		/* String (nss_operating_mode; mcs_operating_mode) */
		int nss, mcs, ratecode;
		char *saveptr;

		token = strdup(val);
		if (!token)
			return ERROR_SEND_STATUS;

		result = strtok_r(token, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"HE NSS not specified");
			goto failed;
		}
		nss = 1;
		if (strcasecmp(result, "def") != 0)
			nss = atoi(result);

		result = strtok_r(NULL, ";", &saveptr);
		if (!result) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"HE MCS not specified");
			goto failed;
		}
		mcs = 7;
		if (strcasecmp(result, "def") != 0)
			mcs = atoi(result);

		ratecode = 0x20; /* for nss:1 MCS 0 */
		if (nss == 2) {
			ratecode = 0x40; /* for nss:2 MCS 0 */
		} else if (nss > 2) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"HE NSS %d not supported", nss);
			goto failed;
		}

		if (nss == 2) {
			/* set nss to 2 */
			if (wcn_sta_set_sp_stream(dut, intf, "2SS") < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"set nss to 2SS failed");
				goto failed;
			}
		}
		if (nss == 1) {
			/* set nss to 1 */
			if (wcn_sta_set_sp_stream(dut, intf, "1SS") < 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"set nss to 1SS failed");
				goto failed;
			}
		}

		/* Add the MCS to the ratecode */
		if (mcs >= 0 && mcs <= 11) {
			ratecode += mcs;
			tx_rate = 1 << mcs;
#ifdef NL80211_SUPPORT
			if (dut->device_type == STA_testbed) {
				enum he_mcs_config mcs_config;
				int ret;

				if (mcs <= 7)
					mcs_config = HE_80_MCS0_7;
				else if (mcs <= 9)
					mcs_config = HE_80_MCS0_9;
				else
					mcs_config = HE_80_MCS0_11;
				ret = sta_set_he_mcs(dut, intf, mcs_config);
				if (ret) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"nss_mcs_opt: mcs setting failed, mcs:%d, mcs_config %d, ret:%d",
							mcs, mcs_config, ret);
					goto failed;
				}
			}
#endif /* NL80211_SUPPORT */
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"HE MCS %d not supported", mcs);
			goto failed;
		}

		if (wcn_set_he_tx_rate(dut, intf, tx_rate, dut->sta_nss)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"wcn_set_he_tx_rate failed, using iwpriv");
			snprintf(buf, sizeof(buf),
				 "iwpriv %s set_11ax_rate 0x%03x",
				 intf, ratecode);
			if (system(buf) != 0) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"iwpriv setting of 11ax rates failed");
				goto failed;
			}
		}
		free(token);
	}

	val = get_param(cmd, "ActiveTxMultiLinks");
	if (val) {
		char value[50];
		char *result = NULL;
		char *saveptr;
		uint8_t num_links = 0;
		uint8_t link_addr[2][6];

		strlcpy(value, val, sizeof(value));
		result = strtok_r(value, " ", &saveptr);
		if (!result) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Link address not specified");
			return STATUS_SENT_ERROR;
		}

		while (result && num_links < 2) {
			if (hwaddr_aton(result, &link_addr[num_links][0]) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Invalid link address");
				return STATUS_SENT_ERROR;
			}
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"link_mac[%d]: %02x:%02x:%02x:%02x:%02x:%02x",
					num_links, link_addr[num_links][0],
					link_addr[num_links][1],
					link_addr[num_links][2],
					link_addr[num_links][3],
					link_addr[num_links][4],
					link_addr[num_links][5]);
			num_links++;
			result = strtok_r(NULL, " ", &saveptr);
		}
		sta_set_eht_mlo_active_tx_links(dut, intf,
						num_links, link_addr);
	}

	val = get_param(cmd, "InactiveAllMultiLinks");
	if (val) {
		if (atoi(val) == 1) {
			val = get_param(cmd, "InactivePeriod");
			if (!val) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,InactivePeriod not set");
				return STATUS_SENT_ERROR;
			}

			if (sta_set_inactive_period(dut, intf, atoi(val))) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to set InactivePeriod");
				return STATUS_SENT_ERROR;
			}
		} else {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"InactiveAllMultiLinks is not 1");
		}
	}

	val = get_param(cmd, "GI");
	if (val) {
		int fix_rate_sgi;
		u8 he_gi_val = 0;

		if (strcmp(val, "0.8") == 0) {
			snprintf(buf, sizeof(buf), "iwpriv %s shortgi 9", intf);
			fix_rate_sgi = 1;
			he_gi_val = NL80211_RATE_INFO_HE_GI_0_8;
		} else if (strcmp(val, "1.6") == 0) {
			snprintf(buf, sizeof(buf), "iwpriv %s shortgi 10",
				 intf);
			fix_rate_sgi = 2;
			he_gi_val = NL80211_RATE_INFO_HE_GI_1_6;
		} else if (strcmp(val, "3.2") == 0) {
			snprintf(buf, sizeof(buf), "iwpriv %s shortgi 11",
				 intf);
			fix_rate_sgi = 3;
			he_gi_val = NL80211_RATE_INFO_HE_GI_3_2;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,GI value not supported");
			return STATUS_SENT_ERROR;
		}
		if (wcn_set_he_gi(dut, intf, he_gi_val)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"wcn_set_he_gi failed, using iwpriv");
			if (system(buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
						"errorCode,Failed to set shortgi");
				return STATUS_SENT_ERROR;
			}
			snprintf(buf, sizeof(buf), "iwpriv %s shortgi %d",
					intf, fix_rate_sgi);
			if (system(buf) != 0) {
				send_resp(dut, conn, SIGMA_ERROR,
						"errorCode,Failed to set fix rate shortgi");
				return STATUS_SENT_ERROR;
			}
		}
	}

	val = get_param(cmd, "LTF");
	if (val) {
#ifdef NL80211_SUPPORT
		if (strcmp(val, "3.2") == 0) {
			wcn_set_he_ltf(dut, intf, QCA_WLAN_HE_LTF_1X);
		} if (strcmp(val, "6.4") == 0) {
			wcn_set_he_ltf(dut, intf, QCA_WLAN_HE_LTF_2X);
		} else if (strcmp(val, "12.8") == 0) {
			wcn_set_he_ltf(dut, intf, QCA_WLAN_HE_LTF_4X);
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode, LTF value not supported");
			return STATUS_SENT_ERROR;
		}
#else /* NL80211_SUPPORT */
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"LTF cannot be set without NL80211_SUPPORT defined");
		return ERROR_SEND_STATUS;
#endif /* NL80211_SUPPORT */
	}

	val = get_param(cmd, "KeepAlive");
	if (val) {
		int set_val = QCA_WLAN_KEEP_ALIVE_DEFAULT;

		if (strcasecmp(val, "Data") == 0)
			set_val = QCA_WLAN_KEEP_ALIVE_DATA;
		else if (strcasecmp(val, "Mgmt") == 0)
			set_val = QCA_WLAN_KEEP_ALIVE_MGMT;

		if (sta_set_keep_alive_data_cfg(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set keep alive type config");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "TxSUPPDU");
	if (val) {
		int set_val = 1;

		if (strcasecmp(val, "Enable") == 0)
			set_val = 1;
		else if (strcasecmp(val, "Disable") == 0)
			set_val = 0;

		if (sta_set_tx_su_ppdu_cfg(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set Tx SU PPDU config");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "Mgmt_Data_TX_Resp_Frame");
	if (val) {
		int set_val = 0;

		if (strcasecmp(val, "Enable") == 0)
			set_val = 0;
		else if (strcasecmp(val, "Disable") == 0)
			set_val = 1;

		if (sta_set_mgmt_data_tx_disable_cfg(dut, intf, set_val)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set mgmt/data Tx disable config");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "TWT_Setup");
	if (val) {
#ifdef NL80211_SUPPORT
		if (dut->sta_async_twt_supp && nl80211_open_event_sock(dut))
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open nl80211 event socket");
#endif /* NL80211_SUPPORT */
		if (strcasecmp(val, "Request") == 0) {
			if (set_power_save_wcn(dut, intf, 1) < 0)
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Failed to enable power save");
			if (sta_twt_request(dut, conn, cmd)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,TWT setup failed");
				return STATUS_SENT_ERROR;
			}
		} else if (strcasecmp(val, "Teardown") == 0) {
			if (sta_twt_teardown(dut, conn, cmd)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,TWT teardown failed");
				return STATUS_SENT_ERROR;
			}
		}
	}

	val = get_param(cmd, "TWT_Operation");
	if (val) {
#ifdef NL80211_SUPPORT
		if (dut->sta_async_twt_supp && nl80211_open_event_sock(dut))
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failed to open nl80211 event socket");
#endif /* NL80211_SUPPORT */
		if (strcasecmp(val, "Suspend") == 0) {
			if (sta_twt_suspend_or_nudge(dut, conn, cmd)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,TWT suspend failed");
				return STATUS_SENT_ERROR;
			}
		} else if (strcasecmp(val, "Resume") == 0) {
			if (sta_twt_resume(dut, conn, cmd)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,TWT resume failed");
				return STATUS_SENT_ERROR;
			}
		}
	}

	val = get_param(cmd, "transmitOMI");
	if (val && sta_transmit_omi(dut, conn, cmd)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,sta_transmit_omi failed");
		return STATUS_SENT_ERROR;
	}

	val = get_param(cmd, "Powersave");
	if (val) {
		int ps;

		if (strcasecmp(val, "off") == 0) {
			ps = 2;
		} else if (strcasecmp(val, "on") == 0) {
			ps = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported Powersave value '%s'",
					val);
			return INVALID_SEND_STATUS;
		}
		if (set_power_save_wcn(dut, intf, ps) < 0)
			return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "MU_EDCA");
	if (val) {
		if (strcasecmp(val, "Override") == 0) {
			if (sta_set_mu_edca_override(dut, intf, 1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,MU EDCA override set failed");
				return STATUS_SENT;
			}
		} else if (strcasecmp(val, "Disable") == 0) {
			if (sta_set_mu_edca_override(dut, intf, 0)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,MU EDCA override disable failed");
				return STATUS_SENT;
			}
		}
	}

	val = get_param(cmd, "RUAllocTone");
	if (val && strcasecmp(val, "242") == 0) {
		if (sta_set_ru_242_tone_tx(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set RU 242 tone Tx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "PPDUTxType");
	if (val && strcasecmp(val, "ER-SU") == 0) {
		if (sta_set_er_su_ppdu_type_tx(dut, intf, 1)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set ER-SU PPDU type Tx");
			return STATUS_SENT_ERROR;
		}
	}

	val = get_param(cmd, "EMLSR_Operation");
	if (val) {
		enum qca_wlan_emlsr_mode mode;

		if (strcasecmp(val, "Initiate") == 0) {
			mode = QCA_WLAN_EMLSR_MODE_ENTER;
		} else if (strcasecmp(val, "Exit") == 0) {
			mode = QCA_WLAN_EMLSR_MODE_EXIT;
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Invalid EMLSR operation");
			return STATUS_SENT_ERROR;
		}
		sta_config_params(dut, intf, STA_SET_EMLSR_MODE_SWITCH, mode);
	}

	val = get_param(cmd, "CodingType");
	if (val) {
		int ldpc;

		ldpc = strcasecmp(val, "BCCCoding") != 0;
		if (run_iwpriv(dut, intf, "ldpc %d", ldpc))
			sta_config_params(dut, intf, STA_SET_LDPC, ldpc);
	}

	val = get_param(cmd, "Ch_Pref");
	if (val && mbo_set_non_pref_ch_list(dut, conn, intf, cmd) == 0)
		return STATUS_SENT;

	val = get_param(cmd, "Cellular_Data_Cap");
	if (val && mbo_set_cellular_data_capa(dut, conn, intf, atoi(val)) == 0)
		return STATUS_SENT;

	val = get_param(cmd, "EPCS_Setup");
	if (val) {
		int epcs_function;

		if (strcasecmp(val, "Request") == 0) {
			epcs_function = 1;
		} else if (strcasecmp(val, "Teardown") == 0) {
			epcs_function = 0;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported EPCS setup value '%s'",
					val);
			return INVALID_SEND_STATUS;
		}
		sta_config_params(dut, intf, STA_SET_EPCS_FUNCTION,
				  epcs_function);
	}

	return SUCCESS_SEND_STATUS;

failed:
	free(token);
	return ERROR_SEND_STATUS;
}


static enum sigma_cmd_result
cmd_sta_set_rfeature_he(const char *intf, struct sigma_dut *dut,
			struct sigma_conn *conn, struct sigma_cmd *cmd)
{
	switch (get_driver_type(dut)) {
	case DRIVER_WCN:
		return wcn_sta_set_rfeature_he(intf, dut, conn, cmd);
	default:
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Unsupported sta_set_rfeature(HE) with the current driver");
		return STATUS_SENT_ERROR;
	}
}


static int cmd_sta_set_power_save_he(const char *intf, struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "powersave");
	if (val) {
		int ps;

		if (strcasecmp(val, "off") == 0) {
			ps = 2;
		} else if (strcasecmp(val, "on") == 0) {
			ps = 1;
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Unsupported power save config");
			return -1;
		}
		if (set_power_save_wcn(dut, intf, ps) < 0)
			return 0;
		return 1;
	}

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported command");

	return 0;
}


static int btm_query_candidate_list(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *bssid, *info, *op_class, *ch, *phy_type, *pref;
	int len, ret;
	char buf[10];

	/*
	 * Neighbor Report elements format:
	 * neighbor=<BSSID>,<BSSID Information>,<Operating Class>,
	 * <Channel Number>,<PHY Type>[,<hexdump of Optional Subelements>]
	 * eg: neighbor=aa:bb:cc:dd:ee:ff,17,81,6,1,030101
	 */

	bssid = get_param(cmd, "Nebor_BSSID");
	if (!bssid) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_BSSID is missing");
		return 0;
	}

	info = get_param(cmd, "Nebor_Bssid_Info");
	if (!info) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Using default value for Nebor_Bssid_Info: %s",
				DEFAULT_NEIGHBOR_BSSID_INFO);
		info = DEFAULT_NEIGHBOR_BSSID_INFO;
	}

	op_class = get_param(cmd, "Nebor_Op_Class");
	if (!op_class) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_Op_Class is missing");
		return 0;
	}

	ch = get_param(cmd, "Nebor_Op_Ch");
	if (!ch) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,Nebor_Op_Ch is missing");
		return 0;
	}

	phy_type = get_param(cmd, "Nebor_Phy_Type");
	if (!phy_type) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Using default value for Nebor_Phy_Type: %s",
				DEFAULT_NEIGHBOR_PHY_TYPE);
		phy_type = DEFAULT_NEIGHBOR_PHY_TYPE;
	}

	/* Parse optional subelements */
	buf[0] = '\0';
	pref = get_param(cmd, "Nebor_Pref");
	if (pref) {
		/* hexdump for preferrence subelement */
		ret = snprintf(buf, sizeof(buf), ",0301%02x", atoi(pref));
		if (ret < 0 || ret >= (int) sizeof(buf)) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"snprintf failed for optional subelement ret: %d",
					ret);
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,snprintf failed for subelement");
			return 0;
		}
	}

	if (!dut->btm_query_cand_list) {
		dut->btm_query_cand_list = calloc(1, NEIGHBOR_REPORT_SIZE);
		if (!dut->btm_query_cand_list) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to allocate memory for btm_query_cand_list");
			return 0;
		}
	}

	len = strlen(dut->btm_query_cand_list);
	ret = snprintf(dut->btm_query_cand_list + len,
		       NEIGHBOR_REPORT_SIZE - len, " neighbor=%s,%s,%s,%s,%s%s",
		       bssid, info, op_class, ch, phy_type, buf);
	if (ret < 0 || ret >= NEIGHBOR_REPORT_SIZE - len) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"snprintf failed for neighbor report list ret: %d",
				ret);
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,snprintf failed for neighbor report");
		free(dut->btm_query_cand_list);
		dut->btm_query_cand_list = NULL;
		return 0;
	}

	return 1;
}


int sta_extract_60g_ese(struct sigma_dut *dut, struct sigma_cmd *cmd,
			struct sigma_ese_alloc *allocs, int *allocs_size)
{
	int max_count = *allocs_size;
	int count = 0, i;
	const char *val;

	do {
		val = get_param_indexed(cmd, "AllocID", count);
		if (val)
			count++;
	} while (val);

	if (count == 0 || count > max_count) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid number of allocations(%d)", count);
		return -1;
	}

	for (i = 0; i < count; i++) {
		val = get_param_indexed(cmd, "PercentBI", i);
		if (!val) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Missing PercentBI parameter at index %d",
					i);
			return -1;
		}
		allocs[i].percent_bi = atoi(val);

		val = get_param_indexed(cmd, "SrcAID", i);
		if (val)
			allocs[i].src_aid = strtol(val, NULL, 0);
		else
			allocs[i].src_aid = ESE_BCAST_AID;

		val = get_param_indexed(cmd, "DestAID", i);
		if (val)
			allocs[i].dst_aid = strtol(val, NULL, 0);
		else
			allocs[i].dst_aid = ESE_BCAST_AID;

		allocs[i].type = ESE_CBAP;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Alloc %d PercentBI %d SrcAID %d DstAID %d",
				i, allocs[i].percent_bi, allocs[i].src_aid,
				allocs[i].dst_aid);
	}

	*allocs_size = count;
	return 0;
}


static int sta_set_60g_ese(struct sigma_dut *dut, int count,
			   struct sigma_ese_alloc *allocs)
{
	switch (get_driver_type(dut)) {
#ifdef __linux__
	case DRIVER_WIL6210:
		if (wil6210_set_ese(dut, count, allocs))
			return -1;
		return 1;
#endif /* __linux__ */
	default:
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Unsupported sta_set_60g_ese with the current driver");
		return -1;
	}
}


static int cmd_sta_set_rfeature_60g(const char *intf, struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "ExtSchIE");
	if (val && !strcasecmp(val, "Enable")) {
		struct sigma_ese_alloc allocs[MAX_ESE_ALLOCS];
		int count = MAX_ESE_ALLOCS;

		if (sta_extract_60g_ese(dut, cmd, allocs, &count))
			return -1;
		return sta_set_60g_ese(dut, count, allocs);
	}

	val = get_param(cmd, "MCS_FixedRate");
	if (val) {
		int sta_mcs = atoi(val);

		sigma_dut_print(dut, DUT_MSG_INFO, "Force STA MCS to %d",
				sta_mcs);
		wil6210_set_force_mcs(dut, 1, sta_mcs);

		return SUCCESS_SEND_STATUS;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Invalid sta_set_rfeature(60G)");
	return STATUS_SENT;
}


static int wcn_sta_override_oci(struct sigma_dut *dut, const char *intf,
				const char *oci_frametype, uint32_t oci_freq)
{
#ifdef NL80211_SUPPORT
	struct nl_msg *msg;
	int ret = 0;
	struct nlattr *params;
	struct nlattr *attr;
	int ifindex;
	u8 frame_type;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	if (strcasecmp(oci_frametype, "SAQueryReq") == 0) {
		frame_type = QCA_WLAN_VENDOR_OCI_OVERRIDE_FRAME_SA_QUERY_REQ;
	} else if (strcasecmp(oci_frametype, "SAQueryResp") == 0) {
		frame_type = QCA_WLAN_VENDOR_OCI_OVERRIDE_FRAME_SA_QUERY_RESP;
	} else if (strcasecmp(oci_frametype, "Reassocreq") == 0) {
		frame_type = QCA_WLAN_VENDOR_OCI_OVERRIDE_FRAME_FT_REASSOC_REQ;
	} else {
		sigma_dut_print(dut, DUT_MSG_ERROR, "%s: Unknown frametype %s",
				__func__, oci_frametype);
		return -1;
	}


	if (!(msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
				    NL80211_CMD_VENDOR)) ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_WIFI_TEST_CONFIGURATION) ||
	    !(attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA)) ||
	    !(params = nla_nest_start(
		      msg,
		      QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_OCI_OVERRIDE)) ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_OCI_OVERRIDE_FRAME_TYPE,
		       frame_type) ||
	    nla_put_u32(msg, QCA_WLAN_VENDOR_ATTR_OCI_OVERRIDE_FREQUENCY,
			oci_freq)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd and vendor_data",
				__func__);
		nlmsg_free(msg);
		return -1;
	}
	nla_nest_end(msg, params);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);
	}
	return ret;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"OCI override not possible without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_sta_ignore_csa(struct sigma_dut *dut, const char *intf,
			      uint8_t ignore_csa)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_IGNORE_CSA, ignore_csa);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"IgnoreCSA can't be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_sta_set_rsnxe_used(struct sigma_dut *dut, const char *intf,
				  uint8_t rsnxe_used)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_FT_REASSOCREQ_RSNXE_USED,
		rsnxe_used);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"RSNXE_Used can't be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int wcn_sta_ignore_sa_query_timeout(struct sigma_dut *dut,
					   const char *intf,
					   uint8_t ignore_sa_query_timeout)
{
#ifdef NL80211_SUPPORT
	return wcn_wifi_test_config_set_u8(
		dut, intf,
		QCA_WLAN_VENDOR_ATTR_WIFI_TEST_CONFIG_IGNORE_SA_QUERY_TIMEOUT,
		ignore_sa_query_timeout);
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"Ignore SA Query timeout can't be set without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static enum sigma_cmd_result
cmd_sta_set_rfeature_wpa3(const char *intf, struct sigma_dut *dut,
			  struct sigma_conn *conn,
			  struct sigma_cmd *cmd)
{
	const char *val, *oci_chan, *oci_frametype;

	val = get_param(cmd, "ReassocReq_RSNXE_Used");
	if (val && atoi(val) == 1) {
		if (wifi_chip_type == DRIVER_WCN) {
			if (wcn_sta_set_rsnxe_used(dut, intf, 1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set ft_rsnxe_used");
				return STATUS_SENT_ERROR;
			}
			return SUCCESS_SEND_STATUS;
		} else if (wpa_command(intf, "SET ft_rsnxe_used 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set ft_rsnxe_used");
			return STATUS_SENT_ERROR;
		}
		return SUCCESS_SEND_STATUS;
	}

	oci_chan = get_param(cmd, "OCIChannel");
	oci_frametype = get_param(cmd, "OCIFrameType");
	if (oci_chan && oci_frametype) {
		unsigned int oci_freq = channel_to_freq(dut, atoi(oci_chan));
		char buf[100];

		if (!oci_freq) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Invalid OCIChannel number");
			return STATUS_SENT_ERROR;
		}

		if (wifi_chip_type == DRIVER_WCN &&
		    (strcasecmp(oci_frametype, "SAQueryReq") == 0 ||
		     strcasecmp(oci_frametype, "SAQueryResp") == 0 ||
		     strcasecmp(oci_frametype, "Reassocreq") == 0)) {
			if (wcn_sta_override_oci(dut, intf, oci_frametype,
						 oci_freq)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to override OCI");
				return STATUS_SENT_ERROR;
			}
			return SUCCESS_SEND_STATUS;
		}

		if (strcasecmp(oci_frametype, "eapolM2") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET oci_freq_override_eapol %d", oci_freq);
		} else if (strcasecmp(oci_frametype, "SAQueryReq") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET oci_freq_override_saquery_req %d",
				 oci_freq);
		} else if (strcasecmp(oci_frametype, "SAQueryResp") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET oci_freq_override_saquery_resp %d",
				 oci_freq);
		} else if (strcasecmp(oci_frametype, "GrpKeyM2") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET oci_freq_override_eapol_g2 %d",
				 oci_freq);
		} else if (strcasecmp(oci_frametype, "Reassocreq") == 0) {
			snprintf(buf, sizeof(buf),
				 "SET oci_freq_override_ft_assoc %d",
				 oci_freq);
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported OCIFrameType");
			return STATUS_SENT_ERROR;
		}
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to set oci_freq_override");
			return STATUS_SENT_ERROR;
		}
		return SUCCESS_SEND_STATUS;
	}

	val = get_param(cmd, "IgnoreCSA");
	if (val && atoi(val) == 1) {
		if (wifi_chip_type == DRIVER_WCN) {
			if (wcn_sta_ignore_csa(dut, intf, 1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set ignore CSA");
				return STATUS_SENT_ERROR;
			}
			return SUCCESS_SEND_STATUS;
		}
	}

	val = get_param(cmd, "Deauth_Per_SAQueryResp");
	if (val && atoi(val) == 0) {
		if (wifi_chip_type == DRIVER_WCN) {
			if (wcn_sta_ignore_sa_query_timeout(dut, intf, 1)) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Failed to set ignore SA Query timeout");
				return STATUS_SENT_ERROR;
			}
			return SUCCESS_SEND_STATUS;
		}
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported WPA3 rfeature");
	return STATUS_SENT_ERROR;
}


static enum sigma_cmd_result
cmd_sta_set_rfeature_qm(const char *intf, struct sigma_dut *dut,
			struct sigma_conn *conn, struct sigma_cmd *cmd)
{
	const char *val;

	val = get_param(cmd, "DomainName_Domain");
	if (val) {
		if (strlen(val) >= sizeof(dut->qm_domain_name))
			return ERROR_SEND_STATUS;

		strlcpy(dut->qm_domain_name, val, sizeof(dut->qm_domain_name));
		return SUCCESS_SEND_STATUS;
	}

	val = get_param(cmd, "DSCPPolicy_PolicyID");
	if (val) {
		unsigned int i;
		int policy_id = atoi(val);

		val = get_param(cmd, "DSCPPolicy_RequestType");

		if (!policy_id || !val)
			return INVALID_SEND_STATUS;

		if (dut->num_dscp_status >= ARRAY_SIZE(dut->dscp_status)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,DSCP status list full");
			return STATUS_SENT_ERROR;
		}

		for (i = 0; i < dut->num_dscp_status; i++)
			if (dut->dscp_status[i].id == policy_id)
				break;

		/* New policy configured */
		if (i == dut->num_dscp_status) {
			dut->dscp_status[i].id = policy_id;
			dut->num_dscp_status++;
		}

		dut->dscp_status[i].status = strcasecmp(val, "Remove") ?
			DSCP_POLICY_SUCCESS : DSCP_POLICY_REJECT;

		return SUCCESS_SEND_STATUS;
	}

	send_resp(dut, conn, SIGMA_ERROR,
		  "errorCode,Unsupported QM rfeature");
	return STATUS_SENT_ERROR;
}


static enum sigma_cmd_result
cmd_sta_set_rfeature_loc_r2(const char *intf, struct sigma_dut *dut,
			    struct sigma_conn *conn, struct sigma_cmd *cmd)
{
	const char *rnm_mfp = get_param(cmd, "RNM_MFP");

	if (rnm_mfp) {
		dut->rnm_mfp = atoi(rnm_mfp);
		sigma_dut_print(dut, DUT_MSG_INFO,
				"rnm_mfp value is %d", dut->rnm_mfp);
	}

	return SUCCESS_SEND_STATUS;
}


static enum sigma_cmd_result cmd_sta_set_rfeature(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *prog = get_param(cmd, "Prog");
	const char *val;

	if (!prog)
		prog = get_param(cmd, "Program");

	if (intf == NULL || prog == NULL)
		return -1;

	/* BSS Transition candidate list for BTM query */
	val = get_param(cmd, "Nebor_BSSID");
	if (val && btm_query_candidate_list(dut, conn, cmd) == 0)
		return 0;

	if (strcasecmp(prog, "TDLS") == 0)
		return cmd_sta_set_rfeature_tdls(intf, dut, conn, cmd);

	if (strcasecmp(prog, "VHT") == 0)
		return cmd_sta_set_rfeature_vht(intf, dut, conn, cmd);

	if (strcasecmp(prog, "HE") == 0 || strcasecmp(prog, "EHT") == 0 ||
	    dut->device_mode == MODE_11BE)
		return cmd_sta_set_rfeature_he(intf, dut, conn, cmd);

	if (strcasecmp(prog, "MBO") == 0) {
		val = get_param(cmd, "Cellular_Data_Cap");
		if (val &&
		    mbo_set_cellular_data_capa(dut, conn, intf, atoi(val)) == 0)
			return 0;

		val = get_param(cmd, "Ch_Pref");
		if (val && mbo_set_non_pref_ch_list(dut, conn, intf, cmd) == 0)
			return 0;

		return 1;
	}

	if (strcasecmp(prog, "60GHz") == 0)
		return cmd_sta_set_rfeature_60g(intf, dut, conn, cmd);

	if (strcasecmp(prog, "WPA3") == 0)
		return cmd_sta_set_rfeature_wpa3(intf, dut, conn, cmd);
	if (strcasecmp(prog, "QM") == 0)
		return cmd_sta_set_rfeature_qm(intf, dut, conn, cmd);

	if (strcasecmp(prog, "LOCR2") == 0)
		return cmd_sta_set_rfeature_loc_r2(intf, dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported Prog");
	return 0;
}


static enum sigma_cmd_result cmd_sta_set_radio(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mode = get_param(cmd, "Mode");
	int res;

	if (intf == NULL || mode == NULL)
		return -1;

	if (strcasecmp(mode, "On") == 0)
		res = wpa_command(intf, "SET radio_disabled 0");
	else if (strcasecmp(mode, "Off") == 0)
		res = wpa_command(intf, "SET radio_disabled 1");
	else
		return -1;

	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to change "
			  "radio mode");
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_pwrsave(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *mode = get_param(cmd, "Mode");
	const char *prog = get_param(cmd, "program");
	const char *powersave = get_param(cmd, "powersave");
	int res = 0;

	if (intf == NULL)
		return -1;

	if (prog && strcasecmp(prog, "60GHz") == 0) {
		/*
		 * The CAPI mode parameter does not exist in 60G
		 * unscheduled PS.
		 */
		if (powersave && strcasecmp(powersave, "unscheduled") == 0)
			res = set_ps(intf, dut, 1);
	} else if (prog && get_driver_type(dut) == DRIVER_WCN &&
		   strcasecmp(prog, "HE") == 0) {
		return cmd_sta_set_power_save_he(intf, dut, conn, cmd);
	} else {
		if (mode == NULL)
			return -1;

		if (strcasecmp(mode, "On") == 0)
			res = set_ps(intf, dut, 1);
		else if (strcasecmp(mode, "Off") == 0)
			res = set_ps(intf, dut, 0);
		else
			return -1;
	}

	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to change "
			  "power save mode");
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_bssid_pool(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *bssid;
	int res;
	char *buf;
	size_t buf_len;

	val = get_param(cmd, "BSSID_FILTER");
	if (val == NULL)
		return -1;

	bssid = get_param(cmd, "BSSID_List");
	if (atoi(val) == 0 || bssid == NULL) {
		/* Disable BSSID filter */
		if (wpa_command(intf, "SET bssid_filter ")) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed "
				  "to disable BSSID filter");
			return 0;
		}

		return 1;
	}

	buf_len = 100 + strlen(bssid);
	buf = malloc(buf_len);
	if (buf == NULL)
		return -1;

	snprintf(buf, buf_len, "SET bssid_filter %s", bssid);
	res = wpa_command(intf, buf);
	free(buf);
	if (res) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to enable "
			  "BSSID filter");
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_reset_parm(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val;

	/* TODO: ARP */

	val = get_param(cmd, "HS2_CACHE_PROFILE");
	if (val && strcasecmp(val, "All") == 0)
		hs2_clear_credentials(intf);

	return 1;
}


static enum sigma_cmd_result cmd_sta_get_key(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *key_type = get_param(cmd, "KeyType");
	char buf[100], resp[200];

	if (key_type == NULL)
		return -1;

	if (strcasecmp(key_type, "GTK") == 0) {
		if (wpa_command_resp(intf, "GET gtk", buf, sizeof(buf)) < 0 ||
		    strncmp(buf, "FAIL", 4) == 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not fetch current GTK");
			return 0;
		}
		snprintf(resp, sizeof(resp), "KeyValue,%s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	} else {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Unsupported "
			  "KeyType");
		return 0;
	}

	return 1;
}


static int hs2_set_policy(struct sigma_dut *dut)
{
#ifdef ANDROID
	system("ip rule del prio 23000");
	if (system("ip rule add from all lookup main prio 23000") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to run:ip rule add from all lookup main prio");
		return -1;
	}
	if (system("ip route flush cache") != 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to run ip route flush cache");
		return -1;
	}
	return 1;
#else /* ANDROID */
	return 0;
#endif /* ANDROID */
}


static enum sigma_cmd_result cmd_sta_hs2_associate(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val = get_param(cmd, "Ignore_blacklist");
	const char *band = get_param(cmd, "Band");
	struct wpa_ctrl *ctrl;
	int res, r;
	char bssid[20], ssid[40], resp[100], buf[100], blacklisted[100];
	int tries = 0;
	int ignore_blacklist = 0;
	const char *events[] = {
		"CTRL-EVENT-CONNECTED",
		"INTERWORKING-BLACKLISTED",
		"INTERWORKING-NO-MATCH",
		NULL
	};

	start_sta_mode(dut);

	if (band) {
		if (strcmp(band, "2.4") == 0) {
			wpa_command(intf, "SET setband 2G");
		} else if (strcmp(band, "5") == 0) {
			wpa_command(intf, "SET setband 5G");
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Unsupported band");
			return 0;
		}
	}

	blacklisted[0] = '\0';
	if (val && atoi(val))
		ignore_blacklist = 1;

try_again:
	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	tries++;
	if (wpa_command(intf, "INTERWORKING_SELECT auto")) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to start "
			  "Interworking connection");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	buf[0] = '\0';
	while (1) {
		char *pos;
		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		pos = strstr(buf, "INTERWORKING-BLACKLISTED");
		if (!pos)
			break;
		pos += 25;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Found blacklisted AP: %s",
				pos);
		if (!blacklisted[0])
			memcpy(blacklisted, pos, strlen(pos) + 1);
	}

	if (ignore_blacklist && blacklisted[0]) {
		char *end;
		end = strchr(blacklisted, ' ');
		if (end)
			*end = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Try to connect to a blacklisted network: %s",
				blacklisted);
		r = snprintf(buf, sizeof(buf), "INTERWORKING_CONNECT %s",
			     blacklisted);
		if (r < 0 || r >= sizeof(buf) || wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Failed to start Interworking connection to blacklisted network");
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
			return 0;
		}
		res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
					buf, sizeof(buf));
	}

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "connect");
		return 0;
	}

	if (strstr(buf, "INTERWORKING-NO-MATCH") ||
	    strstr(buf, "INTERWORKING-BLACKLISTED")) {
		if (tries < 2) {
			sigma_dut_print(dut, DUT_MSG_INFO, "No match found - try again to verify no APs were missed in the scan");
			goto try_again;
		}
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,No network with "
			  "matching credentials found");
		return 0;
	}

	if (get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0 ||
	    get_wpa_status(intf, "ssid", ssid, sizeof(ssid)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "get current BSSID/SSID");
		return 0;
	}

	snprintf(resp, sizeof(resp), "SSID,%s,BSSID,%s", ssid, bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	hs2_set_policy(dut);
	return 0;
}


static enum sigma_cmd_result cmd_sta_hs2_venue_info(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *display = get_param(cmd, "Display");
	struct wpa_ctrl *ctrl;
	char buf[300], params[400], *pos;
	char bssid[20];
	int info_avail = 0;
	unsigned int old_timeout;
	int res;
	const char *events[] = { "RX-VENUE-URL", "ANQP-QUERY-DONE", NULL };

	if (get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not get current BSSID");
		return 0;
	}
	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -2;
	}

	snprintf(buf, sizeof(buf), "ANQP_GET %s 277", bssid);
	wpa_command(intf, buf);

	res = get_wpa_cli_event(dut, ctrl, "GAS-QUERY-DONE", buf, sizeof(buf));
	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not complete GAS query");
		goto fail;
	}

	old_timeout = dut->default_timeout;
	dut->default_timeout = 2;
	for (;;) {
		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		if (res < 0)
			break;
		if (strstr(buf, "ANQP-QUERY-DONE") != NULL) {
			res = -1;
			break;
		}
		pos = strchr(buf, ' ');
		if (!pos)
			continue;
		pos++;
		pos = strchr(pos, ' ');
		if (!pos)
			continue;
		pos++;

		if (strncmp(pos, "https://", 8) == 0)
			break;

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Ignore non-HTTPS venue URL: %s", pos);
	}
	dut->default_timeout = old_timeout;
	if (res < 0)
		goto done;
	info_avail = 1;
	snprintf(params, sizeof(params), "browser %s", pos);

	if (display && strcasecmp(display, "Yes") == 0) {
		pid_t pid;

		pid = fork();
		if (pid < 0) {
			perror("fork");
			return -1;
		}

		if (pid == 0) {
			run_hs20_osu(dut, params);
			exit(0);
		}
	}

done:
	snprintf(buf, sizeof(buf), "Info_available,%s",
		 info_avail ? "Yes" : "No");
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
fail:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	return 0;
}


static int sta_add_credential_uname_pwd(struct sigma_dut *dut,
					struct sigma_conn *conn,
					const char *ifname,
					struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "REALM");
	if (val && set_cred_quoted(ifname, id, "realm", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "realm");
		return 0;
	}

	val = get_param(cmd, "HOME_FQDN");
	if (val && set_cred_quoted(ifname, id, "domain", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "home_fqdn");
		return 0;
	}

	val = get_param(cmd, "Username");
	if (val && set_cred_quoted(ifname, id, "username", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "username");
		return 0;
	}

	val = get_param(cmd, "Password");
	if (val && set_cred_quoted(ifname, id, "password", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "password");
		return 0;
	}

	val = get_param(cmd, "ROOT_CA");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg), "ErrorCode,ROOT_CA "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "ca_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set root CA");
			return 0;
		}
	}

	return 1;
}


static int update_devdetail_imsi(struct sigma_dut *dut, const char *imsi)
{
	FILE *in, *out;
	char buf[500];
	int found = 0;

	in = fopen("devdetail.xml", "r");
	if (in == NULL)
		return -1;
	out = fopen("devdetail.xml.tmp", "w");
	if (out == NULL) {
		fclose(in);
		return -1;
	}

	while (fgets(buf, sizeof(buf), in)) {
		char *pos = strstr(buf, "<IMSI>");
		if (pos) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Updated DevDetail IMSI to %s",
					imsi);
			pos += 6;
			*pos = '\0';
			fprintf(out, "%s%s</IMSI>\n", buf, imsi);
			found++;
		} else {
			fprintf(out, "%s", buf);
		}
	}

	fclose(out);
	fclose(in);
	if (found)
		rename("devdetail.xml.tmp", "devdetail.xml");
	else
		unlink("devdetail.xml.tmp");

	return 0;
}


static int sta_add_credential_sim(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  const char *ifname, struct sigma_cmd *cmd)
{
	const char *val, *imsi = NULL;
	int id;
	char buf[200];
	int res;
	const char *pos;
	size_t mnc_len;
	char plmn_mcc[4];
	char plmn_mnc[4];

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "PLMN_MCC");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing PLMN_MCC");
		return 0;
	}
	if (strlen(val) != 3) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Invalid MCC");
		return 0;
	}
	snprintf(plmn_mcc, sizeof(plmn_mcc), "%s", val);

	val = get_param(cmd, "PLMN_MNC");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Missing PLMN_MNC");
		return 0;
	}
	if (strlen(val) != 2 && strlen(val) != 3) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Invalid MNC");
		return 0;
	}
	snprintf(plmn_mnc, sizeof(plmn_mnc), "%s", val);

	val = get_param(cmd, "IMSI");
	if (val == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Missing SIM "
			  "IMSI");
		return 0;
	}

	imsi = pos = val;

	if (strncmp(plmn_mcc, pos, 3) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MCC mismatch");
		return 0;
	}
	pos += 3;

	mnc_len = strlen(plmn_mnc);
	if (mnc_len < 2) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MNC not set");
		return 0;
	}

	if (strncmp(plmn_mnc, pos, mnc_len) != 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,MNC mismatch");
		return 0;
	}
	pos += mnc_len;

	res = snprintf(buf, sizeof(buf), "%s%s-%s",plmn_mcc, plmn_mnc, pos);
	if (res < 0 || res >= (int) sizeof(buf))
		return -1;
	if (set_cred_quoted(ifname, id, "imsi", buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
			  "not set IMSI");
		return 0;
	}

	val = get_param(cmd, "Password");
	if (val && set_cred_quoted(ifname, id, "milenage", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
			  "not set password");
		return 0;
	}

	if (is_passpoint_r2_or_newer(dut->program)) {
		/*
		 * Set provisioning_sp for the test cases where SIM/USIM
		 * provisioning is used.
		 */
		if (val && set_cred_quoted(ifname, id, "provisioning_sp",
					   "wi-fi.org") < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set provisioning_sp");
			return 0;
		}

		update_devdetail_imsi(dut, imsi);
	}

	return 1;
}


static int sta_add_credential_cert(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   const char *ifname,
				   struct sigma_cmd *cmd)
{
	const char *val;
	int id;

	id = add_cred(ifname);
	if (id < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Adding credential %d", id);

	val = get_param(cmd, "prefer");
	if (val && atoi(val) > 0)
		set_cred(ifname, id, "priority", "1");

	val = get_param(cmd, "REALM");
	if (val && set_cred_quoted(ifname, id, "realm", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "realm");
		return 0;
	}

	val = get_param(cmd, "HOME_FQDN");
	if (val && set_cred_quoted(ifname, id, "domain", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "home_fqdn");
		return 0;
	}

	val = get_param(cmd, "Username");
	if (val && set_cred_quoted(ifname, id, "username", val) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not set "
			  "username");
		return 0;
	}

	val = get_param(cmd, "clientCertificate");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg),
				 "ErrorCode,clientCertificate "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "client_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set client_cert");
			return 0;
		}
		if (set_cred_quoted(ifname, id, "private_key", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set private_key");
			return 0;
		}
	}

	val = get_param(cmd, "ROOT_CA");
	if (val) {
		char fname[200];
		snprintf(fname, sizeof(fname), "%s/%s", sigma_cert_path, val);
#ifdef __linux__
		if (!file_exists(fname)) {
			char msg[300];
			snprintf(msg, sizeof(msg), "ErrorCode,ROOT_CA "
				 "file (%s) not found", fname);
			send_resp(dut, conn, SIGMA_ERROR, msg);
			return 0;
		}
#endif /* __linux__ */
		if (set_cred_quoted(ifname, id, "ca_cert", fname) < 0) {
			send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could "
				  "not set root CA");
			return 0;
		}
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_add_credential(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *type;

	start_sta_mode(dut);

	type = get_param(cmd, "Type");
	if (!type)
		return -1;

	if (strcasecmp(type, "uname_pwd") == 0)
		return sta_add_credential_uname_pwd(dut, conn, intf, cmd);

	if (strcasecmp(type, "sim") == 0)
		return sta_add_credential_sim(dut, conn, intf, cmd);

	if (strcasecmp(type, "cert") == 0)
		return sta_add_credential_cert(dut, conn, intf, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported credential "
		  "type");
	return 0;
}


static enum sigma_cmd_result cmd_sta_scan(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *val, *bssid, *ssid, *scan_freq, *short_ssid;
	char buf[4096], scan_res[20];
	char ssid_hex[65];
	int wildcard_ssid = 0;
	int res;
	enum sigma_cmd_result status;
	struct wpa_ctrl *ctrl = NULL;

	start_sta_mode(dut);

	val = get_param(cmd, "GetParameter");
	if (val && strcmp(val, "SSID_BSSID") == 0) {
		if (get_wpa_ssid_bssid(dut, get_station_ifname(dut),
				       buf, sizeof(buf)) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Could not get ssid bssid");
			return ERROR_SEND_STATUS;
		}

		sigma_dut_print(dut, DUT_MSG_INFO, "%s", buf);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return STATUS_SENT;
	}

	val = get_param(cmd, "HESSID");
	if (val) {
		res = snprintf(buf, sizeof(buf), "SET hessid %s", val);
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "ACCS_NET_TYPE");
	if (val) {
		res = snprintf(buf, sizeof(buf), "SET access_network_type %s",
			       val);
		if (res < 0 || res >= (int) sizeof(buf))
			return -1;
		wpa_command(intf, buf);
	}

	val = get_param(cmd, "PerSTAProfile_Present");
	sta_set_exclude_sta_prof_ml_ie(dut, intf,
				       val && strcasecmp(val, "false") == 0);

	if (get_param(cmd, "RxMac"))
		sta_set_scan_unicast_probe(dut, intf, 1);

	bssid = get_param(cmd, "Bssid");
	ssid = get_param(cmd, "Ssid");
	if (!bssid)
		bssid = get_param(cmd, "RxMac");

	if (ssid && strcasecmp(ssid, "ZeroLength") == 0 &&
	    dut->device_type == STA_testbed) {
		ssid = NULL;
		wildcard_ssid = 1;
	}

	if (ssid) {
		if (2 * strlen(ssid) >= sizeof(ssid_hex)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Too long SSID");
			return 0;
		}
		ascii2hexstr(ssid, ssid_hex);
	}

	short_ssid = get_param(cmd, "ShortSSID");
	if (short_ssid) {
		uint32_t short_ssid_hex;

		short_ssid_hex = strtoul(short_ssid, NULL, 16);
		short_ssid_hex = ((short_ssid_hex & 0xFF) << 24) |
			(((short_ssid_hex >> 8) & 0xFF) << 16) |
			(((short_ssid_hex >> 16) & 0xFF) << 8) |
			((short_ssid_hex >> 24) & 0xFF);

		res = snprintf(buf, sizeof(buf),
			       "VENDOR_ELEM_ADD 14 ff053a%08x",
			       short_ssid_hex);
		if (res < 0 || res >= (int) sizeof(buf) ||
		    wpa_command(intf, buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to add short SSID");
			return STATUS_SENT_ERROR;
		}
	}

	scan_freq = get_param(cmd, "ChnlFreq");
	if (scan_freq) {
		if (strcasecmp(scan_freq, "2G") == 0)
			scan_freq = "2412-2462";
		else if (strcasecmp(scan_freq, "5G") == 0)
			scan_freq = "5180-5925";
	}

	val = get_param(cmd, "WaitCompletion");
	if (val && atoi(val) == 1) {
		ctrl = open_wpa_mon(intf);
		if (!ctrl) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to open monitor socket");
			return STATUS_SENT_ERROR;
		}
	}

	res = snprintf(buf, sizeof(buf), "SCAN%s%s%s%s%s%s%s",
			bssid ? " bssid=": "",
			bssid ? bssid : "",
			ssid ? " ssid " : "",
			ssid ? ssid_hex : "",
			wildcard_ssid ? " wildcard_ssid=1" : "",
			scan_freq ? " freq=" : "",
			scan_freq ? scan_freq : "");
	if (res < 0 || res >= (int) sizeof(buf)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not build scan command");
		status = STATUS_SENT_ERROR;
		goto remove_s_ssid;
	}

	res = wpa_command_resp(intf, buf, scan_res, sizeof(scan_res));
	if (strncmp(scan_res, "FAIL-BUSY", 9) == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Scan request rejected with busy status, abort ongoing scan and try again");
		wpa_command(intf, "ABORT_SCAN");
		res = wpa_command(intf, buf);
	}

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "errorCode,Could not start "
			  "scan");
		status = STATUS_SENT_ERROR;
	} else {
		status = SUCCESS_SEND_STATUS;
	}

remove_s_ssid:
	if (short_ssid && wpa_command(intf, "VENDOR_ELEM_REMOVE 14 *"))
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to delete vendor element");

	if (ctrl) {
		if (status == SUCCESS_SEND_STATUS) {
			res = get_wpa_cli_event(dut, ctrl,
						"CTRL-EVENT-SCAN-RESULTS",
						buf, sizeof(buf));
			if (res < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,scan did not complete");
				status = STATUS_SENT_ERROR;
			}
		}

		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
	}

	return status;
}


static enum sigma_cmd_result cmd_sta_scan_bss(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *bssid;
	char buf[4096], *pos;
	int freq, chan;
	char *ssid;
	char resp[100];
	int res;
	struct wpa_ctrl *ctrl;

	bssid = get_param(cmd, "BSSID");
	if (!bssid) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "errorCode,BSSID argument is missing");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -1;
	}

	if (wpa_command(intf, "SCAN TYPE=ONLY")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Could not start scan");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-SCAN-RESULTS",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Scan did not complete");
		return 0;
	}

	snprintf(buf, sizeof(buf), "BSS %s", bssid);
	if (wpa_command_resp(intf, buf, buf, sizeof(buf)) < 0 ||
	    strncmp(buf, "id=", 3) != 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Specified BSSID not found");
		return 0;
	}

	pos = strstr(buf, "\nfreq=");
	if (!pos) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Channel not found");
		return 0;
	}
	freq = atoi(pos + 6);
	chan = freq_to_channel(freq);

	pos = strstr(buf, "\nssid=");
	if (!pos) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,SSID not found");
		return 0;
	}
	ssid = pos + 6;
	pos = strchr(ssid, '\n');
	if (pos)
		*pos = '\0';
	snprintf(resp, sizeof(resp), "ssid,%s,bsschannel,%d", ssid, chan);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static enum sigma_cmd_result cmd_sta_set_systime(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
#ifdef __linux__
	struct timeval tv;
	struct tm tm;
	time_t t;
	const char *val;
	int v;

	wpa_command(get_station_ifname(dut), "PMKSA_FLUSH");

	memset(&tm, 0, sizeof(tm));
	val = get_param(cmd, "seconds");
	if (val)
		tm.tm_sec = atoi(val);
	val = get_param(cmd, "minutes");
	if (val)
		tm.tm_min = atoi(val);
	val = get_param(cmd, "hours");
	if (val)
		tm.tm_hour = atoi(val);
	val = get_param(cmd, "date");
	if (val)
		tm.tm_mday = atoi(val);
	val = get_param(cmd, "month");
	if (val) {
		v = atoi(val);
		if (v < 1 || v > 12) {
			send_resp(dut, conn, SIGMA_INVALID,
				  "errorCode,Invalid month");
			return 0;
		}
		tm.tm_mon = v - 1;
	}
	val = get_param(cmd, "year");
	if (val) {
		int year = atoi(val);
#ifdef ANDROID
		if (year > 2035)
			year = 2035; /* years beyond 2035 not supported */
#endif /* ANDROID */
		tm.tm_year = year - 1900;
	}
	t = mktime(&tm);
	if (t == (time_t) -1) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Invalid date or time");
		return 0;
	}

	memset(&tv, 0, sizeof(tv));
	tv.tv_sec = t;

	if (settimeofday(&tv, NULL) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "settimeofday failed: %s",
				strerror(errno));
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Failed to set time");
		return 0;
	}

	return 1;
#endif /* __linux__ */

	return -1;
}


static enum sigma_cmd_result cmd_sta_osu(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *name, *osu_ssid, *val;
	int prod_ess_assoc = 1;
	char buf[300], bssid[100], ssid[100];
	int res;
	struct wpa_ctrl *ctrl;

	name = get_param(cmd, "osuFriendlyName");
	osu_ssid = get_param(cmd, "osu_ssid");

	val = get_param(cmd, "ProdESSAssoc");
	if (val)
		prod_ess_assoc = atoi(val);

	kill_dhcp_client(dut, intf);
	if (start_dhcp_client(dut, intf) < 0)
		return -2;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trigger OSU");
	mkdir("Logs", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	res = snprintf(buf, sizeof(buf),
		       "%s %s%s%s %s%s%s signup osu-ca.pem",
		       prod_ess_assoc ? "" : "-N",
		       name ? "-O'" : "", name ? name : "",
		       name ? "'" : "",
		       osu_ssid ? "-o'" : "", osu_ssid ? osu_ssid : "",
		       osu_ssid ? "'" : "");

	hs2_set_policy(dut);
	if (run_hs20_osu(dut, buf) < 0) {
		FILE *f;

		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to complete OSU");

		f = fopen("hs20-osu-client.res", "r");
		if (f) {
			char resp[400], res[300], *pos;
			if (!fgets(res, sizeof(res), f))
				res[0] = '\0';
			pos = strchr(res, '\n');
			if (pos)
				*pos = '\0';
			fclose(f);
			sigma_dut_summary(dut, "hs20-osu-client provisioning failed: %s",
					  res);
			snprintf(resp, sizeof(resp), "notify-send '%s'", res);
			if (system(resp) != 0) {
			}
			snprintf(resp, sizeof(resp),
				 "SSID,,BSSID,,failureReason,%s", res);
			send_resp(dut, conn, SIGMA_COMPLETE, resp);
			return 0;
		}

		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

	if (!prod_ess_assoc)
		goto report;

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -1;
	}

	res = get_wpa_cli_event(dut, ctrl, "CTRL-EVENT-CONNECTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to connect to "
				"network after OSU");
		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

report:
	if (get_wpa_status(intf, "bssid", bssid, sizeof(bssid)) < 0 ||
	    get_wpa_status(intf, "ssid", ssid, sizeof(ssid)) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to get BSSID/SSID");
		send_resp(dut, conn, SIGMA_COMPLETE, "SSID,,BSSID,");
		return 0;
	}

	snprintf(buf, sizeof(buf), "SSID,%s,BSSID,%s", ssid, bssid);
	send_resp(dut, conn, SIGMA_COMPLETE, buf);
	return 0;
}


static enum sigma_cmd_result cmd_sta_policy_update(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	const char *val;
	int timeout = 120;

	val = get_param(cmd, "PolicyUpdate");
	if (val == NULL || atoi(val) == 0)
		return 1; /* No operation requested */

	val = get_param(cmd, "Timeout");
	if (val)
		timeout = atoi(val);

	if (timeout) {
		/* TODO: time out the command and return
		 * PolicyUpdateStatus,TIMEOUT if needed. */
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trigger policy update");
	mkdir("Logs", S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (run_hs20_osu(dut, "pol_upd fqdn=wi-fi.org") < 0) {
		send_resp(dut, conn, SIGMA_COMPLETE, "PolicyUpdateStatus,FAIL");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "PolicyUpdateStatus,SUCCESS");
	return 0;
}


static enum sigma_cmd_result cmd_sta_er_config(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "Bssid");
	const char *ssid = get_param(cmd, "SSID");
	const char *security = get_param(cmd, "Security");
	const char *passphrase = get_param(cmd, "Passphrase");
	const char *pin = get_param(cmd, "PIN");
	char buf[1000];
	char ssid_hex[200], passphrase_hex[200];
	const char *keymgmt, *cipher;

	if (intf == NULL)
		intf = get_main_ifname(dut);

	if (!bssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Bssid argument");
		return 0;
	}

	if (!ssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing SSID argument");
		return 0;
	}

	if (!security) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Security argument");
		return 0;
	}

	if (!passphrase) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Passphrase argument");
		return 0;
	}

	if (!pin) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing PIN argument");
		return 0;
	}

	if (2 * strlen(ssid) >= sizeof(ssid_hex) ||
	    2 * strlen(passphrase) >= sizeof(passphrase_hex)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Too long SSID/passphrase");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (strcasecmp(security, "wpa2-psk") == 0) {
		keymgmt = "WPA2PSK";
		cipher = "CCMP";
	} else {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported Security value");
		return 0;
	}

	ascii2hexstr(ssid, ssid_hex);
	ascii2hexstr(passphrase, passphrase_hex);
	snprintf(buf, sizeof(buf), "WPS_REG %s %s %s %s %s %s",
		 bssid, pin, ssid_hex, keymgmt, cipher, passphrase_hex);

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start registrar");
		return 0;
	}

	snprintf(dut->er_oper_bssid, sizeof(dut->er_oper_bssid), "%s", bssid);
	dut->er_oper_performed = 1;

	return wps_connection_event(dut, conn, ctrl, intf, 0);
}


static enum sigma_cmd_result
cmd_sta_wps_connect_pw_token(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "Bssid");
	char buf[100];

	if (!bssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Bssid argument");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	snprintf(buf, sizeof(buf), "WPS_NFC %s", bssid);

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start registrar");
		return 0;
	}

	return wps_connection_event(dut, conn, ctrl, intf, 0);
}


static enum sigma_cmd_result cmd_start_wps_registration(struct sigma_dut *dut,
							struct sigma_conn *conn,
							struct sigma_cmd *cmd)
{
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *network_mode = get_param(cmd, "network_mode");
	const char *config_method = get_param(cmd, "WPSConfigMethod");
	const char *role;
	int res;
	char buf[256];
	const char *events[] = {
		"CTRL-EVENT-CONNECTED",
		"WPS-OVERLAP-DETECTED",
		"WPS-TIMEOUT",
		"WPS-FAIL",
		NULL
	};
	int id = 0;

	/* 60G WPS tests do not pass Interface parameter */
	if (!intf)
		intf = get_main_ifname(dut);

	if (dut->mode == SIGMA_MODE_AP)
		return ap_wps_registration(dut, conn, cmd);

	if (config_method) {
		/* WFA_CS_WPS_PIN_KEYPAD mode is set when using the
		 * sta_wps_enter_pin before calling start_wps_registration. */
		if (strcasecmp(config_method, "PBC") == 0)
			dut->wps_method = WFA_CS_WPS_PBC;
	}
	if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,WPS parameters not yet set");
		return STATUS_SENT;
	}

	/* Make sure WPS is enabled (also for STA mode) */
	dut->wps_disable = 0;

	if (dut->band == WPS_BAND_60G && network_mode &&
	    strcasecmp(network_mode, "PBSS") == 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Set PBSS network mode, network id %d", id);
		if (set_network(get_station_ifname(dut), id, "pbss", "1") < 0)
			return -2;
	}

	if (dut->force_rsn_ie) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Force RSN_IE: %d",
				dut->force_rsn_ie);
		if (sta_60g_force_rsn_ie(dut, dut->force_rsn_ie) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to force RSN_IE");
			return ERROR_SEND_STATUS;
		}
	}

	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return -2;
	}

	role = get_param(cmd, "WpsRole");
	if (!role) {
		send_resp(dut, conn, SIGMA_INVALID,
			  "ErrorCode,WpsRole not provided");
		goto fail;
	}

	if (strcasecmp(role, "Enrollee") != 0) {
		/* Registrar role for STA not supported */
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported WpsRole value");
		goto fail;
	}

	if (is_60g_sigma_dut(dut)) {
		if (dut->wps_method == WFA_CS_WPS_PBC)
			snprintf(buf, sizeof(buf), "WPS_PBC");
		else /* WFA_CS_WPS_PIN_KEYPAD */
			snprintf(buf, sizeof(buf), "WPS_PIN any %s",
				 dut->wps_pin);
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to start WPS");
			goto fail;
		}
		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,WPS connection did not complete");
			goto fail;
		}
		if (strstr(buf, "WPS-TIMEOUT")) {
			send_resp(dut, conn, SIGMA_COMPLETE, "WpsState,NoPeer");
		} else if (strstr(buf, "WPS-OVERLAP-DETECTED")) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "WpsState,OverlapSession");
		} else if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "WpsState,Successful");
		} else {
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "WpsState,Failure");
		}
	} else {
		if (dut->wps_method == WFA_CS_WPS_PBC) {
			if (wpa_command(intf, "WPS_PBC") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "ErrorCode,Failed to enable PBC");
				goto fail;
			}
		} else {
			/* TODO: PIN method */
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported WpsConfigMethod value");
			goto fail;
		}
		res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));
		if (res < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,WPS connection did not complete");
			goto fail;
		}
		if (strstr(buf, "WPS-TIMEOUT")) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,NoPeer");
		} else if (strstr(buf, "WPS-OVERLAP-DETECTED")) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,OverlapSession");
		} else if (strstr(buf, "CTRL-EVENT-CONNECTED")) {
			send_resp(dut, conn, SIGMA_COMPLETE, "Successful");
		} else {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,WPS operation failed");
		}
	}

fail:
	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);
	return 0;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


void sta_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_get_ip_config", req_intf,
			  cmd_sta_get_ip_config);
	sigma_dut_reg_cmd("sta_set_ip_config", req_intf,
			  cmd_sta_set_ip_config);
	sigma_dut_reg_cmd("sta_get_info", req_intf, cmd_sta_get_info);
	sigma_dut_reg_cmd("sta_get_mac_address", req_intf,
			  cmd_sta_get_mac_address);
	sigma_dut_reg_cmd("sta_is_connected", req_intf, cmd_sta_is_connected);
	sigma_dut_reg_cmd("sta_verify_ip_connection", req_intf,
			  cmd_sta_verify_ip_connection);
	sigma_dut_reg_cmd("sta_get_bssid", req_intf, cmd_sta_get_bssid);
	sigma_dut_reg_cmd("sta_set_encryption", req_intf,
			  cmd_sta_set_encryption);
	sigma_dut_reg_cmd("sta_set_psk", req_intf, cmd_sta_set_psk);
	sigma_dut_reg_cmd("sta_set_eaptls", req_intf, cmd_sta_set_eaptls);
	sigma_dut_reg_cmd("sta_set_eapttls", req_intf, cmd_sta_set_eapttls);
	sigma_dut_reg_cmd("sta_set_eapsim", req_intf, cmd_sta_set_eapsim);
	sigma_dut_reg_cmd("sta_set_peap", req_intf, cmd_sta_set_peap);
	sigma_dut_reg_cmd("sta_set_eapfast", req_intf, cmd_sta_set_eapfast);
	sigma_dut_reg_cmd("sta_set_eapaka", req_intf, cmd_sta_set_eapaka);
	sigma_dut_reg_cmd("sta_set_eapakaprime", req_intf,
			  cmd_sta_set_eapakaprime);
	sigma_dut_reg_cmd("sta_set_security", req_intf, cmd_sta_set_security);
	sigma_dut_reg_cmd("sta_set_uapsd", req_intf, cmd_sta_set_uapsd);
	/* TODO: sta_set_ibss */
	/* TODO: sta_set_mode */
	sigma_dut_reg_cmd("sta_set_wmm", req_intf, cmd_sta_set_wmm);
	sigma_dut_reg_cmd("sta_associate", req_intf, cmd_sta_associate);
	/* TODO: sta_up_load */
	sigma_dut_reg_cmd("sta_preset_testparameters", req_intf,
			  cmd_sta_preset_testparameters);
	/* TODO: sta_set_system */
	sigma_dut_reg_cmd("sta_set_11n", req_intf, cmd_sta_set_11n);
	/* TODO: sta_set_rifs_test */
	sigma_dut_reg_cmd("sta_set_wireless", req_intf, cmd_sta_set_wireless);
	sigma_dut_reg_cmd("sta_send_addba", req_intf, cmd_sta_send_addba);
	/* TODO: sta_send_coexist_mgmt */
	sigma_dut_reg_cmd("sta_disconnect", req_intf, cmd_sta_disconnect);
	sigma_dut_reg_cmd("sta_reassoc", req_intf, cmd_sta_reassoc);
	sigma_dut_reg_cmd("sta_reassociate", req_intf, cmd_sta_reassoc);
	sigma_dut_reg_cmd("sta_reset_default", req_intf,
			  cmd_sta_reset_default);
	sigma_dut_reg_cmd("sta_send_frame", req_intf, cmd_sta_send_frame);
	sigma_dut_reg_cmd("sta_set_macaddr", req_intf, cmd_sta_set_macaddr);
	sigma_dut_reg_cmd("sta_set_rfeature", req_intf, cmd_sta_set_rfeature);
	sigma_dut_reg_cmd("sta_set_radio", req_intf, cmd_sta_set_radio);
	sigma_dut_reg_cmd("sta_set_pwrsave", req_intf, cmd_sta_set_pwrsave);
	sigma_dut_reg_cmd("sta_set_power_save", req_intf, cmd_sta_set_pwrsave);
	sigma_dut_reg_cmd("sta_bssid_pool", req_intf, cmd_sta_bssid_pool);
	sigma_dut_reg_cmd("sta_reset_parm", req_intf, cmd_sta_reset_parm);
	sigma_dut_reg_cmd("sta_get_key", req_intf, cmd_sta_get_key);
	sigma_dut_reg_cmd("sta_hs2_associate", req_intf,
			  cmd_sta_hs2_associate);
	sigma_dut_reg_cmd("sta_hs2_venue_info", req_intf,
			  cmd_sta_hs2_venue_info);
	sigma_dut_reg_cmd("sta_add_credential", req_intf,
			  cmd_sta_add_credential);
	sigma_dut_reg_cmd("sta_scan", req_intf, cmd_sta_scan);
	sigma_dut_reg_cmd("sta_scan_bss", req_intf, cmd_sta_scan_bss);
	sigma_dut_reg_cmd("sta_set_systime", NULL, cmd_sta_set_systime);
	sigma_dut_reg_cmd("sta_osu", req_intf, cmd_sta_osu);
	sigma_dut_reg_cmd("sta_policy_update", req_intf, cmd_sta_policy_update);
	sigma_dut_reg_cmd("sta_er_config", NULL, cmd_sta_er_config);
	sigma_dut_reg_cmd("sta_wps_connect_pw_token", req_intf,
			  cmd_sta_wps_connect_pw_token);
	sigma_dut_reg_cmd("sta_exec_action", NULL, cmd_sta_exec_action);
	sigma_dut_reg_cmd("sta_get_events", req_intf, cmd_sta_get_events);
	sigma_dut_reg_cmd("sta_get_parameter", req_intf, cmd_sta_get_parameter);
	sigma_dut_reg_cmd("start_wps_registration", NULL,
			  cmd_start_wps_registration);
}
