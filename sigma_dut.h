/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2021, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#ifndef SIGMA_DUT_H
#define SIGMA_DUT_H

#ifdef __GNUC__
#define _GNU_SOURCE	1
#endif

#ifdef ANDROID_MDNS
#include "dns_sd.h"
#endif /* ANDROID_MDNS */
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <net/if.h>
#ifdef __QNXNTO__
#include <sys/select.h>
#include <net/if_ether.h>
#endif /* __QNXNTO__ */
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#ifdef NL80211_SUPPORT
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>
#include <netlink/genl/genl.h>
#include "qca-vendor_copy.h"
#include "nl80211_copy.h"
#endif /* NL80211_SUPPORT */
#ifdef ANDROID_WIFI_HAL
/* avoid duplicate definitions from wifi_hal.h causing issues */
#define u32 wifi_hal_u32
#define u16 wifi_hal_u16
#define u8 wifi_hal_u8
#include "wifi_hal.h"
#undef u32
#undef u16
#undef u8
#endif /*ANDROID_WIFI_HAL*/

#ifdef NL80211_SUPPORT
#ifndef NL_CAPABILITY_VERSION_3_5_0
#define nla_nest_start(msg, attrtype) \
	nla_nest_start(msg, NLA_F_NESTED | (attrtype))
#endif
#endif

#ifdef __GNUC__
#define PRINTF_FORMAT(a,b) __attribute__ ((format (printf, (a), (b))))
#else
#define PRINTF_FORMAT(a,b)
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef SIGMA_TMPDIR
#define SIGMA_TMPDIR "/tmp"
#endif /* SIGMA_TMPDIR */

#ifndef SIGMA_DUT_VER
#define SIGMA_DUT_VER "(unknown)"
#endif /* SIGMA_DUT_VER */

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

#ifndef BIT_ULL
#define BIT_ULL(nr)		(1ULL << (nr))
#endif

#ifndef ETH_P_ARP
#define ETH_P_ARP 0x0806
#endif

#define IPV6_ADDR_LEN 16

struct sigma_dut;

#define MAX_PARAMS 100
#define MAX_RADIO 3

#define NAN_AWARE_IFACE "wifi-aware0"
#define BROADCAST_ADDR "255.255.255.255"

/* Set default operating channel width 80 MHz */
#define VHT_DEFAULT_OPER_CHWIDTH AP_80_VHT_OPER_CHWIDTH

typedef unsigned int u32;
typedef uint16_t u16;
typedef unsigned char u8;

struct ieee80211_hdr_3addr {
	uint16_t frame_control;
	uint16_t duration_id;
	uint8_t addr1[ETH_ALEN];
	uint8_t addr2[ETH_ALEN];
	uint8_t addr3[ETH_ALEN];
	uint16_t seq_ctrl;
} __attribute__((packed));

struct wfa_p2p_attribute {
	uint8_t id;
	uint16_t len;
	uint8_t variable[0];
} __attribute__((packed));

struct dut_hw_modes {
	u16 ht_capab;
	u8 mcs_set[16];
	u8 ampdu_params;
	u32 vht_capab;
	u8 vht_mcs_set[8];
	u8 ap_he_phy_capab[11];
	bool valid;
};

#define WPA_GET_BE32(a) ((((u32) (a)[0]) << 24) | (((u32) (a)[1]) << 16) | \
			 (((u32) (a)[2]) << 8) | ((u32) (a)[3]))
#define WPA_PUT_BE32(a, val)					\
	do {							\
		(a)[0] = (u8) ((((u32) (val)) >> 24) & 0xff);	\
		(a)[1] = (u8) ((((u32) (val)) >> 16) & 0xff);	\
		(a)[2] = (u8) ((((u32) (val)) >> 8) & 0xff);	\
		(a)[3] = (u8) (((u32) (val)) & 0xff);		\
	} while (0)

struct sigma_cmd {
	char *params[MAX_PARAMS];
	char *values[MAX_PARAMS];
	int count;
};

#define MAX_CMD_LEN 4096

struct sigma_conn {
	int s;
	struct sockaddr_in addr;
	socklen_t addrlen;
	char buf[MAX_CMD_LEN + 5];
	int pos;
	int waiting_completion;
};

enum sigma_cmd_result {
	STATUS_SENT_ERROR = -3,
	ERROR_SEND_STATUS = -2,
	INVALID_SEND_STATUS = -1,
	STATUS_SENT = 0,
	SUCCESS_SEND_STATUS = 1
};

struct sigma_cmd_handler {
	struct sigma_cmd_handler *next;
	char *cmd;
	int (*validate)(struct sigma_cmd *cmd);
	/* process return value:
	 * -2 = failed, caller will send status,ERROR
	 * -1 = failed, caller will send status,INVALID
	 * 0 = response already sent
	 * 1 = success, caller will send status,COMPLETE
	 */
	enum sigma_cmd_result (*process)(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd);
};

#define P2P_GRP_ID_LEN 128
#define IP_ADDR_STR_LEN 16

struct wfa_cs_p2p_group {
	struct wfa_cs_p2p_group *next;
	char ifname[IFNAMSIZ];
	int go;
	char grpid[P2P_GRP_ID_LEN];
	char ssid[33];
};

#ifdef CONFIG_TRAFFIC_AGENT

#define MAX_SIGMA_STREAMS 16
#define MAX_SIGMA_STATS 6000

struct sigma_frame_stats {
	unsigned int seqnum;
	unsigned int local_sec;
	unsigned int local_usec;
	unsigned int remote_sec;
	unsigned int remote_usec;
};

struct sigma_stream {
	enum sigma_stream_profile {
		SIGMA_PROFILE_FILE_TRANSFER,
		SIGMA_PROFILE_MULTICAST,
		SIGMA_PROFILE_IPTV,
		SIGMA_PROFILE_TRANSACTION,
		SIGMA_PROFILE_START_SYNC,
		SIGMA_PROFILE_UAPSD
	} profile;
	int sender;
	struct in_addr dst;
	int dst_port;
	struct in_addr src;
	int src_port;
	int frame_rate;
	int duration;
	unsigned int payload_size;
	int start_delay;
	int max_cnt;
	enum sigma_traffic_class {
		SIGMA_TC_VOICE,
		SIGMA_TC_VIDEO,
		SIGMA_TC_BACKGROUND,
		SIGMA_TC_BEST_EFFORT
	} tc;
	int user_priority;
	int user_priority_set;
	int started;
	int no_timestamps;

