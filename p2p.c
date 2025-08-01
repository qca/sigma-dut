/*
 * Sigma Control API DUT (station/AP)
 * Copyright (c) 2010-2011, Atheros Communications, Inc.
 * Copyright (c) 2011-2017, Qualcomm Atheros, Inc.
 * Copyright (c) 2018-2019, The Linux Foundation
 * All Rights Reserved.
 * Licensed under the Clear BSD license. See README for more details.
 */

#include "sigma_dut.h"
#include <sys/stat.h>
#include "wpa_ctrl.h"
#include "wpa_helpers.h"
#include "miracast.h"


int run_system(struct sigma_dut *dut, const char *cmd)
{
	int res;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Running '%s'", cmd);
	res = system(cmd);
	if (res < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Failed to execute command '%s'", cmd);
	}
	return res;
}


int run_system_wrapper(struct sigma_dut *dut, const char *cmd, ...)
{
	va_list ap;
	char *buf;
	int bytes_required;
	int res;

	va_start(ap, cmd);
	bytes_required = vsnprintf(NULL, 0, cmd, ap);
	bytes_required += 1;
	va_end(ap);
	buf = malloc(bytes_required);
	if (!buf) {
		printf("ERROR!! No memory\n");
		return -1;
	}
	va_start(ap, cmd);
	vsnprintf(buf, bytes_required, cmd, ap);
	va_end(ap);
	res = run_system(dut, buf);
	free(buf);
	return res;
}


int run_iwpriv(struct sigma_dut *dut, const char *ifname, const char *cmd, ...)
{
	va_list ap;
	char *buf;
	int bytes_required;
	int res;
	size_t prefix_len;

	if (!ifname)
		return -1;
	prefix_len = strlen(dut->priv_cmd) + 1 + strlen(ifname) + 1;
	va_start(ap, cmd);
	bytes_required = vsnprintf(NULL, 0, cmd, ap);
	bytes_required += 1;
	va_end(ap);
	buf = malloc(prefix_len + bytes_required);
	if (!buf) {
		printf("ERROR!! No memory\n");
		return -1;
	}
	snprintf(buf, prefix_len + bytes_required, "%s %s ",
		 dut->priv_cmd, ifname);
	va_start(ap, cmd);
	vsnprintf(buf + prefix_len, bytes_required, cmd, ap);
	va_end(ap);
	res = run_system(dut, buf);
	free(buf);
	return res;
}


static int get_60g_freq(int chan)
{
	int freq = 0;

	switch(chan) {
	case 1:
		freq = 58320;
		break;
	case 2:
		freq = 60480;
		break;
	case 3:
		freq = 62640;
		break;
	case 4:
		/* freq = 64800; Not supported in Sparrow 2.0 */
		break;
	default:
		break;
	}

	return freq;
}


#define GO_IP_ADDR "192.168.43.1"
#define START_IP_RANGE "192.168.43.10"
#define END_IP_RANGE "192.168.43.100"
#define FLUSH_IP_ADDR "0.0.0.0"

void start_dhcp(struct sigma_dut *dut, const char *group_ifname, int go)
{
#ifdef __linux__
	char buf[200];

	if (go) {
		snprintf(buf, sizeof(buf), "ifconfig %s %s", group_ifname,
			 GO_IP_ADDR);
		run_system(dut, buf);
#ifdef ANDROID
		snprintf(buf, sizeof(buf),
			 "/system/bin/dnsmasq -x /data/dnsmasq.pid --no-resolv --no-poll --dhcp-range=%s,%s,1h",
			 START_IP_RANGE, END_IP_RANGE);
#else /* ANDROID */
		run_system(dut, "killall dnsmasq");
		snprintf(buf, sizeof(buf),
			 "dnsmasq --no-resolv --no-poll --port=5353 --dhcp-range=%s,%s,1h",
			 START_IP_RANGE, END_IP_RANGE);
#endif /* ANDROID */
	} else {
#ifdef ANDROID
		if (access("/system/bin/dhcpcd", F_OK) != -1) {
			snprintf(buf, sizeof(buf), "/system/bin/dhcpcd -KL %s",
				 group_ifname);
		} else if (access("/system/bin/dhcptool", F_OK) != -1) {
			snprintf(buf, sizeof(buf), "/system/bin/dhcptool %s",
				 group_ifname);
		} else if (access("/vendor/bin/dhcpcd", F_OK) != -1) {
			snprintf(buf, sizeof(buf), "/vendor/bin/dhcpcd %s",
				 group_ifname);
		} else if (access("/vendor/bin/dhcptool", F_OK) != -1) {
			snprintf(buf, sizeof(buf), "/vendor/bin/dhcptool %s",
				 group_ifname);
		} else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"DHCP client program missing");
			return;
		}
#else /* ANDROID */
		snprintf(buf, sizeof(buf),
			 "dhclient -nw -pf /var/run/dhclient-%s.pid %s",
			 group_ifname, group_ifname);
#endif /* ANDROID */
	}

	run_system(dut, buf);
#endif /* __linux__ */
}


void stop_dhcp(struct sigma_dut *dut, const char *group_ifname, int go)
{
#ifdef __linux__
	char path[128];
	char buf[200];
	struct stat s;

	if (go) {
		snprintf(path, sizeof(path), "/data/dnsmasq.pid");
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Kill previous DHCP server: %s", buf);
	} else {
#ifdef ANDROID
		if (access("/system/bin/dhcpcd", F_OK) != -1) {
			snprintf(path, sizeof(path),
				 "/data/misc/dhcp/dhcpcd-%s.pid", group_ifname);
		} else {
			/*
			 * dhcptool terminates as soon as IP is
			 * assigned/registered using ioctls, no need to kill it
			 * explicitly.
			 */
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"No active DHCP client program");
			return;
		}
		snprintf(path, sizeof(path), "/data/misc/dhcp/dhcpcd-%s.pid",
			 group_ifname);
#else /* ANDROID */
		snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid",
			 group_ifname);
#endif /* ANDROID */
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Kill previous DHCP client: %s", buf);
	}
	if (stat(path, &s) == 0) {
		snprintf(buf, sizeof(buf), "kill `cat %s`", path);
		run_system(dut, buf);
		unlink(path);
		sleep(1);
	}

	snprintf(buf, sizeof(buf), "ip address flush dev %s", group_ifname);
	run_system(dut, buf);
	snprintf(buf, sizeof(buf), "ifconfig %s %s",
		 group_ifname, FLUSH_IP_ADDR);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Clear IP address: %s", buf);
	run_system(dut, buf);
#endif /* __linux__ */
}


static int stop_event_rx = 0;

#ifdef __linux__
void stop_event_thread()
{
	stop_event_rx = 1;
	printf("sigma_dut dhcp terminating\n");
}
#endif /* __linux__ */


static void * wpa_event_recv(void *ptr)
{
	struct sigma_dut *dut = ptr;
	struct wpa_ctrl *ctrl;
	char buf[4096];
	char *pos, *gtype, *p2p_group_ifname = NULL;
	int fd, ret, i;
	int go = 0;
	fd_set rfd;
	struct timeval tv;
	size_t len;

	const char *events[] = {
		"P2P-GROUP-STARTED",
		"P2P-GROUP-REMOVED",
		NULL
	};

	ctrl = open_wpa_mon(dut->p2p_ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return NULL;
	}

	for (i = 0; events[i]; i++) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting for wpa_cli event: %s", events[i]);
	}

	fd = wpa_ctrl_get_fd(ctrl);
	if (fd < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return NULL;
	}

	while (!stop_event_rx) {
		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		if (ret == 0)
			continue;
		if (ret < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "select: %s",
					strerror(errno));
			usleep(100000);
			continue;
		}

		len = sizeof(buf);
		if (wpa_ctrl_recv(ctrl, buf, &len) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failure while waiting for events");
			continue;
		}

		ret = 0;
		pos = strchr(buf, '>');
		if (pos) {
			for (i = 0; events[i]; i++) {
				if (strncmp(pos + 1, events[i],
					    strlen(events[i])) == 0) {
					ret = 1;
					break; /* Event found */
				}
			}
		}
		if (!ret)
			continue;

		if (strstr(buf, "P2P-GROUP-")) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Group event '%s'",
					buf);
			p2p_group_ifname = strchr(buf, ' ');
			if (!p2p_group_ifname)
				continue;
			p2p_group_ifname++;
			pos = strchr(p2p_group_ifname, ' ');
			if (!pos)
				continue;
			*pos++ = '\0';
			gtype = pos;
			pos = strchr(gtype, ' ');
			if (!pos)
				continue;
			*pos++ = '\0';

			go = strcmp(gtype, "GO") == 0;
		}

		if (strstr(buf, "P2P-GROUP-STARTED")) {
			start_dhcp(dut, p2p_group_ifname, go);
		} else if (strstr(buf, "P2P-GROUP-REMOVED")) {
			stop_dhcp(dut, p2p_group_ifname, go);
			go = 0;
		}
	}

	/* terminate DHCP server, if runnin! */
	if (go)
		stop_dhcp(dut, p2p_group_ifname, go);

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	pthread_exit(0);
	return NULL;
}


void p2p_create_event_thread(struct sigma_dut *dut)
{
	static pthread_t event_thread;

	/* create event thread */
	pthread_create(&event_thread, NULL, &wpa_event_recv, (void *) dut);
}


static int p2p_group_add(struct sigma_dut *dut, const char *ifname,
			 int go, const char *grpid, const char *ssid,
			 const char *peer_mac)
{
	struct wfa_cs_p2p_group *grp;

	if (go)
		dut->go = 1;
	else
		dut->p2p_client = 1;
	grp = malloc(sizeof(*grp));
	if (grp == NULL)
		return -1;
	memset(grp, 0, sizeof(*grp));
	strlcpy(grp->ifname, ifname, IFNAMSIZ);
	grp->go = go;
	strlcpy(grp->grpid, grpid, P2P_GRP_ID_LEN);
	strlcpy(grp->ssid, ssid, sizeof(grp->ssid));
	if (peer_mac)
		strlcpy(grp->peer_mac, peer_mac, sizeof(grp->peer_mac));

	grp->next = dut->groups;
	dut->groups = grp;

	return 0;
}


static int p2p_group_remove(struct sigma_dut *dut, const char *grpid)
{
	struct wfa_cs_p2p_group *grp, *prev;

	prev = NULL;
	grp = dut->groups;
	while (grp) {
		if (strcmp(grpid, grp->grpid) == 0) {
			if (prev)
				prev->next = grp->next;
			else
				dut->groups = grp->next;
			free(grp);
			return 0;
		}
		prev = grp;
		grp = grp->next;
	}
	return -1;
}


static struct wfa_cs_p2p_group * p2p_group_get(struct sigma_dut *dut,
					       const char *grpid)
{
	struct wfa_cs_p2p_group *grp;
	char buf[1000], buf2[4096], *ifname, *pos;
	char go_dev_addr[50];
	char ssid[33];

	for (grp = dut->groups; grp; grp = grp->next) {
		if (strcmp(grpid, grp->grpid) == 0)
			return grp;
	}

	/*
	 * No group found based on group id. As a workaround for GO Negotiation
	 * responder case where we do not store group id, try to find an active
	 * group that matches with the requested group id.
	 */

	pos = strchr(grpid, ' ');
	if (pos == NULL)
		return NULL;
	if (pos - grpid >= (int) sizeof(go_dev_addr))
		return NULL;
	memcpy(go_dev_addr, grpid, pos - grpid);
	go_dev_addr[pos - grpid] = '\0';
	strlcpy(ssid, pos + 1, sizeof(ssid));
	ssid[sizeof(ssid) - 1] = '\0';
	printf("Trying to find suitable interface for group: go_dev_addr='%s' "
	       "grpid='%s'\n", go_dev_addr, grpid);

	if (wpa_command_resp(get_main_ifname(dut), "INTERFACES",
			     buf, sizeof(buf)) < 0)
		return NULL;
	ifname = buf;
	while (ifname && *ifname) {
		int add = 0;
		int go = 0;
		pos = strchr(ifname, '\n');
		if (pos)
			*pos++ = '\0';
		printf("Considering interface '%s' for group\n", ifname);

		if (wpa_command_resp(ifname, "STATUS", buf2, sizeof(buf2)) ==
		    0) {
			if (strstr(buf2, ssid)) {
				printf("Selected interface '%s' based on "
				       "STATUS\n", ifname);
				add = 1;
			}
			if (strstr(buf2, "P2P GO"))
				go = 1;
		}

		if (wpa_command_resp(ifname, "LIST_NETWORKS", buf2,
				     sizeof(buf2)) == 0) {
			char *line, *end;
			line = buf2;
			while (line && *line) {
				end = strchr(line, ' ');
				if (end)
					*end++ = '\0';
				if (strstr(line, ssid) &&
				    strstr(line, "[CURRENT]")) {
					printf("Selected interface '%s' "
					       "based on LIST_NETWORKS\n",
					       ifname);
					add = 1;
					break;
				}
				line = end;
			}
		}

		if (add) {
			p2p_group_add(dut, ifname, go, grpid, ssid, NULL);
			return dut->groups;
		}

		ifname = pos;
	}

	return NULL;
}


const char * get_p2p_group_ifname(struct sigma_dut *dut, const char *ifname)
{
	char *iface, *pos;
	static char buf[1000];

	/* Try to find a suitable group interface */
	if (wpa_command_resp(get_main_ifname(dut), "INTERFACES",
			     buf, sizeof(buf)) < 0)
		return ifname;

	iface = buf;
	while (iface && *iface) {
		pos = strchr(iface, '\n');
		if (pos)
			*pos++ = '\0';
		if (memcmp(iface, "p2p-", 4) == 0) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Considering interface '%s' for IP address",
					iface);
			return iface;
		}
		iface = pos;
	}
	return ifname;
}


