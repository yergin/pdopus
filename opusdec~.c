#include "m_pd.h"
#include <opus.h>
#include <opus_private.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKET_SIZE 1024

static t_class* opusdec_tilde_class;

enum LOG_LEVEL
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_NORMAL
};

typedef struct _opusdec_tilde
{
    t_object x_obj;
    t_outlet* _audioOutlet;
    OpusDecoder* _decoder;
    int _sampleRate;
    int _opusFrameSizeMs;
    int _opusFrameSize;
    int _masterFrameSize;
    float* _frameBuffer;
    int _frameBufferSize;
    int _writePosition;
    int _readPosition;
    int _framesDecoded;
} t_opusdec_tilde;

void opusdec_tilde_setup();
void* opusdec_tilde_new(t_floatarg frameSize);
void opusdec_tilde_free(t_opusdec_tilde* x);
void opusdec_tilde_dsp(t_opusdec_tilde* x, t_signal** sp);
t_int* opusdec_tilde_perform(t_int* w);
void opusdec_tilde_reset(t_opusdec_tilde* x);
void opusdec_tilde_packet(t_opusdec_tilde* x, t_symbol* s, int argc, t_atom* argv);
void opusdec_tilde_bang(t_opusdec_tilde* x);
int setOpusSampleRate(t_opusdec_tilde* x, int sampleRate);
int setBufferSizes(t_opusdec_tilde* x, int masterFrameSize, int opusFrameSize);

void opusdec_tilde_setup()
{
    opusdec_tilde_class = class_new(gensym("opusdec~"),
                                   (t_newmethod)opusdec_tilde_new,
                                   (t_method)opusdec_tilde_free,
                                   sizeof(t_opusdec_tilde),
                                   CLASS_DEFAULT,
                                   A_FLOAT,
                                   0);
    
    class_addmethod(opusdec_tilde_class, (t_method)opusdec_tilde_dsp, gensym("dsp"), 0);
    class_addlist(opusdec_tilde_class, (t_method)opusdec_tilde_packet);
    class_addbang(opusdec_tilde_class, (t_method)opusdec_tilde_bang);
}

void* opusdec_tilde_new(t_floatarg frameSize)
{
    t_opusdec_tilde* x = (t_opusdec_tilde*)pd_new(opusdec_tilde_class);
    if (!x)
        return 0;
    
    x->_audioOutlet = outlet_new(&x->x_obj, &s_signal);
    x->_sampleRate = (int)sys_getsr();
    x->_opusFrameSizeMs = frameSize;
    x->_opusFrameSize = 0;
    x->_masterFrameSize = 0;
    x->_frameBuffer = 0;
    x->_frameBufferSize = 0;
    x->_writePosition = 0;
    x->_readPosition = 0;
    x->_framesDecoded = 0;
    
    int err = 0;
    x->_decoder = opus_decoder_create(x->_sampleRate, 1, &err);
    if (err)
    {
        error("could not create OPUS decoder: %s", opus_strerror(err));
        opusdec_tilde_free(x);
        return 0;
    }
    
    verbose(LOG_LEVEL_NORMAL, "OPUS decoder initialised @%dhz", x->_sampleRate);

    setBufferSizes(x, sys_getblksize(), x->_opusFrameSizeMs * x->_sampleRate / 1000);
    
    return x;
}

void opusdec_tilde_free(t_opusdec_tilde* x)
{
    if (x->_decoder)
    {
        opus_decoder_destroy(x->_decoder);
        x->_decoder = 0;
    }
}

int setOpusSampleRate(t_opusdec_tilde* x, int sampleRate)
{
    if (x->_sampleRate == sampleRate)
    {
        return 1;
    }
    
    x->_sampleRate = sampleRate;
    
    int err = opus_decoder_init(x->_decoder, sampleRate, 1);
    if (err)
    {
        error("could not initialise OPUS encoder @%dhz: %s", sampleRate, opus_strerror(err));
        return 0;
    }
    
    verbose(LOG_LEVEL_NORMAL, "OPUS encoder initialised @%dhz", sampleRate);
    
    return setBufferSizes(x, x->_masterFrameSize, x->_opusFrameSizeMs * x->_sampleRate / 1000);
}