	int sock;
	pthread_t thr;
	int stop;
	int ta_send_in_progress;
	int trans_proto;

	/* Statistics */
	int tx_act_frames; /*
			    * Number of frames generated by the traffic
			    * generator application. The name is defined in the
			    * Sigma CAPI spec.
			    */
	int tx_frames;
	int rx_frames;
	unsigned long long tx_payload_bytes;
	unsigned long long rx_payload_bytes;
	int out_of_seq_frames;
	struct sigma_frame_stats *stats;
	unsigned int num_stats;
	unsigned int stream_id;

	/* U-APSD */
	unsigned int sta_id;
	unsigned int rx_cookie;
	unsigned int uapsd_sta_tc;
	unsigned int uapsd_rx_state;
	unsigned int uapsd_tx_state;
	unsigned int tx_stop_cnt;
	unsigned int tx_hello_cnt;
	pthread_t uapsd_send_thr;
	pthread_cond_t tx_thr_cond;
	pthread_mutex_t tx_thr_mutex;
	int reset_rx;
	int num_retry;
	char ifname[IFNAMSIZ]; /* ifname from the command */
	struct sigma_dut *dut; /* for traffic agent thread to access context */
	/* console */
	char test_name[9]; /* test case name */
	int can_quit;
	int reset;
	int tos;
};

#endif /* CONFIG_TRAFFIC_AGENT */

/* extended scheduling test */
enum sigma_ese_type {
	ESE_CBAP,
	ESE_SP,
};

struct sigma_ese_alloc {
	unsigned int percent_bi;
	enum sigma_ese_type type;
	unsigned int src_aid, dst_aid;
};

#define ESE_BCAST_AID	255
#define MAX_ESE_ALLOCS	4

#define NUM_AP_AC 4
#define AP_AC_BE 0
#define AP_AC_BK 1
#define AP_AC_VI 2
#define AP_AC_VO 3

#define MAX_WLAN_TAGS 3
#define MBO_MAX_PREF_BSSIDS 10
#define MAX_FT_BSS_LIST 10

#define TRANSPORT_PROTO_TYPE_TCP 0x06
#define TRANSPORT_PROTO_TYPE_UDP 0x11
#define NAN_TRANSPORT_PORT_DEFAULT 7000
#define NAN_TRANSPORT_PROTOCOL_DEFAULT TRANSPORT_PROTO_TYPE_TCP

enum value_not_set_enabled_disabled {
	VALUE_NOT_SET,
	VALUE_ENABLED,
	VALUE_DISABLED
};

enum sec_ch_offset {
	SEC_CH_NO,
	SEC_CH_40ABOVE,
	SEC_CH_40BELOW
};

struct mbo_pref_ap {
	int ap_ne_class;
	int ap_ne_op_ch;
	int ap_ne_pref;
	unsigned char mac_addr[ETH_ALEN];
};

#ifdef NL80211_SUPPORT
#define SOCK_BUF_SIZE (32 * 1024)
struct nl80211_ctx {
	struct nl_sock *sock;
	int netlink_familyid;
	int nlctrl_familyid;
	size_t sock_buf_size;
	struct nl_sock *event_sock;
};
#endif /* NL80211_SUPPORT */

/* hardcoded long WSC IE values to force fragmentation */
#define WPS_LONG_DEVICE_NAME	"Qti1234511adtest1234567890123456"
#define WPS_LONG_MANUFACTURER	"Qti1234511adQti1234511adQti1234511adQti1234511adQti1234511ad"
#define WPS_LONG_MODEL_NAME	"Qti1234511adtest1234567890123456"
#define WPS_LONG_MODEL_NUMBER	"11111111111111111111111111111111"
#define WPS_LONG_SERIAL_NUMBER	"22222222222222222222222222222222"

enum akm_suite_values {
	AKM_WPA_EAP = 1,
	AKM_WPA_PSK = 2,
	AKM_FT_EAP = 3,
	AKM_FT_PSK = 4,
	AKM_EAP_SHA256 = 5,
	AKM_PSK_SHA256 = 6,
	AKM_SAE = 8,
	AKM_FT_SAE = 9,
	AKM_SUITE_B = 12,
	AKM_FT_SUITE_B = 13,
	AKM_FILS_SHA256 = 14,
	AKM_FILS_SHA384 = 15,
	AKM_FT_FILS_SHA256 = 16,
	AKM_FT_FILS_SHA384 = 17,
	AKM_SAE_EXT_KEY = 24,
	AKM_FT_SAE_EXT_KEY = 25,

};

enum ip_version {
	DEFAULT_IP_VERSION = 0,
	IPV4 = 4,
	IPV6 = 6,
};

enum ip_protocol {
	DEFAULT_PROTOCOL = 0,
	PROTOCOL_UDP = 17,
	PROTOCOL_TCP = 6,
	PROTOCOL_ESP = 50,
};

#define DSCP_POLICY_SUCCESS 0
#define DSCP_POLICY_REJECT 1

struct dscp_policy_status {
	int id;
	int status;
};

struct dscp_policy_data {
	char domain_name[250];
	int policy_id;
	enum ip_version ip_version;
	char src_ip[INET6_ADDRSTRLEN];
	char dst_ip[INET6_ADDRSTRLEN];
	int src_port;
	int dst_port;
	int start_port;
	int end_port;
	enum ip_protocol protocol;
	int dscp;
	int granularity_score;
	struct dscp_policy_data *next;
};

enum dpp_mdns_role {
	DPP_MDNS_NOT_RUNNING,
	DPP_MDNS_RELAY,
	DPP_MDNS_CONTROLLER,
	DPP_MDNS_BOOTSTRAPPING,
};

enum loc_i2r_lmr_policy {
	LOC_USE_DEFAULT_I2R_LMR_POLICY = 0,
	LOC_FORCE_FTM_I2R_LMR_POLICY = 1,
	LOC_ABORT_ON_I2R_LMR_POLICY_MISMATCH = 2,
};

#define NAN_MAX_COOKIE_LEN 64
#define NAN_MAX_PASSWORD_LEN 63
#define NAN_NIK_LEN 16

