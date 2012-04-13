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
 * ofono.h -- Ofono compatible Endpoint Module
 *
 */

#define __STDC_LIMIT_MACROS

#ifdef WIN32
#define HAVE_VSNPRINTF
#pragma warning(disable: 4290)
#endif //WIN32
#define MY_EVENT_DUMP "ofono::dump_event"
#define MY_EVENT_ALARM "ofono::alarm"

#define ALARM_FAILED_INTERFACE              0
#define ALARM_NO_NETWORK_REGISTRATION       1
#define ALARM_ROAMING_NETWORK_REGISTRATION  2
#define ALARM_NETWORK_NO_SERVICE            3
#define ALARM_NETWORK_NO_SIGNAL             4
#define ALARM_NETWORK_LOW_SIGNAL            5
/*
 #undef GIOVA48

 #ifndef GIOVA48
 #define SAMPLES_PER_FRAME   160
 #else // GIOVA48
 #define SAMPLES_PER_FRAME   960
 #endif // GIOVA48
 #ifndef GIOVA48
 #define OFONO_FRAME_SIZE    160
 #else //GIOVA48
 #define OFONO_FRAME_SIZE    960
 #endif //GIOVA48
 #define SAMPLERATE_OFONO    8000
 */

#define SAMPLES_PER_FRAME	160
#define OFONO_FRAME_SIZE	160
#define SAMPLERATE_OFONO	8000

#ifndef NO_ALSA
#define OFONO_ALSA
#endif // NO_ALSA
#include <switch.h>
#include <switch_version.h>
#ifndef WIN32
#include <termios.h>
#include <sys/ioctl.h>
#include <iconv.h>
#endif //WIN32
#include <signal.h>
#include <glib.h>
#include "gdbus/gdbus.h"
//#include <libteletone.h>

#ifdef OFONO_ALSA
#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>
#endif /* OFONO_ALSA */

#ifdef OFONO_PORTAUDIO
#include "pablio.h"
#undef WANT_SPEEX
#ifdef WANT_SPEEX
#include "speex/speex_preprocess.h"
#include "speex/speex_echo.h"
#endif                          /* WANT_SPEEX */
#endif// OFONO_PORTAUDIO
//#include "celliax_spandsp.h"
#ifndef WIN32
#include <sys/time.h>
//#include <X11/Xlib.h>
//#include <X11/Xlibint.h>
//#include <X11/Xatom.h>
#endif //WIN32
#define SPANDSP_EXPOSE_INTERNAL_STRUCTURES
#include <spandsp.h>
#include <spandsp/version.h>

#ifdef _MSC_VER
//Windows macro  for FD_SET includes a warning C4127: conditional expression is constant
#pragma warning(push)
#pragma warning(disable:4127)
#endif

//#define SAMPLERATE_OFONO 16000
//#define SAMPLES_PER_FRAME SAMPLERATE_OFONO/50

#ifndef OFONO_SVN_VERSION
#define OFONO_SVN_VERSION SWITCH_VERSION_REVISION
#endif /* OFONO_SVN_VERSION */

typedef enum {
	TFLAG_IO = (1 << 0),
	TFLAG_INBOUND = (1 << 1),
	TFLAG_OUTBOUND = (1 << 2),
	TFLAG_DTMF = (1 << 3),
	TFLAG_VOICE = (1 << 4),
	TFLAG_HANGUP = (1 << 5),
	TFLAG_LINEAR = (1 << 6),
	TFLAG_CODEC = (1 << 7),
	TFLAG_BREAK = (1 << 8)
} TFLAGS;

typedef enum {
	GFLAG_MY_CODEC_PREFS = (1 << 0)
} GFLAGS;

