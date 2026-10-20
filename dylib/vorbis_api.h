// stb_vorbis の必要最小限の宣言(実装は vorbis_impl.c)
#pragma once
typedef struct stb_vorbis stb_vorbis;
typedef struct {
    unsigned int sample_rate;
    int channels;
    unsigned int setup_memory_required;
    unsigned int setup_temp_memory_required;
    unsigned int temp_memory_required;
    int max_frame_size;
} stb_vorbis_info;
stb_vorbis *stb_vorbis_open_filename(const char *filename, int *error, void *alloc_buffer);
stb_vorbis_info stb_vorbis_get_info(stb_vorbis *f);
unsigned int stb_vorbis_stream_length_in_samples(stb_vorbis *f);
int stb_vorbis_get_samples_float_interleaved(stb_vorbis *f, int channels, float *buffer, int num_floats);
void stb_vorbis_close(stb_vorbis *f);