enum nan_bootstrapping_state {
	NAN_BOOTSTRAP_IDLE,
	NAN_BOOTSTRAP_REQ_SENT,
	NAN_BOOTSTRAP_REQ_RECVD,
	NAN_BOOTSTRAP_COMEBACK_RSP_SENT,
	NAN_BOOTSTRAP_COMEBACK_RSP_RECVD,
	NAN_BOOTSTRAP_COMEBACK_REQ_SENT,
	NAN_BOOTSTRAPPING_DONE,
};

enum secure_nan_role {
	SECURE_NAN_IDLE,
	SECURE_NAN_BOOTSTRAPPING_INITIATOR,
	SECURE_NAN_BOOTSTRAPPING_RESPONDER,
	SECURE_NAN_PAIRING_INITIATOR,
	SECURE_NAN_PAIRING_RESPONDER,
};

struct device_pairing_info {
	bool pairing_setup;
	bool npk_nik_caching;
	bool pairing_verification;
	int bootstrapping_methods;
	int dialog_token;
	bool password_valid;
	char password[NAN_MAX_PASSWORD_LEN];
	bool nik_valid;
	char nik[NAN_NIK_LEN];
	enum secure_nan_role role;
	bool trigger_verification;
};

enum nan_akm {
	NAN_AKM_SAE,
	NAN_AKM_PASN = 1
};

struct peer_pairing_info {
	u16 publish_subscribe_id;
	int pairing_instance_id;
	char peer_mac_addr[ETH_ALEN];
	bool pairing_setup;
	bool npk_nik_caching;
	bool pairing_verification;
	int supported_bootstrap_methods;
	int selected_bootstrap_method;
	enum nan_bootstrapping_state bs_state;
	bool nik_valid;
	char nik[NAN_NIK_LEN];
	enum secure_nan_role role;
	int cookie_len;
	char cookie[NAN_MAX_COOKIE_LEN];
	enum nan_akm akm;
	bool is_paired;
};

#ifdef ANDROID_MDNS
struct mdnssd_apis {
	__typeof__(DNSServiceCreateConnection) *service_create_connection;
	__typeof__(DNSServiceRefSockFD) *service_socket_fd;
	__typeof__(DNSServiceProcessResult) *service_process_result;
	__typeof__(DNSServiceRegister) *service_register;
	__typeof__(DNSServiceRefDeallocate) *service_deallocate;
	__typeof__(DNSServiceBrowse) *service_browse;
	__typeof__(DNSServiceResolve) *service_resolve;
	__typeof__(DNSServiceGetAddrInfo) *get_addr_info;
	__typeof__(TXTRecordCreate) *txt_create;
	__typeof__(TXTRecordSetValue) *txt_set_value;
	__typeof__(TXTRecordDeallocate) *txt_deallocate;
	__typeof__(TXTRecordContainsKey) *txt_contains_key;
	__typeof__(TXTRecordGetValuePtr) *txt_get_value;
	__typeof__(TXTRecordGetLength) *txt_get_length;
	__typeof__(TXTRecordGetBytesPtr) *txt_get_bytes;
};

struct mdnss_discovery_info {
	char *type;
	char *name;
	char *domain;
	char *host_name;
	char *bskeyhash;
	char ipaddr[100];
	uint16_t port;
	uint32_t ifindex;
};
#endif /* ANDROID_MDNS */

struct sigma_dut {
	const char *main_ifname;
	char *main_ifname_2g;
	char *main_ifname_5g;
	const char *station_ifname;
	char *station_ifname_2g;
	char *station_ifname_5g;
	char *p2p_ifname_buf;
	int use_5g;
	int ap_band_6g;
	int sta_2g_started;
	int sta_5g_started;

	int s; /* server TCP socket */
	int debug_level;
	int stdout_debug;
	struct sigma_cmd_handler *cmds;
	int response_sent;

	const char *sigma_tmpdir;

	/* Default timeout value (seconds) for commands */
	unsigned int default_timeout;
	unsigned int user_config_timeout;

	int next_streamid;

	const char *bridge; /* bridge interface to use in AP mode */

	enum sigma_mode {
		SIGMA_MODE_UNKNOWN,
		SIGMA_MODE_STATION,
		SIGMA_MODE_AP,
		SIGMA_MODE_SNIFFER
	} mode;

	/*
	 * Local cached values to handle API that does not provide all the
	 * needed information with commands that actually trigger some
	 * operations.
	 */
	int listen_chn;
	int persistent;
	int intra_bss;
	int noa_duration;
	int noa_interval;
	int noa_count;
	enum wfa_cs_wps_method {
		WFA_CS_WPS_NOT_READY,
		WFA_CS_WPS_PBC,
		WFA_CS_WPS_PIN_DISPLAY,
		WFA_CS_WPS_PIN_LABEL,
		WFA_CS_WPS_PIN_KEYPAD
	} wps_method;
	char wps_pin[9];

	struct wfa_cs_p2p_group *groups;

	char infra_ssid[33];
	int infra_network_id;

	enum p2p_mode {
		P2P_IDLE, P2P_DISCOVER, P2P_LISTEN, P2P_DISABLE
	} p2p_mode;

	int go;
	int p2p_client;
	const char *p2p_ifname;

	int client_uapsd;

	char arp_ipaddr[IP_ADDR_STR_LEN];
	char arp_ifname[IFNAMSIZ + 1];

	enum sta_pmf {
		STA_PMF_DISABLED,
		STA_PMF_OPTIONAL,
		STA_PMF_REQUIRED
	} sta_pmf;

	int sta_ft_ds;

	int no_tpk_expiration;

	int er_oper_performed;
	char er_oper_bssid[20];
	int amsdu_size;
	int back_rcv_buf;

	int testbed_flag_txsp;
	int testbed_flag_rxsp;
	int chwidth;

	unsigned int akm_values;