const char * get_group_ifname(struct sigma_dut *dut, const char *ifname)
{
	static char buf[1000];
	char *iface, *pos;
	char state[100];

	if (dut->groups) {
		sigma_dut_print(dut, DUT_MSG_DEBUG, "%s: Use group interface "
				"%s instead of main interface %s",
				__func__, dut->groups->ifname, ifname);
		return dut->groups->ifname;
	}

	/* Try to find a suitable group interface */
	if (wpa_command_resp(get_main_ifname(dut), "INTERFACES",
			     buf, sizeof(buf)) < 0)
		return ifname;

	iface = buf;
	while (iface && *iface) {
		pos = strchr(iface, '\n');
		if (pos)
			*pos++ = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Considering interface "
				"'%s' for IP address", iface);
		if (get_wpa_status(iface, "wpa_state", state, sizeof(state)) ==
		    0 && strcmp(state, "COMPLETED") == 0)
			return iface;
		iface = pos;
	}

	return ifname;
}


static int p2p_peer_known(const char *ifname, const char *peer, int full)
{
	char buf[4096];

	snprintf(buf, sizeof(buf), "P2P_PEER %s", peer);
	if (wpa_command_resp(ifname, buf, buf, sizeof(buf)) < 0)
		return 0;
	if (strncasecmp(buf, peer, strlen(peer)) != 0)
		return 0;
	if (!full)
		return 1;
	return strstr(buf, "[PROBE_REQ_ONLY]") == NULL ? 1 : 0;
}


int p2p_discover_peer(struct sigma_dut *dut, const char *ifname,
		      const char *peer, int full)
{
	unsigned int count;

	if (p2p_peer_known(ifname, peer, full))
		return 0;
	printf("Peer not yet discovered - start discovery\n");
	if (wpa_command(ifname, "P2P_FIND type=progressive") < 0) {
		printf("Failed to start discovery\n");
		return -1;
	}

	count = 0;
	while (count < dut->default_timeout) {
		count++;
		sleep(1);
		if (p2p_peer_known(ifname, peer, full)) {
			printf("Peer discovered - return to previous state\n");
			switch (dut->p2p_mode) {
			case P2P_IDLE:
				wpa_command(ifname, "P2P_STOP_FIND");
				break;
			case P2P_DISCOVER:
				/* Already running discovery */
				break;
			case P2P_LISTEN:
				wpa_command(ifname, "P2P_LISTEN");
				break;
			case P2P_DISABLE:
				printf("Invalid state - P2P was disabled?!\n");
				break;
			}
			return 0;
		}
	}

	printf("Peer discovery timed out - peer not discovered\n");
	wpa_command(ifname, "P2P_STOP_FIND");

	return -1;
}


static void add_dummy_services(const char *intf)
{
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 0b5f6166706f766572746370c00c000c01 074578616d706c65c027");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 076578616d706c650b5f6166706f766572746370c00c001001 00");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 045f697070c00c000c01 094d795072696e746572c027");
	wpa_command(intf, "P2P_SERVICE_ADD bonjour 096d797072696e746572045f697070c00c001001 09747874766572733d311a70646c3d6170706c69636174696f6e2f706f7374736372797074");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:6859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:5566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:5566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:6859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:1859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:2859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");

	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3859dede-8574-59ab-9332-123456789012::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3566d33e-9774-09ab-4822-333456785632::upnp:rootdevice");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:4122de4e-8574-59ab-9322-333456789044::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3566d33e-9774-09ab-4822-333456785632::urn:schemas-upnp-org:service:ContentDirectory:2");
	wpa_command(intf, "P2P_SERVICE_ADD upnp 10 uuid:3859dede-8574-59ab-9332-123456789012::urn:schemas-upnp-org:device:InternetGatewayDevice:1");
}


void disconnect_station(struct sigma_dut *dut)
{
	wpa_command(get_station_ifname(dut), "DISCONNECT");
	remove_wpa_networks(get_station_ifname(dut));
	dut->infra_ssid[0] = '\0';
#ifdef __linux__
	{
		char path[128];
		char buf[200];
		struct stat s;
		snprintf(path, sizeof(path), "/var/run/dhclient-%s.pid",
			 get_station_ifname(dut));
		if (stat(path, &s) == 0) {
			snprintf(buf, sizeof(buf),
				 "kill `cat %s`", path);
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Kill previous DHCP client: %s", buf);
			run_system(dut, buf);
			unlink(path);
		}
		snprintf(buf, sizeof(buf),
			 "ifconfig %s 0.0.0.0", get_station_ifname(dut));
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Clear infrastructure station IP address: %s",
				buf);
		run_system(dut, buf);
   }
#endif /* __linux__ */
}


static enum sigma_cmd_result
cmd_sta_get_p2p_dev_address(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	char buf[100], resp[200];

	start_sta_mode(dut);
	if (get_wpa_status(intf, "p2p_device_address", buf, sizeof(buf)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	snprintf(resp, sizeof(resp), "DevID,%s", buf);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static enum sigma_cmd_result cmd_sta_set_p2p(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	char buf[256];
	const char *val;
	const char *noa_dur, *noa_int, *noa_count;
	const char *ext_listen_int, *ext_listen_period;

	val = get_param(cmd, "LISTEN_CHN");
	if (val) {
		dut->listen_chn = atoi(val);
		if (dut->listen_chn == 2) {
			/* social channel 2 on 60 GHz band */
			snprintf(buf, sizeof(buf),
				 "P2P_SET listen_channel 2 180");
		} else {
			/* social channels 1/6/11 on 2.4 GHz band */
			snprintf(buf, sizeof(buf), "P2P_SET listen_channel %d",
				 dut->listen_chn);
		}
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	ext_listen_int = get_param(cmd, "Ext_Listen_Time_Interval");
	ext_listen_period = get_param(cmd, "Ext_Listen_Time_Period");

	if (ext_listen_int || ext_listen_period) {
		if (!ext_listen_int || !ext_listen_period) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Only one "
					"ext_listen_time parameter included; "
					"both are needed");
			return -1;
		}
		snprintf(buf, sizeof(buf), "P2P_EXT_LISTEN %d %d",
			 atoi(ext_listen_period),
			 atoi(ext_listen_int));
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	val = get_param(cmd, "P2P_MODE");
	if (val) {
		if (strcasecmp(val, "Listen") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_LISTEN") < 0)
				return -2;
			dut->p2p_mode = P2P_LISTEN;
		} else if (strcasecmp(val, "Discover") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_FIND") < 0)
				return -2;
			dut->p2p_mode = P2P_DISCOVER;
		} else if (strcasecmp(val, "Idle") == 0) {
			wpa_command(intf, "P2P_SET disabled 0");
			if (wpa_command(intf, "P2P_STOP_FIND") < 0)
				return -2;
			dut->p2p_mode = P2P_IDLE;
		} else if (strcasecmp(val, "Disable") == 0) {
			if (wpa_command(intf, "P2P_SET disabled 1") < 0)
				return -2;
			dut->p2p_mode = P2P_DISABLE;
		} else
			return -1;
	}

	val = get_param(cmd, "PERSISTENT");
	if (val) {
		dut->persistent = atoi(val);
	}

	val = get_param(cmd, "INTRA_BSS");
	if (val) {
		int intra_bss = atoi(val);
		/* TODO: add support for this */
		if (!intra_bss) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Disabling of "
					"intra-BSS bridging not supported");
			return -1;
		}
		dut->intra_bss = intra_bss;
	}

	/* NoA is not applicable for 60 GHz */
	if (dut->program != PROGRAM_60GHZ) {
		noa_dur = get_param(cmd, "NoA_duration");
		noa_int = get_param(cmd, "NoA_Interval");
		noa_count = get_param(cmd, "NoA_Count");
		if (noa_dur)
			dut->noa_duration = atoi(noa_dur);

		if (noa_int)
			dut->noa_interval = atoi(noa_int);

		if (noa_count)
			dut->noa_count = atoi(noa_count);

		if (noa_dur || noa_int || noa_count) {
			int start = 0;
			const char *ifname;

			snprintf(buf, sizeof(buf),
				 "DRIVER P2P_SET_NOA %d %d %d %d",
				 dut->noa_count, start,
				 dut->noa_duration, dut->noa_interval);
			ifname = get_group_ifname(dut, intf);
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Set GO NoA for interface %s", ifname);

			if (wpa_command(ifname, buf) == 0)
				goto noa_done;

			if (dut->noa_count == 0 && dut->noa_duration == 0)
				start = 0;
			else if (dut->noa_duration > 102) /* likely non-periodic
							   * NoA */
				start = 50;
			else
				start = 102 - dut->noa_duration;
			snprintf(buf, sizeof(buf), "P2P_SET noa %d,%d,%d",
				dut->noa_count, start,
				dut->noa_duration);
			if (wpa_command(ifname, buf) == 0)
				goto noa_done;

			snprintf(buf, sizeof(buf), "P2P_SET_NOA %d %d %d %d",
				 dut->noa_count, start, dut->noa_duration,
				 dut->noa_interval);
			if (wcn_driver_cmd(intf, buf) < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Use of NoA as GO not supported");
				return 0;
			}
		}
	}
noa_done:

	val = get_param(cmd, "Concurrency");
	if (val) {
		/* TODO */
	}

	val = get_param(cmd, "P2PInvitation");
	if (val) {
		/* TODO */
	}

	val = get_param(cmd, "BCN_INT");
	if (val) {
		/* TODO */
	}

	val = get_param(cmd, "Discoverability");
	if (val) {
		snprintf(buf, sizeof(buf), "P2P_SET discoverability %d",
			 atoi(val));
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	val = get_param(cmd, "GenNewDeviceAddr");
	if (val && atoi(val) == 1) {
		sigma_dut_print(dut, DUT_MSG_INFO,
				"Generate new P2P Device Address");
		if (wpa_command(intf, "NEW_RANDOM_MAC_ADDRESS") < 0)
			return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "PASN");
	if (val && strcasecmp(val, "Enable") == 0) {
		/* Support Wi-Fi Direct R2 capabilities */
		sigma_dut_print(dut, DUT_MSG_INFO, "PASN Enable");
		val = get_param(cmd, "PASN_Unsupported_DH_Group");
		if (val) {
			if (atoi(val) == 20)
				dut->pasn_type &= ~0xc;
			else if (atoi(val) == 19)
				dut->pasn_type &= ~0x3;
		}
		snprintf(buf, sizeof(buf), "P2P_SET pasn_type %d",
			 dut->pasn_type);
		if (wpa_command(intf, buf) < 0)
			return ERROR_SEND_STATUS;
	}

	val = get_param(cmd, "Service_Discovery");
	if (val) {
		int sd = atoi(val);
		if (sd) {
			wpa_command(intf, "P2P_SERVICE_FLUSH");

			if (sd == 2)
				wpa_command(intf, "P2P_SET force_long_sd 1");

			/*
			 * Set up some dummy service to create a large SD
			 * response that requires fragmentation.
			 */
			add_dummy_services(intf);
		} else {
			wpa_command(intf, "P2P_SERVICE_FLUSH");
		}
	}

	val = get_param(cmd, "CrossConnection");
	if (val) {
		if (atoi(val)) {
			if (wpa_command(intf, "P2P_SET cross_connect 1") < 0)
				return -2;
		} else {
			if (wpa_command(intf, "P2P_SET cross_connect 0") < 0)
				return -2;
		}
	}

	val = get_param(cmd, "P2PManaged");
	if (val) {
		if (atoi(val)) {
			send_resp(dut, conn, SIGMA_INVALID, "ErrorCode,"
				  "P2P Managed functionality not supported");
			return 0;
		}
	}

	val = get_param(cmd, "GO_APSD");
	if (val) {
		if (atoi(val)) {
			if (wpa_command(intf, "P2P_SET go_apsd 1") < 0)
				return -2;
		} else {
			if (wpa_command(intf, "P2P_SET go_apsd 0") < 0)
				return -2;
		}
	}

	val = get_param(cmd, "UnsyncServDsc");
	if (val) {
		if (strcasecmp(val, "On") == 0) {
			/* Support USD functionality */
			dut->usd_enabled = true;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Unsynchronized Service Discovery ON");
		} else {
			dut->usd_enabled = false;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Unsynchronized Service Discovery OFF");
		}
	}

	val  = get_param(cmd, "WFDR2Capabilities");
	if (val) {
		if (strcasecmp(val, "On") == 0) {
			/* Support Wi-Fi Direct R2 capabilities */
			dut->p2p_r2_capable = true;
			dut->persistent = 1;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Wi-Fi Direct R2 Capabilities ON");
			snprintf(buf, sizeof(buf),
				 "P2P_SET chan_switch_req_enable 1");
			if (wpa_command(intf, buf) < 0)
				return ERROR_SEND_STATUS;
			wpa_command(intf, "P2P_SET reginfo 2");
		} else {
			dut->p2p_r2_capable = false;
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Wi-Fi Direct R2 Capabilities OFF");
		}

	}

	val = get_param(cmd, "TWT_Power_Management");
	if (val) {
		int twt_pm = get_enable_disable(val);

		snprintf(buf, sizeof(buf), "P2P_SET twt_power_mgmt %d", twt_pm);
		if (wpa_command(intf, buf) < 0)
			return ERROR_SEND_STATUS;

		dut->is_p2p_twt_power_mgmt_enabled = twt_pm;
		sigma_dut_print(dut, DUT_MSG_INFO,
				"TWT_Power_Management enable: %d", twt_pm);
	}

	val = get_param(cmd, "UnavailabilityMode");
	if (val)
		dut->twt_param.unavailability_mode = !!atoi(val);

	val = get_param(cmd, "TWT_Trigger");
	if (val)
		dut->twt_param.twt_trigger = !!atoi(val);

	val = get_param(cmd, "WakeIntervalMantissa");
	if (val)
		dut->twt_param.wake_interval_mantissa = atoi(val);

	val = get_param(cmd, "WakeIntervalExp");
	if (val)
		dut->twt_param.wake_interval_exp = atoi(val);

	val = get_param(cmd, "NominalMinWakeDur");
	if (val)
		dut->twt_param.nominal_min_wake_dur = atoi(val);

	val = get_param(cmd, "BTWT_ID");
	if (val) {
		dut->twt_param.bcast_twt_id = atoi(val);
		dut->twt_param.is_bcast_twt = true;
		dut->twt_param.is_user_config = true;
		dut->twt_param.responder_pm = 1;
	}

	val = get_param(cmd, "BTWT_Persistence");
	if (val)
		dut->twt_param.bcast_twt_persis = atoi(val);

	val = get_param(cmd, "BTWT_Recommendation");
	if (val)
		dut->twt_param.bcast_twt_recommdn = atoi(val);

	return 1;
}


static int set_p2p_twt_unavailability_mode(struct sigma_dut *dut,
					   struct sigma_conn *conn,
					   const char *intf)
{
#ifdef NL80211_SUPPORT
	struct nlattr *params;
	struct nlattr *attr;
	struct nl_msg *msg;
	int ifindex, ret;

	ifindex = if_nametoindex(intf);
	if (ifindex == 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: Index for interface %s failed",
				__func__, intf);
		return -1;
	}

	msg = nl80211_drv_msg(dut, dut->nl_ctx, ifindex, 0,
			      NL80211_CMD_VENDOR);
	if (!msg ||
	    nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_ID, OUI_QCA) ||
	    nla_put_u32(msg, NL80211_ATTR_VENDOR_SUBCMD,
			QCA_NL80211_VENDOR_SUBCMD_CONFIG_TWT)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_cmd", __func__);
		nlmsg_free(msg);
		return -1;
	}

	attr = nla_nest_start(msg, NL80211_ATTR_VENDOR_DATA);
	if (!attr ||
	    nla_put_u8(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_OPERATION,
		       QCA_WLAN_TWT_SET_PARAM)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor attr", __func__);
		nlmsg_free(msg);
		return -1;
	}

	params = nla_nest_start(msg, QCA_WLAN_VENDOR_ATTR_CONFIG_TWT_PARAMS);
	if (!params ||
	    nla_put_flag(msg,
			 QCA_WLAN_VENDOR_ATTR_TWT_SET_PARAM_UNAVAILABILITY_MODE)) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in adding vendor_params", __func__);
		nlmsg_free(msg);
		return -1;
	}

	nla_nest_end(msg, params);
	nla_nest_end(msg, attr);

	ret = send_and_recv_msgs(dut, dut->nl_ctx, msg, NULL, NULL);
	if (ret)
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"%s: err in send_and_recv_msgs, ret=%d",
				__func__, ret);

	return 0;
