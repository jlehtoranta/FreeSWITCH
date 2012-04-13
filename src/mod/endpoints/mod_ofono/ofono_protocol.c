/*
 * FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 * Copyright (C) 2005-2012, Anthony Minessale II <anthm@freeswitch.org>
 *
 * Version: MPL 1.1
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is FreeSWITCH Modular Media Switching Software Library / Soft-Switch Application
 *
 * The Initial Developer of the Original Code is
 * Anthony Minessale II <anthm@freeswitch.org>
 * Portions created by the Initial Developer are Copyright (C)
 * the Initial Developer. All Rights Reserved.
 *
 * This module (mod_ofono) has been contributed by:
 * Jarkko Lehtoranta <devel@jlranta.com>
 *
 * Further Contributors:
 *
 *
 * Partly based on mod_gsmopen, which has been contributed by:
 * Giovanni Maruzzelli <gmaruzz@gmail.com>
 *
 * ofono_protocol.c -- Ofono compatible Endpoint Module
 *
 */

#include "ofono.h"

#ifdef ASTERISK
#define ofono_sleep usleep
#define ofono_strncpy strncpy
#define tech_pvt p
extern int ofono_debug;
extern char *ofono_console_active;
#else /* FREESWITCH */
#define ofono_sleep switch_sleep
#define ofono_strncpy switch_copy_string
extern switch_memory_pool_t *ofono_module_pool;
extern switch_endpoint_interface_t *ofono_endpoint_interface;
#endif /* ASTERISK */

static int ofono_search_modem(private_t *tech_pvt);
static void ofono_search_modem_reply(DBusPendingCall *call, void *user_data);
static void ofono_dbus_connect(DBusConnection *conn, void *user_data);
static void ofono_disconnect(DBusConnection *conn, void *user_data);
static void ofono_disconnect_callback(DBusConnection *conn, void *user_data);
static int ofono_get_network_info(private_t *tech_pvt);
static void ofono_get_network_info_reply(DBusPendingCall *call, void *user_data);
static int ofono_get_sim_info(private_t *tech_pvt);
static void ofono_get_sim_info_reply(DBusPendingCall *call, void *user_data);
static gboolean ofono_signal_call_state(DBusConnection *conn, DBusMessage *msg,
		void *user_data);
static gboolean ofono_signal_call_added(DBusConnection *conn, DBusMessage *msg,
		void *user_data);
static gboolean ofono_signal_call_removed(DBusConnection *conn,
		DBusMessage *msg, void *user_data);
static gboolean ofono_signal_receivesms(DBusConnection *conn, DBusMessage *msg,
		void *user_data);
static gboolean ofono_signal_modem_state(DBusConnection *conn, DBusMessage *msg,
		void *user_data);
static gboolean ofono_signal_modem_removed(DBusConnection *conn,
		DBusMessage *msg, void *user_data);
static gboolean ofono_signal_modem_network_state(DBusConnection *conn,
		DBusMessage *msg, void *user_data);
static gboolean ofono_signal_audio_status(DBusConnection *conn,
		DBusMessage *msg, void *user_data);

/*int samplerate_ofono = SAMPLERATE_OFONO;*/

/* Ofono D-BUS */
#define OFONO_SERVICE "org.ofono"
#define OFONO_MANAGER_INTERFACE			OFONO_SERVICE ".Manager"
#define OFONO_MODEM_INTERFACE			OFONO_SERVICE ".Modem"
#define OFONO_CALLMANAGER_INTERFACE		OFONO_SERVICE ".VoiceCallManager"
#define OFONO_CALL_INTERFACE			OFONO_SERVICE ".VoiceCall"
#define OFONO_AUDIO_INTERFACE			OFONO_SERVICE ".AudioSettings"
#define OFONO_SUPPLEMENTARY_INTERFACE	OFONO_SERVICE ".SupplementaryServices"
#define OFONO_MESSAGEMANAGER_INTERFACE	OFONO_SERVICE ".MessageManager"
#define OFONO_NETWORK_INTERFACE			OFONO_SERVICE ".NetworkRegistration"
#define OFONO_SIM_INTERFACE				OFONO_SERVICE ".SimManager"

extern int running;
int ofono_dir_entry_extension = 1;

int option_debug = 100;

#ifdef WIN32
/***************/
// from http://www.openasthra.com/c-tidbits/gettimeofday-function-for-windows/
#include <time.h>

#if defined(_MSC_VER) || defined(_MSC_EXTENSIONS)
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000Ui64
#else /*  */
#define DELTA_EPOCH_IN_MICROSECS  11644473600000000ULL
#endif /*  */
struct sk_timezone {
	int tz_minuteswest; /* minutes W of Greenwich */
	int tz_dsttime; /* type of dst correction */
};
int gettimeofday(struct timeval *tv, struct sk_timezone *tz)
{
	FILETIME ft;
	unsigned __int64 tmpres = 0;
	static int tzflag;
	if (NULL != tv) {
		GetSystemTimeAsFileTime(&ft);
		tmpres |= ft.dwHighDateTime;
		tmpres <<= 32;
		tmpres |= ft.dwLowDateTime;

		/*converting file time to unix epoch */
		tmpres /= 10; /*convert into microseconds */
		tmpres -= DELTA_EPOCH_IN_MICROSECS;
		tv->tv_sec = (long) (tmpres / 1000000UL);
		tv->tv_usec = (long) (tmpres % 1000000UL);
	}
	if (NULL != tz) {
		if (!tzflag) {
			_tzset();
			tzflag++;
		}
		tz->tz_minuteswest = _timezone / 60;
		tz->tz_dsttime = _daylight;
	}
	return 0;
}

/***************/
#endif /* WIN32 */

#ifdef OFONO_PORTAUDIO
#include "pablio.h"

#ifndef GIOVA48
#define SAMPLES_PER_FRAME 160
#else // GIOVA48
#define SAMPLES_PER_FRAME 960
#endif // GIOVA48
int ofono_portaudio_devlist(private_t *tech_pvt)
{
	int i, numDevices;
	const PaDeviceInfo *deviceInfo;

	numDevices = Pa_GetDeviceCount();
	if (numDevices < 0) {
		return 0;
	}
	for (i = 0; i < numDevices; i++) {
		deviceInfo = Pa_GetDeviceInfo(i);
		NOTICA
		("Found PORTAUDIO device: id=%d\tname=%s\tmax input channels=%d\tmax output channels=%d\n",
				OFONO_P_LOG, i, deviceInfo->name, deviceInfo->maxInputChannels,
				deviceInfo->maxOutputChannels);
	}

	return numDevices;
}

int ofono_portaudio_init(private_t *tech_pvt)
{
	PaError err;
	int c;
	PaStreamParameters inputParameters, outputParameters;
	int numdevices;
	const PaDeviceInfo *deviceInfo;

#ifndef GIOVA48
	setenv("PA_ALSA_PLUGHW", "1", 1);
#endif // GIOVA48
	err = Pa_Initialize();
	if (err != paNoError)
	return err;

	numdevices = ofono_portaudio_devlist(tech_pvt);

	if (tech_pvt->portaudiocindex > (numdevices - 1)) {
		ERRORA("Portaudio Capture id=%d is out of range: valid id are from 0 to %d\n",
				OFONO_P_LOG, tech_pvt->portaudiocindex, (numdevices - 1));
		return -1;
	}

	if (tech_pvt->portaudiopindex > (numdevices - 1)) {
		ERRORA("Portaudio Playback id=%d is out of range: valid id are from 0 to %d\n",
				OFONO_P_LOG, tech_pvt->portaudiopindex, (numdevices - 1));
		return -1;
	}
	//inputParameters.device = 0;
	if (tech_pvt->portaudiocindex != -1) {
		inputParameters.device = tech_pvt->portaudiocindex;
	} else {
		inputParameters.device = Pa_GetDefaultInputDevice();
	}
	deviceInfo = Pa_GetDeviceInfo(inputParameters.device);
	NOTICA
	("Using INPUT PORTAUDIO device: id=%d\tname=%s\tmax input channels=%d\tmax output channels=%d\n",
			OFONO_P_LOG, inputParameters.device, deviceInfo->name,
			deviceInfo->maxInputChannels, deviceInfo->maxOutputChannels);
	if (deviceInfo->maxInputChannels == 0) {
		ERRORA
		("No INPUT channels on device: id=%d\tname=%s\tmax input channels=%d\tmax output channels=%d\n",
				OFONO_P_LOG, inputParameters.device, deviceInfo->name,
				deviceInfo->maxInputChannels, deviceInfo->maxOutputChannels);
		return -1;
	}
	inputParameters.channelCount = 1;
	inputParameters.sampleFormat = paInt16;
	//inputParameters.suggestedLatency = Pa_GetDeviceInfo(inputParameters.device)->defaultHighInputLatency;
	inputParameters.suggestedLatency = 0.1;
	inputParameters.hostApiSpecificStreamInfo = NULL;

	//outputParameters.device = 3;
	if (tech_pvt->portaudiopindex != -1) {
		outputParameters.device = tech_pvt->portaudiopindex;
	} else {
		outputParameters.device = Pa_GetDefaultOutputDevice();
	}
	deviceInfo = Pa_GetDeviceInfo(outputParameters.device);
	NOTICA
	("Using OUTPUT PORTAUDIO device: id=%d\tname=%s\tmax input channels=%d\tmax output channels=%d\n",
			OFONO_P_LOG, outputParameters.device, deviceInfo->name,
			deviceInfo->maxInputChannels, deviceInfo->maxOutputChannels);
	if (deviceInfo->maxOutputChannels == 0) {
		ERRORA
		("No OUTPUT channels on device: id=%d\tname=%s\tmax input channels=%d\tmax output channels=%d\n",
				OFONO_P_LOG, inputParameters.device, deviceInfo->name,
				deviceInfo->maxInputChannels, deviceInfo->maxOutputChannels);
		return -1;
	}
#ifndef GIOVA48
	outputParameters.channelCount = 1;
#else // GIOVA48
	outputParameters.channelCount = 2;
#endif // GIOVA48
	outputParameters.sampleFormat = paInt16;
	//outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultHighOutputLatency;
	outputParameters.suggestedLatency = 0.1;
	outputParameters.hostApiSpecificStreamInfo = NULL;

	/* build the pipe that will be polled on by pbx */
	c = pipe(tech_pvt->audiopipe);
	if (c) {
		ERRORA("Unable to create audio pipe\n", OFONO_P_LOG);
		return -1;
	}
	fcntl(tech_pvt->audiopipe[0], F_SETFL, O_NONBLOCK);
	fcntl(tech_pvt->audiopipe[1], F_SETFL, O_NONBLOCK);

	err =
#ifndef GIOVA48
	OpenAudioStream(&tech_pvt->stream, &inputParameters, &outputParameters, 8000,
			paClipOff|paDitherOff, SAMPLES_PER_FRAME, 0);
	//&tech_pvt->speexecho, &tech_pvt->speexpreprocess, &tech_pvt->owner);

#else // GIOVA48
	OpenAudioStream(&tech_pvt->stream, &inputParameters, &outputParameters, 48000,
			paDitherOff | paClipOff, SAMPLES_PER_FRAME, tech_pvt->audiopipe[1],
			&tech_pvt->speexecho, &tech_pvt->speexpreprocess, &tech_pvt->owner);

#endif// GIOVA48
	if (err != paNoError) {
		ERRORA("Unable to open audio stream: %s\n", OFONO_P_LOG, Pa_GetErrorText(err));
		return -1;
	}

	/* the pipe is our audio fd for pbx to poll on */
	tech_pvt->ofono_sound_capt_fd = tech_pvt->audiopipe[0];

	return 0;
}
//int ofono_portaudio_write(private_t *tech_pvt, struct ast_frame *f)
int ofono_portaudio_write(private_t * tech_pvt, short *data, int datalen)
{
	int samples;
#ifdef GIOVA48
	//short buf[OFONO_FRAME_SIZE * 2];
	short buf[3840];
	short *buf2;

	//ERRORA("1 f->datalen=: %d\n", OFONO_P_LOG, f->datalen);

	memset(buf, '\0', OFONO_FRAME_SIZE *2);

	buf2 = f->data;

	int i=0, a=0;

	for(i=0; i< f->datalen / sizeof(short); i++) {
//stereo, 2 chan 48 -> mono 8
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		buf[a] = buf2[i];
		a++;
		/*
		 */
	}
	f->data = &buf;
	f->datalen = f->datalen * 6;
	//ERRORA("2 f->datalen=: %d\n", OFONO_P_LOG, f->datalen);
	//f->datalen = f->datalen;
#endif // GIOVA48
	samples =
	WriteAudioStream(tech_pvt->stream, (short *) data, (int) (datalen / sizeof(short)), &tech_pvt->timer_write);

	if (samples != (int) (datalen / sizeof(short)))
	ERRORA("WriteAudioStream wrote: %d of %d\n", OFONO_P_LOG, samples,
			(int) (datalen / sizeof(short)));

	return samples;
}
//struct ast_frame *ofono_portaudio_read(private_t *tech_pvt)
#define AST_FRIENDLY_OFFSET 0
int ofono_portaudio_read(private_t * tech_pvt, short *data, int datalen)
{
#if 0
	//static struct ast_frame f;
	static short __buf[OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2];
	short *buf;
	static short __buf2[OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2];
	short *buf2;
	int samples;
	//char c;

	memset(__buf, '\0', (OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2));

	buf = __buf + AST_FRIENDLY_OFFSET / 2;

	memset(__buf2, '\0', (OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2));

	buf2 = __buf2 + AST_FRIENDLY_OFFSET / 2;

#if 0
	f.frametype = AST_FRAME_NULL;
	f.subclass = 0;
	f.samples = 0;
	f.datalen = 0;

#ifdef ASTERISK_VERSION_1_6_1
	f.data.ptr = NULL;
#else
	f.data = NULL;
#endif /* ASTERISK_VERSION_1_6_1 */
	f.offset = 0;
	f.src = ofono_type;
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;
#endif //0
	//if ((samples = ReadAudioStream(tech_pvt->stream, buf, SAMPLES_PER_FRAME)) == 0)
	//if ((samples = ReadAudioStream(tech_pvt->stream, data, datalen/sizeof(short))) == 0)
	if (samples = ReadAudioStream(tech_pvt->stream, (short *)data, datalen, &tech_pvt->timer_read) == 0) {
		//do nothing
	} else {
#ifdef GIOVA48
		int i=0, a=0;

		samples = samples / 6;
		for(i=0; i< samples; i++) {
			buf2[i] = buf[a];
			a = a + 6; //mono, 1 chan 48 -> 8
		}
		buf = buf2;

#if 0
		/* A real frame */
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = OFONO_FRAME_SIZE/6;
		f.datalen = OFONO_FRAME_SIZE * 2/6;
#endif //0
#else// GIOVA48
#if 0
		/* A real frame */
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = OFONO_FRAME_SIZE;
		f.datalen = OFONO_FRAME_SIZE * 2;
#endif //0
#endif// GIOVA48
#if 0
#ifdef ASTERISK_VERSION_1_6_1
		f.data.ptr = buf;
#else
		f.data = buf;
#endif /* ASTERISK_VERSION_1_6_1 */
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = ofono_type;
		f.mallocd = 0;
#endif //0
	}

#if 0
	read(tech_pvt->audiopipe[0], &c, 1);

	return &f;
#endif //0
#endif //0
	int samples;
	samples = ReadAudioStream(tech_pvt->stream, (short *)data, datalen, &tech_pvt->timer_read);
	//WARNINGA("samples=%d\n", OFONO_P_LOG, samples);

	return samples;
}
int ofono_portaudio_shutdown(private_t *tech_pvt)
{
	PaError err;

	err = CloseAudioStream(tech_pvt->stream);

	if (err != paNoError)
	ERRORA("not able to CloseAudioStream\n", OFONO_P_LOG);

	Pa_Terminate();
	return 0;
}

