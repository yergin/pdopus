#include "m_pd.h"
#include <opus.h>
#include <opus_private.h>
#include <stdlib.h>
#include <string.h>

#define MAX_PACKET_SIZE 1024

static t_class* opusenc_tilde_class;

enum LOG_LEVEL
{
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_NORMAL
};

typedef struct _packet
{
    int _size;
    unsigned char _data[MAX_PACKET_SIZE];
    float _dbov;
} Packet;

typedef struct _opusenc_tilde
{
    t_object x_obj;
    t_float _dc;
    t_outlet* _packetOutlet;
    t_outlet* _dbovOutlet;
    t_clock* _clock;
    OpusEncoder* _encoder;
    int _bitrate;
    t_symbol* _mode;
    int _fec;
    int _dtx;
    int _packetLoss;
    float* _buffer;
    int _writePosition;
    Packet* _packetBuffer;
    int _packetBufferSize;
    int _packetCount;
    int _sampleRate;
    int _opusFrameSizeMs;
    int _opusFrameSize;
    int _masterFrameSize;
} t_opusenc_tilde;

void opusenc_tilde_setup();
void* opusenc_tilde_new(t_floatarg frameSize);
void opusenc_tilde_free(t_opusenc_tilde* x);
void opusenc_tilde_dsp(t_opusenc_tilde* x, t_signal** sp);
void opusenc_tilde_reset(t_opusenc_tilde* x);
void opusenc_tilde_status(t_opusenc_tilde* x);
void opusenc_tilde_bitrate(t_opusenc_tilde* x, t_floatarg bitrate);
void opusenc_tilde_mode(t_opusenc_tilde* x, t_symbol* s);
void opusenc_tilde_fec(t_opusenc_tilde* x, t_floatarg enabled);
void opusenc_tilde_dtx(t_opusenc_tilde* x, t_floatarg enabled);
void opusenc_tilde_loss(t_opusenc_tilde* x, t_floatarg loss);
t_int* opusenc_tilde_perform(t_int* w);
void setEncoderOptions(t_opusenc_tilde* x);
int setOpusSampleRate(t_opusenc_tilde* x, int sampleRate);
int setBufferSizes(t_opusenc_tilde* x, int masterFrameSize, int opusFrameSize);
void writeOpusBuffer(t_opusenc_tilde* x, const float* in);
int isOpusFrameReady(t_opusenc_tilde* x);
void processOpusFrame(t_opusenc_tilde* x);
void outputPacket(t_opusenc_tilde* x);

void opusenc_tilde_setup()
{
    opusenc_tilde_class = class_new(gensym("opusenc~"),
                                   (t_newmethod)opusenc_tilde_new,
                                   (t_method)opusenc_tilde_free,
                                   sizeof(t_opusenc_tilde),
                                   CLASS_DEFAULT,
                                   A_FLOAT,
                                   0);
    
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_dsp, gensym("dsp"), A_CANT, 0);
    CLASS_MAINSIGNALIN(opusenc_tilde_class, t_opusenc_tilde, _dc);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_reset, gensym("reset"), 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_status, gensym("status"), 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_bitrate, gensym("bitrate"), A_FLOAT, 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_mode, gensym("mode"), A_SYMBOL, 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_fec, gensym("fec"), A_FLOAT, 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_dtx, gensym("dtx"), A_FLOAT, 0);
    class_addmethod(opusenc_tilde_class, (t_method)opusenc_tilde_loss, gensym("loss"), A_FLOAT, 0);
}