#else /* NL80211_SUPPORT */
	sigma_dut_print(dut, DUT_MSG_ERROR,
			"TWT unavailability mode cannot be done without NL80211_SUPPORT defined");
	return -1;
#endif /* NL80211_SUPPORT */
}


static int set_p2p_twt_params(struct sigma_dut *dut, struct sigma_conn *conn,
			      const char *intf)
{
	/* Set default param when user config is not available. */
	if (!dut->twt_param.is_user_config) {
		dut->twt_param.is_bcast_twt = true;
		dut->twt_param.unavailability_mode = false;
		dut->twt_param.twt_trigger = false;
		dut->twt_param.flow_type = 1;
		dut->twt_param.protection = 0;
		dut->twt_param.target_wake_time = 0;
		dut->twt_param.wake_interval_mantissa = 112;
		/* SI = pow(2, exp) * mantissa */
		dut->twt_param.wake_interval_exp = 10;
		/* SP = val * 256 us */
		dut->twt_param.nominal_min_wake_dur = 78;
		dut->twt_param.bcast_twt_id = 0;
		dut->twt_param.bcast_twt_persis = 0;
		dut->twt_param.bcast_twt_recommdn = 0;
		dut->twt_param.responder_pm = 1;
	}

	dut->twt_param.cmd_type = 1;
	dut->twt_param.ifindex = if_nametoindex(intf);
	if (sta_twt_request(dut, conn, NULL) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set default BCAST TWT parms");
		return -1;
	}

	/* Configure Unavailability Mode bit in control field of TWT element */
	if (dut->twt_param.unavailability_mode &&
	    set_p2p_twt_unavailability_mode(dut, conn, intf) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set unavailability mode");
		return -1;
	}
	return 0;
}


static enum sigma_cmd_result
cmd_sta_start_autonomous_go(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	const char *oper_chn = get_param(cmd, "OPER_CHN");
	const char *ssid_param = get_param(cmd, "SSID");
#ifdef MIRACAST
	const char *rtsp = get_param(cmd, "RTSP");
#endif /* MIRACAST */
	int freq, chan, res;
	char buf[256], grpid[100], resp[200];
	struct wpa_ctrl *ctrl;
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char *go_dev_addr;

	if (oper_chn == NULL)
		return -1;

	chan = atoi(oper_chn);
	if (dut->program == PROGRAM_60GHZ) {
		freq = get_60g_freq(chan);
		if (freq == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid channel: %d", chan);
			return -1;
		}
	} else if (chan >= 1 && chan <= 13)
		freq = 2407 + chan * 5;
	else if (chan == 14)
		freq = 2484;
	else if (chan >= 36 && chan <= 165)
		freq = 5000 + chan * 5;
	else {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Invalid channel: %d", chan);
		return -1;
	}

	if (ssid_param)
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix %s",
			 ssid_param);
	else
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix ");
	if (wpa_command(intf, buf) < 0)
		return -2;

	/* Stop Listen/Discovery state to avoid issues with GO operations */
	if (wpa_command(intf, "P2P_STOP_FIND") < 0)
		return -2;

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (dut->p2p_r2_capable) {
		const char *type = get_param(cmd, "type");
		int p2p_mode = 0;

		if (type && strcasecmp(type, "PCC") == 0)
			p2p_mode = 2;

		snprintf(buf, sizeof(buf),
			 "P2P_GROUP_ADD %sfreq=%d p2pmode=%d he p2p2",
			 dut->persistent ? "persistent " : "", freq, p2p_mode);
	} else {
		snprintf(buf, sizeof(buf), "P2P_GROUP_ADD %sfreq=%d",
			 dut->persistent ? "persistent " : "", freq);
	}

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -2;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,GO starting "
			  "did not complete");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group started event '%s'", buf);
	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group SSID %s", ssid);

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (go_dev_addr == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No GO P2P Device Address "
				"found");
		return -2;
	}
	go_dev_addr += 12;
	if (strlen(go_dev_addr) < 17) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Too short GO P2P Device "
				"Address '%s'", go_dev_addr);
		return -2;
	}
	go_dev_addr[17] = '\0';
	*pos = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "GO P2P Device Address %s",
			go_dev_addr);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid,
		      NULL);

	snprintf(resp, sizeof(resp), "GroupID,%s", grpid);

#ifdef MIRACAST
	if (rtsp && atoi(rtsp) == 1) {
		/* Start RTSP Thread for incoming connections */
		miracast_start_autonomous_go(dut, conn, cmd, ifname);
	}
#endif /* MIRACAST */

	if (dut->is_p2p_twt_power_mgmt_enabled &&
	    set_p2p_twt_params(dut, conn, ifname) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to set default TWT-based P2P Power Management");
		return ERROR_SEND_STATUS;
	}

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static enum sigma_cmd_result sta_p2p_client_connect(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	int i, ret = 0;
	const char *ssid;
	char buf[256], *pos, *end;
	struct wpa_ctrl *ctrl = NULL;
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	const char *grpid = get_param(cmd, "GroupID");
	const char *mac = get_param(cmd, "P2PDevID");
	const char *password = get_param(cmd, "Passphrase");
	char ssid_hex[100];

	if (!intf || !grpid || !mac) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
			  "Interface or groupID or peermac unknown");
		return INVALID_SEND_STATUS;
	}

	ssid = strstr(grpid, " ");
	if (!ssid || strlen(ssid) * 2 >= sizeof(ssid_hex)) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,SSID not found");
		return STATUS_SENT_ERROR;
	}
	ssid++;

	memset(buf, 0, sizeof(buf));
	pos = buf;
	end = buf + sizeof(buf);

	pos += snprintf(pos, end - pos, "P2P_CONNECT %s pair he skip_prov join",
			mac);

	for (i = 0; i < strlen(ssid); i++) {
		ret = snprintf(ssid_hex + i * 2, sizeof(ssid_hex) - i * 2,
			       "%02x", ssid[i]);
		if (ret < 0 || ret >= sizeof(ssid_hex) - i * 2)
			return ERROR_SEND_STATUS;
	}

	ssid_hex[strlen(ssid) * 2] = '\0';

	pos += snprintf(pos, end - pos, " ssid=%s", ssid_hex);

	if (dut->p2p_r2_capable)
		pos += snprintf(pos, end - pos, " p2p2");

	if (password)
		pos += snprintf(pos, end - pos, " password=%s", password);

	ctrl = open_wpa_mon(intf);
	if (!ctrl)
		return ERROR_SEND_STATUS;

	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to connect");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return ERROR_SEND_STATUS;
	}

	ret = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (ret < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "Result,Association did not complete");
		return STATUS_SENT_ERROR;
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Connection event: %s", buf);
	if (!dut->p2p_r2_capable) {
		/*
		 * Interface MAC address will be changed by
		 * wpa_supplicant before connection attempt when
		 * client privacy enabled. Restart DHCP client
		 * to make sure DHCP frames use the correct
		 * source MAC address.
		 */
		kill_dhcp_client(dut, intf);
		if (start_dhcp_client(dut, intf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "Result,DHCP client start failed");
			return STATUS_SENT_ERROR;
		}
	}
	send_resp(dut, conn, SIGMA_COMPLETE,
		  "Result,Connection success");
	return STATUS_SENT;
}


static enum sigma_cmd_result cmd_sta_p2p_connect(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	const char *devid = get_param(cmd, "P2PDevID");
	const char *grpid_param = get_param(cmd, "GroupID");
	int res;
	char buf[256];
	struct wpa_ctrl *ctrl;
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char grpid[100];

	if (devid == NULL)
		return -1;

	if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
		/* P2P Client Infrastructure BSS connection */
		if (grpid_param)
			return sta_p2p_client_connect(dut, conn, cmd);

		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,WPS parameters not yet set");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trying to discover GO %s", devid);
	if (p2p_discover_peer(dut, intf, devid, 1) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	switch (dut->wps_method) {
	case WFA_CS_WPS_PBC:
		snprintf(buf, sizeof(buf), "P2P_CONNECT %s pbc join",
			 devid);
		break;
	case WFA_CS_WPS_PIN_DISPLAY:
		snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s display join",
			 devid, dut->wps_pin);
		break;
	case WFA_CS_WPS_PIN_KEYPAD:
		snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s keypad join",
			 devid, dut->wps_pin);
		break;
	default:
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unknown WPS "
			  "method for sta_p2p_connect");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to join "
			  "the group");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Group joining "
			  "did not complete");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group started event '%s'", buf);
	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group SSID %s", ssid);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", bssid, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid,
		      devid);

	return 1;
}


static int p2p_group_formation_event(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct wpa_ctrl *ctrl,
				     const char *intf, const char *peer_role,
				     int nfc);

static enum sigma_cmd_result
cmd_sta_p2p_start_group_formation(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	const char *devid = get_param(cmd, "P2PDevID");
	const char *intent_val = get_param(cmd, "INTENT_VAL");
	const char *init_go_neg = get_param(cmd, "INIT_GO_NEG");
	const char *oper_chn = get_param(cmd, "OPER_CHN");
	const char *ssid_param = get_param(cmd, "SSID");
	int freq = 0, chan = 0, init;
	char buf[256];
	struct wpa_ctrl *ctrl;
	int intent;

	if (devid == NULL || intent_val == NULL)
		return -1;

	intent = atoi(intent_val);
	if (intent > 15)
		intent = 1;
	if (init_go_neg)
		init = atoi(init_go_neg);
	else
		init = 0;

	if (dut->program == PROGRAM_60GHZ) {
		if (!oper_chn)
			return -1;
		chan = atoi(oper_chn);
		freq = get_60g_freq(chan);
		if (freq == 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid channel: %d", chan);
			return -1;
		}
	} else if (oper_chn) {
		chan = atoi(oper_chn);
		if (chan >= 1 && chan <= 13)
			freq = 2407 + chan * 5;
		else if (chan == 14)
			freq = 2484;
		else if (chan >= 36 && chan <= 165)
			freq = 5000 + chan * 5;
		else {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Invalid channel: %d", chan);
			return -1;
		}
	}

	if (dut->wps_method == WFA_CS_WPS_NOT_READY) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS parameters "
			  "not yet set");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Trying to discover peer %s for group formation chan %d (freq %d)",
			devid, chan, freq);
	if (p2p_discover_peer(dut, intf, devid, init) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return 0;
	}

	if (ssid_param)
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix %s",
			 ssid_param);
	else
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix ");
	if (wpa_command(intf, buf) < 0)
		return -2;

	if (init) {
		ctrl = open_wpa_mon(intf);
		if (ctrl == NULL) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
					"wpa_supplicant monitor connection");
			return -2;
		}
	} else
		ctrl = NULL;

	snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s%s%s%s go_intent=%d",
		 devid,
		 dut->wps_method == WFA_CS_WPS_PBC ?
		 "pbc" : dut->wps_pin,
		 dut->wps_method == WFA_CS_WPS_PBC ? "" :
		 (dut->wps_method == WFA_CS_WPS_PIN_DISPLAY ? " display" :
		  (dut->wps_method == WFA_CS_WPS_PIN_LABEL ? " label" :
		   " keypad" )),
		 dut->persistent ? " persistent" : "",
		 init ? "" : " auth",
		 intent);
	if (freq > 0) {
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf),
			 " freq=%d", freq);
	}
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Failed to start "
			  "group formation");
		if (ctrl) {
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
		}
		return 0;
	}

	if (!init)
		return 1;

	return p2p_group_formation_event(dut, conn, ctrl, intf, NULL, 0);
}