#endif // OFONO_PORTAUDIO
/* Shutdown function for a D-BUS connection. This is called manually. */
int ofono_shutdown_dbus(private_t * tech_pvt) {
	DEBUGA_OFONO("D-BUS disconnect\n", OFONO_P_LOG);

	/* Making sure that there will be no active calls costing money after shutdown */
	ofono_hangup(tech_pvt);

	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_service_watch);

	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_modem_removed_watch);
	tech_pvt->dbus_modem_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_modem_state_watch);
	tech_pvt->dbus_modem_state_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_added_watch);
	tech_pvt->dbus_call_added_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_removed_watch);
	tech_pvt->dbus_call_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_changed_watch);
	tech_pvt->dbus_call_changed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_receivesms_watch);
	tech_pvt->dbus_receivesms_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_network_state_watch);
	tech_pvt->dbus_network_state_watch = -1;

	dbus_connection_unref(tech_pvt->dbus_conn);
	g_main_loop_unref(tech_pvt->dbus_main_loop);

	g_free((gpointer) tech_pvt->dbus_call_path);
	g_free(tech_pvt->callid_number);
	g_free(tech_pvt->sms_body);
	g_free(tech_pvt->sms_sender);
	g_free(tech_pvt->sms_date);
	g_free(tech_pvt->ofono_modem_operator_name);
	g_free(tech_pvt->ofono_modem_imsi);
	tech_pvt->dbus_main_context = NULL;
	tech_pvt->dbus_main_loop = NULL;
	tech_pvt->dbus_conn = NULL;
	tech_pvt->dbus_call_path = NULL;
	tech_pvt->callid_number = NULL;
	tech_pvt->sms_body = NULL;
	tech_pvt->sms_sender = NULL;
	tech_pvt->sms_date = NULL;
	tech_pvt->ofono_modem_operator_name = NULL;
	tech_pvt->ofono_modem_imsi = NULL;

	tech_pvt->dbus_modem_state_online = 0;
	switch_sleep(1000000);
	return 0;
}

/* Checks and sends new D-BUS signals */
int ofono_dbus_context_iter(private_t * tech_pvt) {
	/* sleep 10ms */
	switch_sleep(10000);
	while (g_main_context_iteration(tech_pvt->dbus_main_context, FALSE))
		;
	return 0;
}

/* Initializes a new D-BUS connection and configures a modem,
 * which is connected to Ofono.
 */
int ofono_config_dbus_init(private_t * tech_pvt) {

	DBusError err;
	DBusMessage *msg;

	DEBUGA_OFONO("INFO: Initializing D-BUS and configuring Ofono\n",
			OFONO_P_LOG);

	tech_pvt->dbus_error = 0;
	tech_pvt->dbus_main_loop = g_main_loop_new(NULL, FALSE);
	tech_pvt->dbus_main_context = g_main_loop_get_context(
			tech_pvt->dbus_main_loop);

	dbus_error_init(&err);

	tech_pvt->dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, &err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		ERRORA("Ofono D-BUS can't register with system bus\n", OFONO_P_LOG);
		return -1;
	}

	g_dbus_set_disconnect_function(tech_pvt->dbus_conn,
			ofono_disconnect_callback, tech_pvt, NULL);

	if (tech_pvt->ofono_modem_name == NULL) {
		ERRORA("D-BUS modem name not defined in config\n", OFONO_P_LOG);
		ERRORA("Disconnecting D-BUS\n", OFONO_P_LOG);
		dbus_connection_unref(tech_pvt->dbus_conn);
		g_main_loop_unref(tech_pvt->dbus_main_loop);
		tech_pvt->dbus_main_context = NULL;
		tech_pvt->dbus_main_loop = NULL;
		tech_pvt->dbus_conn = NULL;
		return -1;
	}

	DEBUGA_OFONO("Modem path: \"%s\"\n", OFONO_P_LOG,
			tech_pvt->ofono_modem_name);
	tech_pvt->dbus_modem_state_online = -1;

	tech_pvt->dbus_service_watch = g_dbus_add_service_watch(tech_pvt->dbus_conn,
			OFONO_SERVICE, ofono_dbus_connect, ofono_disconnect, tech_pvt,
			NULL);
	switch_sleep(100000);
	ofono_dbus_context_iter(tech_pvt);
	switch_sleep(100000);
	ofono_dbus_context_iter(tech_pvt);

	if (tech_pvt->dbus_modem_state_online == -1) {
		ERRORA("Couldn't find modem path \"%s\"\n", OFONO_P_LOG,
				tech_pvt->ofono_modem_name);
		ERRORA("Disconnecting D-BUS\n", OFONO_P_LOG);
		ofono_shutdown_dbus(tech_pvt);
		return -1;
	}
	tech_pvt->dbus_modem_attached = 1;

	DEBUGA_OFONO("INFO: Found modem path \"%s\"\n", OFONO_P_LOG,
			tech_pvt->ofono_modem_name);

	if (tech_pvt->dbus_modem_state_powered == 0) {
		dbus_bool_t state = TRUE;
		const char *property = "Powered";
		DBusMessageIter iter, variant;

		msg = dbus_message_new_method_call(OFONO_SERVICE,
				tech_pvt->ofono_modem_name, OFONO_MODEM_INTERFACE,
				"SetProperty");
		if (msg == NULL) {
			ERRORA("Setting modem powered failed! Couldn't create message\n",
					OFONO_P_LOG);
		} else {
			dbus_message_iter_init_append(msg, &iter);
			dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &property);
			dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b",
					&variant);
			dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &state);
			dbus_message_iter_close_container(&iter, &variant);

			dbus_message_set_auto_start(msg, FALSE);

			dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg,
					20000, &err);

			if (dbus_error_is_set(&err)) {
				ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
				dbus_error_free(&err);
				ERRORA("Setting modem powered failed! Couldn't send message\n",
						OFONO_P_LOG);
				dbus_message_unref(msg);
			} else {
				int i;

				dbus_message_unref(msg);

				/* IMSI should be known before setting modem to online state */
				for (i = 0; i < 5; i++) {
					switch_sleep(1000000);
					ofono_get_sim_info(tech_pvt);
					switch_sleep(100000);
					ofono_dbus_context_iter(tech_pvt);
					switch_sleep(100000);
					ofono_dbus_context_iter(tech_pvt);
					if (tech_pvt->ofono_modem_imsi != NULL) {
						break;
					}
				}
			}
		}
	} else {
		ofono_get_sim_info(tech_pvt);
		switch_sleep(100000);
		ofono_dbus_context_iter(tech_pvt);
		switch_sleep(100000);
		ofono_dbus_context_iter(tech_pvt);
	}

	tech_pvt->ofono_modem_signal_strength = -1;

	if (tech_pvt->ofono_modem_imsi == NULL) {
		WARNINGA(
				"WARNING: IMSI of the SIM is not known. \
				Setting modem to online state might result into an error!\n",
				OFONO_P_LOG);
	}

	if (tech_pvt->dbus_modem_state_online == 0) {
		dbus_bool_t state = TRUE;
		const char *property = "Online";
		DBusMessageIter iter;
		DBusMessageIter variant;

		msg = dbus_message_new_method_call(OFONO_SERVICE,
				tech_pvt->ofono_modem_name, OFONO_MODEM_INTERFACE,
				"SetProperty");
		if (msg == NULL) {
			ERRORA("Setting modem online failed! Couldn't create message\n",
					OFONO_P_LOG);
		} else {
			dbus_message_iter_init_append(msg, &iter);
			dbus_message_iter_append_basic(&iter, DBUS_TYPE_STRING, &property);
			dbus_message_iter_open_container(&iter, DBUS_TYPE_VARIANT, "b",
					&variant);
			dbus_message_iter_append_basic(&variant, DBUS_TYPE_BOOLEAN, &state);
			dbus_message_iter_close_container(&iter, &variant);

			dbus_message_set_auto_start(msg, FALSE);

			dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg,
					20000, &err);

			if (dbus_error_is_set(&err)) {
				ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
				dbus_error_free(&err);
				ERRORA("Setting modem online failed! Couldn't send message\n",
						OFONO_P_LOG);
				dbus_message_unref(msg);
			}

			dbus_message_unref(msg);
		}
	} else {
		ofono_get_network_info(tech_pvt);
	}

	switch_sleep(100000);
	ofono_dbus_context_iter(tech_pvt);
	switch_sleep(100000);
	ofono_dbus_context_iter(tech_pvt);

	if (tech_pvt->dbus_modem_state_powered == 0
			|| tech_pvt->dbus_modem_state_online == 0) {
		ERRORA("Couldn't set modem \"%s\" state on and online\n", OFONO_P_LOG,
				tech_pvt->ofono_modem_name);
		ERRORA("Disconnecting D-BUS\n", OFONO_P_LOG);
		ofono_shutdown_dbus(tech_pvt);
		return -1;
	}

	/* If this function is called due to a D-BUS disconnect signal,
	 we might have missed a hang-up and have an active call in Ofono.
	 Making sure that there are no active calls costing money.. */
	if (!strlen(tech_pvt->session_uuid_str)) {
		ofono_hangup(tech_pvt);
	}

	switch_sleep(100000);
	ofono_dbus_context_iter(tech_pvt);

	DEBUGA_OFONO("SUCCESS: D-BUS initialized, Ofono configured\n", OFONO_P_LOG);
	return 0;
}