void* opusenc_tilde_new(t_floatarg frameSize)
{
    t_opusenc_tilde* x = (t_opusenc_tilde*)pd_new(opusenc_tilde_class);
    if (!x)
        return 0;
    
    x->_packetOutlet = outlet_new(&x->x_obj, &s_list);
    x->_dbovOutlet = outlet_new(&x->x_obj, &s_float);
    x->_dc = 0;
    x->_clock = clock_new(x, (t_method)outputPacket);
    x->_encoder = 0;
    x->_bitrate = 32000;
    x->_mode = gensym("hybrid");
    x->_fec = 1;
    x->_dtx = 1;
    x->_packetLoss = 0;
    x->_buffer = 0;
    x->_writePosition = 0;
    x->_packetBuffer = 0;
    x->_packetBufferSize = 0;
    x->_packetCount = 0;
    x->_sampleRate = (int)sys_getsr();
    x->_opusFrameSizeMs = frameSize;
    x->_opusFrameSize = 0;
    x->_masterFrameSize = 0;
    
    int err = 0;
    x->_encoder = opus_encoder_create(x->_sampleRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err)
    {
        error("Could not create OPUS encoder: %s", opus_strerror(err));
        opusenc_tilde_free(x);
        return 0;
    }

    verbose(LOG_LEVEL_NORMAL, "OPUS encoder initialised @%dhz", x->_sampleRate);
    
    setEncoderOptions(x);
    setBufferSizes(x, sys_getblksize(), x->_opusFrameSizeMs * x->_sampleRate / 1000);
    
    return x;
}

void opusenc_tilde_free(t_opusenc_tilde* x)
{
    if (x->_encoder)
    {
        opus_encoder_destroy(x->_encoder);
        x->_encoder = 0;
    }
    
    if (x->_packetBuffer)
    {
        free(x->_packetBuffer);
        x->_packetBuffer = 0;
        x->_packetBufferSize = 0;
    }
    
    if (x->_buffer)
    {
        free(x->_buffer);
        x->_buffer = 0;
    }

    clock_free(x->_clock);
}

void opusenc_tilde_dsp(t_opusenc_tilde* x, t_signal** sp)
{
    setOpusSampleRate(x, sp[0]->s_sr);
    
    dsp_add(opusenc_tilde_perform, 3, x, sp[0]->s_vec, sp[0]->s_n);
}

void opusenc_tilde_reset(t_opusenc_tilde* x)
{
    x->_writePosition = 0;
    x->_packetCount = 0;
}

float frameDuration(int code)
{
    switch (code) {
        case OPUS_FRAMESIZE_2_5_MS: return 2.5f;
        case OPUS_FRAMESIZE_5_MS: return 5;
        case OPUS_FRAMESIZE_10_MS: return 10;
        case OPUS_FRAMESIZE_20_MS: return 20;
        case OPUS_FRAMESIZE_40_MS: return 40;
        case OPUS_FRAMESIZE_60_MS: return 60;
        default:
            break;
    }
    return 0;
}

int bandwidth(int code)
{
    switch (code) {
        case OPUS_BANDWIDTH_NARROWBAND: return 4000;
        case OPUS_BANDWIDTH_MEDIUMBAND: return 6000;
        case OPUS_BANDWIDTH_WIDEBAND: return 8000;
        case OPUS_BANDWIDTH_SUPERWIDEBAND: return 12000;
        case OPUS_BANDWIDTH_FULLBAND: return 20000;
        default:
            break;
    }
    return 0;
    
}

const char* mode(int code)
{
    switch (code) {
        case MODE_SILK_ONLY: return "SILK only";
        case MODE_HYBRID: return "HYBRID";
        case MODE_CELT_ONLY: return "CELT only";
        default:
            break;
    }
    return "";
}

void opusenc_tilde_status(t_opusenc_tilde* x)
{
    int val, err;
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_BITRATE(&val));
    if (err)
        error("failed to get encoder bitrate: %s", opus_strerror(err));
    else
        post("bitrate: %d", val);
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_SAMPLE_RATE(&val));
    if (err)
        error("failed to get sample rate: %s", opus_strerror(err));
    else
        post("sample rate: %d", val);
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_BANDWIDTH(&val));
    if (err)
        error("failed to get SILK bandwidth: %s", opus_strerror(err));
    else
        post("bandwidth: %d", bandwidth(val));
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_INBAND_FEC(&val));
    if (err)
        error("failed to get SILK encoder in-band FEC: %s", opus_strerror(err));
    else
        post("FEC: %d", val);
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_DTX(&val));
    if (err)
        error("failed to get DTX: %s", opus_strerror(err));
    else
        post("DTX: %d", val);
    
    err = opus_encoder_ctl(x->_encoder, OPUS_GET_PACKET_LOSS_PERC(&val));
    if (err)
        error("failed to get packet loss: %s", opus_strerror(err));
    else
        post("packet loss: %d", val);
}

