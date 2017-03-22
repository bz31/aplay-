// ©2017 Yuichiro Nakada
// clang -Os -o aplay+ aplay+.c -lasound

#include <stdio.h>
#include <sys/mman.h>
#include "alsa.h"
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"
#define DR_WAV_IMPLEMENTATION
#include "dr_wav.h"
#include "minimp3.h"
#include "stb_vorbis.h"

#include "ls.h"
#include "kbhit.h"
int cmd;
int key(AUDIO *a)
{
	if (!kbhit()) return 0;

	int c = readch();
	cmd = c;
	printf("%x\n", c);
	if (c==0x20) {
		snd_pcm_pause(a->handle, 1);
		do {
			usleep(1000);	// us
		} while (!kbhit());
		snd_pcm_pause(a->handle, 0);
		snd_pcm_prepare(a->handle);
		return 0;
	}

	return c;
}

//char *dev = "default";	// "plughw:0,0"
char *dev = "hw:0,0";	// BitPerfect

#define FRAMES	32
void play_wav(char *name)
{
	drwav wav;
	if (drwav_init_file(&wav, name)) {
		printf("%dHz %dch\n", wav.sampleRate, wav.channels);

		AUDIO a;
		AUDIO_init(&a, dev, wav.sampleRate, wav.channels, FRAMES, 1);

		while (drwav_read_s16(&wav, a.frames * wav.channels, (dr_int16*)a.buffer) > 0) {
			AUDIO_play0(&a);
			AUDIO_wait(&a, 100);
			if (key(&a)) break;
		}

		AUDIO_close(&a);
		drwav_uninit(&wav);
	}
}

void play_flac(char *name)
{
	drflac *flac = drflac_open_file(name);
	printf("%dHz %dch\n", flac->sampleRate, flac->channels);

	AUDIO a;
	AUDIO_init(&a, dev, flac->sampleRate, flac->channels, FRAMES, 1);

	if (flac) {
		while (drflac_read_s16(flac, a.frames * flac->channels, (dr_int16*)a.buffer) > 0) {
			AUDIO_play0(&a);
			AUDIO_wait(&a, 100);
			if (key(&a)) break;
		}
	}

	AUDIO_close(&a);
}

int play_mp3(char *name)
{
	mp3_info_t info;
	void *file_data;
	unsigned char *stream_pos;
	short sample_buf[MP3_MAX_SAMPLES_PER_FRAME];
	int bytes_left;
	int frame_size;
	int value;

	int fd = open(name, O_RDONLY);
	if (fd < 0) {
		printf("Error: cannot open `%s`\n", name);
		return 1;
	}

	bytes_left = lseek(fd, 0, SEEK_END);
	file_data = mmap(0, bytes_left, PROT_READ, MAP_PRIVATE, fd, 0);
	stream_pos = (unsigned char *) file_data;
	bytes_left -= 100;

	mp3_decoder_t mp3 = mp3_create();
	frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, &info);
	if (!frame_size) {
		printf("Error: not a valid MP3 audio file!\n");
		return 1;
	}

	printf("%dHz %dch\n", info.sample_rate, info.channels);
	AUDIO a;
	AUDIO_init(&a, dev, info.sample_rate, info.channels, FRAMES/*MP3_MAX_SAMPLES_PER_FRAME*//*frame_size*/, 1);

	while ((bytes_left >= 0) && (frame_size > 0)) {
		stream_pos += frame_size;
		bytes_left -= frame_size;
		AUDIO_play(&a, (char*)sample_buf, info.audio_bytes/2/info.channels);
		AUDIO_wait(&a, 100);
		if (key(&a)) break;

		frame_size = mp3_decode(mp3, stream_pos, bytes_left, sample_buf, NULL);
	}

	AUDIO_close(&a);
	return 0;
}

void play_ogg(char *name)
{
	int n, num_c, error;
	//short *outputs;
	short outputs[FRAMES*2*100];

	stb_vorbis *v = stb_vorbis_open_filename(name, &error, NULL);
	//if (!v) stb_fatal("Couldn't open {%s}", name);
	printf("%dHz %dch\n", v->sample_rate, v->channels);

	AUDIO a;
	AUDIO_init(&a, dev, v->sample_rate, v->channels, FRAMES*2, 1);

	//while ((n = stb_vorbis_get_frame_short(v, 1, &outputs, FRAMES))) {
	while ((n = stb_vorbis_get_frame_short_interleaved(v, v->channels, outputs, FRAMES*100))) {
		AUDIO_play(&a, (char*)outputs, n);
		AUDIO_wait(&a, 100);
		if (key(&a)) break;
	}

	AUDIO_close(&a);
	stb_vorbis_close(v);
}

void play_dir(char *name)
{
	char path[1024];
	int num;

	LS_LIST *ls = ls_dir(name, 1, &num);
	for (int i=0; i<num; i++) {
		printf("%s\n", ls[i].d_name);
		//snprintf(path, 1024, "%s/%s", name, ls[i].d_name);
		snprintf(path, 1024, "%s", ls[i].d_name);

		if (strstr(ls[i].d_name, ".flac")) play_flac(path);
		else if (strstr(ls[i].d_name, ".mp3")) play_mp3(path);
		else if (strstr(ls[i].d_name, ".ogg")) play_ogg(path);
		else if (strstr(ls[i].d_name, ".wav")) play_wav(path);

		if (cmd=='q' || cmd==0x1b) break;
	}
}

int main(int argc, char *argv[])
{
	if (argc>2) dev = argv[2];

	init_keyboard();
	play_dir(argv[1]);
	close_keyboard();
}