	/* AP configuration */
	char ap_ssid[33];
	/*
	 * WLAN-TAG of 1 will use 'ap_' variables;
	 * tag higher than 1 will use 'ap_tag_' variables.
	 */
	char ap_tag_ssid[MAX_WLAN_TAGS - 1][33];
	enum ap_mode {
		AP_11a,
		AP_11g,
		AP_11b,
		AP_11na,
		AP_11ng,
		AP_11ac,
		AP_11ad,
		AP_11ax,
		AP_inval
	} ap_mode;
	int ap_channel;
	int ap_tag_channel[MAX_WLAN_TAGS - 1];
	int ap_rts;
	int ap_frgmnt;
	int ap_bcnint;
	int ap_start_disabled;
	struct qos_params {
		int ac;
		int cwmin;
		int cwmax;
		int aifs;
		int txop;
		int acm;
	} ap_qos[NUM_AP_AC], ap_sta_qos[NUM_AP_AC];
	enum value_not_set_enabled_disabled ap_noack;
	enum value_not_set_enabled_disabled ap_ampdu;
	enum value_not_set_enabled_disabled ap_amsdu;
	enum value_not_set_enabled_disabled ap_rx_amsdu;
	int ap_ampdu_exp;
	int ap_max_mpdu_len;
	enum value_not_set_enabled_disabled ap_addba_reject;
	int ap_fixed_rate;
	int ap_mcs;
	int ap_rx_streams;
	int ap_tx_streams;
	unsigned int ap_vhtmcs_map;
	enum value_not_set_enabled_disabled ap_ldpc;
	enum value_not_set_enabled_disabled ap_sig_rts;
	enum ap_chwidth {
		AP_20,
		AP_40,
		AP_80,
		AP_160,
		AP_80_80,
		AP_AUTO
	} ap_chwidth;
	enum ap_chwidth default_11na_ap_chwidth;
	enum ap_chwidth default_11ng_ap_chwidth;
	enum value_not_set_enabled_disabled ap_tx_stbc;
	enum value_not_set_enabled_disabled ap_dyn_bw_sig;
	int ap_sgi80;
	int ap_p2p_mgmt;
	enum ap_key_mgmt {
		AP_OPEN,
		AP_WPA2_PSK,
		AP_WPA_PSK,
		AP_WPA2_EAP,
		AP_WPA_EAP,
		AP_WPA2_EAP_MIXED,
		AP_WPA2_PSK_MIXED,
		AP_WPA2_SAE,
		AP_WPA2_PSK_SAE,
		AP_SUITEB,
		AP_WPA2_OWE,
		AP_WPA2_EAP_OSEN,
		AP_WPA2_FT_EAP,
		AP_WPA2_FT_PSK,
		AP_WPA2_EAP_SHA256,
		AP_WPA2_PSK_SHA256,
		AP_WPA2_ENT_FT_EAP,
		AP_OSEN,
	} ap_key_mgmt;
	enum ap_tag_key_mgmt {
		AP2_OPEN,
		AP2_OSEN,
		AP2_WPA2_PSK,
		AP2_WPA2_OWE,
	} ap_tag_key_mgmt[MAX_WLAN_TAGS - 1];
	int ap_add_sha256;
	int ap_add_sha384;
	int ap_rsn_preauth;
	enum ap_pmf {
		AP_PMF_DISABLED,
		AP_PMF_OPTIONAL,
		AP_PMF_REQUIRED
	} ap_pmf;
	enum ap_cipher {
		AP_NO_GROUP_CIPHER_SET,
		AP_CCMP,
		AP_TKIP,
		AP_WEP,
		AP_PLAIN,
		AP_CCMP_TKIP,
		AP_GCMP_256,
		AP_GCMP_128,
		AP_CCMP_256,
		AP_CCMP_128_GCMP_256,
	} ap_cipher, ap_group_cipher;
	enum ap_group_mgmt_cipher {
		AP_NO_GROUP_MGMT_CIPHER_SET,
		AP_BIP_GMAC_256,
		AP_BIP_CMAC_256,
		AP_BIP_GMAC_128,
		AP_BIP_CMAC_128,
	} ap_group_mgmt_cipher;
	char *ap_sae_groups;
	int sae_anti_clogging_threshold;
	int sae_reflection;
	int ap_sae_commit_status;
	int ap_sae_pk_omit;
	int sae_confirm_immediate;
	char ap_passphrase[101];
	char ap_tag_passphrase[MAX_WLAN_TAGS - 1][101];
	char ap_psk[65];
	char *ap_sae_passwords;
	char *ap_sae_pk_modifier;
	char *ap_sae_pk_keypair;
	char *ap_sae_pk_keypair_sig;
	int ap_sae_pk;
	char ap_wepkey[27];
	char ap_radius_ipaddr[20];
	int ap_radius_port;
	char ap_radius_password[200];
	char ap2_radius_ipaddr[20];
	int ap2_radius_port;
	char ap2_radius_password[200];
	int ap_tdls_prohibit;
	int ap_tdls_prohibit_chswitch;
	int ap_hs2;
	int ap_dgaf_disable;
	int ap_p2p_cross_connect;
	int ap_oper_name;
	int ap_wan_metrics;
	int ap_conn_capab;
	int ap_oper_class;

	int ap_interworking;
	int ap_access_net_type;
	int ap_internet;
	int ap_venue_group;
	int ap_venue_type;
	char ap_hessid[20];
	char ap_roaming_cons[100];
	int ap_venue_name;
	int ap_net_auth_type;
	int ap_nai_realm_list;
	char ap_domain_name_list[1000];
	int ap_ip_addr_type_avail;
	char ap_plmn_mcc[10][4];
	char ap_plmn_mnc[10][4];
	int ap_gas_cb_delay;
	int ap_proxy_arp;
	int ap2_proxy_arp;
	int ap2_osu;
	int ap_l2tif;
	int ap_anqpserver;
	int ap_anqpserver_on;
	int ap_osu_provider_list;
	int ap_osu_provider_nai_list;
	int ap_qos_map_set;
	int ap_bss_load;
	char ap_osu_server_uri[10][256];
	char ap_osu_ssid[33];
	int ap_osu_method[10];
	int ap_osu_icon_tag;
	int ap_venue_url;
	int ap_advice_of_charge;
	int ap_oper_icon_metadata;
	int ap_tnc_file_name;
	unsigned int ap_tnc_time_stamp;

	int ap_fake_pkhash;
	int ap_disable_protection;
	int ap_allow_vht_wep;
	int ap_allow_vht_tkip;

	enum ap_vht_chwidth {
		AP_20_40_VHT_OPER_CHWIDTH,
		AP_80_VHT_OPER_CHWIDTH,
		AP_160_VHT_OPER_CHWIDTH
	} ap_vht_chwidth;
	int ap_txBF;
	int ap_mu_txBF;
	enum ap_regulatory_mode {
		AP_80211D_MODE_DISABLED,
		AP_80211D_MODE_ENABLED,
	} ap_regulatory_mode;
	enum ap_dfs_mode {
		AP_DFS_MODE_DISABLED,
		AP_DFS_MODE_ENABLED,
	} ap_dfs_mode;
	int ap_ndpa_frame;