static int p2p_group_formation_event(struct sigma_dut *dut,
				     struct sigma_conn *conn,
				     struct wpa_ctrl *ctrl,
				     const char *intf, const char *peer_role,
				     int nfc)
{
	int res;
	char buf[256], grpid[50], resp[256];
	char *ifname, *gtype, *pos, *ssid, bssid[20];
	char *go_dev_addr;
	char role[30];
	const char *events[] = {
		"P2P-GROUP-STARTED",
		"P2P-GO-NEG-FAILURE",
		"P2P-NFC-PEER-CLIENT",
		"P2P-GROUP-FORMATION-FAILURE",
		NULL
	};

	role[0] = '\0';
	if (peer_role)
		snprintf(role, sizeof(role), ",PeerRole,%s", peer_role);

	res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Group formation "
			  "did not complete");
		return 0;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group started event '%s'", buf);

	if (strstr(buf, "P2P-NFC-PEER-CLIENT")) {
		snprintf(resp, sizeof(resp),
			 "Result,,GroupID,,PeerRole,1,PauseFlag,0");
		send_resp(dut, conn, SIGMA_COMPLETE, resp);
		return 0;
	}

	if (strstr(buf, "P2P-GROUP-FORMATION-FAILURE")) {
		snprintf(buf, sizeof(buf), "ErrorCode,Group formation failed");
		send_resp(dut, conn, SIGMA_ERROR, buf);
		return 0;
	}

	if (strstr(buf, "P2P-GO-NEG-FAILURE")) {
		int status = -1;
		pos = strstr(buf, " status=");
		if (pos)
			status = atoi(pos + 8);
		sigma_dut_print(dut, DUT_MSG_INFO, "GO Negotiation failed "
				"(status=%d)", status);
		if (status == 9) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Both devices "
					"tried to use GO Intent 15");
			send_resp(dut, conn, SIGMA_COMPLETE, "result,FAIL");
			return 0;
		}
		snprintf(buf, sizeof(buf), "ErrorCode,GO Negotiation failed "
			 "(status=%d)", status);
		send_resp(dut, conn, SIGMA_ERROR, buf);
		return 0;
	}

	ifname = strchr(buf, ' ');
	if (ifname == NULL)
		return -2;
	ifname++;
	pos = strchr(ifname, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (ssid == NULL)
		return -2;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (pos == NULL)
		return -2;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group SSID %s", ssid);

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (go_dev_addr == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "No GO P2P Device Address "
				"found\n");
		return -2;
	}
	go_dev_addr += 12;
	if (strlen(go_dev_addr) < 17) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Too short GO P2P Device "
				"Address '%s'", go_dev_addr);
		return -2;
	}
	go_dev_addr[17] = '\0';
	*pos = '\0';
	sigma_dut_print(dut, DUT_MSG_ERROR, "GO P2P Device Address %s",
			go_dev_addr);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return -2;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr, ssid);
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid, NULL);
	snprintf(resp, sizeof(resp), "Result,%s,GroupID,%s%s%s",
		 strcmp(gtype, "GO") == 0 ? "GO" : "CLIENT", grpid, role,
		 nfc ? ",PauseFlag,0" : "");
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

#ifdef __QNXNTO__
	/* Start DHCP server if we became the GO */
	if (strcmp(gtype, "GO") == 0 &&
	    system("dhcpd -cf /etc/dhcpd.conf -pf /var/run/dhcpd qca1 &") == 0)
	     sigma_dut_print(dut, DUT_MSG_ERROR,
			     "Failed to start DHCPD server");
#endif /* __QNXNTO__ */

	return 0;
}


int wps_connection_event(struct sigma_dut *dut, struct sigma_conn *conn,
			 struct wpa_ctrl *ctrl, const char *intf, int p2p_resp)
{
	int res;
	char buf[256];
	const char *events[] = {
		"CTRL-EVENT-CONNECTED",
		"WPS-FAIL",
		"WPS-TIMEOUT",
		NULL
	};

	res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
#ifdef USE_ERROR_RETURNS
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS connection "
			  "did not complete");
#else
		send_resp(dut, conn, SIGMA_COMPLETE, "ErrorCode,WPS connection "
			  "did not complete");
#endif
		return 0;
	}

	if (strstr(buf, "WPS-FAIL") || strstr(buf, "WPS-TIMEOUT")) {
#ifdef USE_ERROR_RETURNS
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,WPS operation "
			  "failed");
#else
		send_resp(dut, conn, SIGMA_COMPLETE, "ErrorCode,WPS operation "
			  "failed");
#endif
		return 0;
	}

	if (!p2p_resp)
		return 1;
	send_resp(dut, conn, SIGMA_COMPLETE, "Result,,GroupID,,PeerRole,");
	return 0;
}


static enum sigma_cmd_result cmd_sta_p2p_dissolve(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *grpid = get_param(cmd, "GroupID");
	struct wfa_cs_p2p_group *grp;
	char buf[128];

	if (grpid == NULL)
		return -1;

	wpa_command(intf, "NAN_FLUSH");
	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Requested group "
			  "not found");
		return 0;
	}

	snprintf(buf, sizeof(buf), "P2P_GROUP_REMOVE %s", grp->ifname);
	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to remove the "
				"specified group from wpa_supplicant - assume "
				"group has already been removed");
	}
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Removed group %s", grpid);
	if (grp->go)
		dut->go = 0;
	else
		dut->p2p_client = 0;
	p2p_group_remove(dut, grpid);
	return 1;
}


static enum sigma_cmd_result
cmd_sta_send_p2p_invitation_req(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *devid = get_param(cmd, "P2PDevID");
	const char *grpid = get_param(cmd, "GroupID");
	const char *reinvoke = get_param(cmd, "Reinvoke");
	char c[256];
	char buf[4096];
	struct wpa_ctrl *ctrl;
	int res;
	const char *events[] = {
		"P2P-INVITATION-RESULT",
		"P2P-INVITATION-ACCEPTED",
		NULL
	};

	if (devid == NULL || grpid == NULL)
		return -1;

	if (reinvoke && atoi(reinvoke)) {
		int id = -1;
		char *ssid, *pos;

		ssid = strchr(grpid, ' ');
		if (ssid == NULL) {
			sigma_dut_print(dut, DUT_MSG_INFO, "Invalid grpid");
			return -1;
		}
		ssid++;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Search for persistent "
				"group credentials based on SSID: '%s'", ssid);
		if (wpa_command_resp(intf, "LIST_NETWORKS",
				     buf, sizeof(buf)) < 0)
			return -2;
		pos = strstr(buf, ssid);
		if (pos == NULL || pos == buf || pos[-1] != '\t' ||
		    pos[strlen(ssid)] != '\t') {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "Persistent group credentials not found");
			return 0;
		}
		while (pos > buf && pos[-1] != '\n')
			pos--;
		id = atoi(pos);
		snprintf(c, sizeof(c), "P2P_INVITE persistent=%d peer=%s",
			 id, devid);
	} else {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,"
				  "No active P2P group found for invitation");
			return 0;
		}
		snprintf(c, sizeof(c), "P2P_INVITE group=%s peer=%s",
			 grp->ifname, devid);
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trying to discover peer %s for "
			"invitation", devid);
	if (p2p_discover_peer(dut, intf, devid, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return 0;
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (wpa_command(intf, c) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to send invitation "
				"request");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return -2;
	}

	res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0)
		return -2;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Invitation event: '%s'", buf);
	return 1;
}


static enum sigma_cmd_result
cmd_sta_accept_p2p_invitation_req(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *devid = get_param(cmd, "P2PDevID");
	const char *grpid = get_param(cmd, "GroupID");
	const char *reinvoke = get_param(cmd, "Reinvoke");
	char buf[100];

	if (devid == NULL || grpid == NULL)
		return -1;

	if (reinvoke && atoi(reinvoke)) {
		/*
		 * Assume persistent reconnect is enabled and there is no need
		 * to do anything here.
		 */
		return 1;
	}

	/*
	 * In a client-joining-a-running-group case, we need to separately
	 * authorize the invitation.
	 */

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trying to discover GO %s", devid);
	if (p2p_discover_peer(dut, intf, devid, 1) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return 0;
	}

	snprintf(buf, sizeof(buf), "P2P_CONNECT %s %s join auth",
		 devid,
		 dut->wps_method == WFA_CS_WPS_PBC ?
		 "pbc" : dut->wps_pin);
	if (wpa_command(intf, buf) < 0)
		return -2;

	return 1;
}


static enum sigma_cmd_result
cmd_sta_send_p2p_provision_dis_req(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *conf_method = get_param(cmd, "ConfigMethod");
	const char *devid = get_param(cmd, "P2PDevID");
	char buf[256];
	char *method;

	if (conf_method == NULL || devid == NULL)
		return -1;

	if (strcasecmp(conf_method, "Display") == 0)
		method = "display";
	else if (strcasecmp(conf_method, "Keyboard") == 0 ||
		 strcasecmp(conf_method, "keypad") == 0)
		method = "keypad";
	else if (strcasecmp(conf_method, "Label") == 0)
		method = "label";
	else if (strcasecmp(conf_method, "pbc") == 0 ||
		 strcasecmp(conf_method, "pushbutton") == 0)
		method = "pbc";
	else
		return -1;

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Trying to discover peer %s for "
			"provision discovery", devid);
	if (p2p_discover_peer(dut, intf, devid, 0) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Could not "
			  "discover the requested peer");
		return 0;
	}

	snprintf(buf, sizeof(buf), "P2P_PROV_DISC %s %s", devid, method);
	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to send provision "
				"discovery request");
		return -2;
	}

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_wps_pbc(struct sigma_dut *dut,
						 struct sigma_conn *conn,
						 struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = get_param(cmd, "GroupID");

	if (grpid) {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			wpa_command(grp->ifname, "WPS_PBC");
			return 1;
		}
	}

	dut->wps_method = WFA_CS_WPS_PBC;
	return 1;
}


static enum sigma_cmd_result cmd_sta_wps_read_pin(struct sigma_dut *dut,
						  struct sigma_conn *conn,
						  struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *grpid = get_param(cmd, "GroupID");
	char pin[9], addr[20];
	char resp[100];

	if (get_wpa_status(intf, "address", addr, sizeof(addr)) < 0 ||
	    get_wps_pin_from_mac(dut, addr, pin, sizeof(pin)) < 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Failed to calculate PIN from MAC, use default");
		strlcpy(pin, "12345670", sizeof(pin));
	}

	if (grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			if (wpa_command(grp->ifname, buf) < 0)
				return -1;
			goto done;
		}
	}

	strlcpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_method = WFA_CS_WPS_PIN_DISPLAY;
done:
	snprintf(resp, sizeof(resp), "PIN,%s", pin);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

	return 0;
}


static enum sigma_cmd_result cmd_sta_wps_read_label(struct sigma_dut *dut,
						    struct sigma_conn *conn,
						    struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = get_param(cmd, "GroupID");
	char *pin = "12345670";
	char resp[100];

	if (grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			wpa_command(grp->ifname, buf);
			return 1;
		}
	}

	strlcpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_method = WFA_CS_WPS_PIN_LABEL;
	snprintf(resp, sizeof(resp), "LABEL,%s", pin);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

	return 0;
}


static enum sigma_cmd_result cmd_sta_wps_enter_pin(struct sigma_dut *dut,
						   struct sigma_conn *conn,
						   struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *grpid = get_param(cmd, "GroupID");
	const char *pin = get_param(cmd, "PIN");

	if (pin == NULL)
		return -1;

	if (grpid) {
		char buf[100];
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp && grp->go) {
			sigma_dut_print(dut, DUT_MSG_DEBUG, "Authorize a "
					"client to join with WPS");
			snprintf(buf, sizeof(buf), "WPS_PIN any %s", pin);
			wpa_command(grp->ifname, buf);
			return 1;
		}
	}

	strlcpy(dut->wps_pin, pin, sizeof(dut->wps_pin));
	dut->wps_pin[sizeof(dut->wps_pin) - 1] = '\0';
	dut->wps_method = WFA_CS_WPS_PIN_KEYPAD;

	return 1;
}


static enum sigma_cmd_result cmd_sta_get_psk(struct sigma_dut *dut,
					     struct sigma_conn *conn,
					     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "interface"); */
	const char *grpid = get_param(cmd, "GroupID");
	struct wfa_cs_p2p_group *grp;
	char passphrase[64], resp[200];

	if (grpid == NULL)
		return -1;

	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Requested group not found");
		return 0;
	}
	if (!grp->go) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Local role is not GO in the specified "
			  "group");
		return 0;
	}

	if (wpa_command_resp(grp->ifname, "P2P_GET_PASSPHRASE",
			     passphrase, sizeof(passphrase)) < 0)
		return -2;

	snprintf(resp, sizeof(resp), "passPhrase,%s,ssid,%s",
		 passphrase, grp->ssid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);

	return 0;
}


static bool stop_p2p_resp_event_rx = false;

static void stop_p2p_event_mon_thread(struct sigma_dut *dut)
{
	if (dut->p2p_event_mon_thread) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Stopping P2P event monitoring thread");
		dut->p2p_event_mon_thread = 0;
		stop_p2p_resp_event_rx = true;
	}
}