/* Creates watches for D-BUS signals and runs a modem search. */
static void ofono_dbus_connect(DBusConnection *conn, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;

	DEBUGA_OFONO(
			"INFO: Searching for the modem and adding D-BUS signal watches\n",
			OFONO_P_LOG);

	/* D-BUS signals */
	tech_pvt->dbus_modem_removed_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, NULL, OFONO_MANAGER_INTERFACE,
			"ModemRemoved", ofono_signal_modem_removed, tech_pvt, NULL);
	tech_pvt->dbus_modem_state_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
			OFONO_MODEM_INTERFACE, "PropertyChanged", ofono_signal_modem_state,
			tech_pvt, NULL);
	tech_pvt->dbus_call_added_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
			OFONO_CALLMANAGER_INTERFACE, "CallAdded", ofono_signal_call_added,
			tech_pvt, NULL);
	tech_pvt->dbus_call_removed_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
			OFONO_CALLMANAGER_INTERFACE, "CallRemoved",
			ofono_signal_call_removed, tech_pvt, NULL);
	tech_pvt->dbus_call_changed_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, NULL, OFONO_CALL_INTERFACE,
			"PropertyChanged", ofono_signal_call_state, tech_pvt, NULL);
	tech_pvt->dbus_receivesms_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
			OFONO_MESSAGEMANAGER_INTERFACE, "IncomingMessage",
			ofono_signal_receivesms, tech_pvt, NULL);
	tech_pvt->dbus_network_state_watch = g_dbus_add_signal_watch(
			tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
			OFONO_NETWORK_INTERFACE, "PropertyChanged",
			ofono_signal_modem_network_state, tech_pvt, NULL);
	if (tech_pvt->huawei_audio == 1) {
		tech_pvt->dbus_audio_status_watch = g_dbus_add_signal_watch(
				tech_pvt->dbus_conn, NULL, tech_pvt->ofono_modem_name,
				OFONO_AUDIO_INTERFACE, "PropertyChanged",
				ofono_signal_audio_status, tech_pvt, NULL);
	}
	ofono_search_modem(tech_pvt);
}

/* A callback function, which catches the D-BUS reply for a list of modems. */
static void ofono_search_modem_reply(DBusPendingCall *call, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, list;
	DBusError err;
	const char *path;
	const char *key;

	DEBUGA_OFONO("INFO: Got D-BUS reply, getting modem info.\n", OFONO_P_LOG);

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}
	if (dbus_message_has_signature(reply, "a(oa{sv})") == FALSE) {
		ERRORA("dbus_message_has_signature == FALSE\n", OFONO_P_LOG);
		goto done;
	}
	if (dbus_message_iter_init(reply, &iter) == FALSE) {
		ERRORA("dbus_message_iter_init == FALSE\n", OFONO_P_LOG);
		goto done;
	}

	dbus_message_iter_recurse(&iter, &list);

	while (dbus_message_iter_get_arg_type(&list) == DBUS_TYPE_STRUCT) {
		DBusMessageIter entry, dict;

		dbus_message_iter_recurse(&list, &entry);
		dbus_message_iter_get_basic(&entry, &path);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &dict);

		DEBUGA_OFONO("INFO: Found modem \"%s\"\n", OFONO_P_LOG, path);
		if (g_str_equal(path, tech_pvt->ofono_modem_name) == TRUE) {
			while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
				DBusMessageIter value;

				dbus_message_iter_recurse(&dict, &entry);
				dbus_message_iter_get_basic(&entry, &key);

				dbus_message_iter_next(&entry);
				dbus_message_iter_recurse(&entry, &value);

				if (g_str_equal(key, "Powered") == TRUE) {
					dbus_bool_t val;

					dbus_message_iter_get_basic(&value, &val);
					if (val == TRUE) {
						DEBUGA_OFONO("INFO: Modem \"%s\" powered\n",
								OFONO_P_LOG, path);
						tech_pvt->dbus_modem_state_powered = 1;
					} else {
						DEBUGA_OFONO("INFO: Modem \"%s\" not powered\n",
								OFONO_P_LOG, path);
						tech_pvt->dbus_modem_state_powered = 0;
					}
				}

				if (g_str_equal(key, "Online") == TRUE) {
					dbus_bool_t val;

					dbus_message_iter_get_basic(&value, &val);
					if (val == TRUE) {
						DEBUGA_OFONO("INFO: Modem \"%s\" online\n", OFONO_P_LOG,
								path);
						tech_pvt->dbus_modem_state_online = 1;
					} else {
						DEBUGA_OFONO("INFO: Modem \"%s\" offline\n",
								OFONO_P_LOG, path);
						tech_pvt->dbus_modem_state_online = 0;
					}
				}

				dbus_message_iter_next(&dict);
			}
		}

		dbus_message_iter_next(&list);
	}

	done: dbus_message_unref(reply);
}

/* Sends a new D-BUS message, which asks for a list of modems connected to Ofono */
static int ofono_search_modem(private_t *tech_pvt) {

	DBusMessage *msg;
	DBusPendingCall *call;

	msg = dbus_message_new_method_call(OFONO_SERVICE, "/",
			OFONO_MANAGER_INTERFACE, "GetModems");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_set_auto_start(msg, FALSE);

	if (dbus_connection_send_with_reply(tech_pvt->dbus_conn, msg, &call, -1)
			== FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, ofono_search_modem_reply, tech_pvt,
			NULL);

	dbus_pending_call_unref(call);

	return 0;
}

/* A callback function, which catches the D-BUS reply for information about
 * the mobile network connectivity of the modem.
 */
static void ofono_get_network_info_reply(DBusPendingCall *call, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, dict;
	DBusError err;
	const char *key;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}
	if (dbus_message_has_signature(reply, "a{sv}") == FALSE) {
		ERRORA("dbus_message_has_signature == FALSE\n", OFONO_P_LOG);
		goto done;
	}
	if (dbus_message_iter_init(reply, &iter) == FALSE) {
		ERRORA("dbus_message_iter_init == FALSE\n", OFONO_P_LOG);
		goto done;
	}

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {

		DBusMessageIter value, entry;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (g_str_equal(key, "Status") == TRUE) {
			const char *status;
			dbus_message_iter_get_basic(&value, &status);
			DEBUGA_OFONO("INFO: Modem network registration status = \"%s\"\n",
					OFONO_P_LOG, status);
		}

		if (g_str_equal(key, "Technology") == TRUE) {
			const char *status;
			dbus_message_iter_get_basic(&value, &status);
			DEBUGA_OFONO("INFO: Current network technology used = \"%s\"\n",
					OFONO_P_LOG, status);
		}

		if (g_str_equal(key, "Strength") == TRUE) {
			unsigned char status;
			dbus_message_iter_get_basic(&value, &status);
			if ((int) status > 15) {
				DEBUGA_OFONO("INFO: Current signal strength = %d%%\n",
						OFONO_P_LOG, (int) status);
			} else {
				WARNINGA("WARNING: Current signal strength = %d%%\n",
						OFONO_P_LOG, (int) status);
			}
			tech_pvt->ofono_modem_signal_strength = (int) status;
		}
		if (g_str_equal(key, "Name") == TRUE) {
			const char *status;
			dbus_message_iter_get_basic(&value, &status);
			DEBUGA_OFONO("INFO: Current operator = \"%s\"\n", OFONO_P_LOG,
					status);
			tech_pvt->ofono_modem_operator_name = g_strdup(status);
		}

		dbus_message_iter_next(&dict);
	}

	done: dbus_message_unref(reply);
}

/* Sends a new D-BUS message, which asks for information about the mobile
 * network connectivity of the modem.
 */
static int ofono_get_network_info(private_t *tech_pvt) {

	DBusMessage *msg;
	DBusPendingCall *call;

	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_NETWORK_INTERFACE,
			"GetProperties");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_set_auto_start(msg, FALSE);

	if (dbus_connection_send_with_reply(tech_pvt->dbus_conn, msg, &call, -1)
			== FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, ofono_get_network_info_reply, tech_pvt,
			NULL);

	dbus_pending_call_unref(call);

	return 0;
}

/* A callback function, which catches the D-BUS reply for information about
 * the SIM properties.
 */
static void ofono_get_sim_info_reply(DBusPendingCall *call, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessage *reply = dbus_pending_call_steal_reply(call);
	DBusMessageIter iter, dict;
	DBusError err;
	const char *key;

	dbus_error_init(&err);

	if (dbus_set_error_from_message(&err, reply) == TRUE) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		goto done;
	}
	if (dbus_message_has_signature(reply, "a{sv}") == FALSE) {
		ERRORA("dbus_message_has_signature == FALSE\n", OFONO_P_LOG);
		goto done;
	}
	if (dbus_message_iter_init(reply, &iter) == FALSE) {
		ERRORA("dbus_message_iter_init == FALSE\n", OFONO_P_LOG);
		goto done;
	}

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {

		DBusMessageIter value, entry;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &key);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (g_str_equal(key, "Present") == TRUE) {
			dbus_bool_t status;
			dbus_message_iter_get_basic(&value, &status);
			if (status == FALSE)
				ERRORA("ERROR: SIM card not present\n", OFONO_P_LOG);
		}

		if (g_str_equal(key, "SubscriberIdentity") == TRUE) {
			const char *status;
			dbus_message_iter_get_basic(&value, &status);
			tech_pvt->ofono_modem_imsi = g_strdup(status);
		}

		dbus_message_iter_next(&dict);
	}

	done: dbus_message_unref(reply);
}

/* Sends a new D-BUS message, which asks for information about the SIM
 * properties.
 */
static int ofono_get_sim_info(private_t *tech_pvt) {

	DBusMessage *msg;
	DBusPendingCall *call;

	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_SIM_INTERFACE, "GetProperties");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_set_auto_start(msg, FALSE);

	if (dbus_connection_send_with_reply(tech_pvt->dbus_conn, msg, &call, -1)
			== FALSE) {
		dbus_message_unref(msg);
		return -EIO;
	}

	dbus_message_unref(msg);

	if (call == NULL)
		return -EINVAL;

	dbus_pending_call_set_notify(call, ofono_get_sim_info_reply, tech_pvt,
			NULL);

	dbus_pending_call_unref(call);

	return 0;
}