	int ap_lci;
	char ap_val_lci[33];
	char ap_infoz[17];
	int ap_lcr;
	char ap_val_lcr[400];
	int ap_rrm;
	int ap_rtt;
	int ap_neighap; /* number of configured neighbor APs */
	unsigned char ap_val_neighap[3][6];
	int ap_opchannel; /* number of oper channels */
	int ap_val_opchannel[3];
	int ap_scan;
	int ap_fqdn_held;
	int ap_fqdn_supl;
	int ap_msnt_type;

	int ap_mbo;
	int ap_ne_class;
	int ap_ne_op_ch;
	int ap_set_bssidpref;
	int ap_btmreq_disassoc_imnt;
	int ap_btmreq_term_bit;
	int ap_disassoc_timer;
	int ap_btmreq_bss_term_dur;
	enum reg_domain {
		REG_DOMAIN_NOT_SET,
		REG_DOMAIN_LOCAL,
		REG_DOMAIN_GLOBAL
	} ap_reg_domain;
	char ap_mobility_domain[10];
	unsigned char ap_cell_cap_pref;
	int ap_ft_oa;
	enum value_not_set_enabled_disabled ap_ft_ds;
	int ap_name;
	int ap_interface_5g;
	int ap_interface_2g;
	int ap_assoc_delay;
	int ap_btmreq_bss_term_tsf;
	int ap_fils_dscv_int;
	int ap_nairealm_int;
	char ap_nairealm[33];
	int ap_blechanutil;
	int ap_ble_admit_cap;
	int ap_datappdudura;
	int ap_airtimefract;
	char ap_dhcpserv_ipaddr[20];
	int ap_dhcp_stop;
	int ap_bawinsize;
	int ap_blestacnt;
	int ap_ul_availcap;
	int ap_dl_availcap;
	int ap_akm;
	unsigned int ap_akm_values;
	int ap_pmksa;
	int ap_pmksa_caching;
	int ap_beacon_prot;
	u8 ap_transition_disable;
	int ap_80plus80;
	int ap_oper_chn;

	struct mbo_pref_ap mbo_pref_aps[MBO_MAX_PREF_BSSIDS];
	struct mbo_pref_ap mbo_self_ap_tuple;
	int mbo_pref_ap_cnt;
	unsigned char ft_bss_mac_list[MAX_FT_BSS_LIST][ETH_ALEN];
	int ft_bss_mac_cnt;

	char *ar_ltf;

	int ap_numsounddim;
	unsigned int he_mcsnssmap;
	int he_ul_mcs;
	int he_mmss;
	int he_srctrl_allow;

	int ap_ocvc;

	enum value_not_set_enabled_disabled ap_oce;
	enum value_not_set_enabled_disabled ap_filsdscv;
	enum value_not_set_enabled_disabled ap_filshlp;
	enum value_not_set_enabled_disabled ap_broadcast_ssid;
	enum value_not_set_enabled_disabled ap_rnr;
	enum value_not_set_enabled_disabled ap_esp;

	enum value_not_set_enabled_disabled ap_he_ulofdma;
	enum value_not_set_enabled_disabled ap_he_dlofdma;
	enum value_not_set_enabled_disabled ap_bcc;
	enum value_not_set_enabled_disabled ap_he_frag;
	enum value_not_set_enabled_disabled ap_mu_edca;
	enum value_not_set_enabled_disabled ap_he_rtsthrshld;
	enum value_not_set_enabled_disabled ap_mbssid;
	enum value_not_set_enabled_disabled ap_twtresp;
	enum value_not_set_enabled_disabled he_sounding;
	enum value_not_set_enabled_disabled he_set_sta_1x1;

	enum ppdu {
		PPDU_NOT_SET,
		PPDU_MU,
		PPDU_SU,
		PPDU_ER,
		PPDU_TB,
		PPDU_HESU,
	} ap_he_ppdu;

	enum bufsize {
		BA_BUFSIZE_NOT_SET,
		BA_BUFSIZE_64,
		BA_BUFSIZE_256,
	} ap_ba_bufsize;

	enum mimo {
		MIMO_NOT_SET,
		MIMO_DL,
		MIMO_UL,
	} ap_he_mimo;

	struct sigma_ese_alloc ap_ese_allocs[MAX_ESE_ALLOCS];
	int ap_num_ese_allocs;

	const char *hostapd_debug_log;
	const char *wpa_supplicant_debug_log;

#ifdef CONFIG_TRAFFIC_AGENT
	/* Traffic Agent */
	struct sigma_stream streams[MAX_SIGMA_STREAMS];
	int stream_id;
	int num_streams;
	pthread_t thr;
#endif /* CONFIG_TRAFFIC_AGENT */

	unsigned int throughput_pktsize; /* If non-zero, override pktsize for
					  * throughput tests */
	int no_timestamps;

	const char *sniffer_ifname;
	const char *set_macaddr;
	int tmp_mac_addr;
	int ap_is_dual;
	enum ap_mode ap_mode_1;
	enum ap_chwidth ap_chwidth_1;
	int ap_channel_1;
	char ap_countrycode[3];

	int ap_wpsnfc;

	enum ap_wme {
		AP_WME_OFF,
		AP_WME_ON,
	} ap_wme;

	enum ap_wmmps {
		AP_WMMPS_OFF,
		AP_WMMPS_ON,
	} ap_wmmps;

	enum sec_ch_offset ap_chwidth_offset;

	char *ap_dpp_conf_addr;
	char *ap_dpp_conf_pkhash;

#ifdef CONFIG_SNIFFER
	pid_t sniffer_pid;
	char sniffer_filename[200];
#endif /* CONFIG_SNIFFER */

	int last_set_ip_config_ipv6;
#ifdef MIRACAST
	pthread_t rtsp_thread_handle;
	int wfd_device_type; /* 0 for source, 1 for sink */
	char peer_mac_address[32];
	char modified_peer_mac_address[32];
	void *miracast_lib;
	const char *miracast_lib_path;
	char mdns_instance_name[64];
#endif /* MIRACAST */