enum sigma_cmd_result sta_p2p_reset_default(struct sigma_dut *dut,
					    struct sigma_conn *conn,
					    struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	struct wfa_cs_p2p_group *grp, *prev;
	char buf[256];

#ifdef MIRACAST
	if (dut->program == PROGRAM_WFD ||
	    dut->program == PROGRAM_DISPLAYR2)
		miracast_sta_reset_default(dut, conn, cmd);
#endif /* MIRACAST */

	dut->go = 0;
	dut->p2p_client = 0;
	dut->wps_method = WFA_CS_WPS_NOT_READY;
	dut->pasn_type = 0xf;
	dut->is_p2p_twt_power_mgmt_enabled = false;
	dut->twt_param.is_user_config = false;
	dut->twt_param.is_bcast_twt = false;
	dut->twt_param.unavailability_mode = false;

	grp = dut->groups;
	while (grp) {
		prev = grp;
		grp = grp->next;

		snprintf(buf, sizeof(buf), "P2P_GROUP_REMOVE %s",
			 prev->ifname);
		wpa_command(intf, buf);
		p2p_group_remove(dut, prev->grpid);
	}

	stop_p2p_event_mon_thread(dut);

	wpa_command(intf, "P2P_GROUP_REMOVE *");
	wpa_command(intf, "P2P_STOP_FIND");
	wpa_command(intf, "P2P_FLUSH");
	wpa_command(intf, "NAN_FLUSH");
	wpa_command(intf, "P2P_SERVICE_FLUSH");
	wpa_command(intf, "P2P_SET disabled 0");
	wpa_command(intf, "P2P_SET ssid_postfix ");
	wpa_command(intf, "P2P_REMOVE_IDENTITY");

	if (dut->program == PROGRAM_60GHZ) {
		wpa_command(intf, "SET p2p_oper_reg_class 180");
		wpa_command(intf, "P2P_SET listen_channel 2 180");
		dut->listen_chn = 2;
	} else {
		wpa_command(intf, "P2P_SET listen_channel 6");
		dut->listen_chn = 6;
	}

	wpa_command(intf, "P2P_EXT_LISTEN");
	wpa_command(intf, "SET p2p_go_intent 7");
	wpa_command(intf, "P2P_SET client_apsd disable");
	wpa_command(intf, "P2P_SET go_apsd disable");
	wpa_command(get_station_ifname(dut), "P2P_SET ps 98");
	wpa_command(get_station_ifname(dut), "P2P_SET ps 96");
	wpa_command(get_station_ifname(dut), "P2P_SET ps 0");
	wpa_command(intf, "P2P_SET ps 0");
	wpa_command(intf, "P2P_SET chan_switch_req_enable 0");
	wpa_command(intf, "SET persistent_reconnect 1");
	wpa_command(intf, "SET ampdu 1");
	run_system(dut, "iptables -F INPUT");
	if (dut->arp_ipaddr[0]) {
		snprintf(buf, sizeof(buf), "ip nei del %s dev %s",
			 dut->arp_ipaddr, dut->arp_ifname);
		run_system(dut, buf);
		dut->arp_ipaddr[0] = '\0';
	}
	snprintf(buf, sizeof(buf), "ip nei flush dev %s",
		 get_station_ifname(dut));
	run_system(dut, buf);
	dut->p2p_mode = P2P_IDLE;
	dut->client_uapsd = 0;
	ath6kl_client_uapsd(dut, intf, 0);

	remove_wpa_networks(intf);

	disconnect_station(dut);

	if (dut->iface_down_on_reset)
		dut_ifc_reset(dut);

	return 1;
}


static enum sigma_cmd_result cmd_sta_p2p_reset(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	dut->program = PROGRAM_P2P;

	if (get_param(cmd, "Runtime_ID"))
		dev_start_test_log(dut, conn, cmd);

	return sta_p2p_reset_default(dut, conn, cmd);
}


static enum sigma_cmd_result cmd_sta_get_p2p_ip_config(struct sigma_dut *dut,
						       struct sigma_conn *conn,
						       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *type = get_param(cmd, "type");
	const char *grpid = get_param(cmd, "GroupID");
	struct wfa_cs_p2p_group *grp = NULL;
	struct wfa_cs_p2p_group group;
	int count;
	char macaddr[20];
	u8 mac[ETH_ALEN];
	char resp[200], info[150];
	char ipv6_buf[100], *dns = "0.0.0.0";
	const char *ifname;

	if (!intf)
		intf = get_main_ifname(dut);


	if (grpid) {
		if (strcmp(grpid, "$P2P_GROUP_ID") == 0)
			return -1;

		/*
		 * If we did not initiate the operation that created the group,
		 * we may not have the group information available in the DUT
		 * code yet and it may take some time to get this from
		 * wpa_supplicant in case we are the P2P client. As such, we
		 * better try this multiple times to allow some time to
		 * complete the operation.
		 */

		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting to find the requested group");

		count = dut->default_timeout;
		while (count > 0) {
			grp = p2p_group_get(dut, grpid);
			if (!grp) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Requested group not yet found (count=%d)",
						count);
				sleep(1);
			} else {
				break;
			}
			count--;
		}
		if (!grp) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return STATUS_SENT_ERROR;
		}
	} else {
		ifname = get_group_ifname(dut, intf);
		strlcpy(group.ifname, ifname, IFNAMSIZ);
		grp = &group;
	}

	if (get_wpa_status(grp->ifname, "address",
			   macaddr, sizeof(macaddr)) < 0) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to get interface "
				"address for group interface %s",
				grp->ifname);
		return -2;
	}

	if (type && atoi(type) == 2) {
		/* IPv6 Type */

		if (parse_mac_address(dut, macaddr, mac))
			return -1;

		convert_mac_addr_to_ipv6_lladdr(mac, ipv6_buf,
						sizeof(ipv6_buf));
#ifdef ANDROID
		add_ipv6_rule(dut, grp->ifname);
#endif /* ANDROID */
		if (set_ipv6_addr(dut, ipv6_buf, "64", grp->ifname) != 0)
			return -1;

		snprintf(resp, sizeof(resp),
			 "dhcp,%d,ip,%s,mask,%d,primary-dns,%s,P2PInterfaceAddress,%s",
			 0, ipv6_buf, 64, dns, macaddr);
	} else {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting for IP address on group interface %s",
				grp->ifname);
		if (wait_ip_addr(dut, grp->ifname, dut->default_timeout) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,No IP address received");
			return STATUS_SENT_ERROR;
		}

		if (get_ip_config(dut, grp->ifname, info, sizeof(info)) < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Failed to get IP address for group interface %s",
					grp->ifname);
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Failed to get IP address");
			return STATUS_SENT_ERROR;
		}
		snprintf(resp, sizeof(resp), "%s,P2PInterfaceAddress,%s",
			 info, macaddr);
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"IP address for group interface %s found", grp->ifname);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return 0;
}


static enum sigma_cmd_result
cmd_sta_send_p2p_presence_req(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *dur = get_param(cmd, "Duration");
	const char *interv = get_param(cmd, "Interval");
	/* const char *grpid = get_param(cmd, "GroupID"); */
	const char *ifname;
	char buf[100];

	if (dur == NULL || interv == NULL)
		return -1;

	/* TODO: need to add groupid into parameters in CAPI spec; for now,
	 * pick the first active group */
	ifname = get_group_ifname(dut, intf);
	snprintf(buf, sizeof(buf), "P2P_PRESENCE_REQ %s %s", dur, interv);
	if (wpa_command(ifname, buf) < 0)
		return -2;

	return 1;
}


static enum sigma_cmd_result cmd_sta_set_sleep(struct sigma_dut *dut,
					       struct sigma_conn *conn,
					       struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	struct wfa_cs_p2p_group *grp;
	const char *ifname;
	const char *grpid = get_param(cmd, "GroupID");

	if (dut->program == PROGRAM_60GHZ) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,UAPSD Sleep is not applicable for 60 GHz");
		return 0;
	}

	if (grpid == NULL)
		ifname = get_station_ifname(dut);
	else {
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return 0;
		}
		ifname = grp->ifname;
	}

	if (dut->client_uapsd) {
#ifdef __linux__
		/* no special handling for nl80211 yet */
		char path[128];
		struct stat s;
		snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211",
			 ifname);
		if (stat(path, &s) == 0) {
			if (wpa_command(ifname, "P2P_SET ps 1") < 0) {
				send_resp(dut, conn, SIGMA_ERROR,
					  "errorCode,Going to sleep not supported");
				return 0;
			}
			return 1;
		}
#endif /* __linux__ */
		if (wpa_command(ifname, "P2P_SET ps 99") < 0)
			return -2;
	} else {
		if (wpa_command(ifname, "P2P_SET ps 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Going to sleep not supported");
			return 0;
		}
	}

	return 1;
}


static enum sigma_cmd_result
cmd_sta_set_opportunistic_ps(struct sigma_dut *dut, struct sigma_conn *conn,
			     struct sigma_cmd *cmd)
{
	/* const char *intf = get_param(cmd, "Interface"); */
	const char *intf = get_p2p_ifname(dut, get_param(cmd, "Interface"));
	struct wfa_cs_p2p_group *grp;
	char buf[100];
	const char *grpid = get_param(cmd, "GroupID");
	const char *ctwindow = get_param(cmd, "CTWindow");
	int ret;

	if (grpid == NULL || ctwindow == NULL)
		return -1;

	grp = p2p_group_get(dut, grpid);
	if (grp == NULL) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Requested group not found");
		return 0;
	}

	snprintf(buf, sizeof(buf), "P2P_SET ctwindow %d", atoi(ctwindow));
	if (wpa_command(grp->ifname, buf) < 0) {
		ret = snprintf(buf, sizeof(buf), "P2P_SET_PS %d %d %d",
			       -1, 1, atoi(ctwindow));
		if (ret < 0 || ret >= (int) sizeof(buf) ||
		    wcn_driver_cmd(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Use of CTWindow and opps_PS as GO not supported");
			return STATUS_SENT_ERROR;
		}
		return SUCCESS_SEND_STATUS;
	}

	if (wpa_command(grp->ifname, "P2P_SET oppps 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "errorCode,Use of OppPS as GO not supported");
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result
cmd_sta_send_service_discovery_req(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *devid = get_param(cmd, "P2PDevID");
	char buf[128];

	if (devid == NULL)
		return -1;

	snprintf(buf, sizeof(buf), "P2P_SERV_DISC_REQ %s 02000001",
		 devid);
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR, NULL);
		return 0;
	}

	return 1;
}


static enum sigma_cmd_result
cmd_sta_add_arp_table_entry(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	char buf[256];
	const char *ifname;
	const char *grpid, *ipaddr, *macaddr;

	grpid = get_param(cmd, "GroupID");
	ipaddr = get_param(cmd, "IPAddress");
	macaddr = get_param(cmd, "MACAddress");
	if (ipaddr == NULL || macaddr == NULL)
		return -1;

	if (grpid == NULL)
		ifname = get_station_ifname(dut);
	else {
		struct wfa_cs_p2p_group *grp;
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return 0;
		}
		ifname = grp->ifname;
	}

	snprintf(dut->arp_ipaddr, sizeof(dut->arp_ipaddr), "%s",
		 ipaddr);
	snprintf(dut->arp_ifname, sizeof(dut->arp_ifname), "%s",
		 ifname);

	snprintf(buf, sizeof(buf), "ip nei add %s lladdr %s dev %s",
		 ipaddr, macaddr, ifname);
	run_system(dut, buf);

	return 1;
}


static enum sigma_cmd_result
cmd_sta_block_icmp_response(struct sigma_dut *dut, struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	char buf[256];
	struct wfa_cs_p2p_group *grp;
	const char *ifname;
	const char *grpid, *ipaddr;

	grpid = get_param(cmd, "GroupID");
	ipaddr = get_param(cmd, "IPAddress");
	if (ipaddr == NULL)
		return -1;

	if (grpid == NULL)
		ifname = get_station_ifname(dut);
	else {
		grp = p2p_group_get(dut, grpid);
		if (grp == NULL) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "errorCode,Requested group not found");
			return 0;
		}
		ifname = grp->ifname;
	}

	snprintf(buf, sizeof(buf),
		 "iptables -I INPUT -s %s -p icmp -i %s -j DROP",
		 ipaddr, ifname);
	run_system(dut, buf);

	return 1;
}


static int run_nfc_command(struct sigma_dut *dut, const char *cmd,
			   const char *info)
{
	int res;

	sigma_dut_summary(dut, "NFC operation: %s", info);
	printf("\n\n\n=====[ NFC operation ]=========================\n\n");
	printf("%s\n\n", info);

	nfc_status(dut, "START", info);
	res = run_system(dut, cmd);
	nfc_status(dut, res ? "FAIL" : "SUCCESS", info);
	if (res) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed to run '%s': %d",
				cmd, res);
		return res;
	}

	return 0;
}


static int nfc_write_p2p_select(struct sigma_dut *dut, struct sigma_conn *conn,
				struct sigma_cmd *cmd)
{
	int res;
	const char *ifname = get_param(cmd, "Interface");
	char buf[300];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	if (wpa_command(ifname, "WPS_NFC_TOKEN NDEF") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to generate NFC password token");
		return 0;
	}

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./p2p-nfc.py -1 --no-wait %s%s --success nfc-success write-p2p-sel",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf,
			      "Touch NFC Tag to write P2P connection handover select");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to write tag");
		return 0;
	}

	if (wpa_command(ifname, "P2P_SET nfc_tag 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable NFC password token");
		return 0;
	}

	if (!dut->go && wpa_command(ifname, "P2P_LISTEN") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start listen mode");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "Result,,GroupID,,PeerRole,,PauseFlag,0");
	return 0;
}


static int nfc_write_config_token(struct sigma_dut *dut,
				  struct sigma_conn *conn,
				  struct sigma_cmd *cmd)
{
	int res;
	const char *bssid = get_param(cmd, "Bssid");
	const char *intf = get_param(cmd, "Interface");
	char buf[200];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");
	unlink("nfc-success");
	if (dut->er_oper_performed && bssid) {
		char current_bssid[30], id[10];
		if (get_wpa_status(intf, "id", id, sizeof(id)) < 0 ||
		    get_wpa_status(intf, "bssid", current_bssid,
				   sizeof(current_bssid)) < 0 ||
		    strncasecmp(bssid, current_bssid, strlen(current_bssid)) !=
		    0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,No configuration known for BSSID");
			return 0;
		}
		snprintf(buf, sizeof(buf),
			 "./wps-nfc.py --id %s --no-wait %s%s --success nfc-success write-config",
			 id,
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "");
		res = run_nfc_command(dut, buf,
				      "Touch NFC Tag to write WPS configuration token");
	} else {
		snprintf(buf, sizeof(buf),
			 "./wps-nfc.py --no-wait %s%s --success nfc-success write-config",
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "");
		res = run_nfc_command(dut, buf,
				      "Touch NFC Tag to write WPS configuration token");
	}
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to write tag");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "Result,,GroupID,,PeerRole,,PauseFlag,0");
	return 0;
}