#define DEBUGA_OFONO(...) switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "rev "OFONO_SVN_VERSION "[%p|%-7lx][DEBUG_OFONO  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );
#define DEBUGA_CALL(...)  switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "rev "OFONO_SVN_VERSION "[%p|%-7lx][DEBUG_CALL  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );
#define DEBUGA_PBX(...)   switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_DEBUG, "rev "OFONO_SVN_VERSION "[%p|%-7lx][DEBUG_PBX  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );
#define ERRORA(...)       switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "rev "OFONO_SVN_VERSION "[%p|%-7lx][ERRORA  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );
#define WARNINGA(...)     switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_WARNING, "rev "OFONO_SVN_VERSION "[%p|%-7lx][WARNINGA  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );
#define NOTICA(...)       switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "rev "OFONO_SVN_VERSION "[%p|%-7lx][NOTICA  %-5d][%-10s][%2d,%2d,%2d] " __VA_ARGS__ );

#define OFONO_P_LOG (void *)NULL, (unsigned long)55, __LINE__, tech_pvt ? tech_pvt->name ? tech_pvt->name : "none" : "none", -1, tech_pvt ? tech_pvt->interface_state : -1, tech_pvt ? tech_pvt->phone_callflow : -1

/*********************************/
#define OFONO_CAUSE_NORMAL                  1
#define OFONO_CAUSE_FAILURE                 2
#define OFONO_CAUSE_NO_ANSWER               3
/*********************************/
#define OFONO_FRAME_DTMF                    1
/*********************************/
#define OFONO_CONTROL_RINGING               1
#define OFONO_CONTROL_ANSWER                2
#define OFONO_CONTROL_HANGUP                3
#define OFONO_CONTROL_BUSY                  4

/*********************************/
#define	OFONO_STATE_IDLE                    0
#define	OFONO_STATE_DOWN                    1
#define	OFONO_STATE_RING                    2
#define	OFONO_STATE_DIALING                 3
#define	OFONO_STATE_BUSY                    4
#define	OFONO_STATE_UP                      5
#define	OFONO_STATE_RINGING                 6
#define	OFONO_STATE_PRERING                 7
#define	OFONO_STATE_ERROR_DOUBLE_CALL       8
#define	OFONO_STATE_SELECTED                9
#define OFONO_STATE_HANGUP_REQUESTED        10
#define	OFONO_STATE_PREANSWER               11
/*********************************/
/* call flow from the device */
#define CALLFLOW_CALL_IDLE                  0
#define CALLFLOW_CALL_DOWN                  1
#define CALLFLOW_INCOMING_RING              2
#define CALLFLOW_CALL_DIALING               3
#define CALLFLOW_CALL_LINEBUSY              4
#define CALLFLOW_CALL_ACTIVE                5
#define CALLFLOW_INCOMING_HANGUP            6
#define CALLFLOW_CALL_RELEASED              7
#define CALLFLOW_CALL_NOCARRIER             8
#define CALLFLOW_CALL_INFLUX                9
#define CALLFLOW_CALL_INCOMING              10
#define CALLFLOW_CALL_FAILED                11
#define CALLFLOW_CALL_NOSERVICE             12
#define CALLFLOW_CALL_OUTGOINGRESTRICTED    13
#define CALLFLOW_CALL_SECURITYFAIL          14
#define CALLFLOW_CALL_NOANSWER              15
#define CALLFLOW_STATUS_FINISHED            16
#define CALLFLOW_STATUS_CANCELLED           17
#define CALLFLOW_STATUS_FAILED              18
#define CALLFLOW_STATUS_REFUSED             19
#define CALLFLOW_STATUS_RINGING             20
#define CALLFLOW_STATUS_INPROGRESS          21
#define CALLFLOW_STATUS_UNPLACED            22
#define CALLFLOW_STATUS_ROUTING             23
#define CALLFLOW_STATUS_EARLYMEDIA          24
#define CALLFLOW_INCOMING_CALLID            25
#define CALLFLOW_STATUS_REMOTEHOLD          26
#define CALLFLOW_CALL_REMOTEANSWER          27
#define CALLFLOW_CALL_HANGUP_REQUESTED      28

/*********************************/

#define OFONO_MAX_INTERFACES 64

struct private_object {
	unsigned int flags;
	switch_codec_t read_codec;
	switch_codec_t write_codec;
	switch_frame_t read_frame;
	unsigned char databuf[SWITCH_RECOMMENDED_BUFFER_SIZE];
	char session_uuid_str[SWITCH_UUID_FORMATTED_LENGTH + 1];
	switch_caller_profile_t *caller_profile;
	switch_mutex_t *mutex;
	switch_mutex_t *flag_mutex;