/* If a D-BUS service watch disconnect signal is caught, this function gets called. */
static void ofono_disconnect(DBusConnection *conn, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	ERRORA(
			"D-BUS service watch disconnect. Trying to restart D-BUS connection.\n",
			OFONO_P_LOG);

	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_service_watch);

	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_modem_removed_watch);
	tech_pvt->dbus_modem_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_modem_state_watch);
	tech_pvt->dbus_modem_state_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_added_watch);
	tech_pvt->dbus_call_added_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_removed_watch);
	tech_pvt->dbus_call_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_changed_watch);
	tech_pvt->dbus_call_changed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_receivesms_watch);
	tech_pvt->dbus_receivesms_watch = -1;
	if (tech_pvt->huawei_audio == 1) {
		g_dbus_remove_watch(tech_pvt->dbus_conn,
				tech_pvt->dbus_audio_status_watch);
		tech_pvt->dbus_audio_status_watch = -1;
	}
	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_network_state_watch);
	tech_pvt->dbus_network_state_watch = -1;

	dbus_connection_unref(tech_pvt->dbus_conn);
	g_main_loop_unref(tech_pvt->dbus_main_loop);

	g_free((gpointer) tech_pvt->dbus_call_path);
	g_free(tech_pvt->callid_number);
	g_free(tech_pvt->sms_body);
	g_free(tech_pvt->sms_sender);
	g_free(tech_pvt->sms_date);
	tech_pvt->dbus_main_context = NULL;
	tech_pvt->dbus_main_loop = NULL;
	tech_pvt->dbus_conn = NULL;
	tech_pvt->dbus_call_path = NULL;
	tech_pvt->callid_number = NULL;
	tech_pvt->sms_body = NULL;
	tech_pvt->sms_sender = NULL;
	tech_pvt->sms_date = NULL;

	tech_pvt->dbus_modem_state_online = 0;
	switch_sleep(1000000);

	if (ofono_config_dbus_init(tech_pvt) == 0) {
		WARNINGA("D-BUS connection successfully restarted.\n", OFONO_P_LOG);
	} else {
		ERRORA("Restarting D-BUS connection failed!", OFONO_P_LOG);
		tech_pvt->dbus_error = 1;
	}
}

/* If the D-BUS connection breaks for some reason, this function gets called. */
static void ofono_disconnect_callback(DBusConnection *conn, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	ERRORA("Got D-BUS disconnect. Trying to restart D-BUS connection.\n",
			OFONO_P_LOG);

	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_service_watch);

	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_modem_removed_watch);
	tech_pvt->dbus_modem_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_modem_state_watch);
	tech_pvt->dbus_modem_state_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_added_watch);
	tech_pvt->dbus_call_added_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_removed_watch);
	tech_pvt->dbus_call_removed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_call_changed_watch);
	tech_pvt->dbus_call_changed_watch = -1;
	g_dbus_remove_watch(tech_pvt->dbus_conn, tech_pvt->dbus_receivesms_watch);
	tech_pvt->dbus_receivesms_watch = -1;
	if (tech_pvt->huawei_audio == 1) {
		g_dbus_remove_watch(tech_pvt->dbus_conn,
				tech_pvt->dbus_audio_status_watch);
		tech_pvt->dbus_audio_status_watch = -1;
	}
	g_dbus_remove_watch(tech_pvt->dbus_conn,
			tech_pvt->dbus_network_state_watch);
	tech_pvt->dbus_network_state_watch = -1;

	dbus_connection_unref(tech_pvt->dbus_conn);
	g_main_loop_unref(tech_pvt->dbus_main_loop);

	g_free((gpointer) tech_pvt->dbus_call_path);
	g_free(tech_pvt->callid_number);
	g_free(tech_pvt->sms_body);
	g_free(tech_pvt->sms_sender);
	g_free(tech_pvt->sms_date);
	tech_pvt->dbus_main_context = NULL;
	tech_pvt->dbus_main_loop = NULL;
	tech_pvt->dbus_conn = NULL;
	tech_pvt->dbus_call_path = NULL;
	tech_pvt->callid_number = NULL;
	tech_pvt->sms_body = NULL;
	tech_pvt->sms_sender = NULL;
	tech_pvt->sms_date = NULL;

	tech_pvt->dbus_modem_state_online = 0;
	switch_sleep(1000000);

	if (ofono_config_dbus_init(tech_pvt) == 0) {
		WARNINGA("D-BUS connection successfully restarted.\n", OFONO_P_LOG);
	} else {
		ERRORA("Restarting D-BUS connection failed!", OFONO_P_LOG);
		tech_pvt->dbus_error = 1;
	}
}

/* Perhaps in the future a way to communicate directly with the Ofono D-BUS API. */
int ofono_dbus_write_request(private_t * tech_pvt, const char *data) {
	/* TODO: dbus method calls from freeswitch */

	ERRORA("NOT IMPLEMENTED!\n", OFONO_P_LOG);

	return 0;
}

/* Answers to an incoming call. The function is called from "channel_answer_channel". */
int ofono_answer(private_t * tech_pvt) {

	DBusMessage *msg;
	DBusError err;

	dbus_error_init(&err);

	if (option_debug) {
		DEBUGA_OFONO("ENTERING FUNC\n", OFONO_P_LOG);
	}

	msg = dbus_message_new_method_call(OFONO_SERVICE, tech_pvt->dbus_call_path,
			OFONO_CALL_INTERFACE, "Answer");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_set_auto_start(msg, FALSE);

	dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg, -1,
			&err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(msg);
		ERRORA("D-BUS modem answer failed\n", OFONO_P_LOG);
		return -EIO;
	}

	DEBUGA_OFONO("INFO: D-BUS modem answered\n", OFONO_P_LOG);

	dbus_message_unref(msg);

	tech_pvt->interface_state = OFONO_STATE_UP;
	tech_pvt->phone_callflow = CALLFLOW_CALL_ACTIVE;

	DEBUGA_PBX("Call answered\n", OFONO_P_LOG);

	if (option_debug) {
		DEBUGA_PBX("EXITING FUNC\n", OFONO_P_LOG);
	}

	return 0;
}

/* Creates a new channel for an incoming call. The function is called from
 * "ofono_do_ofonoapi_thread_func".
 */
int ofono_incoming_call(private_t * tech_pvt) {

	int res = 0;
	switch_core_session_t *session = NULL;
	switch_channel_t *channel = NULL;

	if (option_debug) {
		DEBUGA_PBX("ENTERING FUNC\n", OFONO_P_LOG);
	}

	session = switch_core_session_locate(tech_pvt->session_uuid_str);
	if (session) {
		switch_core_session_rwunlock(session);
		return 0;
	}

	new_inbound_channel(tech_pvt);

	ofono_sleep(10000);

	session = switch_core_session_locate(tech_pvt->session_uuid_str);
	if (session) {
		channel = switch_core_session_get_channel(session);

		switch_core_session_queue_indication(session,
				SWITCH_MESSAGE_INDICATE_RINGING);
		if (channel) {
			switch_channel_mark_ring_ready(channel);
		} else {
			ERRORA("no session\n", OFONO_P_LOG);
		}
		switch_core_session_rwunlock(session);
	} else {
		ERRORA("no session\n", OFONO_P_LOG);

	}

	if (option_debug) {
		DEBUGA_PBX("EXITING FUNC\n", OFONO_P_LOG);
	}

	tech_pvt->interface_state = OFONO_STATE_PREANSWER;

	return res;
}

/* Hangs up all active calls on the voice modem. */
int ofono_hangup(private_t * tech_pvt) {

	DBusMessage *msg;
	DBusError err;

	dbus_error_init(&err);

	/* if there is not ofono pvt why we are here ? */
	if (!tech_pvt) {
		ERRORA("Asked to hangup channel not connected\n", OFONO_P_LOG);
		return 0;
	}

	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_CALLMANAGER_INTERFACE,
			"HangupAll");
	if (msg == NULL)
		return -ENOMEM;

	dbus_message_set_auto_start(msg, FALSE);

	dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg, -1,
			&err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(msg);
		ERRORA("D-BUS modem hang-up failed\n", OFONO_P_LOG);
		return -EIO;
	}

	DEBUGA_OFONO("INFO: D-BUS modem hung-up\n", OFONO_P_LOG);

	dbus_message_unref(msg);

	tech_pvt->interface_state = OFONO_STATE_IDLE;
	tech_pvt->phone_callflow = CALLFLOW_CALL_IDLE;

	switch_set_flag(tech_pvt, TFLAG_HANGUP);

	return 0;
}

/* Changes the state of the modem's audio interface based on an AudioSettings signal. */
static gboolean ofono_signal_audio_status(DBusConnection *conn,
		DBusMessage *msg, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, value;
	const char *key;
	dbus_bool_t status;

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &key);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (g_str_equal(key, "Active") == TRUE) {
		dbus_message_iter_get_basic(&value, &status);
		if (status == TRUE) {
			DEBUGA_OFONO(
					"INFO: D-BUS Ofono modem audio channel state = enabled\n",
					OFONO_P_LOG);
			tech_pvt->modem_audio_active = 1;
			if (tech_pvt->huawei_audio == 1) {
				huawei_audio_init(tech_pvt);
			}
		} else {
			DEBUGA_OFONO(
					"INFO: D-BUS Ofono modem audio channel state = disabled\n",
					OFONO_P_LOG);
			tech_pvt->modem_audio_active = 0;
			if (tech_pvt->huawei_audio == 1) {
				huawei_audio_shutdown(tech_pvt);
			}
		}
	}

	return TRUE;

}

/* Huawei serial audio initialization function. */
int huawei_audio_init(private_t *tech_pvt) {
	struct termios ti;
	int fd;

	switch_mutex_lock(tech_pvt->mutex_audio_in);
	switch_mutex_lock(tech_pvt->mutex_audio_out);

	/* Huawei serial audio init */
	fd = open(tech_pvt->huawei_serial_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		ERRORA("huawei_audio: Failed to open Huawei audio port (path=%s)\n",
				OFONO_P_LOG, tech_pvt->huawei_serial_path);
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		switch_mutex_unlock(tech_pvt->mutex_audio_out);
		return -1;
	}

	/* Switch TTY to raw mode */
	memset(&ti, 0, sizeof(ti));
	cfmakeraw(&ti);

	tcflush(fd, TCIOFLUSH);
	tcsetattr(fd, TCSANOW, &ti);

	/* Huawei serial audio transmit/receive channel */
	tech_pvt->huawei_audio_channel = g_io_channel_unix_new(fd);
	if (tech_pvt->huawei_audio_channel == NULL) {
		ERRORA("huawei_audio: Failed to create an IO channel\n", OFONO_P_LOG);
		close(fd);
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		switch_mutex_unlock(tech_pvt->mutex_audio_out);
		return -1;
	}
	g_io_channel_set_flags(tech_pvt->huawei_audio_channel, G_IO_FLAG_NONBLOCK,
			NULL);
	g_io_channel_set_encoding(tech_pvt->huawei_audio_channel, NULL, NULL);
	g_io_channel_set_buffered(tech_pvt->huawei_audio_channel, FALSE);

	DEBUGA_OFONO("huawei_audio: Channel successfully initialized!\n",
			OFONO_P_LOG);

	switch_mutex_unlock(tech_pvt->mutex_audio_in);
	switch_mutex_unlock(tech_pvt->mutex_audio_out);
	return 0;
}

/* Huawei serial audio shutdown function. */
int huawei_audio_shutdown(private_t *tech_pvt) {

	switch_mutex_lock(tech_pvt->mutex_audio_in);
	switch_mutex_lock(tech_pvt->mutex_audio_out);

	if (tech_pvt->huawei_audio_channel == NULL) {
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		switch_mutex_unlock(tech_pvt->mutex_audio_out);
		return 0;
	}

	g_io_channel_shutdown(tech_pvt->huawei_audio_channel, TRUE, NULL);
	g_io_channel_unref(tech_pvt->huawei_audio_channel);
	tech_pvt->huawei_audio_channel = NULL;

	DEBUGA_OFONO("huawei_audio: Channel shutdown\n", OFONO_P_LOG);

	switch_mutex_unlock(tech_pvt->mutex_audio_in);
	switch_mutex_unlock(tech_pvt->mutex_audio_out);
	return 0;
}