void opusenc_tilde_bitrate(t_opusenc_tilde* x, t_floatarg bitrate)
{
    x->_bitrate = bitrate;

    int err = opus_encoder_ctl(x->_encoder, OPUS_SET_BITRATE(bitrate));
    if (err)
        error("failed to set encoder bitrate to %d: %s", (int)bitrate, opus_strerror(err));
    else
        verbose(LOG_LEVEL_NORMAL, "set encoder bitrate to %d", (int)bitrate);
}

void opusenc_tilde_mode(t_opusenc_tilde* x, t_symbol* s)
{
    int mode = MODE_HYBRID;
    if (!strcmp(s->s_name, "hybrid"))
        mode = MODE_HYBRID;
    else if (!strcmp(s->s_name, "silk"))
        mode = MODE_SILK_ONLY;
    else if (!strcmp(s->s_name, "celt"))
        mode = MODE_CELT_ONLY;
    else
    {
        error("mode must be 'hybrid', 'silk' or 'celt'");
        return;
    }

    x->_mode = s;

    int err = opus_encoder_ctl(x->_encoder, OPUS_SET_FORCE_MODE(mode));
    if (err)
        error("failed to set encoder mode to %s: %s", s->s_name, opus_strerror(err));
    else
        verbose(LOG_LEVEL_NORMAL, "set encoder mode to %s", s->s_name);
}

void opusenc_tilde_fec(t_opusenc_tilde* x, t_floatarg enabled)
{
    int f = (enabled == 0 ? 0 : 1);

    x->_fec = f;

    int err = opus_encoder_ctl(x->_encoder, OPUS_SET_INBAND_FEC_REQUEST, f);
    if (err)
        error("failed to set encoder FEC to %d: %s", f, opus_strerror(err));
    else
        verbose(LOG_LEVEL_NORMAL, "set encoder FEC to %d", f);
}

void opusenc_tilde_dtx(t_opusenc_tilde* x, t_floatarg enabled)
{
    int f = (enabled == 0 ? 0 : 1);

    x->_dtx = f;

    int err = opus_encoder_ctl(x->_encoder, OPUS_SET_DTX_REQUEST, f);
    if (err)
        error("failed to set encoder DTX to %d: %s", f, opus_strerror(err));
    else
        verbose(LOG_LEVEL_NORMAL, "set encoder DTX to %d", f);
}

void opusenc_tilde_loss(t_opusenc_tilde* x, t_floatarg loss)
{
    x->_packetLoss = loss;
    
    int err = opus_encoder_ctl(x->_encoder, OPUS_SET_PACKET_LOSS_PERC(loss));
    if (err)
        error("failed to set encoder packet loss to %d: %s", (int)loss, opus_strerror(err));
    else
        verbose(LOG_LEVEL_NORMAL, "set encoder packet loss to %d", (int)loss);
}

void setEncoderOptions(t_opusenc_tilde* x)
{
    opusenc_tilde_bitrate(x, x->_bitrate);
    opusenc_tilde_mode(x, x->_mode);
    opusenc_tilde_fec(x, x->_fec);
    opusenc_tilde_dtx(x, x->_dtx);
    opusenc_tilde_loss(x, x->_packetLoss);
}

int setOpusSampleRate(t_opusenc_tilde* x, int sampleRate)
{
    if (x->_sampleRate == sampleRate)
    {
        return 1;
    }
    
    x->_sampleRate = sampleRate;
    
    int err = opus_encoder_init(x->_encoder, sampleRate, 1, OPUS_APPLICATION_VOIP);
    if (err)
    {
        error("could not initialise OPUS encoder @%dhz: %s", sampleRate, opus_strerror(err));
        return 0;
    }

    verbose(LOG_LEVEL_NORMAL, "OPUS encoder initialised @%dhz", sampleRate);
    
    setEncoderOptions(x);

    return setBufferSizes(x, x->_masterFrameSize, x->_opusFrameSizeMs * x->_sampleRate / 1000);
}

