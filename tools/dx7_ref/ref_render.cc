// Reference: Dexed's msfa (Apache-2.0), LG_N = 5, rendering one note of a voice file (155 bytes VCED)
//   ref_render voice.bin NOTE VEL BLOCKS KEYUP_BLOCK > out.raw   (voice.bin: 155 bytes VCED, or 128 packed)
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include "synth.h"
#include "sin.h"
#include "exp2.h"
#include "freqlut.h"
#include "lfo.h"
#include "pitchenv.h"
#include "env.h"
#include "porta.h"
#include "controllers.h"
#include "fm_core.h"
#include "dx7note.h"
#include "tuning.h"
#include "patch.h"

void dexed_trace(const char *, const char *, ...) {}

struct StdTuning : public TuningState {
    int32_t midinote_to_logfreq(int mn) override { return 50857777 + ((1 << 24) / 12) * mn; }
};
std::shared_ptr<TuningState> createStandardTuning() { return std::make_shared<StdTuning>(); }

int main(int argc, char **argv) {
    if (argc < 6) return 2;
    uint8_t v[160] = {0};
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    size_t got = fread(v, 1, 155, f);
    fclose(f);
    if (got == 128) {                       // a packed bank voice: msfa's own UnpackPatch
        char packed[128], un[156];
        memcpy(packed, v, 128);
        UnpackPatch(packed, un);
        memcpy(v, un, 155);
    }
    int note = atoi(argv[2]), vel = atoi(argv[3]), blocks = atoi(argv[4]), keyup = atoi(argv[5]);
    double sr = 44100;
    Sin::init(); Exp2::init(); Tanh::init(); Freqlut::init(sr); Lfo::init(sr); PitchEnv::init(sr);
    Env::init_sr(sr); Porta::init_sr(sr);
    Controllers c;
    c.values_[kControllerPitch] = 0x2000;
    c.values_[kControllerPitchRangeUp] = 3;
    c.values_[kControllerPitchRangeDn] = 3;
    c.values_[kControllerPitchStep] = 0;
    c.masterTune = 0;
    c.modwheel_cc = c.breath_cc = c.foot_cc = c.aftertouch_cc = 0;
    c.portamento_enable_cc = false; c.portamento_cc = 0; c.portamento_gliss_cc = false;
    c.mpeEnabled = false;
    c.refresh();
    FmCore core;
    c.core = &core;
    Lfo lfo; memset((void*)&lfo, 0, sizeof lfo);
    lfo.reset(v + 137);
    lfo.keydown();
    Dx7Note n(createStandardTuning(), nullptr);
    n.init(v, note + v[144] - 24, vel, 1, &c);
    if (v[136]) n.oscSync();
    AlignedBuf<int32_t, N> buf;
    for (int b = 0; b < blocks; b++) {
        int32_t lv = lfo.getsample(), ld = lfo.getdelay();
        if (b == keyup) n.keyup();
        for (int i = 0; i < N; i++) buf.get()[i] = 0;
        n.compute(buf.get(), lv, ld, &c);
        fwrite(buf.get(), 4, N, stdout);
    }
    return 0;
}