/* Transmits audio frames to the Huawei serial audio interface. */
int huawei_audio_write(private_t *tech_pvt, short *data, int datalen) {
	static char hwbuf[320];
	struct pollfd pollfd;
	/* datalen in bytes, len in bytes */
	int len = datalen;
	int n = 0;
	int samples = 0;
	gsize tmplen = 0;
	GError *err = NULL;

	switch_mutex_lock(tech_pvt->mutex_audio_out);

	if (tech_pvt->no_sound == 1 && tech_pvt->huawei_audio == 0) {
		switch_mutex_unlock(tech_pvt->mutex_audio_out);
		return -2;
	}
	if (tech_pvt->huawei_audio_channel == NULL) {
		switch_mutex_unlock(tech_pvt->mutex_audio_out);
		return -3;
	}

	pollfd.fd = g_io_channel_unix_get_fd(tech_pvt->huawei_audio_channel);
	pollfd.events = POLLOUT | POLLWRBAND;

	while (len > 0) {
		tmplen = 0;
		memset(hwbuf, 0, sizeof(hwbuf));
		/* Wait for channel to become available */
		poll(&pollfd, 1, 1000);
		/* Write 320 bytes per iteration */
		if (len > sizeof(hwbuf)) {
			memcpy(hwbuf, data + (n * sizeof(hwbuf)), sizeof(hwbuf));
			len -= sizeof(hwbuf);
		} else {
			memcpy(hwbuf, data + (n * sizeof(hwbuf)), len);
			len = 0;
		}
		if (g_io_channel_write_chars(tech_pvt->huawei_audio_channel, hwbuf,
				sizeof(hwbuf), &tmplen, &err) != G_IO_STATUS_NORMAL) {
			if (err) {
				ERRORA("huawei_audio: Write error = \"%s\"\n", OFONO_P_LOG,
						err->message);
				g_error_free(err);
			}
			ERRORA("huawei_audio: Writing to Huawei failed\n", OFONO_P_LOG);
		}
		samples += tmplen / 2;
		n++;
	}
	memset(data, 0, datalen);

	switch_mutex_unlock(tech_pvt->mutex_audio_out);
	return samples;
}

/* Receives audio frames from the Huawei serial audio interface. */
int huawei_audio_read(private_t *tech_pvt, short *data, int datalen) {
	static char hwbuf[1280];
	struct pollfd pollfd;
	/* datalen in frames, len in bytes */
	int len = datalen * 2;
	int samples = 0;
	gsize grlen = 0;
	GError *err = NULL;

	switch_mutex_lock(tech_pvt->mutex_audio_in);

	if (tech_pvt->no_sound == 1 && tech_pvt->huawei_audio == 0) {
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		return -2;
	}
	if (tech_pvt->huawei_audio_channel == NULL) {
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		return -3;
	}

	if (len > sizeof(hwbuf)) {
		ERRORA(
				"huawei_audio: Read length exceeds the size of hwbuf. Adjusting it to the max size.\n",
				OFONO_P_LOG);
		len = sizeof(hwbuf);
	}
	memset(hwbuf, 0, len);

	pollfd.fd = g_io_channel_unix_get_fd(tech_pvt->huawei_audio_channel);
	pollfd.events = POLLIN | POLLPRI;

	poll(&pollfd, 1, 1000);

	if (g_io_channel_read_chars(tech_pvt->huawei_audio_channel, hwbuf, len,
			&grlen, &err) != G_IO_STATUS_NORMAL) {
		if (err) {
			ERRORA("huawei_audio: Read error = \"%s\"\n", OFONO_P_LOG,
					err->message);
			g_error_free(err);
		}
		ERRORA("huawei_audio: Reading from Huawei failed\n", OFONO_P_LOG);
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		return -1;
	}
	samples = grlen / 2;

	if (samples < OFONO_FRAME_SIZE) {
		/* If we haven't got a full frame, add silence to the end */
		memcpy(data, hwbuf, (OFONO_FRAME_SIZE * 2));
		switch_mutex_unlock(tech_pvt->mutex_audio_in);
		return OFONO_FRAME_SIZE;
	}

	memcpy(data, hwbuf, grlen);
	switch_mutex_unlock(tech_pvt->mutex_audio_in);
	return samples;
}

#ifdef OFONO_ALSA
/*! \brief ALSA pcm format, according to endianess  */
#if __BYTE_ORDER == __LITTLE_ENDIAN
snd_pcm_format_t ofono_format = SND_PCM_FORMAT_S16_LE;
#else
snd_pcm_format_t ofono_format = SND_PCM_FORMAT_S16_BE;
#endif

/*!
 * \brief Initialize the ALSA soundcard channels (capture AND playback) used by one interface (a multichannel soundcard can be used by multiple interfaces)
 * \param p the ofono_pvt of the interface
 *
 * This function call alsa_open_dev to initialize the ALSA soundcard for each channel (capture AND playback) used by one interface (a multichannel soundcard can be used by multiple interfaces). Called by sound_init
 *
 * \return zero on success, -1 on error.
 */
int alsa_init(private_t * tech_pvt)
{
	tech_pvt->alsac = alsa_open_dev(tech_pvt, SND_PCM_STREAM_CAPTURE);
	if (!tech_pvt->alsac) {
		ERRORA("Failed opening ALSA capture device: %s\n", OFONO_P_LOG, tech_pvt->alsacname);
		if (alsa_shutdown(tech_pvt)) {
			ERRORA("alsa_shutdown failed\n", OFONO_P_LOG);
			return -1;
		}
		return -1;
	}
	tech_pvt->alsap = alsa_open_dev(tech_pvt, SND_PCM_STREAM_PLAYBACK);
	if (!tech_pvt->alsap) {
		ERRORA("Failed opening ALSA playback device: %s\n", OFONO_P_LOG, tech_pvt->alsapname);
		if (alsa_shutdown(tech_pvt)) {
			ERRORA("alsa_shutdown failed\n", OFONO_P_LOG);
			return -1;
		}
		return -1;
	}

	/* make valgrind very happy */
	snd_config_update_free_global();
	return 0;
}

/*!
 * \brief Shutdown the ALSA soundcard channels (input and output) used by one interface (a multichannel soundcard can be used by multiple interfaces)
 * \param p the ofono_pvt of the interface
 *
 * This function shutdown the ALSA soundcard channels (input and output) used by one interface (a multichannel soundcard can be used by multiple interfaces). Called by sound_init
 *
 * \return zero on success, -1 on error.
 */

int alsa_shutdown(private_t * tech_pvt)
{

	int err;

	if (tech_pvt->alsap) {
		err = snd_pcm_drop(tech_pvt->alsap);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_drop failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsapname, snd_strerror(err));
			return -1;
		}
		err = snd_pcm_hw_free(tech_pvt->alsap);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_hw_free failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsapname, snd_strerror(err));
			return -1;
		}
		err = snd_pcm_close(tech_pvt->alsap);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_close failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsapname, snd_strerror(err));
			return -1;
		}
		tech_pvt->alsap = NULL;
	}
	if (tech_pvt->alsac) {
		err = snd_pcm_drop(tech_pvt->alsac);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_drop failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsacname, snd_strerror(err));
			return -1;
		}
		err = snd_pcm_hw_free(tech_pvt->alsac);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_hw_free failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsacname, snd_strerror(err));
			return -1;
		}
		err = snd_pcm_close(tech_pvt->alsac);
		if (err < 0) {
			ERRORA("device [%s], snd_pcm_close failed with error '%s'\n", OFONO_P_LOG, tech_pvt->alsacname, snd_strerror(err));
			return -1;
		}
		tech_pvt->alsac = NULL;
	}

	return 0;
}

/*!
 * \brief Setup and open the ALSA device (capture OR playback)
 * \param p the ofono_pvt of the interface
 * \param stream the ALSA capture/playback definition
 *
 * This function setup and open the ALSA device (capture OR playback). Called by alsa_init
 *
 * \return zero on success, -1 on error.
 */