	int tid_to_handle[8]; /* Mapping of TID to handle */
	int dialog_token; /* Used for generating unique handle for an addTs */

	enum sigma_program {
		PROGRAM_UNKNOWN = 0,
		PROGRAM_TDLS,
		PROGRAM_HS2,
		PROGRAM_HS2_R2,
		PROGRAM_WFD,
		PROGRAM_DISPLAYR2,
		PROGRAM_PMF,
		PROGRAM_WPS,
		PROGRAM_60GHZ,
		PROGRAM_HT,
		PROGRAM_VHT,
		PROGRAM_NAN,
		PROGRAM_LOC,
		PROGRAM_MBO,
		PROGRAM_IOTLP,
		PROGRAM_DPP,
		PROGRAM_OCE,
		PROGRAM_WPA3,
		PROGRAM_HE,
		PROGRAM_HS2_R3,
		PROGRAM_QM,
		PROGRAM_HS2_R4,
		PROGRAM_HS2_2022,
		PROGRAM_LOCR2,
		PROGRAM_EHT,
	} program;

	enum device_type {
		device_type_unknown,
		AP_unknown,
		AP_testbed,
		AP_dut,
		STA_unknown,
		STA_testbed,
		STA_dut
	} device_type;

	enum {
		DEVROLE_UNKNOWN = 0,
		DEVROLE_STA,
		DEVROLE_PCP,
		DEVROLE_STA_CFON,
		DEVROLE_AP,
	} dev_role;

	enum wps_band {
		WPS_BAND_NON_60G = 0,
		WPS_BAND_60G,
	} band;

	enum dev_mode {
		MODE_UNKNOWN = 0,
		MODE_11AC,
		MODE_11AX,
		MODE_11BE,
	} device_mode;

	int wps_disable; /* Used for 60G to disable PCP from sending WPS IE */
	int wsc_fragment; /* simulate WSC IE fragmentation */
	int eap_fragment; /* simulate EAP fragmentation */
	int wps_forced_version; /* Used to force reported WPS version */
	enum {
		/* no change */
		FORCE_RSN_IE_NONE = 0,
		/* if exists, remove and clear privacy bit */
		FORCE_RSN_IE_REMOVE,
		/* if not exists, add and set privacy bit */
		FORCE_RSN_IE_ADD,
	} force_rsn_ie; /* override RSN IE in association request */

	const char *version;
	int no_ip_addr_set;
	int sta_channel;
	int data_ch_freq;

	const char *summary_log;
	const char *hostapd_entropy_log;

	int iface_down_on_reset;
	int write_stats; /* traffic stream e2e*.txt files */
	int sim_no_username; /* do not set SIM username to use real SIM */

	const char *vendor_name; /* device_get_info vendor override */
	const char *model_name; /* device_get_info model override */
	const char *version_name; /* device_get_info version override */
	const char *log_file_dir; /* Directory to generate log file */
	FILE *log_file_fd; /* Pointer to log file */

	int ndp_enable; /* Flag which is set once the NDP is setup */

	int ndpe; /* Flag indicating NDPE is supported */
	u16 trans_port; /* transport port number for TCP/UDP connection */
	u8 trans_proto; /* transport protocol, 0x06: TCP, 0x11: UDP */
	u8 nan_ipv6_addr[IPV6_ADDR_LEN]; /* NAN IPv6 address */
	u8 nan_ipv6_len; /* NAN IPv6 address length */

	/* Length of nan_pmk in octets */
	u8 nan_pmk_len;

	/*
	 * PMK: Info is optional in Discovery phase. PMK info can
	 *  be passed during the NDP session.
	 */
	u8 nan_pmk[32];

	enum value_not_set_enabled_disabled wnm_bss_max_feature;
	int wnm_bss_max_idle_time;
	enum value_not_set_enabled_disabled wnm_bss_max_protection;

	char *non_pref_ch_list; /* MBO: non-preferred channel report */
	char *btm_query_cand_list; /* Candidate list for BTM Query */

	char *sae_commit_override;
	char *rsne_override;
	char *rsnxe_override_eapol;
	int sta_associate_wait_connect;
	char server_cert_hash[65];
	int server_cert_tod;
	int sta_tod_policy;
	const char *hostapd_bin;
	int use_hostapd_pid_file;
	const char *hostapd_ifname;
	int hostapd_running;

	char *dpp_peer_uri;
	int dpp_local_bootstrap;
	int dpp_conf_id;
	int dpp_network_id;
	enum dpp_mdns_role dpp_mdns;

	u8 fils_hlp;
	pthread_t hlp_thread;

#ifdef NL80211_SUPPORT
	struct nl80211_ctx *nl_ctx;
	int config_rsnie;
#endif /* NL80211_SUPPORT */

	int sta_nss;

	int sta_async_twt_supp; /* Asynchronous TWT response event support */

#ifdef ANDROID
	int nanservicediscoveryinprogress;
#endif /* ANDROID */

	const char *priv_cmd; /* iwpriv / cfg80211tool command name */

	unsigned int wpa_log_size;
	char dev_start_test_runtime_id[100];
#ifdef ANDROID_WIFI_HAL
	wifi_interface_handle wifi_hal_iface_handle;
	wifi_handle wifi_hal_handle;
	bool wifi_hal_initialized;
#endif /*ANDROID_WIFI_HAL*/

	int sae_h2e_default;
	enum {
		SAE_PWE_DEFAULT,
		SAE_PWE_LOOP,
		SAE_PWE_H2E
	} sae_pwe;
	int owe_ptk_workaround;
	struct dut_hw_modes hw_modes;
	int ocvc;
	int beacon_prot;
	int client_privacy;
	int client_privacy_default;
	int saquery_oci_freq;
	char device_driver[32];
	int user_config_ap_ocvc;
	int user_config_ap_beacon_prot;
	char qm_domain_name[250];
	struct dscp_policy_data *dscp_policy_table;
	pthread_t dscp_policy_mon_thread;
	int reject_dscp_policies;
	int dscp_reject_resp_code;
	struct dscp_policy_status dscp_status[5];
	unsigned int num_dscp_status;
	unsigned int prev_disable_scs_support;
	unsigned int prev_disable_mscs_support;
	int dscp_use_iptables;
	int autoconnect_default;
	int dhcp_client_running;
	int i2rlmr_iftmr;
	int i2rlmrpolicy;
	int rnm_mfp;
	struct device_pairing_info dev_info;
	struct peer_pairing_info peer_info;
#ifdef ANDROID_MDNS
	DNSServiceRef mdns_service;
	void *mdnssd_so;
	struct mdnssd_apis mdnssd;
	struct mdnss_discovery_info mdns_discover;
#endif /* ANDROID_MDNS */
};