static int nfc_write_password_token(struct sigma_dut *dut,
				    struct sigma_conn *conn,
				    struct sigma_cmd *cmd)
{
	int res;
	char buf[300];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");
	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-nfc.py --no-wait %s%s --success nfc-success write-password",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf,
			      "Touch NFC Tag to write WPS password token");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to write tag");
		return 0;
	}

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "Result,,GroupID,,PeerRole,,PauseFlag,0");
	return 0;
}


static int nfc_read_tag(struct sigma_dut *dut,
			struct sigma_conn *conn,
			struct sigma_cmd *cmd)
{
	int res;
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *oper_chn = get_param(cmd, "OPER_CHN");
	char buf[1000], freq_str[20];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	freq_str[0] = '\0';
	if (oper_chn) {
		int chan = atoi(oper_chn);
		if (chan >= 1 && chan <= 11)
			snprintf(freq_str, sizeof(freq_str), " --freq %d",
				 2407 + chan * 5);
	}

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./p2p-nfc.py -1 -t %s%s --success nfc-success --no-wait%s",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "",
		 freq_str);
	res = run_nfc_command(dut, buf,
			      "Touch NFC Tag to read it");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to read tag");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	if (dut->p2p_mode == P2P_DISABLE)
		return wps_connection_event(dut, conn, ctrl, intf, 1);

	if (dut->go || dut->p2p_client) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "Result,,GroupID,,PeerRole,,PauseFlag,0");
		return 0;
	}

	/* FIX: PeerRole */
	return p2p_group_formation_event(dut, conn, ctrl, intf, "0", 1);
}


static int nfc_wps_read_tag(struct sigma_dut *dut,
			    struct sigma_conn *conn,
			    struct sigma_cmd *cmd)
{
	int res;
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	char buf[300];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-nfc.py -1 --no-wait %s%s --success nfc-success",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf, "Touch NFC Tag to read it");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to read tag");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	return wps_connection_event(dut, conn, ctrl, intf, 1);
}


static int er_ap_add_match(const char *event, const char *bssid,
			   const char *req_uuid,
			   char *ret_uuid, size_t max_uuid_len)
{
	const char *pos, *uuid;

	pos = strchr(event, ' ');
	if (pos == NULL)
		return 0;
	pos++;
	uuid = pos;

	pos = strchr(pos, ' ');
	if (pos == NULL)
		return 0;
	if (ret_uuid) {
		if ((size_t) (pos - uuid + 1) < max_uuid_len) {
			memcpy(ret_uuid, uuid, pos - uuid);
			ret_uuid[pos - uuid] = '\0';
		} else
			ret_uuid[0] = '\0';
	}

	if (req_uuid && strncasecmp(req_uuid, uuid, pos - uuid) == 0)
		return 1;

	pos++;
	/* at BSSID */

	return strncasecmp(pos, bssid, strlen(bssid)) == 0;
}


static int er_start(struct sigma_dut *dut, struct sigma_conn *conn,
		    struct wpa_ctrl *ctrl, const char *intf, const char *bssid,
		    const char *uuid, char *ret_uuid, size_t max_uuid_len)
{
	char id[10];
	int res;
	char buf[1000];

	sigma_dut_print(dut, DUT_MSG_INFO, "Trying to find WPS AP %s over UPnP",
			bssid);

	if (wpa_command(intf, "WPS_ER_START") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start ER");
		return 0;
	}

	for (;;) {
		res = get_wpa_cli_event(dut, ctrl, "WPS-ER-AP-ADD",
					buf, sizeof(buf));
		if (res < 0) {
#ifdef USE_ERROR_RETURNS
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Could not find the AP over UPnP");
#else
			send_resp(dut, conn, SIGMA_COMPLETE,
				  "ErrorCode,Could not find the AP over UPnP");
#endif
			return 0;
		}

		if (er_ap_add_match(buf, bssid, uuid, ret_uuid, max_uuid_len)) {
			sigma_dut_print(dut, DUT_MSG_INFO,
					"Found WPS AP over UPnP: %s", buf);
			break;
		}
	}

	if (get_wpa_status(intf, "id", id, sizeof(id)) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Could not find AP configuration");
		return 0;
	}

	if (ret_uuid) {
		snprintf(buf, sizeof(buf), "WPS_ER_SET_CONFIG %s %s",
			 ret_uuid, id);
	} else if (uuid) {
		snprintf(buf, sizeof(buf), "WPS_ER_SET_CONFIG %s %s",
			 uuid, id);
	} else {
		snprintf(buf, sizeof(buf), "WPS_ER_SET_CONFIG %s %s",
			 bssid, id);
	}
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to select network configuration for ER");
		return 0;
	}

	return 1;
}


static int nfc_wps_read_passwd(struct sigma_dut *dut,
			       struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	int res;
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	const char *bssid = get_param(cmd, "Bssid");
	const char *ssid = get_param(cmd, "SSID");
	const char *security = get_param(cmd, "Security");
	const char *passphrase = get_param(cmd, "Passphrase");
	char ssid_hex[200], passphrase_hex[200];
	const char *val;
	int sta_action;
	char buf[1000];
	const char *keymgmt, *cipher;

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	if ((ssid && 2 * strlen(ssid) >= sizeof(ssid_hex)) ||
	    (passphrase && 2 * strlen(passphrase) >= sizeof(passphrase_hex))) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Too long SSID/passphrase");
		return 0;
	}

	val = get_param(cmd, "WpsStaAction");
	if (!val) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing WpsStaAction argument");
		return 0;
	}

	sta_action = atoi(val);
	if (sta_action != 1 && sta_action != 2) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Unsupported WpsStaAction value");
		return 0;
	}

	if (!bssid) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Missing Bssid argument");
		return 0;
	}

	if (sta_action == 2) {
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
	}

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	if (sta_action == 1) {
		const char *uuid = get_param(cmd, "UUID");
		res = er_start(dut, conn, ctrl, intf, bssid, uuid, NULL, 0);
		if (res != 1) {
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
			return res;
		}
	}

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-nfc.py -1 --no-wait %s%s --success nfc-success",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf, "Touch NFC Tag to read it");
	if (res || !file_exists("nfc-success")) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to read tag");
		return 0;
	}

	if (sta_action == 1) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Prepared device password for ER to enroll a new station");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "Result,,GroupID,,PeerRole,");
		return 0;
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
	snprintf(buf, sizeof(buf), "WPS_REG %s nfc-pw %s %s %s %s",
		 bssid, ssid_hex, keymgmt, cipher, passphrase_hex);

	if (wpa_command(intf, buf) < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start registrar");
		return 0;
	}

	return wps_connection_event(dut, conn, ctrl, intf, 1);
}


static int nfc_wps_read_config(struct sigma_dut *dut,
			       struct sigma_conn *conn,
			       struct sigma_cmd *cmd)
{
	int res;
	struct wpa_ctrl *ctrl;
	const char *intf = get_param(cmd, "Interface");
	char buf[300];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	unlink("nfc-success");
	snprintf(buf, sizeof(buf),
		 "./wps-nfc.py -1 --no-wait %s%s --success nfc-success",
		 dut->summary_log ? "--summary " : "",
		 dut->summary_log ? dut->summary_log : "");
	res = run_nfc_command(dut, buf, "Touch NFC Tag to read it");
	if (res || !file_exists("nfc-success")) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to read tag");
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return 0;
	}

	return wps_connection_event(dut, conn, ctrl, intf, 1);
}


static int nfc_wps_connection_handover(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	int res;
	const char *init = get_param(cmd, "Init");
	struct wpa_ctrl *ctrl = NULL;
	char buf[300];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	unlink("nfc-success");
	if ((!init || atoi(init) == 0) && dut->er_oper_performed) {
		const char *bssid = get_param(cmd, "Bssid");
		const char *req_uuid = get_param(cmd, "UUID");
		char uuid[100];

		if (bssid == NULL)
			bssid = dut->er_oper_bssid;

		res = er_start(dut, conn, ctrl, intf, bssid, req_uuid, uuid,
			       sizeof(uuid));
		if (res != 1) {
			wpa_ctrl_detach(ctrl);
			wpa_ctrl_close(ctrl);
			return res;
		}

		snprintf(buf, sizeof(buf),
			 "./wps-nfc.py -1 --uuid %s %s%s --success nfc-success",
			 uuid,
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "");
		res = run_nfc_command(dut, buf,
				      "Touch NFC Device to respond to WPS connection handover");
	} else if (!init || atoi(init)) {
		snprintf(buf, sizeof(buf),
			 "./wps-nfc.py -1 --no-wait %s%s --success nfc-success",
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "");
		res = run_nfc_command(dut, buf,
				      "Touch NFC Device to initiate WPS connection handover");
	} else {
		snprintf(buf, sizeof(buf),
			 "./p2p-nfc.py -1 --no-wait --no-input %s%s --success nfc-success --handover-only",
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "");
		res = run_nfc_command(dut, buf,
				      "Touch NFC Device to respond to WPS connection handover");
	}
	if (res) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable NFC for connection "
			  "handover");
		return 0;
	}
	if (!file_exists("nfc-success")) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to complete NFC connection handover");
		return 0;
	}

	if (init && atoi(init))
		return wps_connection_event(dut, conn, ctrl, intf, 1);

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	send_resp(dut, conn, SIGMA_COMPLETE,
		  "Result,,GroupID,,PeerRole,,PauseFlag,0");
	return 0;
}


static int nfc_p2p_connection_handover(struct sigma_dut *dut,
				       struct sigma_conn *conn,
				       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	int res;
	const char *init = get_param(cmd, "Init");
	const char *oper_chn = get_param(cmd, "OPER_CHN");
	struct wpa_ctrl *ctrl;
	char buf[1000], freq_str[20];

	run_system(dut, "killall wps-nfc.py");
	run_system(dut, "killall p2p-nfc.py");

	ctrl = open_wpa_mon(intf);
	if (ctrl == NULL) {
		sigma_dut_print(dut, DUT_MSG_ERROR, "Failed to open "
				"wpa_supplicant monitor connection");
		return -2;
	}

	freq_str[0] = '\0';
	if (oper_chn) {
		int chan = atoi(oper_chn);
		if (chan >= 1 && chan <= 11)
			snprintf(freq_str, sizeof(freq_str), " --freq %d",
				 2407 + chan * 5);
	}

	unlink("nfc-success");
	if (init && atoi(init)) {
		snprintf(buf, sizeof(buf),
			 "./p2p-nfc.py -1 -I -N --no-wait %s%s --success nfc-success --no-input%s --handover-only",
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "",
			 freq_str);
		res = run_nfc_command(dut, buf,
				      "Touch NFC Device to initiate P2P connection handover");
	} else {
		snprintf(buf, sizeof(buf),
			 "./p2p-nfc.py -1 --no-wait %s%s --success nfc-success --no-input%s --handover-only",
			 dut->summary_log ? "--summary " : "",
			 dut->summary_log ? dut->summary_log : "",
			 freq_str);
		res = run_nfc_command(dut, buf,
				      "Touch NFC Device to respond to P2P connection handover");
	}
	if (res) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to enable NFC for connection "
			  "handover");
		return 0;
	}
	if (!file_exists("nfc-success")) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to complete NFC connection handover");
		return 0;
	}

	if (dut->go || dut->p2p_client) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		send_resp(dut, conn, SIGMA_COMPLETE,
			  "Result,,GroupID,,PeerRole,,PauseFlag,0");
		return 0;
	}

	/* FIX: peer role from handover message */
	return p2p_group_formation_event(dut, conn, ctrl, intf, "0", 1);
}


