/*
 * Copyright 2004, ASUSTek Inc.
 * All Rights Reserved.
 * 
 * THIS SOFTWARE IS OFFERED "AS IS", AND ASUS GRANTS NO WARRANTIES OF ANY
 * KIND, EXPRESS OR IMPLIED, BY STATUTE, COMMUNICATION OR OTHERWISE. BROADCOM
 * SPECIFICALLY DISCLAIMS ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A SPECIFIC PURPOSE OR NONINFRINGEMENT CONCERNING THIS SOFTWARE.
 *
 * $Id$
 */
#ifdef USB_SUPPORT 
#include <stdio.h>
#include <signal.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <bcmnvram.h>
#include <shutils.h>
#include <rc.h>
#include <stdarg.h>

int
start_rcamd(void)
{
	eval("insmod", "videodev");
	eval("insmod", "v4l2-common");
	eval("insmod", "input-core");
	eval("insmod", "uvcvideo");
	return 0;
}

int
stop_rcamd(void)
{
	eval("killall", "mjpg_streamer");
	eval("rmmod", "uvcvideo");
	eval("rmmod", "input-core");
	eval("rmmod", "v4l2-common");
	eval("rmmod", "videodev");
	return 0;
}

int
hotplug_usb_webcam(char *product)
{
	char input_plugin_param[256], output_plugin_param[256], security[256];
	char *rcamd_argv[]={"mjpg_streamer", "-b",
				"-p", "/var/run/rcamd.pid",
				"-i", input_plugin_param,
				"-o", output_plugin_param,
				NULL};
	char *res;
	pid_t pid;

	if (nvram_match("usb_webenable_x", "0") || strlen(product) == 0) return 1;
	
	nvram_set("usb_webdriver_x", "uvc");

	// image size
	switch (atoi(nvram_safe_get("usb_webimage_x")))
	{
		case 8:
			res = "1920x1080";
			break;
		case 7:
			res = "1280x720";
			break;
		case 6:
			res = "1600x1200";
			break;
		case 5:
			res = "1280x1024";
			break;
		case 4:
			res = "1024x768";
			break;
		case 3:
			res = "800x600";
			break;
		case 2:
			res = "640x480";
			break;
		case 1:
			res = "320x240";
			break;
		case 0:
		default:
			res = "160x120";
			break;
	}

//	mkdir("/tmp/webcam", 0777);
//	symlink("/var/tmp/display.jpg", "/tmp/webcam/display.jpg");

	if (nvram_match("usb_webhttpcheck_x","1"))
	{
		sprintf(security, "-c %s:%s", nvram_safe_get("usb_webhttp_username"), nvram_safe_get("usb_webhttp_passwd"));
	}

	sprintf(input_plugin_param,  "input_uvc.so -r %s -f %s %s", res, nvram_safe_get("usb_webfresh_x"),
		(nvram_match("usb_webformat_x", "1") ? "-y" : ""));
	sprintf(output_plugin_param, "output_http.so -p %s %s", nvram_safe_get("usb_webhttpport_x"),(nvram_match("usb_webhttpcheck_x","1") ? security : ""));

	chdir("/tmp");

	return _eval(rcamd_argv, ">/dev/null", 0, &pid);
}

int
remove_usb_webcam(char *product)
{
	kill_pidfile("/var/run/rcamd.pid");

	nvram_set("usb_webdriver_x", "");
	return 0;
}

#endif