snd_pcm_t *alsa_open_dev(private_t * tech_pvt, snd_pcm_stream_t stream)
{

	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *params;
	snd_pcm_sw_params_t *swparams;
	snd_pcm_uframes_t buffer_size;
	int err;
	size_t n;
	//snd_pcm_uframes_t xfer_align;
	unsigned int rate;
	snd_pcm_uframes_t start_threshold, stop_threshold;
	snd_pcm_uframes_t period_size = 0;
	snd_pcm_uframes_t chunk_size = 0;
	int start_delay = 0;
	int stop_delay = 0;
	snd_pcm_state_t state;
	snd_pcm_info_t *info;
	unsigned int chan_num;

	period_size = tech_pvt->alsa_period_size;

	snd_pcm_hw_params_alloca(&params);
	snd_pcm_sw_params_alloca(&swparams);

	if (stream == SND_PCM_STREAM_CAPTURE) {
		err = snd_pcm_open(&handle, tech_pvt->alsacname, stream, 0 | SND_PCM_NONBLOCK);
	} else {
		err = snd_pcm_open(&handle, tech_pvt->alsapname, stream, 0 | SND_PCM_NONBLOCK);
	}
	if (err < 0) {
		ERRORA
		("snd_pcm_open failed with error '%s' on device '%s', if you are using a plughw:n device please change it to be a default:n device (so to allow it to be shared with other concurrent programs), or maybe you are using an ALSA voicemodem and slmodemd"
				" is running?\n", OFONO_P_LOG, snd_strerror(err), stream == SND_PCM_STREAM_CAPTURE ? tech_pvt->alsacname : tech_pvt->alsapname);
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		return NULL;
	}

	snd_pcm_info_alloca(&info);
	if ((err = snd_pcm_info(handle, info)) < 0) {
		ERRORA("info error: %s", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	err = snd_pcm_nonblock(handle, 1);
	if (err < 0) {
		ERRORA("nonblock setting error: %s", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	err = snd_pcm_hw_params_any(handle, params);
	if (err < 0) {
		ERRORA("Broken configuration for this PCM, no configurations available: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	err = snd_pcm_hw_params_set_access(handle, params, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0) {
		ERRORA("Access type not available: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
	err = snd_pcm_hw_params_set_format(handle, params, ofono_format);
	if (err < 0) {
		ERRORA("Sample format non available: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
	err = snd_pcm_hw_params_set_channels(handle, params, 1);
	if (err < 0) {
		DEBUGA_OFONO("Channels count set failed: %s\n", OFONO_P_LOG, snd_strerror(err));
	}
#if 1
	err = snd_pcm_hw_params_get_channels(params, &chan_num);
	if (err < 0) {
		ERRORA("Channels count non available: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
	if (chan_num < 1 || chan_num > 2) {
		ERRORA("Channels count MUST BE 1 or 2, it is: %d\n", OFONO_P_LOG, chan_num);
		ERRORA("Channels count MUST BE 1 or 2, it is: %d on %s %s\n", OFONO_P_LOG, chan_num, tech_pvt->alsapname, tech_pvt->alsacname);
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	} else {
		// OFONO ALWAYS MONO!
		if (chan_num == 1) {
			if (stream == SND_PCM_STREAM_CAPTURE)
			tech_pvt->alsa_capture_is_mono = 1;
			else
			tech_pvt->alsa_play_is_mono = 1;
		} else {
			if (stream == SND_PCM_STREAM_CAPTURE)
			tech_pvt->alsa_capture_is_mono = 0;
			else
			tech_pvt->alsa_play_is_mono = 0;
		}
	}
#else
	tech_pvt->alsa_capture_is_mono = 1;
	tech_pvt->alsa_play_is_mono = 1;
#endif

#if 1
	rate = tech_pvt->alsa_sound_rate;
	err = snd_pcm_hw_params_set_rate_near(handle, params, &rate, 0);
	if ((float) tech_pvt->alsa_sound_rate * 1.05 < rate || (float) tech_pvt->alsa_sound_rate * 0.95 > rate) {
		WARNINGA("Rate is not accurate (requested = %iHz, got = %iHz)\n", OFONO_P_LOG, tech_pvt->alsa_sound_rate, rate);
	}

	if (err < 0) {
		ERRORA("Error setting rate: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
	tech_pvt->alsa_sound_rate = rate;

	err = snd_pcm_hw_params_set_period_size_near(handle, params, &period_size, 0);

	if (err < 0) {
		ERRORA("Error setting period_size: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	tech_pvt->alsa_period_size = period_size;

	tech_pvt->alsa_buffer_size = tech_pvt->alsa_period_size * tech_pvt->alsa_periods_in_buffer;

	err = snd_pcm_hw_params_set_buffer_size_near(handle, params, &tech_pvt->alsa_buffer_size);

	if (err < 0) {
		ERRORA("Error setting buffer_size: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
#endif

	err = snd_pcm_hw_params(handle, params);
	if (err < 0) {
		ERRORA("Unable to install hw params: %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	snd_pcm_hw_params_get_period_size(params, &chunk_size, 0);
	snd_pcm_hw_params_get_buffer_size(params, &buffer_size);
	if (chunk_size == buffer_size) {
		ERRORA("Can't use period equal to buffer size (%lu == %lu)\n", OFONO_P_LOG, chunk_size, buffer_size);
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	snd_pcm_sw_params_current(handle, swparams);

	/*
	 if (sleep_min)
	 xfer_align = 1;
	 err = snd_pcm_sw_params_set_sleep_min(handle, swparams,
	 0);

	 if (err < 0) {
	 ERRORA("Error setting slep_min: %s\n", OFONO_P_LOG, snd_strerror(err));
	 }
	 */
	n = chunk_size;
	err = snd_pcm_sw_params_set_avail_min(handle, swparams, n);
	if (err < 0) {
		ERRORA("Error setting avail_min: %s\n", OFONO_P_LOG, snd_strerror(err));
	}
	if (stream == SND_PCM_STREAM_CAPTURE) {
		start_delay = 1;
	}
	if (start_delay <= 0) {
		start_threshold = n + (snd_pcm_uframes_t) rate *start_delay / 1000000;
	} else {
		start_threshold = (snd_pcm_uframes_t) rate *start_delay / 1000000;
	}
	if (start_threshold < 1)
	start_threshold = 1;
	if (start_threshold > n)
	start_threshold = n;
	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0) {
		ERRORA("Error setting start_threshold: %s\n", OFONO_P_LOG, snd_strerror(err));
	}

	if (stop_delay <= 0)
	stop_threshold = buffer_size + (snd_pcm_uframes_t) rate *stop_delay / 1000000;
	else
	stop_threshold = (snd_pcm_uframes_t) rate *stop_delay / 1000000;

	if (stream == SND_PCM_STREAM_CAPTURE) {
		stop_threshold = -1;
	}

	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);

	if (err < 0) {
		ERRORA("Error setting stop_threshold: %s\n", OFONO_P_LOG, snd_strerror(err));
	}

	if (snd_pcm_sw_params(handle, swparams) < 0) {
		ERRORA("Error installing software parameters: %s\n", OFONO_P_LOG, snd_strerror(err));
	}

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0) {
		ERRORA("Unable to get a poll descriptors count, error is %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	if (err != 1) { //number of poll descriptors
		DEBUGA_OFONO("Can't handle more than one device\n", OFONO_P_LOG);
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}

	err = snd_pcm_poll_descriptors(handle, &tech_pvt->pfd, err);
	if (err != 1) {
		ERRORA("snd_pcm_poll_descriptors failed, %s\n", OFONO_P_LOG, snd_strerror(err));
		snd_pcm_hw_params_free(params);
		snd_pcm_sw_params_free(swparams);
		snd_pcm_info_free(info);
		return NULL;
	}
	DEBUGA_OFONO("Acquired fd %d from the poll descriptor\n", OFONO_P_LOG, tech_pvt->pfd.fd);

	if (stream == SND_PCM_STREAM_CAPTURE) {
		tech_pvt->ofono_sound_capt_fd = tech_pvt->pfd.fd;
	}

	state = snd_pcm_state(handle);

	if (state != SND_PCM_STATE_RUNNING) {
		if (state != SND_PCM_STATE_PREPARED) {
			err = snd_pcm_prepare(handle);
			if (err) {
				ERRORA("snd_pcm_prepare failed, %s\n", OFONO_P_LOG, snd_strerror(err));
				snd_pcm_hw_params_free(params);
				snd_pcm_sw_params_free(swparams);
				snd_pcm_info_free(info);
				return NULL;
			}
			DEBUGA_OFONO("prepared!\n", OFONO_P_LOG);
		}
		if (stream == SND_PCM_STREAM_CAPTURE) {
			err = snd_pcm_start(handle);
			if (err) {
				ERRORA("snd_pcm_start failed, %s\n", OFONO_P_LOG, snd_strerror(err));
				snd_pcm_hw_params_free(params);
				snd_pcm_sw_params_free(swparams);
				snd_pcm_info_free(info);
				return NULL;
			}
			DEBUGA_OFONO("started!\n", OFONO_P_LOG);
		}
	}
	if (option_debug > 1) {
		snd_output_t *output = NULL;
		err = snd_output_stdio_attach(&output, stdout, 0);
		if (err < 0) {
			ERRORA("snd_output_stdio_attach failed: %s\n", OFONO_P_LOG, snd_strerror(err));
		}
		snd_pcm_dump(handle, output);
		snd_output_close(output);
	}

	//snd_pcm_info_free(info);

	if (option_debug > 1)
	DEBUGA_OFONO("ALSA handle = %ld\n", OFONO_P_LOG, (long int) handle);
	return handle;

}

/*! \brief Write audio frames to interface */
#endif /* OFONO_ALSA */

/* Creates a new outgoing voice call on the modem.
 * The function is called from "channel_outgoing_channel".
 */
int ofono_call(private_t * tech_pvt, const char *rdest, int timeout) {

	DBusMessage *msg;
	const char *hidecallerid = "default";
	DBusError err;

	dbus_error_init(&err);

	DEBUGA_OFONO("Calling GSM, rdest is: %s\n", OFONO_P_LOG, rdest);
	DEBUGA_OFONO("ENTERING FUNC\n", OFONO_P_LOG);
	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_CALLMANAGER_INTERFACE, "Dial");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &rdest,
			DBUS_TYPE_STRING, &hidecallerid, DBUS_TYPE_INVALID) == FALSE) {
		ERRORA("Dialing failed!\n", OFONO_P_LOG);
	}

	dbus_message_set_auto_start(msg, FALSE);

	dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg, -1,
			&err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(msg);
		ERRORA("D-BUS modem dialing failed\n", OFONO_P_LOG);
		return -EIO;
	}

	DEBUGA_OFONO("INFO: D-BUS modem dialing %s\n", OFONO_P_LOG, rdest);

	dbus_message_unref(msg);

	ofono_dbus_context_iter(tech_pvt);

	tech_pvt->phone_callflow = CALLFLOW_CALL_DIALING;
	tech_pvt->interface_state = OFONO_STATE_DIALING;

	if (option_debug) {
		DEBUGA_PBX("EXITING FUNC\n", OFONO_P_LOG);
	}

	ofono_dbus_context_iter(tech_pvt);
	return 0;
}

/* A D-BUS callback function for call state changes. */
static gboolean ofono_signal_call_state(DBusConnection *conn, DBusMessage *msg,
		void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, value;
	const char *property_name;
	const char *property_status = NULL;
	switch_core_session_t *session = NULL;

	DEBUGA_OFONO("INFO: D-BUS Ofono call state changed\n", OFONO_P_LOG);
	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	tech_pvt->dbus_call_path = g_strdup(dbus_message_get_path(msg));

	dbus_message_iter_get_basic(&iter, &property_name);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING)
		dbus_message_iter_get_basic(&value, &property_status);

	if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
		dbus_bool_t val;

		dbus_message_iter_get_basic(&value, &val);
		property_status = (val == TRUE) ? "yes" : "no";
	}

	DEBUGA_OFONO("updating call (%s) [ %s = %s ]\n", OFONO_P_LOG,
			tech_pvt->ofono_modem_name, property_name,
			property_status ? property_status : "...");

	if (g_str_equal(property_name, "State") == TRUE) {
		if (g_str_equal(property_status, "active")) {
			if (tech_pvt->phone_callflow == CALLFLOW_STATUS_RINGING
					&& tech_pvt->interface_state == OFONO_STATE_RINGING) {
				session = switch_core_session_locate(
						tech_pvt->session_uuid_str);
				if (session) {
					switch_core_session_rwunlock(session);
					switch_channel_mark_answered(
							switch_core_session_get_channel(session));
				}
			}
			tech_pvt->phone_callflow = CALLFLOW_CALL_ACTIVE;
			tech_pvt->interface_state = OFONO_STATE_UP;
		} else if (g_str_equal(property_status, "held")) {
			tech_pvt->phone_callflow = CALLFLOW_STATUS_REMOTEHOLD;
			tech_pvt->interface_state = OFONO_STATE_UP;
		} else if (g_str_equal(property_status, "dialing")) {
			tech_pvt->phone_callflow = CALLFLOW_CALL_DIALING;
			tech_pvt->interface_state = OFONO_STATE_DIALING;
		} else if (g_str_equal(property_status, "alerting")) {
			tech_pvt->phone_callflow = CALLFLOW_STATUS_RINGING;
			tech_pvt->interface_state = OFONO_STATE_RINGING;
		} else if (g_str_equal(property_status, "incoming")) {
			tech_pvt->phone_callflow = CALLFLOW_INCOMING_RING;
			tech_pvt->interface_state = OFONO_STATE_RING;
		} else if (g_str_equal(property_status, "waiting")) {
			/*tech_pvt->phone_callflow = CALLFLOW_CALL_LINEBUSY;
			 tech_pvt->interface_state = OFONO_STATE_DOWN;*/
			return TRUE;
		} else if (g_str_equal(property_status, "disconnected")) {
			session = switch_core_session_locate(tech_pvt->session_uuid_str);
			if (tech_pvt->phone_callflow == CALLFLOW_STATUS_RINGING) {
				tech_pvt->phone_callflow = CALLFLOW_CALL_LINEBUSY;
				tech_pvt->interface_state = OFONO_STATE_DOWN;
				if (session) {
					switch_core_session_rwunlock(session);
					switch_channel_hangup(
							switch_core_session_get_channel(session),
							SWITCH_CAUSE_CALL_REJECTED);
				}
			} else {
				if (session) {
					switch_core_session_rwunlock(session);
					switch_channel_hangup(
							switch_core_session_get_channel(session),
							SWITCH_CAUSE_NORMAL_CLEARING);
				}
			}
			tech_pvt->phone_callflow = CALLFLOW_CALL_DOWN;
			tech_pvt->interface_state = OFONO_STATE_DOWN;
		}
	}

	return TRUE;
}

/* A D-BUS callback function for new voice calls. */
static gboolean ofono_signal_call_added(DBusConnection *conn, DBusMessage *msg,
		void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, dict;
	const char *path;

	DEBUGA_OFONO("INFO: D-BUS Ofono call added\n", OFONO_P_LOG);
	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &path);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &dict);

	DEBUGA_OFONO("INFO: D-BUS Ofono call path: \"%s\"\n", OFONO_P_LOG, path);
	tech_pvt->dbus_call_path = g_strdup(path);

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *property_name;
		const char *property_status = NULL;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &property_name);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&value, &property_status);

		if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
			dbus_bool_t val;

			dbus_message_iter_get_basic(&value, &val);
			property_status = (val == TRUE) ? "yes" : "no";
		}

		DEBUGA_OFONO("updating call (%s) [ %s = %s ]\n", OFONO_P_LOG,
				tech_pvt->dbus_call_path, property_name,
				property_status ? property_status : "...");

		if (g_str_equal(property_name, "LineIdentification") == TRUE) {
			tech_pvt->callid_number = g_strdup(property_status);
		}

		dbus_message_iter_next(&dict);
	}

	tech_pvt->callid_name = NULL;
	tech_pvt->phone_callflow = CALLFLOW_INCOMING_RING;
	tech_pvt->interface_state = OFONO_STATE_RING;

	return TRUE;
}

/* A D-BUS callback function for removed voice calls. */
static gboolean ofono_signal_call_removed(DBusConnection *conn,
		DBusMessage *msg, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	tech_pvt->phone_callflow = CALLFLOW_CALL_IDLE;
	tech_pvt->interface_state = OFONO_STATE_IDLE;
	g_free((gpointer) tech_pvt->dbus_call_path);
	g_free(tech_pvt->callid_number);
	g_free(tech_pvt->sms_body);
	g_free(tech_pvt->sms_sender);
	g_free(tech_pvt->sms_date);
	tech_pvt->dbus_call_path = NULL;
	tech_pvt->callid_number = NULL;
	tech_pvt->sms_body = NULL;
	tech_pvt->sms_sender = NULL;
	tech_pvt->sms_date = NULL;
	DEBUGA_OFONO("INFO: D-BUS Ofono call removed\n", OFONO_P_LOG);
	return TRUE;
}

/* A D-BUS callback function for incoming SMS messages. */
static gboolean ofono_signal_receivesms(DBusConnection *conn, DBusMessage *msg,
		void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, dict;
	const char *sms_string;

	DEBUGA_OFONO("INFO: D-BUS Ofono incoming SMS\n", OFONO_P_LOG);
	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &sms_string);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &dict);

	tech_pvt->sms_body = g_strdup(sms_string);

	dbus_message_iter_recurse(&iter, &dict);

	while (dbus_message_iter_get_arg_type(&dict) == DBUS_TYPE_DICT_ENTRY) {
		DBusMessageIter entry, value;
		const char *property_name;
		const char *property_status = NULL;

		dbus_message_iter_recurse(&dict, &entry);
		dbus_message_iter_get_basic(&entry, &property_name);

		dbus_message_iter_next(&entry);
		dbus_message_iter_recurse(&entry, &value);

		if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_STRING)
			dbus_message_iter_get_basic(&value, &property_status);

		if (dbus_message_iter_get_arg_type(&value) == DBUS_TYPE_BOOLEAN) {
			dbus_bool_t val;

			dbus_message_iter_get_basic(&value, &val);
			property_status = (val == TRUE) ? "yes" : "no";
		}

		DEBUGA_OFONO("updating sms (%s) [ %s = %s ]\n", OFONO_P_LOG,
				tech_pvt->ofono_modem_name, property_name,
				property_status ? property_status : "...");

		if (g_str_equal(property_name, "Sender")) {
			tech_pvt->sms_sender = g_strdup(property_status);
		} else if (g_str_equal(property_name, "LocalSentTime")) {
			tech_pvt->sms_date = g_strdup(property_status);
		}

		dbus_message_iter_next(&dict);
	}

	sms_incoming(tech_pvt);

	return TRUE;
}

/* A D-BUS callback function, which gets called if a modem is removed from Ofono */
static gboolean ofono_signal_modem_removed(DBusConnection *conn,
		DBusMessage *msg, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter;
	const char *path;

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &path);

	if (g_str_equal(tech_pvt->ofono_modem_name, path) == TRUE) {
		ERRORA("Ofono modem %s removed!\n", OFONO_P_LOG,
				tech_pvt->ofono_modem_name);
		tech_pvt->dbus_modem_attached = 0;
	}

	return TRUE;
}

/* A D-BUS callback function for modem state changes. */
static gboolean ofono_signal_modem_state(DBusConnection *conn, DBusMessage *msg,
		void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, value;
	const char *key;
	dbus_bool_t status;

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &key);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (g_str_equal(key, "Powered") == TRUE) {
		dbus_message_iter_get_basic(&value, &status);
		if (status == TRUE) {
			DEBUGA_OFONO("INFO: D-BUS Ofono modem state powered\n",
					OFONO_P_LOG);
			tech_pvt->dbus_modem_state_powered = 1;
		} else {
			DEBUGA_OFONO("INFO: D-BUS Ofono modem state not powered\n",
					OFONO_P_LOG);
			tech_pvt->dbus_modem_state_powered = 0;
		}
	}

	if (g_str_equal(key, "Online") == TRUE) {
		dbus_message_iter_get_basic(&value, &status);
		if (status == TRUE) {
			DEBUGA_OFONO("INFO: D-BUS Ofono modem state Online\n", OFONO_P_LOG);
			tech_pvt->dbus_modem_state_online = 1;
		} else {
			DEBUGA_OFONO("INFO: D-BUS Ofono modem state Offline\n",
					OFONO_P_LOG);
			tech_pvt->dbus_modem_state_online = 0;
		}
	}

	return TRUE;
}

/* A D-BUS callback function for changes in the mobile network connectivity of the modem. */
static gboolean ofono_signal_modem_network_state(DBusConnection *conn,
		DBusMessage *msg, void *user_data) {

	private_t *tech_pvt = (private_t *) user_data;
	DBusMessageIter iter, value;
	const char *key;

	if (dbus_message_iter_init(msg, &iter) == FALSE)
		return TRUE;

	dbus_message_iter_get_basic(&iter, &key);

	dbus_message_iter_next(&iter);
	dbus_message_iter_recurse(&iter, &value);

	if (g_str_equal(key, "Strength") == TRUE) {
		unsigned char status;
		dbus_message_iter_get_basic(&value, &status);
		if ((int) status <= 15) {
			WARNINGA("WARNING: Current signal strength = %d%%\n", OFONO_P_LOG,
					(int) status);
		}
		if (tech_pvt->ofono_modem_signal_strength != -1
				&& tech_pvt->ofono_modem_signal_strength <= 15) {
			DEBUGA_OFONO("INFO: Current signal strength = %d%%\n", OFONO_P_LOG,
					(int) status);
		}
		tech_pvt->ofono_modem_signal_strength = (int) status;
	}

	if (g_str_equal(key, "Name") == TRUE) {
		const char *status;
		dbus_message_iter_get_basic(&value, &status);
		if (tech_pvt->ofono_modem_operator_name == NULL
				|| g_str_equal(tech_pvt->ofono_modem_operator_name, status)
						== FALSE) {
			ofono_get_network_info(tech_pvt);
		}
		tech_pvt->ofono_modem_operator_name = g_strdup(status);
	}

	return TRUE;
}

/* Sends a DTMF signal using the internal commands of the modem.
 * This function is called from "channel_send_dtmf".
 */
int ofono_senddigit(private_t * tech_pvt, const char *digit) {

	DBusMessage *msg;
	DBusError err;

	dbus_error_init(&err);

	DEBUGA_OFONO("DIGIT received: \"%s\"\n", OFONO_P_LOG, digit);
	DEBUGA_OFONO("ENTERING FUNC\n", OFONO_P_LOG);

	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_CALLMANAGER_INTERFACE,
			"SendTones");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &digit,
			DBUS_TYPE_INVALID) == FALSE) {
		ERRORA("D-BUS modem DTMF sending failed. Couldn't append args.\n",
				OFONO_P_LOG);
	}

	dbus_message_set_auto_start(msg, FALSE);

	dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg, -1,
			&err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(msg);
		ERRORA(
				"D-BUS modem DTMF sending failed. Couldn't send the msg or didn't get a reply\n",
				OFONO_P_LOG);
		return -EIO;
	}

	DEBUGA_OFONO("INFO: D-BUS modem DTMF sent\n", OFONO_P_LOG);

	dbus_message_unref(msg);

	ofono_dbus_context_iter(tech_pvt);

	if (option_debug) {
		DEBUGA_PBX("EXITING FUNC\n", OFONO_P_LOG);
	}

	return 0;
}

