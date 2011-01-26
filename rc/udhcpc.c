/*
 * udhcpc scripts
 *
 * Copyright 2004, Broadcom Corporation
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND BROADCOM GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id$
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <net/route.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#include <bcmnvram.h>
#include <netconf.h>
#include <shutils.h>
#include <rc.h>

char udhcpstate[8];

static int
expires(char *wan_ifname, unsigned int in)
{
	time_t now;
	FILE *fp;
	char tmp[100];
	int unit;

	if ((unit = wan_ifunit(wan_ifname)) < 0)
		return -1;
	
	time(&now);
	snprintf(tmp, sizeof(tmp), "/tmp/udhcpc%d.expires", unit); 
	if (!(fp = fopen(tmp, "w"))) {
		perror(tmp);
		return errno;
	}
	fprintf(fp, "%d", (unsigned int) now + in);
	fclose(fp);
	return 0;
}	

/* 
 * deconfig: This argument is used when udhcpc starts, and when a
 * leases is lost. The script should put the interface in an up, but
 * deconfigured state.
*/
static int
deconfig(void)
{
	char *wan_ifname = safe_getenv("interface");

	if (nvram_match("wan0_ifname", wan_ifname)
	&& (nvram_match("wan0_proto", "l2tp") || nvram_match("wan0_proto", "pptp")))
	{
		/* fix kernel route-loop issue */
		logmessage("dhcp client", "skipping resetting IP address to 0.0.0.0");
	} else
		ifconfig(wan_ifname, IFUP, "0.0.0.0", NULL);

	expires(wan_ifname, 0);

	wan_down(wan_ifname);

	logmessage("dhcp client", "%s: lease is lost", udhcpstate);
	wanmessage("lost IP from server");
	dprintf("done\n");
	return 0;
}

/*
 * bound: This argument is used when udhcpc moves from an unbound, to
 * a bound state. All of the paramaters are set in enviromental
 * variables, The script should configure the interface, and set any
 * other relavent parameters (default gateway, dns server, etc).
*/
static int
bound(void)
{
	char *wan_ifname = safe_getenv("interface");
	char *value;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	char route[] = "255.255.255.255/255";
	int unit;
	int gateway = 0;

	if ((unit = wan_ifunit(wan_ifname)) < 0) 
		strcpy(prefix, "wanx_");
	else
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);

	if ((value = getenv("ip")))
		nvram_set(strcat_r(prefix, "ipaddr", tmp), trim_r(value));
	if ((value = getenv("subnet")))
		nvram_set(strcat_r(prefix, "netmask", tmp), trim_r(value));
        if ((value = getenv("router"))) {
		nvram_set(strcat_r(prefix, "gateway", tmp), trim_r(value));
		gateway = 1;
	}
	if ((value = getenv("dns")))
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));

	/* classful static routes*/
	nvram_set(strcat_r(prefix, "routes", tmp), getenv("routes"));

	/* ms classless static routes*/
	nvram_set(strcat_r(prefix, "routes_ms", tmp), getenv("msstaticroutes"));

	/* rfc3442 classless static routes */
	nvram_set(strcat_r(prefix, "routes_rfc", tmp), getenv("staticroutes"));

	if (!gateway) {
		foreach(route, nvram_safe_get(strcat_r(prefix, "routes_rfc", tmp)), value) {
			if (gateway) {
				nvram_set(strcat_r(prefix, "gateway", tmp), route);
				break;
			} else
				gateway = !strcmp(route, "0.0.0.0/0");
		}
    	}

#if 0
	if ((value = getenv("hostname")))
		sethostname(trim_r(value), strlen(value) + 1);
#endif
	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
	if ((value = getenv("lease"))) {
		nvram_set(strcat_r(prefix, "lease", tmp), trim_r(value));
		expires(wan_ifname, atoi(value));
	}

	ifconfig(wan_ifname, IFUP,
		 nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)),
		 nvram_safe_get(strcat_r(prefix, "netmask", tmp)));

	wan_up(wan_ifname);

	logmessage("dhcp client", "%s IP : %s from %s", 
		udhcpstate, 
		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)), 
		nvram_safe_get(strcat_r(prefix, "gateway", tmp)));

	wanmessage("");
	dprintf("done\n");
	return 0;
}

/*
 * renew: This argument is used when a DHCP lease is renewed. All of
 * the paramaters are set in enviromental variables. This argument is
 * used when the interface is already configured, so the IP address,
 * will not change, however, the other DHCP paramaters, such as the
 * default gateway, subnet mask, and dns server may change.
 */