int setBufferSizes(t_opusdec_tilde* x, int masterFrameSize, int opusFrameSize)
{
    if (x->_masterFrameSize == masterFrameSize && x->_opusFrameSize == opusFrameSize)
        return 1;
    
    x->_masterFrameSize = masterFrameSize;
    x->_opusFrameSize = opusFrameSize;
    
    if (x->_frameBuffer)
        free(x->_frameBuffer);
    
    x->_frameBufferSize = 3 * x->_opusFrameSize;
    x->_frameBuffer = (float*)calloc(x->_frameBufferSize, sizeof(float));
    
    opusdec_tilde_reset(x);
    
    verbose(LOG_LEVEL_NORMAL, "pd~ buffer size: %d, OPUS frame size: %d", x->_masterFrameSize, x->_opusFrameSize);
    verbose(LOG_LEVEL_NORMAL, "frame buffer of size %d allocated", x->_frameBufferSize);
    
    return 1;
}

void opusdec_tilde_dsp(t_opusdec_tilde* x, t_signal** sp)
{
    setOpusSampleRate(x, sp[0]->s_sr);
    
    dsp_add(opusdec_tilde_perform, 3, x, sp[0]->s_vec, sp[0]->s_n);
}

void opusdec_tilde_reset(t_opusdec_tilde* x)
{
    x->_writePosition = 0;
    x->_readPosition = x->_frameBufferSize - x->_opusFrameSize;
    memset(x->_frameBuffer, 0, x->_frameBufferSize * sizeof(float));
    x->_framesDecoded = 0;
}

void opusdec_tilde_bang(t_opusdec_tilde* x)
{
    int decoded = opus_decode_float(x->_decoder, 0, 0, x->_frameBuffer + x->_writePosition, x->_opusFrameSize, 0);
    if (decoded != x->_opusFrameSize)
    {
        error("error generating samples");
        return;
    }
    
    verbose(LOG_LEVEL_NORMAL, "generated %d samples from decoder state", decoded);
    
    x->_writePosition = (x->_writePosition + decoded) % x->_frameBufferSize;
    x->_framesDecoded = 1;
}

void opusdec_tilde_packet(t_opusdec_tilde* x, t_symbol* s, int argc, t_atom* argv)
{
    verbose(LOG_LEVEL_NORMAL, "packet of size %d received", argc);
    
    unsigned char d[MAX_PACKET_SIZE];
    for (int i = 0; i < argc; ++i)
    {
        if (argv[i].a_type != A_FLOAT || argv[i].a_w.w_float != (int)argv[i].a_w.w_float || argv[i].a_w.w_float < 0 || argv[i].a_w.w_float > 255)
        {
            error("recieved invalid packet data (%f) at byte index: %d", argv[i].a_w.w_float, i);
            return;
        }
        
        d[i] = (int)argv[i].a_w.w_float;
    }
    
    int decoded = opus_decode_float(x->_decoder, d, argc, x->_frameBuffer + x->_writePosition, x->_opusFrameSize, 0);
    if (decoded != x->_opusFrameSize)
    {
        error("error decoding packet of size %d", argc);
        return;
    }
    
    verbose(LOG_LEVEL_NORMAL, "decoded %d samples from a packet of size %d", decoded, argc);
    
    x->_writePosition = (x->_writePosition + decoded) % x->_frameBufferSize;
    x->_framesDecoded = 1;
}

void readFrameBuffer(t_opusdec_tilde* x, float* out)
{
    if (!x->_framesDecoded)
    {
        memset(out, 0, x->_masterFrameSize * sizeof(float));
        return;
    }
    
    if (x->_readPosition + x->_masterFrameSize > x->_frameBufferSize)
    {
        int count = x->_frameBufferSize - x->_readPosition;
        memcpy(out, x->_frameBuffer + x->_readPosition, count * sizeof(float));
        memcpy(out + count, x->_frameBuffer, (x->_masterFrameSize - count) * sizeof(float));
    }
    else
    {
        memcpy(out, x->_frameBuffer + x->_readPosition, x->_masterFrameSize * sizeof(float));
    }
    x->_readPosition = (x->_readPosition + x->_masterFrameSize) % x->_frameBufferSize;
}

t_int* opusdec_tilde_perform(t_int* w)
{
    t_opusdec_tilde* x = (t_opusdec_tilde*)(w[1]);
    t_sample* out = (t_sample*)(w[2]);
    int n = (int)(w[3]);
    
    setBufferSizes(x, n, x->_opusFrameSize);
    
    readFrameBuffer(x, out);
    
    return w + 4;
}