	char id[80];
	char name[80];
	char dialplan[80];
	char context[80];
	char dial_regex[256];
	char fail_dial_regex[256];
	char hold_music[256];
	char type[256];
	char X11_display[256];
#ifdef WIN32
	unsigned short tcp_cli_port;
	unsigned short tcp_srv_port;
#else
	int tcp_cli_port;
	int tcp_srv_port;
#endif

	int interface_state; /*!< \brief 'state' of the interface (channel) */
	char language[80]; /*!< \brief default Asterisk dialplan language for this interface */
	char exten[80]; /*!< \brief default Asterisk dialplan extension for this interface */
	int alsa_sound_rate; /*!< \brief rate of the sound device, in Hz, eg: 8000 */
	char *callid_name;
	char *callid_number;
	double playback_boost;
	double capture_boost;
	int stripmsd;
	char ofono_call_id[512];
	int ofono_call_ongoing;
	char ofono_friends[4096];
	char ofono_fullname[512];
	char ofono_displayname[512];
	int phone_callflow; /*!< \brief 'callflow' of the ofono interface (as opposed to phone interface) */
	int ofono; /*!< \brief config flag, bool, OFONOGSM support on this interface (0 if false, -1 if true) */
	int control_to_send;
#ifdef WIN32
	switch_file_t *audiopipe[2];
	switch_file_t *audioofonopipe[2];
	switch_file_t *ofono_sound_capt_fd; /*!< \brief file descriptor for sound capture dev */
#else							/* WIN32 */
	int audiopipe[2];
	int audioofonopipe[2];
	int ofono_sound_capt_fd; /*!< \brief file descriptor for sound capture dev */
#endif							/* WIN32 */
	switch_thread_t *tcp_srv_thread;
	switch_thread_t *tcp_cli_thread;
	switch_thread_t *ofono_api_thread;

	int ofono_dir_entry_extension_prefix;
	char ofono_user[256];
	char ofono_password[256];
	char destination[256];
	struct timeval answer_time;

	struct timeval transfer_time;
	char transfer_callid_number[50];
	char ofono_transfer_call_id[512];
	int running;
	unsigned long ib_calls;
	unsigned long ob_calls;
	unsigned long ib_failed_calls;
	unsigned long ob_failed_calls;

	unsigned int early_audio;

	int unread_sms_msg_id;
	int reading_sms_msg;
	char *sms_sender;
	char *sms_date;
	char *sms_body;
	char sms_datacodingscheme[256];
	char sms_servicecentreaddress[256];
	int sms_messagetype;
	int sms_cnmi_not_supported;
	int sms_pdu_not_supported;

	struct timeval call_incoming_time;
	switch_mutex_t *controldev_lock;

	int phonebook_listing;
	int phonebook_querying;
	int phonebook_listing_received_calls;

	int phonebook_first_entry;
	int phonebook_last_entry;
	int phonebook_number_lenght;
	int phonebook_text_lenght;
	FILE *phonebook_writing_fp;

	struct timeval ringtime;
#ifdef OFONO_ALSA
	snd_pcm_t *alsac; /*!< \brief handle of the ALSA capture audio device */
	snd_pcm_t *alsap; /*!< \brief handle of the ALSA playback audio device */
	char alsacname[50]; /*!< \brief name of the ALSA capture audio device */
	char alsapname[50]; /*!< \brief name of the ALSA playback audio device */
	int alsa_period_size; /*!< \brief ALSA period_size, in byte */
	int alsa_periods_in_buffer; /*!< \brief how many periods in ALSA buffer, to calculate buffer_size */
	unsigned long int alsa_buffer_size; /*!< \brief ALSA buffer_size, in byte */
	int alsawrite_filled;
	int alsa_capture_is_mono;
	int alsa_play_is_mono;
	struct pollfd pfd;
#endif							// OFONO_ALSA
	time_t audio_play_reset_timestamp;
	int audio_play_reset_period;

