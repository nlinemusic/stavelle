/* Jubelle gain tool: the Vorbis decoder FluidSynth calls for .sf3 soundfonts */
extern int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

int vorbis_decode_memory(const unsigned char* mem, unsigned int len, short** output, unsigned int* channels,
                         unsigned int* sample_rate)
{
    int ch = 0, sr = 0;
    int n = stb_vorbis_decode_memory(mem, (int)len, &ch, &sr, output);
    if (channels) {
        *channels = (unsigned int)ch;
    }
    if (sample_rate) {
        *sample_rate = (unsigned int)sr;
    }
    return n;
}