int setBufferSizes(t_opusenc_tilde* x, int masterFrameSize, int opusFrameSize)
{
    if (x->_masterFrameSize == masterFrameSize && x->_opusFrameSize == opusFrameSize)
        return 1;
    
    x->_masterFrameSize = masterFrameSize;
    x->_opusFrameSize = opusFrameSize;
    
    if (x->_buffer)
        free(x->_buffer);

    x->_buffer = (float*)malloc(x->_opusFrameSize * sizeof(float));

    if (x->_packetBuffer)
        free(x->_packetBuffer);

    x->_packetBufferSize = x->_masterFrameSize / x->_opusFrameSize + 1;
    x->_packetBuffer = (Packet*)malloc(x->_packetBufferSize * sizeof(Packet));
    
    opusenc_tilde_reset(x);

    verbose(LOG_LEVEL_NORMAL, "pd~ buffer size: %d, OPUS frame size: %d", x->_masterFrameSize, x->_opusFrameSize);
    verbose(LOG_LEVEL_NORMAL, "%d packet buffer(s) allocated", x->_packetBufferSize);
    
    return 1;
}

void processOpusFrame(t_opusenc_tilde* x)
{
    if (x->_packetCount == x->_packetBufferSize)
    {
        error("packet overflow");
        return;
    }

    Packet* packet = &x->_packetBuffer[x->_packetCount];

    packet->_size = opus_encode_float(x->_encoder, x->_buffer, x->_opusFrameSize, packet->_data, MAX_PACKET_SIZE);
    
    packet->_dbov = 0;
    for (int i = 0; i < x->_opusFrameSize; ++i)
        packet->_dbov += x->_buffer[i] * x->_buffer[i];
    packet->_dbov /= x->_opusFrameSize;

    if (packet->_dbov == 0)
    {
        packet->_dbov = 127;
    }
    else if (packet->_dbov >= 1)
    {
        packet->_dbov = 0;
    }
    else
    {
        packet->_dbov = -10 * log10(packet->_dbov);
        packet->_dbov = packet->_dbov > 127 ? 127 : (int)(packet->_dbov + 0.5f);
    }

    verbose(LOG_LEVEL_NORMAL, "OPUS encoded %d samples into a packet of size %d bytes starting with 0x%02x", x->_opusFrameSize, packet->_size, packet->_data[0]);

    x->_packetCount++;
}

t_int* opusenc_tilde_perform(t_int* w)
{
    t_opusenc_tilde* x = (t_opusenc_tilde*)(w[1]);
    t_sample* in = (t_sample*)(w[2]);
    int n = (int)(w[3]);
    
    setBufferSizes(x, n, x->_opusFrameSize);
    
    while (n)
    {
        int count = x->_writePosition + n > x->_opusFrameSize ? x->_opusFrameSize - x->_writePosition : n; 
        memcpy(x->_buffer + x->_writePosition, in, count * sizeof(t_sample));
        x->_writePosition += count;
        n -= count;
        if (x->_writePosition == x->_opusFrameSize)
        {
            processOpusFrame(x);
            x->_writePosition = 0;
        }
    }

    if (x->_packetCount)
    	clock_delay(x->_clock, 0);

    return w + 4;
}

void outputPacket(t_opusenc_tilde* x)
{
    for (int i = 0; i < x->_packetCount; ++i)
    {
        outlet_float(x->_dbovOutlet, x->_packetBuffer[i]._dbov);

        t_atom list[MAX_PACKET_SIZE];

        for (int c = 0; c < x->_packetBuffer[i]._size; ++c)
            SETFLOAT(&list[c], x->_packetBuffer[i]._data[c]);
        
        outlet_list(x->_packetOutlet, &s_list, x->_packetBuffer[i]._size, list);
    }
    x->_packetCount = 0;
}