	switch_timer_t timer_read;
	switch_timer_t timer_write;
	teletone_dtmf_detect_state_t dtmf_detect;
	switch_time_t old_dtmf_timestamp;

	int no_sound;
	int huawei_audio;
	int modem_audio_active;
	switch_mutex_t *mutex_audio_in;
	switch_mutex_t *mutex_audio_out;
	char *huawei_serial_path;
	GIOChannel *huawei_audio_channel;

#ifdef OFONO_PORTAUDIO
	int speexecho;
	int speexpreprocess;
	int portaudiocindex; /*!< \brief Index of the Portaudio capture audio device */
	int portaudiopindex; /*!< \brief Index of the Portaudio playback audio device */
	PABLIO_Stream *stream;

#ifdef WANT_SPEEX
	SpeexPreprocessState *preprocess;
	SpeexEchoState *echo_state;
#endif// WANT_SPEEX
#endif// OFONO_PORTAUDIO
	dtmf_rx_state_t dtmf_state;
	int active;
	int home_network_registered;
	int roaming_registered;
	int not_registered;
	int got_signal;
	char imei[128];
	int requesting_imei;
	char imsi[128];
	int requesting_imsi;
	int network_creg_not_supported;
	char creg[128];

	/* Ofono modem data */
	char *ofono_modem_name;
	char *ofono_modem_operator_name;
	char *ofono_modem_imsi;
	int ofono_modem_signal_strength;

	char *dbus_path;
	const char *dbus_call_path;
	const char *dbus_sms_path;

	DBusConnection *dbus_conn;
	GMainContext *dbus_main_context;
	GMainLoop *dbus_main_loop;
	int dbus_error;
	guint dbus_service_watch;
	guint dbus_call_added_watch;
	guint dbus_call_removed_watch;
	guint dbus_call_changed_watch;
	guint dbus_audio_changed_watch;
	guint dbus_receivesms_watch;
	guint dbus_modem_state_watch;
	guint dbus_modem_removed_watch;
	guint dbus_network_state_watch;
	guint dbus_audio_status_watch;

	int dbus_modem_attached;
	int dbus_modem_state_online;
	int dbus_modem_state_powered;
	int dbus_has_callmanager;
	int dbus_has_audiosettings;
	int dbus_has_messagemanager;
	int dbus_audio_users;
	unsigned int dbus_audio_watch;

};

typedef struct private_object private_t;

void *SWITCH_THREAD_FUNC ofono_api_thread_func(switch_thread_t * thread, void *obj);

int ofono_audio_read(private_t * tech_pvt);
int ofono_audio_init(private_t * tech_pvt);
int ofono_signaling_read(private_t * tech_pvt);

int ofono_call(private_t * tech_pvt, const char *rdest, int timeout);
int ofono_senddigit(private_t * tech_pvt, const char *digit);

void *ofono_do_tcp_srv_thread_func(void *obj);
void *SWITCH_THREAD_FUNC ofono_do_tcp_srv_thread(switch_thread_t * thread, void *obj);

void *ofono_do_tcp_cli_thread_func(void *obj);
void *SWITCH_THREAD_FUNC ofono_do_tcp_cli_thread(switch_thread_t * thread, void *obj);

void *ofono_do_ofonoapi_thread_func(void *obj);
void *SWITCH_THREAD_FUNC ofono_do_ofonoapi_thread(switch_thread_t * thread, void *obj);
int dtmf_received(private_t * tech_pvt, char *value);
int start_audio_threads(private_t * tech_pvt);
int new_inbound_channel(private_t * tech_pvt);
int outbound_channel_answered(private_t * tech_pvt);
//int ofono_signaling_write(private_t * tech_pvt, char *msg_to_ofono);
#if defined(WIN32) && !defined(__CYGWIN__)
int ofono_pipe_read(switch_file_t * pipe, short *buf, int howmany);
int ofono_pipe_write(switch_file_t * pipe, short *buf, int howmany);
/* Visual C do not have strsep ? */
char *strsep(char **stringp, const char *delim);
#else
int ofono_pipe_read(int pipe, short *buf, int howmany);
int ofono_pipe_write(int pipe, short *buf, int howmany);
#endif /* WIN32 */
int ofono_close_socket(unsigned int fd);
private_t *find_available_ofono_interface_rr(private_t * tech_pvt_calling);
int remote_party_is_ringing(private_t * tech_pvt);
int remote_party_is_early_media(private_t * tech_pvt);
int ofono_answer(private_t * tech_pvt);
//int ofono_answer(private_t * tech_pvt, char *id, char *value);
#if 0
int ofono_transfer(private_t * tech_pvt, char *id, char *value);
#endif //0
int ofono_socket_create_and_bind(private_t * tech_pvt, int *which_port);