static enum sigma_cmd_result cmd_sta_nfc_action(struct sigma_dut *dut,
						struct sigma_conn *conn,
						struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "Interface");
	const char *oper = get_param(cmd, "Operation");
	const char *ssid_param = get_param(cmd, "SSID");
	const char *intent_val = get_param(cmd, "INTENT_VAL");
	const char *oper_chn = get_param(cmd, "OPER_CHN");
	char buf[256];

	if (oper == NULL)
		return -1;

	if (ssid_param)
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix %s",
			 ssid_param);
	else
		snprintf(buf, sizeof(buf), "P2P_SET ssid_postfix ");
	if (wpa_command(intf, buf) < 0)
		sigma_dut_print(dut, DUT_MSG_INFO, "Failed P2P ssid_postfix - ignore and assume this is for non-P2P case");

	if (intent_val) {
		snprintf(buf, sizeof(buf), "SET p2p_go_intent %s", intent_val);
		if (wpa_command(intf, buf) < 0)
			return -2;
	}

	if (oper_chn) {
		int chan = atoi(oper_chn);
		if (chan < 1 || chan > 11) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Unsupported operating channel");
			return 0;
		}
		snprintf(buf, sizeof(buf), "SET p2p_oper_channel %d", chan);
		if (wpa_command(intf, "SET p2p_oper_reg_class 81") < 0 ||
		    wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set operating channel");
			return 0;
		}
	}

	if (strcasecmp(oper, "WRITE_SELECT") == 0)
		return nfc_write_p2p_select(dut, conn, cmd);
	if (strcasecmp(oper, "WRITE_CONFIG") == 0)
		return nfc_write_config_token(dut, conn, cmd);
	if (strcasecmp(oper, "WRITE_PASSWD") == 0)
		return nfc_write_password_token(dut, conn, cmd);
	if (strcasecmp(oper, "READ_TAG") == 0)
		return nfc_read_tag(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_READ_TAG") == 0)
		return nfc_wps_read_tag(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_READ_PASSWD") == 0)
		return nfc_wps_read_passwd(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_READ_CONFIG") == 0)
		return nfc_wps_read_config(dut, conn, cmd);
	if (strcasecmp(oper, "CONN_HNDOVR") == 0)
		return nfc_p2p_connection_handover(dut, conn, cmd);
	if (strcasecmp(oper, "WPS_CONN_HNDOVR") == 0)
		return nfc_wps_connection_handover(dut, conn, cmd);

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported operation");
	return 0;
}


int p2p_cmd_sta_get_parameter(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *parameter = get_param(cmd, "Parameter");
	char buf[100];

	if (parameter == NULL)
		return -1;
	if (strcasecmp(parameter, "ListenChannel") == 0) {
		snprintf(buf, sizeof(buf), "ListenChnl,%u", dut->listen_chn);
		send_resp(dut, conn, SIGMA_COMPLETE, buf);
		return 0;
	}

	send_resp(dut, conn, SIGMA_ERROR, "ErrorCode,Unsupported parameter");
	return 0;
}


static int req_intf(struct sigma_cmd *cmd)
{
	return get_param(cmd, "interface") == NULL ? -1 : 0;
}


static int p2p_pasn_responder_auth(struct sigma_dut *dut,
				   struct sigma_conn *conn,
				   const char *intf)
{
	char buf[512], *pos, *end;

	pos = buf;
	end = buf + sizeof(buf);

	pos += snprintf(pos, end - pos,
			"P2P_CONNECT %s pair provdisc he go_intent=%d persistent allow_6ghz p2p2",
			dut->p2p_connect_info.peer_mac,
			dut->p2p_connect_info.go_intent);

	pos += snprintf(pos, end - pos, " bstrapmethod=%d",
			dut->p2p_connect_info.bootstrap);

	if (dut->p2p_connect_info.join)
		pos += snprintf(pos, end - pos, " join");

	if (dut->p2p_connect_info.pairing_role)
		pos += snprintf(pos, end - pos, " auth");

	if (dut->p2p_connect_info.password[0] != '\0')
		pos += snprintf(pos, end - pos, " password=%s",
				dut->p2p_connect_info.password);

	if (dut->p2p_connect_info.freq)
		pos += snprintf(pos, end - pos, " freq=%d",
				dut->p2p_connect_info.freq);

	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Failed to start P2P PASN group formation");
		return -2;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "P2P Authorize");
	return 0;
}


static enum sigma_cmd_result p2p_pasn_initiator_connect(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      const char *intf)
{
	struct wpa_ctrl *ctrl = NULL;
	char buf[512], *pos, *end, resp[256], grpid[50];
	char *ifname, *gtype, *ssid, *freq_str, bssid[20];
	char *go_dev_addr;
	int res;

	pos = buf;
	end = buf + sizeof(buf);

	pos += snprintf(pos, end - pos,
			"P2P_CONNECT %s pair provdisc he go_intent=%d persistent allow_6ghz p2p2",
			dut->p2p_connect_info.peer_mac,
			dut->p2p_connect_info.go_intent);

	pos += snprintf(pos, end - pos, " bstrapmethod=%d",
			dut->p2p_connect_info.bootstrap);

	if (dut->p2p_connect_info.join)
		pos += snprintf(pos, end - pos, " join");

	if (dut->p2p_connect_info.pairing_role)
		pos += snprintf(pos, end - pos, " auth");

	if (dut->p2p_connect_info.password[0] != '\0')
		pos += snprintf(pos, end - pos, " password=%s",
				dut->p2p_connect_info.password);

	if (dut->p2p_connect_info.freq)
		pos += snprintf(pos, end - pos, " freq=%d",
				dut->p2p_connect_info.freq);

	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	if (wpa_command(intf, buf) < 0) {
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Failed to start P2P PASN group formation");
		return ERROR_SEND_STATUS;
	}

	res = get_wpa_cli_event(dut, ctrl, "P2P-GROUP-STARTED",
				buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0 && !dut->p2p_connect_info.pairing_role) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Group joining did not complete on Initiator");
		return STATUS_SENT_ERROR;
	} else if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Group joining did not complete on Responder");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group started event '%s'", buf);
	ifname = strchr(buf, ' ');
	if (!ifname)
		return ERROR_SEND_STATUS;
	ifname++;
	pos = strchr(ifname, ' ');
	if (!pos)
		return ERROR_SEND_STATUS;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group interface %s", ifname);

	gtype = pos;
	pos = strchr(gtype, ' ');
	if (!pos)
		return ERROR_SEND_STATUS;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group type %s", gtype);

	ssid = strstr(pos, "ssid=\"");
	if (!ssid)
		return ERROR_SEND_STATUS;
	ssid += 6;
	pos = strchr(ssid, '"');
	if (!pos)
		return ERROR_SEND_STATUS;
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group SSID %s", ssid);

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (!go_dev_addr) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"No GO P2P Device Address found");
		return ERROR_SEND_STATUS;
	}
	go_dev_addr += 12;
	if (strlen(go_dev_addr) < 17) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Too short GO P2P Device Address '%s'",
				go_dev_addr);
		return ERROR_SEND_STATUS;
	}
	go_dev_addr[17] = '\0';
	*pos++ = '\0';
	sigma_dut_print(dut, DUT_MSG_DEBUG, "GO P2P Device Address %s",
			go_dev_addr);

	freq_str = strstr(pos, "freq=");
	if (!freq_str)
		return ERROR_SEND_STATUS;
	freq_str += 5;
	pos = strchr(freq_str, ' ');
	if (!pos)
		return ERROR_SEND_STATUS;
	*pos = '\0';
	dut->p2p_connect_info.freq = atoi(freq_str);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "freq %d",
			dut->p2p_connect_info.freq);

	if (get_wpa_status(ifname, "bssid", bssid, sizeof(bssid)) < 0)
		return ERROR_SEND_STATUS;
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Group BSSID %s", bssid);

	res = snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr, ssid);
	if (res < 0 || res >= (int) sizeof(grpid))
		return ERROR_SEND_STATUS;
	p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0, grpid, ssid,
		      dut->p2p_connect_info.peer_mac);

	snprintf(resp, sizeof(resp), "Result,%s,GroupID,%s",
		 strcmp(gtype, "GO") == 0 ? "GO" : "CLIENT", grpid);

	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


static void * wpa_pairing_resp_event_recv(void *ptr)
{
	struct sigma_dut *dut = ptr;
	struct wpa_ctrl *ctrl;
	char buf[4096], grpid[50];
	char *pos, *pos1;
	char *ifname, *gtype, *ssid, *freq_str, bssid[20];
	char *go_dev_addr;
	int fd, ret, i;
	fd_set rfd;
	struct timeval tv;
	size_t len;
	const char *events[] = {
		"P2P-BOOTSTRAP-REQUEST",
		"P2P-GROUP-STARTED",
		"P2P-GO-NEG-FAILURE",
		"P2P-GROUP-FORMATION-FAILURE",
		NULL
	};

	if (dut->p2p_connect_info.is_opportunistic_bs) {
		if (p2p_pasn_responder_auth(dut, NULL, dut->main_ifname) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"P2P PASN Connect Failed");
			return NULL;
		}
	}

	ctrl = open_wpa_mon(dut->main_ifname);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return NULL;
	}

	for (i = 0; events[i]; i++)
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Waiting for wpa_cli event: %s", events[i]);

	fd = wpa_ctrl_get_fd(ctrl);
	if (fd < 0) {
		wpa_ctrl_detach(ctrl);
		wpa_ctrl_close(ctrl);
		return NULL;
	}

	while (!stop_p2p_resp_event_rx) {
		FD_ZERO(&rfd);
		FD_SET(fd, &rfd);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		ret = select(fd + 1, &rfd, NULL, NULL, &tv);
		if (ret == 0)
			continue;
		if (ret < 0) {
			sigma_dut_print(dut, DUT_MSG_INFO, "select: %s",
					strerror(errno));
			usleep(100000);
			continue;
		}

		len = sizeof(buf);
		if (wpa_ctrl_recv(ctrl, buf, &len) < 0) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Failure while waiting for events");
			continue;
		}

		ret = 0;
		pos = strchr(buf, '>');
		if (pos) {
			for (i = 0; events[i]; i++) {
				if (strncmp(pos + 1, events[i],
					    strlen(events[i])) == 0) {
					ret = 1;
					break; /* Event found */
				}
			}
		}
		if (!ret)
			continue;

		if (strstr(buf, "P2P-BOOTSTRAP-REQUEST")) {
			pos = strchr(buf, ' ');
			if (!pos)
				continue;
			pos++;
			pos1 = strchr(pos, ' ');
			if (!pos1)
				continue;
			if (pos1 - pos >
			    (int) sizeof(dut->p2p_connect_info.peer_mac))
				continue;
			if (pos1 - pos < 17) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Too short P2P Peer Address");
				continue;
			}
			if (strncasecmp(pos, dut->p2p_connect_info.peer_mac,
					pos1 - pos) == 0) {
				sigma_dut_print(dut, DUT_MSG_DEBUG,
						"Bootstrap request event '%s' sleep for 500ms",
						buf);
				usleep(500000);
				if (p2p_pasn_responder_auth(dut, NULL,
							    dut->main_ifname) <
				    0) {
					sigma_dut_print(dut, DUT_MSG_ERROR,
							"P2P PASN Connect Failed");
					break;
				}
			}
		} else if (strstr(buf, "P2P-GO-NEG-FAILURE")) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"ErrorCode,Group Negotiation failed");
			break;
		} else if (strstr(buf, "P2P-GROUP-FORMATION-FAILURE")) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"ErrorCode,Group formation failed");
			break;
		} else if (strstr(buf, "P2P-GROUP-STARTED")) {
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Group started event '%s'", buf);
			ifname = strchr(buf, ' ');
			if (!ifname)
				continue;
			ifname++;
			pos = strchr(ifname, ' ');
			if (!pos)
				continue;
			*pos++ = '\0';
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Group interface %s", ifname);

			gtype = pos;
			pos = strchr(gtype, ' ');
			if (!pos)
				continue;
			*pos++ = '\0';
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Group type %s", gtype);

			ssid = strstr(pos, "ssid=\"");
			if (!ssid)
				continue;
			ssid += 6;
			pos = strchr(ssid, '"');
			if (!pos)
				continue;
			*pos++ = '\0';
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Group SSID %s", ssid);

			go_dev_addr = strstr(pos, "go_dev_addr=");
			if (!go_dev_addr) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"No GO P2P Device Address found");
				continue;
			}
			go_dev_addr += 12;
			if (strlen(go_dev_addr) < 17) {
				sigma_dut_print(dut, DUT_MSG_ERROR,
						"Too short GO P2P Device Address '%s'",
						go_dev_addr);
				continue;
			}
			go_dev_addr[17] = '\0';
			*pos++ = '\0';
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"GO P2P Device Address %s",
					go_dev_addr);

			freq_str = strstr(pos, "freq=");
			if (!freq_str)
				continue;
			freq_str += 5;
			pos = strchr(freq_str, ' ');
			if (!pos)
				continue;
			*pos = '\0';
			dut->p2p_connect_info.freq = atoi(freq_str);
			sigma_dut_print(dut, DUT_MSG_DEBUG, "freq %d",
					dut->p2p_connect_info.freq);

			if (get_wpa_status(ifname, "bssid", bssid,
					   sizeof(bssid)) < 0)
				continue;
			sigma_dut_print(dut, DUT_MSG_DEBUG,
					"Group BSSID %s", bssid);

			ret = snprintf(grpid, sizeof(grpid), "%s %s", go_dev_addr,
				       ssid);
			if (ret < 0 || ret >= (int) sizeof(grpid))
				continue;
			p2p_group_add(dut, ifname, strcmp(gtype, "GO") == 0,
				      grpid, ssid,
				      dut->p2p_connect_info.peer_mac);
		}
	}

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	pthread_exit(0);
	return NULL;
}


static enum sigma_cmd_result
p2p_pasn_pairing_setup(struct sigma_dut *dut, struct sigma_conn *conn,
		       struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *mac = get_param(cmd, "MAC");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *role = get_param(cmd, "Role");
	const char *bstrapmethod = get_param(cmd, "PairingBootstrapMethod");
	const char *pmk_devik_caching = get_param(cmd, "PMKDevIKCaching");
	const char *comeback_after = get_param(cmd, "ComebackAfter");
	const char *comeback_cookie = get_param(cmd, "ComebackCookie");
	const char *pairing_setup = get_param(cmd, "PairingSetup");
	const char *password = get_param(cmd, "PairingPassword");
	const char *intent_value = get_param(cmd, "IntentValue");
	const char *oper_chan = get_param(cmd, "GOOperChan");
	const char *rcv_freq = get_param(cmd, "GOOperChanFreq");
	char buf[256];
	int freq = 0, chan = 0, ret;

	if (!dut->p2p_r2_capable)
		return INVALID_SEND_STATUS;

	if (!mac || !service_name || !pairing_setup)
		return INVALID_SEND_STATUS;

	if (pmk_devik_caching && strcasecmp(pmk_devik_caching, "Enable") == 0 &&
	    wpa_command(intf, "P2P_SET pairing_cache 1") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to set pmk_devik_caching");
		return STATUS_SENT_ERROR;
	}

	if (comeback_after && comeback_cookie) {
		ret = snprintf(buf, sizeof(buf), "P2P_SET comeback_after %s",
			       comeback_after);
		if (ret < 0 || ret >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to form set command");
			return STATUS_SENT_ERROR;
		}
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set comeback_after");
			return STATUS_SENT_ERROR;
		}
	}

	memset(&dut->p2p_connect_info, 0, sizeof(struct p2p_r2_connect_info));
	strlcpy(dut->p2p_connect_info.peer_mac, mac,
		sizeof(dut->p2p_connect_info.peer_mac));

	if (role && strcasecmp(role, "Responder") == 0) {
		dut->p2p_connect_info.pairing_role = 1;
	} else {
		if (wpa_command(intf, "NAN_FLUSH") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,NAN_FLUSH failed");
			return STATUS_SENT_ERROR;
		}
	}

	if (intent_value)
		dut->p2p_connect_info.go_intent = atoi(intent_value);

	if (bstrapmethod)
		dut->p2p_connect_info.bootstrap = atoi(bstrapmethod);

	if (strcasecmp(pairing_setup, "Password") == 0) {
		if (!password) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Password not set");
			return INVALID_SEND_STATUS;
		}
		strlcpy(dut->p2p_connect_info.password, password,
			sizeof(dut->p2p_connect_info.password));
	} else {
		dut->p2p_connect_info.password[0] = '\0';
	}

	if (strcasecmp(pairing_setup, "OpportunisticBS") == 0)
		dut->p2p_connect_info.is_opportunistic_bs = true;

	if (rcv_freq) {
		freq = atoi(rcv_freq);
		dut->p2p_connect_info.freq = freq;
	} else if (oper_chan) {
		chan = atoi(oper_chan);
		freq = channel_to_freq(dut, chan);
		dut->p2p_connect_info.freq = freq;
	}

	if (!dut->p2p_connect_info.pairing_role)
		return p2p_pasn_initiator_connect(dut, conn, intf);

	/* Configure operating channel for pairing verification */
	snprintf(buf, sizeof(buf), "P2P_SET inv_oper_freq %d", freq);
	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode, P2P_SET inv_oper_freq Failed");
		return STATUS_SENT_ERROR;
	}

	if (!dut->p2p_event_mon_thread) {
		/* Create a separate event thread to receive
		 * bootstrap request event
		 */
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Starting P2P event monitoring thread");
		stop_p2p_resp_event_rx = false;
		pthread_create(&dut->p2p_event_mon_thread, NULL,
			       &wpa_pairing_resp_event_recv,
			       (void *) dut);
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
	return STATUS_SENT;
}