enum sigma_dut_print_level {
	DUT_MSG_DEBUG, DUT_MSG_INFO, DUT_MSG_ERROR
};

void sigma_dut_print(struct sigma_dut *dut, int level, const char *fmt, ...)
PRINTF_FORMAT(3, 4);

void sigma_dut_summary(struct sigma_dut *dut, const char *fmt, ...)
PRINTF_FORMAT(2, 3);


enum sigma_status {
	SIGMA_RUNNING, SIGMA_INVALID, SIGMA_ERROR, SIGMA_COMPLETE
};

void send_resp(struct sigma_dut *dut, struct sigma_conn *conn,
	       enum sigma_status status, const char *buf);

const char * get_param(struct sigma_cmd *cmd, const char *name);
const char * get_param_indexed(struct sigma_cmd *cmd, const char *name,
			       int index);
const char * get_param_fmt(struct sigma_cmd *cmd, const char *name, ...);

int sigma_dut_reg_cmd(const char *cmd,
		      int (*validate)(struct sigma_cmd *cmd),
		      enum sigma_cmd_result (*process)(struct sigma_dut *dut,
						       struct sigma_conn *conn,
						       struct sigma_cmd *cmd));

void sigma_dut_register_cmds(void);

enum sigma_cmd_result cmd_sta_send_frame(struct sigma_dut *dut,
					 struct sigma_conn *conn,
					 struct sigma_cmd *cmd);
int cmd_sta_set_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd);
enum sigma_cmd_result cmd_ap_send_frame(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd);
enum sigma_cmd_result cmd_wlantest_send_frame(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd);
int sta_cfon_set_wireless(struct sigma_dut *dut, struct sigma_conn *conn,
			  struct sigma_cmd *cmd);
int sta_cfon_get_mac_address(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd);
int sta_cfon_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd);

enum driver_type {
	DRIVER_NOT_SET,
	DRIVER_ATHEROS,
	DRIVER_WCN,
	DRIVER_MAC80211,
	DRIVER_AR6003,
	DRIVER_WIL6210,
	DRIVER_QNXNTO,
	DRIVER_OPENWRT,
	DRIVER_LINUX_WCN,
};

enum openwrt_driver_type {
	OPENWRT_DRIVER_NOT_SET,
	OPENWRT_DRIVER_ATHEROS
};

#define DRIVER_NAME_60G "wil6210"

int set_wifi_chip(const char *chip_type);
enum driver_type get_driver_type(struct sigma_dut *dut);
enum openwrt_driver_type get_openwrt_driver_type(void);
void sigma_dut_get_device_driver_name(const char *ifname, char *name,
				      size_t size);
int file_exists(const char *fname);

struct wpa_ctrl;

int wps_connection_event(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct wpa_ctrl *ctrl, const char *intf, int p2p_resp);
int ascii2hexstr(const char *str, char *hex);
void disconnect_station(struct sigma_dut *dut);
void nfc_status(struct sigma_dut *dut, const char *state, const char *oper);
int get_ip_config(struct sigma_dut *dut, const char *ifname, char *buf,
		  size_t buf_len);
int ath6kl_client_uapsd(struct sigma_dut *dut, const char *intf, int uapsd);
int is_ip_addr(const char *str);
int run_system(struct sigma_dut *dut, const char *cmd);
int run_system_wrapper(struct sigma_dut *dut, const char *cmd, ...);
int run_iwpriv(struct sigma_dut *dut, const char *ifname, const char *cmd, ...);
enum sigma_cmd_result cmd_wlantest_set_channel(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd);
void wlantest_register_cmds(void);
void sniffer_close(struct sigma_dut *dut);

/* sigma_dut.c */
int wifi_hal_initialize(struct sigma_dut *dut);

/* ap.c */
void ap_register_cmds(void);
void ath_disable_txbf(struct sigma_dut *dut, const char *intf);
void ath_config_dyn_bw_sig(struct sigma_dut *dut, const char *ifname,
			   const char *val);
void novap_reset(struct sigma_dut *dut, const char *ifname, int reset);
int get_hwaddr(const char *ifname, unsigned char *hwaddr);
enum sigma_cmd_result cmd_ap_config_commit(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   struct sigma_cmd *cmd);
int ap_wps_registration(struct sigma_dut *dut, struct sigma_conn *conn,
			struct sigma_cmd *cmd);
const char * get_hostapd_ifname(struct sigma_dut *dut);
void get_wiphy_capabilities(struct sigma_dut *dut);

/* sta.c */
void sta_register_cmds(void);
int set_ps(const char *intf, struct sigma_dut *dut, int enabled);
void ath_set_zero_crc(struct sigma_dut *dut, const char *val);
void ath_set_cts_width(struct sigma_dut *dut, const char *ifname,
		       const char *val);
int ath_set_width(struct sigma_dut *dut, struct sigma_conn *conn,
		  const char *intf, const char *val);
int sta_set_60g_abft_len(struct sigma_dut *dut, struct sigma_conn *conn,
			 int abft_len);
int wil6210_send_frame_60g(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd);
int hwaddr_aton(const char *txt, unsigned char *addr);
int set_ipv4_addr(struct sigma_dut *dut, const char *ifname,
		  const char *ip, const char *mask);
int set_ipv4_gw(struct sigma_dut *dut, const char *gw);
int send_addba_60g(struct sigma_dut *dut, struct sigma_conn *conn,
		   struct sigma_cmd *cmd, const char *param);
int wil6210_set_ese(struct sigma_dut *dut, int count,
		    struct sigma_ese_alloc *allocs);
int sta_extract_60g_ese(struct sigma_dut *dut, struct sigma_cmd *cmd,
			struct sigma_ese_alloc *allocs, int *allocs_size);
int wil6210_set_force_mcs(struct sigma_dut *dut, int force, int mcs);
int sta_set_addba_buf_size(struct sigma_dut *dut,
			   const char *intf, int bufsize);