void *ofono_do_controldev_thread(void *data);
int ofono_config_dbus_init(private_t * tech_pvt);
int ofono_shutdown_dbus(private_t * tech_pvt);
int ofono_dbus_write_request(private_t * tech_pvt, const char *data);

int ofono_dbus_context_iter(private_t * tech_pvt);
#define RESULT_FAILURE 0
#define RESULT_SUCCESS 1
//#define PUSHA_UNLOCKA(x)    pthread_cleanup_push(ofono_unlocka_log, (void *) x);
//#define POPPA_UNLOCKA(x)    pthread_cleanup_pop(0);

#define PUSHA_UNLOCKA(x)    if(option_debug > 100) ERRORA("PUSHA_UNLOCKA: %p\n", OFONO_P_LOG, (void *)x);
#define POPPA_UNLOCKA(x)    if(option_debug > 100) ERRORA("POPPA_UNLOCKA: %p\n", OFONO_P_LOG, (void *)x);
//#define LOKKA(x)    if(option_debug > 100) ERRORA("LOKKA: %p\n", OFONO_P_LOG, (void *)x);
#define LOKKA(x)    switch_mutex_lock(x);
#define UNLOCKA(x)  switch_mutex_unlock(x);
//#define UNLOCKA(x)    if(option_debug > 100) ERRORA("UNLOCKA: %p\n", OFONO_P_LOG, (void *)x);

#define ofono_queue_control(x, y) ERRORA("ofono_queue_control: %p, %d\n", OFONO_P_LOG, (void *)x, y);

#define ast_setstate(x, y) ERRORA("ast_setstate: %p, %d\n", OFONO_P_LOG, (void *)x, y);

int ofono_hangup(private_t * tech_pvt);
int ofono_sendsms(private_t * tech_pvt, char *dest, char *text);

#ifdef OFONO_ALSA
int alsa_init(private_t * tech_pvt);
int alsa_shutdown(private_t * tech_pvt);
snd_pcm_t *alsa_open_dev(private_t * tech_pvt, snd_pcm_stream_t stream);
int alsa_write(private_t * tech_pvt, short *data, int datalen);
int alsa_read(private_t * tech_pvt, short *data, int datalen);

#endif /* OFONO_ALSA */

int huawei_audio_init(private_t *tech_pvt);
int huawei_audio_shutdown(private_t *tech_pvt);
int huawei_audio_write(private_t *tech_pvt, short *data, int datalen);
int huawei_audio_read(private_t *tech_pvt, short *data, int datalen);

void ofono_store_boost(char *s, double *boost);
int ofono_sound_boost(void *data, int samples_num, double boost);
int sms_incoming(private_t * tech_pvt);
int ofono_incoming_call(private_t * tech_pvt);

#ifdef OFONO_PORTAUDIO

int ofono_portaudio_devlist(private_t *tech_pvt);

int ofono_portaudio_init(private_t *tech_pvt);

int ofono_portaudio_write(private_t * tech_pvt, short *data, int datalen);

int ofono_portaudio_read(private_t * tech_pvt, short *data, int datalen);

int ofono_portaudio_shutdown(private_t *tech_pvt);

#endif // OFONO_PORTAUDIO
int dump_event(private_t *tech_pvt);
int alarm_event(private_t * tech_pvt, int alarm_code,
		const char *alarm_message);
int dump_event_full(private_t * tech_pvt, int is_alarm, int alarm_code,
		const char *alarm_message);