static int
renew(void)
{
	char *wan_ifname = safe_getenv("interface");
	char *value;
	char tmp[100], prefix[] = "wanXXXXXXXXXX_";
	int unit;
	int metric;

	if ((unit = wan_ifunit(wan_ifname)) < 0) {
		strcpy(prefix, "wanx_");
		metric = 2;
	} else {
		snprintf(prefix, sizeof(prefix), "wan%d_", unit);
		metric = atoi(nvram_safe_get(strcat_r(prefix, "priority", tmp)));
	}

	if (!(value = getenv("subnet")) || nvram_invmatch(strcat_r(prefix, "netmask", tmp), trim_r(value)))
		return bound();
	if (!(value = getenv("router")) || nvram_invmatch(strcat_r(prefix, "gateway", tmp), trim_r(value)))
		return bound();

	if ((value = getenv("dns")) && nvram_invmatch(strcat_r(prefix, "dns", tmp), trim_r(value))) {
		nvram_set(strcat_r(prefix, "dns", tmp), trim_r(value));
		update_resolvconf(wan_ifname, metric, 1);
	}
	if ((value = getenv("wins")))
		nvram_set(strcat_r(prefix, "wins", tmp), trim_r(value));

#if 0
	if ((value = getenv("hostname")))
		sethostname(trim_r(value), strlen(value) + 1);
#endif

	if ((value = getenv("domain")))
		nvram_set(strcat_r(prefix, "domain", tmp), trim_r(value));
	if ((value = getenv("lease"))) {
		nvram_set(strcat_r(prefix, "lease", tmp), trim_r(value));
		expires(wan_ifname, atoi(value));
	}

	//logmessage("dhcp client", "%s IP : %s from %s", 
	//		udhcpstate, 
	//		nvram_safe_get(strcat_r(prefix, "ipaddr", tmp)), 
	//		nvram_safe_get(strcat_r(prefix, "gateway", tmp)));

	wanmessage("");
	dprintf("done\n");
	return 0;
}

int
udhcpc_main(int argc, char **argv)
{
	if (argc<2 || !argv[1])
		return EINVAL;

	strcpy(udhcpstate, argv[1]);

	if (!strcmp(argv[1], "deconfig"))
		return deconfig();
	else if (!strcmp(argv[1], "bound"))
		return bound();
	else if (!strcmp(argv[1], "renew"))
		return renew();
	else if (!strcmp(argv[1], "leasefail"))
		return 0;
	else return deconfig();
}

#ifdef __CONFIG_IPV6__
int dhcp6c_main(int argc, char **argv)
{
	char *wan_ifname = safe_getenv("interface");
	char *dns6 = getenv("new_domain_name_servers");

	if (dns6) {
//TODO: Process new DNS records
		update_resolvconf(wan_ifname, 2, 1);
	}
	// notify radvd of possible change
	eval("killall", "-SIGHUP", "radvd");

	return 0;
}

int start_dhcp6c(char *wan_ifname)
{
	FILE *fp;
	pid_t pid;
	int sla_len, ret, is_wan6_valid;
	struct in6_addr wan6_addr;
	char *argv[] = { "/sbin/dhcp6c", "-d", "-D", "LL",  wan_ifname, NULL };

	if (!nvram_match("ipv6_proto", "dhcp6")) return 1;

	stop_dhcp6c();

	sla_len = 64 - atoi(nvram_safe_get("ipv6_lan_netsize"));
	if (sla_len <= 0)
		sla_len = 0;
	else if (sla_len > 16)
		sla_len = 16;
	is_wan6_valid = inet_pton(AF_INET6, nvram_safe_get("ipv6_wan_addr"), &wan6_addr);

	if ((fp = fopen("/etc/dhcp6c.conf", "w")) == NULL) {
		perror("/etc/dhcp6c.conf");
                return 2;
        }
	fprintf(fp, "interface %s {\n"
		    " send ia-pd 0;\n"
		    "%s"
		    " send rapid-commit;\n"		/* May cause unexpected advertise in case of server don't support rapid-commit */
		    " request domain-name-servers;\n"
		    " script \"/tmp/dhcp6c.script\";\n"
		    "};\n"
		    "id-assoc pd 0 {\n"
		    " prefix-interface %s {\n"
		    "  sla-id 0;\n"
		    "  sla-len %d;\n"
		    " };\n"
		    "};\n"
		    "id-assoc na 0 {};\n",
		    wan_ifname,
		    (is_wan6_valid==1) ? "" : " send ia-na 0;\n",
		    nvram_safe_get("lan_ifname"), sla_len
		);
        fclose(fp);
	ret = _eval(argv, NULL, 0, &pid);

        return ret;
}

void stop_dhcp6c(void)
{
	eval("killall", "-SIGTERM", "dhcp6c.script");
	kill_pidfile("/var/run/dhcp6c.pid");
}

#endif