int wcn_set_he_gi(struct sigma_dut *dut, const char *intf, u8 gi_val);
#ifdef NL80211_SUPPORT
int wcn_set_he_ltf(struct sigma_dut *dut, const char *intf,
		   enum qca_wlan_he_ltf_cfg ltf);
#endif /* NL80211_SUPPORT */
void stop_dscp_policy_mon_thread(struct sigma_dut *dut);
void free_dscp_policy_table(struct sigma_dut *dut);

/* p2p.c */
void p2p_register_cmds(void);
int p2p_cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd);
void p2p_create_event_thread(struct sigma_dut *dut);
void stop_event_thread(void);
void start_dhcp(struct sigma_dut *dut, const char *group_ifname, int go);
void stop_dhcp(struct sigma_dut *dut, const char *group_ifname, int go);
int p2p_discover_peer(struct sigma_dut *dut, const char *ifname,
		      const char *peer, int full);
enum sigma_cmd_result cmd_sta_p2p_reset(struct sigma_dut *dut,
					struct sigma_conn *conn,
					struct sigma_cmd *cmd);

/* basic.c */
void basic_register_cmds(void);
void get_ver(const char *cmd, char *buf, size_t buflen);

/* utils.c */
enum sigma_program sigma_program_to_enum(const char *prog);
enum dev_mode dev_mode_to_enum(const char *mode);
bool is_passpoint_r2_or_newer(enum sigma_program prog);
bool is_passpoint(enum sigma_program prog);
int hex_byte(const char *str);
int parse_hexstr(const char *hex, unsigned char *buf, size_t buflen);
int parse_mac_address(struct sigma_dut *dut, const char *arg,
		      unsigned char *addr);
int is_60g_sigma_dut(struct sigma_dut *dut);
unsigned int channel_to_freq(struct sigma_dut *dut, unsigned int channel);
unsigned int freq_to_channel(unsigned int freq);
int is_ipv6_addr(const char *str);
void convert_mac_addr_to_ipv6_lladdr(u8 *mac_addr, char *ipv6_buf,
				     size_t buf_len);
size_t convert_mac_addr_to_ipv6_linklocal(const u8 *mac_addr, u8 *ipv6);
int snprintf_error(size_t size, int res);

#ifndef ANDROID
size_t strlcpy(char *dest, const char *src, size_t siz);
size_t strlcat(char *dst, const char *str, size_t size);
#endif /* ANDROID */
void hex_dump(struct sigma_dut *dut, u8 *data, size_t len);
int get_wps_pin_from_mac(struct sigma_dut *dut, const char *macaddr,
			 char *pin, size_t len);
void str_remove_chars(char *str, char ch);

int get_wps_forced_version(struct sigma_dut *dut, const char *str);
int base64_encode(const char *src, size_t len, char *out, size_t out_len);
unsigned char * base64_decode(const char *src, size_t len, size_t *out_len);
int random_get_bytes(char *buf, size_t len);
int get_enable_disable(const char *val);
int wcn_driver_cmd(const char *ifname, char *buf);

/* uapsd_stream.c */
void receive_uapsd(struct sigma_stream *s);
void send_uapsd_console(struct sigma_stream *s);

/* nan.c */
int nan_preset_testparameters(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd);
int nan_cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd);
int nan_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd);
int nan_cmd_sta_get_events(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd);
int nan_cmd_sta_transmit_followup(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd);
void nan_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd);
int nan_cmd_sta_preset_testparameters(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd);

/* ftm.c */
int loc_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd);
int loc_cmd_sta_send_frame(struct sigma_dut *dut, struct sigma_conn *conn,
			   struct sigma_cmd *cmd);
int loc_cmd_sta_preset_testparameters(struct sigma_dut *dut,
				      struct sigma_conn *conn,
				      struct sigma_cmd *cmd);
int lowi_cmd_sta_reset_default(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd);
int loc_r2_cmd_sta_exec_action(struct sigma_dut *dut, struct sigma_conn *conn,
			       struct sigma_cmd *cmd);

/* dpp.c */
enum sigma_cmd_result dpp_dev_exec_action(struct sigma_dut *dut,
					  struct sigma_conn *conn,
					  struct sigma_cmd *cmd);
int dpp_mdns_discover_relay_params(struct sigma_dut *dut);
int dpp_mdns_start(struct sigma_dut *dut, enum dpp_mdns_role role);
void dpp_mdns_stop(struct sigma_dut *dut);

/* dhcp.c */
void process_fils_hlp(struct sigma_dut *dut);
void hlp_thread_cleanup(struct sigma_dut *dut);

#ifdef NL80211_SUPPORT
struct nl80211_ctx * nl80211_init(struct sigma_dut *dut);
void nl80211_deinit(struct sigma_dut *dut, struct nl80211_ctx *ctx);
int nl80211_open_event_sock(struct sigma_dut *dut);
void nl80211_close_event_sock(struct sigma_dut *dut);
struct nl_msg * nl80211_drv_msg(struct sigma_dut *dut, struct nl80211_ctx *ctx,
				int ifindex, int flags,
				uint8_t cmd);
int send_and_recv_msgs(struct sigma_dut *dut, struct nl80211_ctx *ctx,
		       struct nl_msg *nlmsg,
		       int (*valid_handler)(struct nl_msg *, void *),
		       void *valid_data);
int wcn_wifi_test_config_set_flag(struct sigma_dut *dut, const char *intf,
				  int attr_id);
int wcn_wifi_test_config_set_u8(struct sigma_dut *dut, const char *intf,
				int attr_id, uint8_t val);
int wcn_wifi_test_config_set_u16(struct sigma_dut *dut, const char *intf,
				 int attr_id, uint16_t val);
#endif /* NL80211_SUPPORT */

void traffic_register_cmds(void);
void traffic_agent_register_cmds(void);
void powerswitch_register_cmds(void);
void atheros_register_cmds(void);
void dev_register_cmds(void);
void sniffer_register_cmds(void);
void server_register_cmds(void);
void miracast_register_cmds(void);
int set_ipv6_addr(struct sigma_dut *dut, const char *ip, const char *mask,
		  const char *ifname);
void kill_pid(struct sigma_dut *dut, const char *pid_file);
int get_ip_addr(const char *ifname, int ipv6, char *buf, size_t len);
bool is_6ghz_freq(int freq);

/* dnssd.c */
int mdnssd_init(struct sigma_dut *dut);

#endif /* SIGMA_DUT_H */