/* Sends an SMS message. This function is called from "chat_send" */
int ofono_sendsms(private_t * tech_pvt, char *dest, char *text) {

	DBusMessage *msg;
	DBusError err;

	dbus_error_init(&err);

	DEBUGA_OFONO("OfonoSendsms: dest=%s text=%s\n", OFONO_P_LOG, dest, text);
	DEBUGA_OFONO("ENTERING FUNC\n", OFONO_P_LOG);

	msg = dbus_message_new_method_call(OFONO_SERVICE,
			tech_pvt->ofono_modem_name, OFONO_MESSAGEMANAGER_INTERFACE,
			"SendMessage");
	if (msg == NULL)
		return -ENOMEM;

	if (dbus_message_append_args(msg, DBUS_TYPE_STRING, &dest, DBUS_TYPE_STRING,
			&text, DBUS_TYPE_INVALID) == FALSE) {
		ERRORA("D-BUS modem SMS sending failed. Couldn't append args.\n",
				OFONO_P_LOG);
	}

	dbus_message_set_auto_start(msg, FALSE);

	dbus_connection_send_with_reply_and_block(tech_pvt->dbus_conn, msg, -1,
			&err);
	if (dbus_error_is_set(&err)) {
		ERRORA("%s: %s\n", OFONO_P_LOG, err.name, err.message);
		dbus_error_free(&err);
		dbus_message_unref(msg);
		ERRORA(
				"D-BUS modem SMS sending failed. Couldn't send the msg or didn't get a reply.\n",
				OFONO_P_LOG);
		return -EIO;
	}

	DEBUGA_OFONO("INFO: D-BUS modem SMS sent\n", OFONO_P_LOG);

	dbus_message_unref(msg);

	ofono_dbus_context_iter(tech_pvt);

	if (option_debug) {
		DEBUGA_PBX("EXITING FUNC\n", OFONO_P_LOG);
	}

	return 0;

}