static enum sigma_cmd_result
p2p_pasn_join(struct sigma_dut *dut, struct sigma_conn *conn,
	      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *mac = get_param(cmd, "MAC");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *role = get_param(cmd, "Role");
	const char *bstrapmethod = get_param(cmd, "PairingBootstrapMethod");
	const char *pmk_devik_caching = get_param(cmd, "PMKDevIKCaching");
	const char *comeback_after = get_param(cmd, "ComebackAfter");
	const char *comeback_cookie = get_param(cmd, "ComebackCookie");
	const char *pairing_setup = get_param(cmd, "PairingSetup");
	const char *password = get_param(cmd, "PairingPassword");
	char buf[256];
	int ret;

	if (!dut->p2p_r2_capable)
		return INVALID_SEND_STATUS;

	if (!mac || !service_name || !pairing_setup)
		return INVALID_SEND_STATUS;

	if (pmk_devik_caching && strcmp(pmk_devik_caching, "Enable") == 0) {
		if (wpa_command(intf, "P2P_SET pairing_cache 1") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set pmk_devik_caching");
			return STATUS_SENT_ERROR;
		}
	}

	if (comeback_after && comeback_cookie) {
		ret = snprintf(buf, sizeof(buf), "P2P_SET comeback_after %s",
			       comeback_after);
		if (ret < 0 || ret >= (int) sizeof(buf)) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to form a P2P_SET command");
			return STATUS_SENT_ERROR;
		}
		if (wpa_command(intf, buf) < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,Failed to set comeback_after");
			return STATUS_SENT_ERROR;
		}
	}

	memset(&dut->p2p_connect_info, 0, sizeof(struct p2p_r2_connect_info));
	dut->p2p_connect_info.join = true;

	strlcpy(dut->p2p_connect_info.peer_mac, mac,
		sizeof(dut->p2p_connect_info.peer_mac));

	if (role && strcasecmp(role, "Responder") == 0) {
		dut->p2p_connect_info.pairing_role = 1;
		dut->p2p_connect_info.go_intent = 15;
	}

	if (bstrapmethod)
		dut->p2p_connect_info.bootstrap = atoi(bstrapmethod);

	if (strcasecmp(pairing_setup, "Password") == 0) {
		if (!password) {
			sigma_dut_print(dut, DUT_MSG_ERROR, "Password not set");
			return INVALID_SEND_STATUS;
		}
		strlcpy(dut->p2p_connect_info.password, password,
			sizeof(dut->p2p_connect_info.password));
	} else {
		dut->p2p_connect_info.password[0] = '\0';
	}

	if (strcasecmp(pairing_setup, "OpportunisticBS") == 0)
		dut->p2p_connect_info.is_opportunistic_bs = true;

	if (!dut->p2p_connect_info.pairing_role) {
		if (wpa_command(intf, "NAN_FLUSH") < 0) {
			send_resp(dut, conn, SIGMA_ERROR,
				  "ErrorCode,NAN_FLUSH Failed");
			return STATUS_SENT_ERROR;
		}
		return p2p_pasn_initiator_connect(dut, conn, intf);
	}

	if (!dut->p2p_event_mon_thread) {
		/* Create a separate event thread to receive bootstrap request
		 * event */
		sigma_dut_print(dut, DUT_MSG_DEBUG,
				"Starting P2P event monitoring thread");
		stop_p2p_resp_event_rx = false;
		pthread_create(&dut->p2p_event_mon_thread, NULL,
			       &wpa_pairing_resp_event_recv, (void *) dut);
	}

	send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
	return STATUS_SENT;
}


static enum sigma_cmd_result
p2p_pasn_pairing_verification(struct sigma_dut *dut, struct sigma_conn *conn,
			      struct sigma_cmd *cmd)
{
	const char *intf = get_param(cmd, "interface");
	const char *mac = get_param(cmd, "MAC");
	const char *service_name = get_param(cmd, "ServiceName");
	const char *grpid = get_param(cmd, "P2PGroupID");
	const char *oper_chan = get_param(cmd, "GOOperChan");
	char buf[256], resp[256];
	int res, freq, chan;
	int id = -1;
	char *ssid, *pos, *end, *inv_ssid, inv_grpid[50];
	struct wpa_ctrl *ctrl;
	char *go_dev_addr;
	const char *events[] = {
		"P2P-INVITATION-ACCEPTED",
		"P2P-INVITATION-RESULT",
		NULL
	};

	if (!dut->p2p_r2_capable)
		return INVALID_SEND_STATUS;

	if (!mac || !service_name || !grpid)
		return INVALID_SEND_STATUS;

	ssid = strchr(grpid, ' ');
	if (!ssid) {
		sigma_dut_print(dut, DUT_MSG_INFO, "Invalid grpid");
		return INVALID_SEND_STATUS;
	}
	*ssid = '\0';
	ssid++;
	sigma_dut_print(dut, DUT_MSG_DEBUG,
			"Search for persistent group credentials based on SSID: '%s'",
			ssid);
	if (wpa_command_resp(intf, "LIST_NETWORKS", buf, sizeof(buf)) < 0)
		return ERROR_SEND_STATUS;
	pos = strstr(buf, ssid);
	if (!pos || pos == buf || pos[-1] != '\t' ||
	    pos[strlen(ssid)] != '\t') {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Persistent group credentials not found");
		return STATUS_SENT_ERROR;
	}
	while (pos > buf && pos[-1] != '\n')
		pos--;
	id = atoi(pos);
	sigma_dut_print(dut, DUT_MSG_DEBUG, "Persistent id %d", id);

	if (wpa_command(intf, "NAN_FLUSH") < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,NAN_FLUSH failed");
		return STATUS_SENT_ERROR;
	}

	pos = buf;
	end = buf + sizeof(buf);

	pos += snprintf(pos, end - pos, "P2P_INVITE persistent peer=%s p2p2",
		 mac);

	if (oper_chan) {
		chan = atoi(oper_chan);
		freq = channel_to_freq(dut, chan);
	} else {
		/* Fetch previous connection freq */
		freq = dut->p2p_connect_info.freq;
		sigma_dut_print(dut, DUT_MSG_DEBUG, "freq %d", freq);
		pos += snprintf(pos, end - pos, " freq=%d", freq);
	}

	ctrl = open_wpa_mon(intf);
	if (!ctrl) {
		sigma_dut_print(dut, DUT_MSG_ERROR,
				"Failed to open wpa_supplicant monitor connection");
		return ERROR_SEND_STATUS;
	}

	if (wpa_command(intf, buf) < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Failed to start P2P PASN pairing verification");
		return STATUS_SENT_ERROR;
	}

	res = get_wpa_cli_events(dut, ctrl, events, buf, sizeof(buf));

	wpa_ctrl_detach(ctrl);
	wpa_ctrl_close(ctrl);

	if (res < 0) {
		send_resp(dut, conn, SIGMA_ERROR,
			  "ErrorCode,Invitation did not complete");
		return STATUS_SENT_ERROR;
	}

	sigma_dut_print(dut, DUT_MSG_DEBUG, "Invitation event: '%s'", buf);

	inv_ssid = strstr(buf, "ssid=\"");
	if (inv_ssid) {
		inv_ssid += 6;
		pos = strchr(inv_ssid, '"');
		if (!pos)
			return STATUS_SENT_ERROR;
		*pos++ = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "Invitation Group SSID %s",
				inv_ssid);
	}

	go_dev_addr = strstr(pos, "go_dev_addr=");
	if (go_dev_addr) {
		go_dev_addr += 12;
		if (strlen(go_dev_addr) < 17) {
			sigma_dut_print(dut, DUT_MSG_ERROR,
					"Too short GO P2P Device Address '%s'",
					go_dev_addr);
			return STATUS_SENT_ERROR;
		}
		go_dev_addr[17] = '\0';
		*pos++ = '\0';
		sigma_dut_print(dut, DUT_MSG_DEBUG, "GO P2P Device Address %s",
				go_dev_addr);
	}

	res = snprintf(inv_grpid, sizeof(inv_grpid), "%s %s",
		       go_dev_addr ? go_dev_addr : grpid,
		       inv_ssid ? inv_ssid : ssid);
	if (res < 0 || res >= (int) sizeof(inv_grpid))
		return STATUS_SENT_ERROR;

	snprintf(resp, sizeof(resp), "GroupID,%s", inv_grpid);
	send_resp(dut, conn, SIGMA_COMPLETE, resp);
	return STATUS_SENT;
}


enum sigma_cmd_result p2p_cmd_sta_exec_action(struct sigma_dut *dut,
					      struct sigma_conn *conn,
					      struct sigma_cmd *cmd)
{
	enum sigma_cmd_result ret = STATUS_SENT;
	const char *method_type = get_param(cmd, "MethodType");

	if (!method_type)
		return ERROR_SEND_STATUS;

	if (strcasecmp(method_type, "PASNGroupFormation") == 0) {
		ret = p2p_pasn_pairing_setup(dut, conn, cmd);
	} else if (strcasecmp(method_type, "PairingVerification") == 0) {
		ret = p2p_pasn_pairing_verification(dut, conn, cmd);
	} else if (strcasecmp(method_type, "PASNConnectGO") == 0) {
		ret = p2p_pasn_join(dut, conn, cmd);
	} else {
		sigma_dut_print(dut, DUT_MSG_INFO, "P2P unsupported method: %s",
				method_type);
		send_resp(dut, conn, SIGMA_COMPLETE, "NULL");
	}

	return ret;
}


void p2p_register_cmds(void)
{
	sigma_dut_reg_cmd("sta_get_p2p_dev_address", req_intf,
			  cmd_sta_get_p2p_dev_address);
	sigma_dut_reg_cmd("sta_set_p2p", req_intf, cmd_sta_set_p2p);
	sigma_dut_reg_cmd("sta_start_autonomous_go", req_intf,
			  cmd_sta_start_autonomous_go);
	sigma_dut_reg_cmd("sta_p2p_connect", req_intf, cmd_sta_p2p_connect);
	sigma_dut_reg_cmd("sta_p2p_start_group_formation", req_intf,
			  cmd_sta_p2p_start_group_formation);
	sigma_dut_reg_cmd("sta_p2p_dissolve", req_intf, cmd_sta_p2p_dissolve);
	sigma_dut_reg_cmd("sta_send_p2p_invitation_req", req_intf,
			  cmd_sta_send_p2p_invitation_req);
	sigma_dut_reg_cmd("sta_accept_p2p_invitation_req", req_intf,
			  cmd_sta_accept_p2p_invitation_req);
	sigma_dut_reg_cmd("sta_send_p2p_provision_dis_req", req_intf,
			  cmd_sta_send_p2p_provision_dis_req);
	sigma_dut_reg_cmd("sta_set_wps_pbc", req_intf, cmd_sta_set_wps_pbc);
	sigma_dut_reg_cmd("sta_wps_read_pin", req_intf, cmd_sta_wps_read_pin);
	sigma_dut_reg_cmd("sta_wps_read_label", req_intf,
			  cmd_sta_wps_read_label);
	sigma_dut_reg_cmd("sta_wps_enter_pin", req_intf,
			  cmd_sta_wps_enter_pin);
	sigma_dut_reg_cmd("sta_get_psk", req_intf, cmd_sta_get_psk);
	sigma_dut_reg_cmd("sta_p2p_reset", req_intf, cmd_sta_p2p_reset);
	sigma_dut_reg_cmd("sta_get_p2p_ip_config", req_intf,
			  cmd_sta_get_p2p_ip_config);
	sigma_dut_reg_cmd("sta_send_p2p_presence_req", req_intf,
			  cmd_sta_send_p2p_presence_req);
	sigma_dut_reg_cmd("sta_set_sleep", req_intf, cmd_sta_set_sleep);
	sigma_dut_reg_cmd("sta_set_opportunistic_ps", req_intf,
			  cmd_sta_set_opportunistic_ps);
	sigma_dut_reg_cmd("sta_send_service_discovery_req", req_intf,
			  cmd_sta_send_service_discovery_req);
	sigma_dut_reg_cmd("sta_add_arp_table_entry", req_intf,
			  cmd_sta_add_arp_table_entry);
	sigma_dut_reg_cmd("sta_block_icmp_response", req_intf,
			  cmd_sta_block_icmp_response);
	sigma_dut_reg_cmd("sta_nfc_action", req_intf, cmd_sta_nfc_action);
}