#ifdef OFONO_ALSA
/*! \brief Write audio frames to interface */
int alsa_write(private_t * tech_pvt, short *data, int datalen)
{
	static char sizbuf[8000];
	static char sizbuf2[16000];
	static char silencebuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int res = 0;
	time_t now_timestamp;
	/* size_t frames = 0; */
	snd_pcm_state_t state;
	snd_pcm_sframes_t delayp1=0;
	snd_pcm_sframes_t delayp2=0;

	if(tech_pvt->no_sound==1) {
		return res;
	}

	memset(sizbuf, 255, sizeof(sizbuf));
	memset(sizbuf2, 255, sizeof(sizbuf));
	memset(silencebuf, 255, sizeof(sizbuf));

	//ERRORA("data=%p, datalen=%d\n", OFONO_P_LOG, (void *)data, datalen);
	/* We have to digest the frame in 160-byte portions */
	if (datalen > sizeof(sizbuf) - sizpos) {
		ERRORA("Frame too large\n", OFONO_P_LOG);
		res = -1;
	} else {
		memcpy(sizbuf + sizpos, data, datalen);
		memset(data, 255, datalen);
		len += datalen;

#ifdef ALSA_MONITOR
		alsa_monitor_write(sizbuf, len);
#endif
		state = snd_pcm_state(tech_pvt->alsap);
		if (state == SND_PCM_STATE_XRUN) {
			int i;

			DEBUGA_OFONO
			("You've got an ALSA write XRUN in the past (ofono can't fill the soundcard buffer fast enough). If this happens often (not after silence or after a pause in the speech, that's OK), and appear to damage the sound quality, first check if you have some IRQ problem, maybe sharing the soundcard IRQ with a broken or heavy loaded ethernet or graphic card. Then consider to increase the alsa_periods_in_buffer (now is set to %d) for this interface in the config file\n",
					OFONO_P_LOG, tech_pvt->alsa_periods_in_buffer);
			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				ERRORA("audio play prepare failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			} else {
				res = snd_pcm_format_set_silence(ofono_format, silencebuf, len / 2);
				if (res < 0) {
					DEBUGA_OFONO("Silence error %s\n", OFONO_P_LOG, snd_strerror(res));
					res = -1;
				}
				for (i = 0; i < (tech_pvt->alsa_periods_in_buffer - 1); i++) {
					res = snd_pcm_writei(tech_pvt->alsap, silencebuf, len / 2);
					if (res != len / 2) {
						DEBUGA_OFONO("Write returned a different quantity: %d\n", OFONO_P_LOG, res);
						res = -1;
					} else if (res < 0) {
						DEBUGA_OFONO("Write error %s\n", OFONO_P_LOG, snd_strerror(res));
						res = -1;
					}
				}
			}

		}

		res = snd_pcm_delay(tech_pvt->alsap, &delayp1);
		if (res < 0) {
			DEBUGA_OFONO("Error %d on snd_pcm_delay: \"%s\"\n", OFONO_P_LOG, res, snd_strerror(res));
			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				DEBUGA_OFONO("snd_pcm_prepare failed: '%s'\n", OFONO_P_LOG, snd_strerror(res));
			}
			res = snd_pcm_delay(tech_pvt->alsap, &delayp1);
		}

		delayp2 = snd_pcm_avail_update(tech_pvt->alsap);
		if (delayp2 < 0) {
			DEBUGA_OFONO("Error %d on snd_pcm_avail_update: \"%s\"\n", OFONO_P_LOG, (int) delayp2, snd_strerror(delayp2));

			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				DEBUGA_OFONO("snd_pcm_prepare failed: '%s'\n", OFONO_P_LOG, snd_strerror(res));
			}
			delayp2 = snd_pcm_avail_update(tech_pvt->alsap);
		}

		if ( /* delayp1 != 0 && delayp1 != 160 */
				delayp1 < 160 || delayp2 > tech_pvt->alsa_buffer_size) {

			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				DEBUGA_OFONO
				("snd_pcm_prepare failed while trying to prevent an ALSA write XRUN: %s, delayp1=%d, delayp2=%d\n",
						OFONO_P_LOG, snd_strerror(res), (int) delayp1, (int) delayp2);
			} else {

				int i;
				for (i = 0; i < (tech_pvt->alsa_periods_in_buffer - 1); i++) {
					res = snd_pcm_format_set_silence(ofono_format, silencebuf, len / 2);
					if (res < 0) {
						DEBUGA_OFONO("Silence error %s\n", OFONO_P_LOG, snd_strerror(res));
						res = -1;
					}
					res = snd_pcm_writei(tech_pvt->alsap, silencebuf, len / 2);
					if (res < 0) {
						DEBUGA_OFONO("Write error %s\n", OFONO_P_LOG, snd_strerror(res));
						res = -1;
					} else if (res != len / 2) {
						DEBUGA_OFONO("Write returned a different quantity: %d\n", OFONO_P_LOG, res);
						res = -1;
					}
				}

				DEBUGA_OFONO
				("PREVENTING an ALSA write XRUN (ofono can't fill the soundcard buffer fast enough). If this happens often (not after silence or after a pause in the speech, that's OK), and appear to damage the sound quality, first check if you have some IRQ problem, maybe sharing the soundcard IRQ with a broken or heavy loaded ethernet or graphic card. Then consider to increase the alsa_periods_in_buffer (now is set to %d) for this interface in the config file. delayp1=%d, delayp2=%d\n",
						OFONO_P_LOG, tech_pvt->alsa_periods_in_buffer, (int) delayp1, (int) delayp2);
			}

		}

		memset(sizbuf2, 0, sizeof(sizbuf2));
		if (tech_pvt->alsa_play_is_mono) {
			res = snd_pcm_writei(tech_pvt->alsap, sizbuf, len / 2);
		} else {
			int a = 0;
			int i = 0;
			for (i = 0; i < 8000;) {
				sizbuf2[a] = sizbuf[i];
				a++;
				i++;
				sizbuf2[a] = sizbuf[i];
				a++;
				i--;
				sizbuf2[a] = sizbuf[i]; // comment out this line to use only left
				a++;
				i++;
				sizbuf2[a] = sizbuf[i];// comment out this line to use only left
				a++;
				i++;
			}
			res = snd_pcm_writei(tech_pvt->alsap, sizbuf2, len);
		}
		if (res == -EPIPE) {
			DEBUGA_OFONO
			("ALSA write EPIPE (XRUN) (ofono can't fill the soundcard buffer fast enough). If this happens often (not after silence or after a pause in the speech, that's OK), and appear to damage the sound quality, first check if you have some IRQ problem, maybe sharing the soundcard IRQ with a broken or heavy loaded ethernet or graphic card. Then consider to increase the alsa_periods_in_buffer (now is set to %d) for this interface in the config file. delayp1=%d, delayp2=%d\n",
					OFONO_P_LOG, tech_pvt->alsa_periods_in_buffer, (int) delayp1, (int) delayp2);
			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				ERRORA("audio play prepare failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			} else {

				if (tech_pvt->alsa_play_is_mono) {
					res = snd_pcm_writei(tech_pvt->alsap, sizbuf, len / 2);
				} else {
					int a = 0;
					int i = 0;
					for (i = 0; i < 8000;) {
						sizbuf2[a] = sizbuf[i];
						a++;
						i++;
						sizbuf2[a] = sizbuf[i];
						a++;
						i--;
						sizbuf2[a] = sizbuf[i];
						a++;
						i++;
						sizbuf2[a] = sizbuf[i];
						a++;
						i++;
					}
					res = snd_pcm_writei(tech_pvt->alsap, sizbuf2, len);
				}

			}

		} else {
			if (res == -ESTRPIPE) {
				ERRORA("You've got some big problems\n", OFONO_P_LOG);
			} else if (res == -EAGAIN) {
				DEBUGA_OFONO("Momentarily busy\n", OFONO_P_LOG);
				res = 0;
			} else if (res < 0) {
				ERRORA("Error %d on audio write: \"%s\"\n", OFONO_P_LOG, res, snd_strerror(res));
			}
		}
	}

	if (tech_pvt->audio_play_reset_period) {
		time(&now_timestamp);
		if ((now_timestamp - tech_pvt->audio_play_reset_timestamp) > tech_pvt->audio_play_reset_period) {
			if (option_debug)
			DEBUGA_OFONO("reset audio play\n", OFONO_P_LOG);
			res = snd_pcm_wait(tech_pvt->alsap, 1000);
			if (res < 0) {
				ERRORA("audio play wait failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			}
			res = snd_pcm_drop(tech_pvt->alsap);
			if (res) {
				ERRORA("audio play drop failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			}
			res = snd_pcm_prepare(tech_pvt->alsap);
			if (res) {
				ERRORA("audio play prepare failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			}
			res = snd_pcm_wait(tech_pvt->alsap, 1000);
			if (res < 0) {
				ERRORA("audio play wait failed: %s\n", OFONO_P_LOG, snd_strerror(res));
			}
			time(&tech_pvt->audio_play_reset_timestamp);
		}
	}
	//res = 0;
	//if (res > 0)
	//res = 0;
	return res;
}

#define AST_FRIENDLY_OFFSET 0
int alsa_read(private_t * tech_pvt, short *data, int datalen)
{
	//static struct ast_frame f;
	static short __buf[OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2];
	static short __buf2[(OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2) * 2];
	short *buf;
	short *buf2;
	static int readpos = 0;
	//static int left = OFONO_FRAME_SIZE;
	static int left;
	snd_pcm_state_t state;
	int r = 0;
	int off = 0;
	int error = 0;
	//time_t now_timestamp;

	//DEBUGA_OFONO("buf=%p, datalen=%d, left=%d\n", OFONO_P_LOG, (void *)buf, datalen, left);
	//memset(&f, 0, sizeof(struct ast_frame)); //giova

	if(tech_pvt->no_sound==1) {
		return r;
	}

	left = datalen;

	state = snd_pcm_state(tech_pvt->alsac);
	if (state != SND_PCM_STATE_RUNNING) {
		DEBUGA_OFONO("ALSA read state is not SND_PCM_STATE_RUNNING\n", OFONO_P_LOG);

		if (state != SND_PCM_STATE_PREPARED) {
			error = snd_pcm_prepare(tech_pvt->alsac);
			if (error) {
				ERRORA("snd_pcm_prepare failed, %s\n", OFONO_P_LOG, snd_strerror(error));
				return r;
			}
			DEBUGA_OFONO("prepared!\n", OFONO_P_LOG);
		}
		ofono_sleep(1000);
		error = snd_pcm_start(tech_pvt->alsac);
		if (error) {
			ERRORA("snd_pcm_start failed, %s\n", OFONO_P_LOG, snd_strerror(error));
			return r;
		}
		DEBUGA_OFONO("started!\n", OFONO_P_LOG);
		ofono_sleep(1000);
	}

	buf = __buf + AST_FRIENDLY_OFFSET / 2;
	buf2 = __buf2 + ((AST_FRIENDLY_OFFSET / 2) * 2);

	if (tech_pvt->alsa_capture_is_mono) {
		r = snd_pcm_readi(tech_pvt->alsac, buf + readpos, left);
		//DEBUGA_OFONO("r=%d, buf=%p, buf+readpos=%p, datalen=%d, left=%d\n", OFONO_P_LOG, r, (void *)buf, (void *)(buf + readpos), datalen, left);
	} else {
		int a = 0;
		int i = 0;
		r = snd_pcm_readi(tech_pvt->alsac, buf2 + (readpos * 2), left);

		for (i = 0; i < (OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2) * 2;) {
			__buf[a] = (__buf2[i] + __buf2[i + 1]) / 2; //comment out this line to use only left
			//__buf[a] = __buf2[i]; // enable this line to use only left
			a++;
			i++;
			i++;
		}
	}

	if (r == -EPIPE) {
		DEBUGA_OFONO("ALSA XRUN on read\n", OFONO_P_LOG);
		return r;
	} else if (r == -ESTRPIPE) {
		ERRORA("-ESTRPIPE\n", OFONO_P_LOG);
		return r;

	} else if (r == -EAGAIN) {
		int count=0;
		while (r == -EAGAIN) {
			ofono_sleep(10000);
			count++;
			DEBUGA_OFONO("%d ALSA read -EAGAIN, the soundcard is not ready to be read by ofono\n", OFONO_P_LOG, count);
			if (tech_pvt->alsa_capture_is_mono) {
				r = snd_pcm_readi(tech_pvt->alsac, buf + readpos, left);
			} else {
				int a = 0;
				int i = 0;
				r = snd_pcm_readi(tech_pvt->alsac, buf2 + (readpos * 2), left);

				for (i = 0; i < (OFONO_FRAME_SIZE + AST_FRIENDLY_OFFSET / 2) * 2;) {
					__buf[a] = (__buf2[i] + __buf2[i + 1]) / 2;
					a++;
					i++;
					i++;
				}
			}

		}
	} else if (r < 0) {
		WARNINGA("ALSA Read error: %s\n", OFONO_P_LOG, snd_strerror(r));
	} else if (r >= 0) {
		//DEBUGA_OFONO("read: r=%d, readpos=%d, left=%d, off=%d\n", OFONO_P_LOG, r, readpos, left, off);
		off -= r;//what is the meaning of this? a leftover, probably
	}
	/* Update positions */
	readpos += r;
	left -= r;

	if (readpos >= OFONO_FRAME_SIZE) {
		int i;
		/* A real frame */
		readpos = 0;
		left = OFONO_FRAME_SIZE;
		for (i = 0; i < r; i++)
		data[i] = buf[i];

	}
	return r;
}

#endif // OFONO_ALSA
/************************************************/

/* LUIGI RIZZO's magic */
/* boost support. BOOST_SCALE * 10 ^(BOOST_MAX/20) must
 * be representable in 16 bits to avoid overflows.
 */
#define BOOST_SCALE     (1<<9)
#define BOOST_MAX       40		/* slightly less than 7 bits */

/*
 * store the boost factor
 */
void ofono_store_boost(char *s, double *boost) {
	private_t *tech_pvt = NULL;

	if (sscanf(s, "%lf", boost) != 1) {
		ERRORA("invalid boost <%s>\n", OFONO_P_LOG, s);
		return;
	}
	if (*boost < -BOOST_MAX) {
		WARNINGA("boost %s too small, using %d\n", OFONO_P_LOG, s, -BOOST_MAX);
		*boost = -BOOST_MAX;
	} else if (*boost > BOOST_MAX) {
		WARNINGA("boost %s too large, using %d\n", OFONO_P_LOG, s, BOOST_MAX);
		*boost = BOOST_MAX;
	}
#ifdef WIN32
	*boost = exp(log ((double)10) * *boost / 20) * BOOST_SCALE;
#else
	*boost = exp(log(10) * *boost / 20) * BOOST_SCALE;
#endif //WIN32
	if (option_debug > 1)
		DEBUGA_OFONO("setting boost %s to %f\n", OFONO_P_LOG, s, *boost);
}

int ofono_sound_boost(void *data, int samples_num, double boost) {
	/* LUIGI RIZZO's magic */
	if (boost != 0 && (boost < 511 || boost > 513)) { /* scale and clip values */
		int i, x;

		int16_t *ptr = (int16_t *) data;

		for (i = 0; i < samples_num; i++) {
			x = (int) (ptr[i] * boost) / BOOST_SCALE;
			if (x > 32767) {
				x = 32767;
			} else if (x < -32768) {
				x = -32768;
			}
			ptr[i] = x;
		}
	} else {
		//printf("BOOST=%f\n", boost);
	}

	return 0;
}

