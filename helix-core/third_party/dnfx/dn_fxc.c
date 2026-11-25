/* SPDX-License-Identifier: GPL-3.0-only
 * DREADNOUGHT: Carnac OS's effects in the FX rack (0.16): the TAPE and BBD echoes and the reverbs SPRING, TOMB, MOD,
 * CELESTIAL, DOLMEN, TUMULUS.
 *
 * Ported from Carnac OS (src/dsp/fx.c, GPL-3.0-only, Copyright (C) 2026 its authors; see docs/UPSTREAM.md), keeping
 * its arithmetic, its tuning and the fixes its comments record (tails that die to exact silence, the tank's guard,
 * the springs in float, the send limiter in dn_fx.c). What changed for DREADNOUGHT:
 *   - every effect is an instance (its state in the slot, its lines in the FX arena), so FX A and FX B can both be
 *     a reverb or both a tape;
 *   - one core: the reverb's front and back run together (no lag ring), the tank's input diffusers sit in its own
 *     ring under the same pointer (no ring of their own), and a 32-sample block (the app's);
 *   - the delays' times are the project tempo's divisions (the FX page's TIME), not a 120 BPM grid;
 *   - the reverbs' send gets a LOW CUT before the predelay (keeps a tail off the sub);
 *   - the slowed tape's speed is worked out in float (no 64-bit divide on the FM-1);
 *   - the tape's and BBD's high-passes in float: in Q15 a coefficient this small left up to 127 LSB of DC for ever;
 *   - the heads start where the first TIME puts them (Carnac glided up from 64 samples: a first echo early).
 * The design notes below are Carnac's, shortened. Copyright (C) 2026 Jamie (TwistedGenetics) for the changes. */
#include "dn_fx.h"
#include "dsp/fastmath.h"

/* ---- the delays: TAPE, BBD ---------------------------------------------------------------------------------------
 * The tape is a loop of TAPE_LEN cells. The record head writes a cell each time it crosses a cell boundary, the play
 * head trails it by the head gap; delay = gap / speed: up to TAPE_GAP_MAX the tape runs at full speed, longer delays
 * keep the gap and slow the tape (darker, as a slowed tape is). Gap and speed slew, so a new TIME bends the echoes'
 * pitch. Wow and flutter wobble the play head, the record path saturates softly, playback loses top (head loss) and
 * bottom (a high-pass): DEGRADE drives all three. PING PONG: a second play head halfway along the gap. FEEDBACK past
 * 100 runs away into a held howl (a darkening low-pass and a hiss seed in the loop). BBD: the same loop as 2048
 * buckets read held (its crumble), fixed dark filters both sides, the compander's breath. */
#define TAPE_LEN 16384u
#define TAPE_MASK (TAPE_LEN - 1u)
#define TAPE_GAP_MAX (TAPE_LEN - 64u)
#define TAPE_SLEW 12
#define WOW_INC 48696u                              /* 0.5 Hz at 44.1 kHz */
#define FLUTTER_INC 613567u                         /* 6.3 Hz */
#define WOW_STEP_Q8 96
#define FLUTTER_STEP_Q8 16
#define BBD_N 2048u
#define DLY_MAX 65088u                              /* the longest delay, samples (1.47 s: the tape at 1/4 speed) */

#define PRE_LEN 4448u                               /* the reverbs' predelay: 127 x 35 samples */
#define MR_RING 32768u
#define MR_SH_LEN 2048u
#define SP_RING 4096u
static const uint16_t RM_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t RM_AP[2] = {556, 441};
#define RM_COMB_T (1116u + 1188u + 1277u + 1356u)
#define RM_AP_T (556u + 441u)

uint32_t dn_fxc_ram(uint32_t type)
{
    switch (type) {
    case DN_FX_TAPE:
    case DN_FX_BBD:
        return TAPE_LEN * 2u;
    case DN_FX_SPRING:
        return (PRE_LEN + SP_RING) * 2u;
    case DN_FX_TOMB:
        return (PRE_LEN + RM_COMB_T + RM_AP_T + 1u) * 2u;
    case DN_FX_MOD:
    case DN_FX_DOLMEN:
    case DN_FX_TUMULUS:
        return (PRE_LEN + MR_RING) * 2u;
    case DN_FX_CELEST:
        return (PRE_LEN + MR_RING + MR_SH_LEN) * 2u;
    default:
        return 0u;
    }
}

void dn_fxc_init(dn_fx_s_t *s)
{
    int16_t *m = (int16_t *)s->mem;
    switch (s->type) {
    case DN_FX_TAPE:
    case DN_FX_BBD: {
        dn_fxt_t *t = &s->u.t;
        t->tape = m;
        t->gap_q16 = t->gap_tgt_q16 = 64 << 16;
        t->speed_q24 = t->speed_tgt_q24 = 1 << 24;
        t->last_len = 0u;                          /* (none yet: fxc_targets snaps to the first) */
        t->bb_rng = 0x2545F491u;
        t->seed_rng = 0x6C8E9CF5u;
        break;
    }
    case DN_FX_SPRING:
        s->u.s.f.pre = m;
        s->u.s.ring = m + PRE_LEN;
        break;
    case DN_FX_TOMB:
        s->u.r.f.pre = m;
        s->u.r.comb = m + PRE_LEN;
        s->u.r.ap = m + PRE_LEN + RM_COMB_T;
        break;
    default: {                                      /* the tank */
        dn_fxk_t *k = &s->u.k;
        k->f.pre = m;
        k->ring = m + PRE_LEN;
        k->sh_buf = s->type == DN_FX_CELEST ? m + PRE_LEN + MR_RING : 0;
        k->ph_r = 0x40000000u;                      /* the two sides' spin a quarter cycle apart */
        k->rng = 0x1F3D5B79u;
        k->duck = k->duck_g = 32767;
        break;
    }
    }
}

/* parabolic sine, Q15 */
static int32_t fxc_sine(uint32_t ph)
{
    int32_t x = (int32_t)ph >> 16, ax = x < 0 ? -x : x;
    return (x * (32767 - ax)) >> 13;
}
static int32_t fxc_loss(uint32_t amt)              /* head loss low-pass, Q15: open .. ~450 Hz */
{
    int32_t inv = 128 - (int32_t)(amt > 127u ? 127u : amt), c = 2048 + ((inv * inv * 30720) >> 14);
    return c > 32767 ? 32767 : c;
}
static int32_t fxc_hp(uint32_t amt) { return 40 + 6 * (int32_t)(amt > 127u ? 127u : amt); }
static int32_t fxc_sat(int32_t x)                   /* record saturation: 1.5u - 0.5u^3, u = 2x / 3 */
{
    int32_t u = dn_fx_c16((x * 21845) >> 15), u2 = (u * u) >> 15;
    return (3 * u - ((u * u2) >> 15)) >> 1;
}
static void fxc_degrade(uint32_t d, int32_t *wow, int32_t *flutter, uint32_t *filter)
{
    if (d > 127u)
        d = 127u;
    *wow = (int32_t)(d * 32u / 36u);
    *flutter = (int32_t)(d * d * 10u / 1296u);
    *filter = d;
}
/* the heads' targets for a delay of len samples (the tape's loop, or the BBD's buckets, at full speed; longer: slower) */
static void fxc_targets(dn_fxt_t *t, uint32_t len, int bbd)
{
    uint32_t gap_max = bbd ? BBD_N : TAPE_GAP_MAX, key = len | (bbd ? 0x80000000u : 0u);
    if (key == t->last_len)
        return;
    int snap = !t->last_len;                        /* (the first time: there at once, no glide up from nothing) */
    t->last_len = key;
    if (len <= gap_max) {
        t->gap_tgt_q16 = (int32_t)(len << 16);
        t->speed_tgt_q24 = 1 << 24;
    } else {
        t->gap_tgt_q16 = (int32_t)(gap_max << 16);
        t->speed_tgt_q24 = (int32_t)((float)gap_max / (float)len * 16777216.f);
    }
    if (snap)
        t->gap_q16 = t->gap_tgt_q16, t->speed_q24 = t->speed_tgt_q24;
}
typedef struct { int32_t lo, hi, run_c; int runaway; } fxc_fb_t;
static fxc_fb_t fxc_feedback(uint32_t knob, uint32_t degrade)
{
    fxc_fb_t f;
    int32_t top = 40000 + (int32_t)(degrade > 127u ? 127u : degrade) * 104;
    int32_t k = knob > 127u ? 127 : (int32_t)knob, g = k <= 100 ? k * 250 : 25000 + (k - 100) * (top - 25000) / 27;
    f.lo = g > 32767 ? 32767 : g;
    f.hi = g - f.lo;
    f.runaway = k > 100;
    f.run_c = 32767 - (k > 100 ? (k - 100) * 900 : 0);
    return f;
}
static uint32_t fxc_len(const dn_fx_p_t *p, uint32_t bpm)
{
    uint32_t len = dn_fx_sync_samples(p->p[0], bpm), pp = p->p[3] != 0u;
    if (len < 64u)
        len = 64u;
    while ((len << pp) > DLY_MAX)                   /* (too long at this tempo: half, still on the beat) */
        len >>= 1;
    return len << pp;
}

static void fxc_tape(dn_fxt_t *t, const dn_fx_p_t *p, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n,
                     uint32_t bpm)
{
    const int pp = p->p[3] != 0u;
    int32_t wow, flutter;
    uint32_t filter, s;
    int16_t *const tape = t->tape;
    fxc_targets(t, fxc_len(p, bpm), 0);
    fxc_degrade(p->p[2], &wow, &flutter, &filter);
    const int32_t gap_tgt = t->gap_tgt_q16, speed_tgt = t->speed_tgt_q24;
    const int32_t wow_q8 = wow * WOW_STEP_Q8, flutter_q8 = flutter * FLUTTER_STEP_Q8;
    const int32_t loss_c = fxc_loss(filter);
    const float hp_c = (float)fxc_hp(filter) * (1.f / 32768.f);
    const fxc_fb_t fb = fxc_feedback(p->p[1], p->p[2]);
    int32_t run_lp = t->run_lp;
    uint32_t seed = t->seed_rng, wpos = t->wpos_q16, wow_ph = t->wow_ph, flutter_ph = t->flutter_ph;
    int32_t gap = t->gap_q16, speed = t->speed_q24, aa_lp = t->aa_lp, loss_lp = t->loss_lp, loss2 = t->loss2_lp;
    float hp_lp = t->hp_lp, hp2 = t->hp2_lp;
    for (s = 0; s < n; s++) {
        gap += (gap_tgt - gap) >> TAPE_SLEW;
        speed += (speed_tgt - speed) >> TAPE_SLEW;
        int32_t speed_q16 = speed >> 8;
        wow_ph += WOW_INC;
        flutter_ph += FLUTTER_INC;
        int32_t mod_q8 = (fxc_sine(wow_ph) * wow_q8 + fxc_sine(flutter_ph) * flutter_q8) >> 15;
        int32_t mod_q16 = (mod_q8 * speed_q16) >> 8;
        uint32_t r = wpos - (uint32_t)gap - (uint32_t)mod_q16, i = r >> 16;
        int32_t frac = (int32_t)((r >> 1) & 0x7FFFu), a = tape[i & TAPE_MASK], b = tape[(i + 1u) & TAPE_MASK];
        int32_t play = a + (((b - a) * frac) >> 15);
        loss_lp += dn_fx_mq(play - loss_lp, loss_c);
        hp_lp += hp_c * ((float)loss_lp - hp_lp);
        int32_t o = loss_lp - (int32_t)hp_lp;
        if (pp) {                                   /* the near head: the left's echoes */
            uint32_t r2 = wpos - ((uint32_t)gap >> 1) - (uint32_t)mod_q16, i2 = r2 >> 16;
            int32_t f2 = (int32_t)((r2 >> 1) & 0x7FFFu), a2 = tape[i2 & TAPE_MASK], b2 = tape[(i2 + 1u) & TAPE_MASK];
            int32_t play2 = a2 + (((b2 - a2) * f2) >> 15);
            loss2 += dn_fx_mq(play2 - loss2, loss_c);
            hp2 += hp_c * ((float)loss2 - hp2);
            out_l[s] = loss2 - (int32_t)hp2;
            out_r[s] = o;
        } else {
            out_l[s] = out_r[s] = o;
        }
        int32_t fbs = o, hiss = 0;
        if (fb.runaway) {
            run_lp += dn_fx_mz(o - run_lp, fb.run_c);
            fbs = run_lp;
            seed = seed * 1664525u + 1013904223u;
            hiss = (int32_t)(int16_t)(seed >> 16) >> 12;
        }
        int32_t rec = fxc_sat(in[s] + dn_fx_mq(fbs, fb.lo) + dn_fx_mq(fbs, fb.hi) + hiss);
        int32_t aa_q15 = (speed_q16 >> 1) + (speed_q16 >> 2);
        if (aa_q15 > 32767)
            aa_q15 = 32767;
        aa_lp += dn_fx_mq(rec - aa_lp, aa_q15);
        uint32_t old_cell = wpos >> 16;
        wpos += (uint32_t)speed_q16;
        if ((wpos >> 16) != old_cell)
            tape[(wpos >> 16) & TAPE_MASK] = (int16_t)dn_fx_c16(aa_lp);
    }
    t->wpos_q16 = wpos, t->wow_ph = wow_ph, t->flutter_ph = flutter_ph, t->gap_q16 = gap, t->speed_q24 = speed;
    t->aa_lp = aa_lp, t->loss_lp = loss_lp, t->loss2_lp = loss2;
    t->hp_lp = hp_lp < 1e-3f && hp_lp > -1e-3f ? 0.f : hp_lp, t->hp2_lp = hp2 < 1e-3f && hp2 > -1e-3f ? 0.f : hp2;
    t->run_lp = run_lp, t->seed_rng = seed;
}

static void fxc_bbd(dn_fxt_t *t, const dn_fx_p_t *p, const int32_t *in, int32_t *out_l, int32_t *out_r, uint32_t n,
                    uint32_t bpm)
{
    const int pp = p->p[3] != 0u;
    int32_t wow, flutter;
    uint32_t filter, s;
    int16_t *const tape = t->tape;
    fxc_targets(t, fxc_len(p, bpm), 1);
    fxc_degrade(p->p[2], &wow, &flutter, &filter);
    const int32_t inv = 128 - (int32_t)filter, lp_c = 4000 + ((inv * inv * 12700) >> 14);
    const float hp_c = (float)fxc_hp(filter) * (1.f / 32768.f);
    const int32_t nz_k = 8 + (int32_t)filter;
    const int32_t gap_tgt = t->gap_tgt_q16, speed_tgt = t->speed_tgt_q24;
    const int32_t wow_q8 = wow * WOW_STEP_Q8, flutter_q8 = flutter * FLUTTER_STEP_Q8;
    const fxc_fb_t fb = fxc_feedback(p->p[1], p->p[2]);
    int32_t run_lp = t->run_lp;
    uint32_t wpos = t->wpos_q16, wow_ph = t->wow_ph, flutter_ph = t->flutter_ph, rng = t->bb_rng;
    int32_t gap = t->gap_q16, speed = t->speed_q24;
    int32_t aa1 = t->bb_aa1, aa2 = t->bb_aa2, rc1 = t->bb_rc1, rc2 = t->bb_rc2, env = t->bb_env;
    int32_t rc1b = t->bb_rc1b, rc2b = t->bb_rc2b;
    float hp = t->bb_hp, hpb = t->bb_hpb;
    for (s = 0; s < n; s++) {
        gap += (gap_tgt - gap) >> TAPE_SLEW;
        speed += (speed_tgt - speed) >> TAPE_SLEW;
        int32_t speed_q16 = speed >> 8;
        wow_ph += WOW_INC;
        flutter_ph += FLUTTER_INC;
        int32_t mod_q8 = (fxc_sine(wow_ph) * wow_q8 + fxc_sine(flutter_ph) * flutter_q8) >> 15;
        int32_t mod_q16 = (mod_q8 * speed_q16) >> 8;
        int32_t play = tape[((wpos - (uint32_t)gap - (uint32_t)mod_q16) >> 16) & TAPE_MASK];
        rc1 += dn_fx_mz(play - rc1, lp_c);
        rc2 += dn_fx_mz(rc1 - rc2, lp_c);
        hp += hp_c * ((float)rc2 - hp);
        int32_t o = rc2 - (int32_t)hp;
        if (pp) {
            int32_t play2 = tape[((wpos - ((uint32_t)gap >> 1) - (uint32_t)mod_q16) >> 16) & TAPE_MASK];
            rc1b += dn_fx_mz(play2 - rc1b, lp_c);
            rc2b += dn_fx_mz(rc1b - rc2b, lp_c);
            hpb += hp_c * ((float)rc2b - hpb);
            out_l[s] = rc2b - (int32_t)hpb;
            out_r[s] = o;
        } else {
            out_l[s] = out_r[s] = o;
        }
        env += ((o < 0 ? -o : o) - env) >> 7;
        rng = rng * 1664525u + 1013904223u;
        int32_t noise = dn_fx_mz((int32_t)(int16_t)(rng >> 16), (env * nz_k) >> 10);
        int32_t fbs = o;
        if (fb.runaway) {
            run_lp += dn_fx_mz(o - run_lp, fb.run_c);
            fbs = run_lp;
            noise += (int32_t)(int16_t)(rng >> 16) >> 9;
        }
        int32_t rec = fxc_sat(in[s] + dn_fx_mz(fbs, fb.lo) + dn_fx_mz(fbs, fb.hi) + noise);
        aa1 += dn_fx_mz(rec - aa1, lp_c);
        aa2 += dn_fx_mz(aa1 - aa2, lp_c);
        uint32_t old_cell = wpos >> 16;
        wpos += (uint32_t)speed_q16;
        if ((wpos >> 16) != old_cell)
            tape[(wpos >> 16) & TAPE_MASK] = (int16_t)dn_fx_c16(aa2);
    }
    t->wpos_q16 = wpos, t->wow_ph = wow_ph, t->flutter_ph = flutter_ph, t->gap_q16 = gap, t->speed_q24 = speed;
    t->bb_rng = rng, t->bb_aa1 = aa1, t->bb_aa2 = aa2, t->bb_rc1 = rc1, t->bb_rc2 = rc2;
    t->bb_env = env, t->bb_rc1b = rc1b, t->bb_rc2b = rc2b, t->run_lp = run_lp;
    t->bb_hp = hp < 1e-3f && hp > -1e-3f ? 0.f : hp, t->bb_hpb = hpb < 1e-3f && hpb > -1e-3f ? 0.f : hpb;
}

/* ---- the reverbs' front: LOW CUT (a one-pole high-pass, off .. 800 Hz), then PREDELAY (35 samples a step) ---- */
static void fxc_front(dn_fxf_t *f, const dn_fx_p_t *p, const int32_t *in, int32_t *x, uint32_t n)
{
    uint32_t s, pre = (uint32_t)p->p[2] * 35u, w = f->pre_w;
    uint32_t lc = p->p[3];
    if (lc) {                                       /* (float: an integer one-pole this slow sticks a few LSB off) */
        float hz = 30.f + (float)(lc * lc) * (770.f / 16129.f), c = hz * (6.2831853f / 44100.f);
        float h = f->lc;
        for (s = 0; s < n; s++) {
            h += c * ((float)in[s] - h);
            x[s] = in[s] - (int32_t)h;
        }
        f->lc = h < 1e-3f && h > -1e-3f ? 0.f : h;
    } else {
        for (s = 0; s < n; s++)
            x[s] = in[s];
        f->lc = 0.f;
    }
    if (!pre)
        return;
    for (s = 0; s < n; s++) {
        uint32_t rd = w + PRE_LEN - pre;
        if (rd >= PRE_LEN)
            rd -= PRE_LEN;
        int32_t o = f->pre[rd];
        f->pre[w] = (int16_t)dn_fx_c16(x[s]);
        if (++w >= PRE_LEN)
            w = 0;
        x[s] = o;
    }
    f->pre_w = w;
}

/* ---- the tank: MOD, CELESTIAL, DOLMEN, TUMULUS ---------------------------------------------------------------------
 * Dattorro's figure-8 tank (JAES 45, 1997, after Griesinger's Lexicon designs) at 1.3x its 29.761 kHz lengths: the
 * send through 4 input allpasses into two halves, each a swept allpass, a delay, a damping low-pass, the decay, a
 * second allpass and a delay, each half's end feeding the other; seven taps a side. Every line can be read short of
 * its length (size), so one set of lines makes the spaces: MOD full size, its swept allpasses moving +-12 samples on
 * a slow spin plus a random wander (the 224 / 480L chorused tail); DOLMEN 0.4 size, still, dense, bright (a small
 * chamber of standing stones); TUMULUS full size, still, long and dark (the cavern under the mound at Carnac);
 * CELESTIAL MOD's tank with an octave-up shifter in its feedback (shimmer). All lines int16 in one ring under one
 * pointer that falls by one a sample (Keith Barr's FV-1 layout). A guard ducks the loop near full scale. */
#define MR_MASK (MR_RING - 1u)
#define MR_L(n) ((uint32_t)((n) * 13u + 5u) / 10u)
#define MR_EXC 12
#define MR_B_IN1 MR_L(142)
#define MR_B_IN2 MR_L(107)
#define MR_B_IN3 MR_L(379)
#define MR_B_IN4 MR_L(277)
#define MR_B_AP1L MR_L(672)
#define MR_B_D1L MR_L(4453)
#define MR_B_AP2L MR_L(1800)
#define MR_B_D2L MR_L(3720)
#define MR_B_AP1R MR_L(908)
#define MR_B_D1R MR_L(4217)
#define MR_B_AP2R MR_L(2656)
#define MR_B_D2R MR_L(3163)
#define MR_SWEEP (2u * MR_EXC + 2u)
#define MR_S_IN1 0u
#define MR_S_IN2 (MR_S_IN1 + MR_B_IN1 + 1u)
#define MR_S_IN3 (MR_S_IN2 + MR_B_IN2 + 1u)
#define MR_S_IN4 (MR_S_IN3 + MR_B_IN3 + 1u)
#define MR_S_AP1L (MR_S_IN4 + MR_B_IN4 + 1u)
#define MR_S_D1L (MR_S_AP1L + MR_B_AP1L + MR_SWEEP + 1u)
#define MR_S_AP2L (MR_S_D1L + MR_B_D1L + 1u)
#define MR_S_D2L (MR_S_AP2L + MR_B_AP2L + 1u)
#define MR_S_AP1R (MR_S_D2L + MR_B_D2L + 1u)
#define MR_S_D1R (MR_S_AP1R + MR_B_AP1R + MR_SWEEP + 1u)
#define MR_S_AP2R (MR_S_D1R + MR_B_D1R + 1u)
#define MR_S_D2R (MR_S_AP2R + MR_B_AP2R + 1u)
#define MR_S_END (MR_S_D2R + MR_B_D2R + 1u)
typedef char fxc_tank_fits[MR_S_END <= MR_RING ? 1 : -1];
#define MR_CHUNK 16u
#define MR_DUCK_AT 16000
#define MR_SHIMMER 19661
#define MR_IN_GAIN 8192
#define MR_OUT_SH 12
#define MR_OUT_GAIN 19661
#define MR_RATE_L 38956u
#define MR_RATE_R 53565u
#define MR_SZ(b, size) ((((b) * (size)) >> 8) ? (((b) * (size)) >> 8) : 1u)
#define MR_RD(s, d) ((int32_t)rg[(p + (s) + (d)) & MR_MASK])
#define MR_WR(s, v) (rg[(p + (s)) & MR_MASK] = (int16_t)dn_fx_c16(v))

static inline __attribute__((always_inline)) int32_t fxc_shift(int16_t *buf, dn_fxsh_t *st, int32_t x)
{
    const uint32_t mask = MR_SH_LEN - 1u, half = MR_SH_LEN / 2u;
    uint32_t w = st->w, c = st->c;
    st->aa1 += dn_fx_mz(x - st->aa1, 18000);
    st->aa2 += dn_fx_mz(st->aa1 - st->aa2, 18000);
    buf[w] = (int16_t)dn_fx_c16(st->aa2);
    int32_t a = buf[(w - (mask - c)) & mask], b = buf[(w - (mask - ((c + half) & mask))) & mask];
    int32_t ga = (int32_t)(c < half ? c : MR_SH_LEN - c) * (32768 / (int32_t)half);
    st->w = (w + 1u) & mask;
    st->c = (c + 1u) & mask;
    int32_t y = b + (((a - b) * ga) >> 15);
    st->hp += dn_fx_mz(y - st->hp, 900);
    st->lp += dn_fx_mz(y - st->hp - st->lp, 14000);
    return st->lp;
}
static inline int32_t fxc_dc(int32_t *s, int32_t x)
{
    *s += (((x << 8) - *s) * 93) >> 15;
    return x - (*s >> 8);
}
static inline __attribute__((always_inline)) int32_t fxc_ap(int16_t *rg, uint32_t p, uint32_t s, uint32_t d, int32_t x,
                                                            int32_t g)
{
    int32_t dl = MR_RD(s, d), v = dn_fx_c16(x + dn_fx_mz(dl, g));
    MR_WR(s, v);
    return dl - dn_fx_mz(v, g);
}
static inline __attribute__((always_inline)) int32_t fxc_ap_swept(int16_t *rg, uint32_t p, uint32_t s, uint32_t d,
                                                                  int32_t e, int32_t x, int32_t g)
{
    int32_t pos_q15 = (int32_t)((d + MR_EXC) << 15) + e * MR_EXC;
    uint32_t di = (uint32_t)pos_q15 >> 15;
    int32_t fr = pos_q15 & 0x7FFF, a = MR_RD(s, di), b = MR_RD(s, di + 1u);
    int32_t dl = a + (((b - a) * fr) >> 15), v = dn_fx_c16(x + dn_fx_mz(dl, g));
    MR_WR(s, v);
    return dl - dn_fx_mz(v, g);
}

static inline __attribute__((always_inline)) void fxc_tank_run(dn_fxk_t *k, int32_t *x, int32_t *r, uint32_t n,
                                                               int32_t decay, int32_t damp, int moving, int celestial,
                                                               const uint32_t size)
{
    int16_t *const rg = k->ring;
    uint32_t p = k->p, chunk = k->wander_n, s;
    int32_t lp_l = k->lp_l, lp_r = k->lp_r, mod_l = k->mod_l, mod_r = k->mod_r, duck = k->duck;
    int32_t dg = k->duck_g, dg_step = k->duck_step, dec = dn_fx_mz(decay, dg), dc_l = k->dc_l, dc_r = k->dc_r;
    dn_fxsh_t sh = k->sh;
    if (!moving)
        mod_l = mod_r = 0;
    for (s = 0; s < n; s++) {
        if (dg_step) {
            dg += dg_step;
            dec = dn_fx_mz(decay, dg);
        }
        /* the input: the send, four allpasses diffusing it (0.75, 0.75, 0.625, 0.625), ducked by the guard */
        int32_t in = dn_fx_mz(x[s], MR_IN_GAIN);
        in = fxc_ap(rg, p, MR_S_IN1, MR_SZ(MR_B_IN1, size), in, 24576);
        in = fxc_ap(rg, p, MR_S_IN2, MR_SZ(MR_B_IN2, size), in, 24576);
        in = fxc_ap(rg, p, MR_S_IN3, MR_SZ(MR_B_IN3, size), in, 20480);
        in = fxc_ap(rg, p, MR_S_IN4, MR_SZ(MR_B_IN4, size), in, 20480);
        in = dn_fx_mz(in, dg);
        int32_t from_r = MR_RD(MR_S_D2R, MR_SZ(MR_B_D2R, size)), from_l = MR_RD(MR_S_D2L, MR_SZ(MR_B_D2L, size));
        if ((chunk++ & (MR_CHUNK - 1u)) == 0u) {    /* once a chunk: the guard, the modulation */
            int32_t pk = from_l < 0 ? -from_l : from_l, pr = from_r < 0 ? -from_r : from_r;
            if (pr > pk)
                pk = pr;
            if (pk > MR_DUCK_AT)
                duck -= duck >> 4;
            else if (duck < 32767)
                duck += ((32767 - duck) >> 8) + 1;
            if (duck > 32767)
                duck = 32767;
            dg_step = (duck - dg) >> 4;
            if (dg_step == 0 && dg != duck) {
                dg = duck;
                dec = dn_fx_mz(decay, dg);
            }
            if (moving) {
                k->ph_l += MR_RATE_L * MR_CHUNK;
                k->ph_r += MR_RATE_R * MR_CHUNK;
                if (chunk >= 4410u) {
                    int j;
                    chunk = 1;
                    for (j = 0; j < 2; j++) {
                        k->rng = k->rng * 1103515245u + 12345u;
                        k->wander_tgt[j] = (int16_t)(k->rng >> 16);
                    }
                }
                k->wander[0] += (k->wander_tgt[0] - k->wander[0]) >> 9;
                k->wander[1] += (k->wander_tgt[1] - k->wander[1]) >> 9;
                mod_l = (fxc_sine(k->ph_l) + k->wander[0]) >> 1;
                mod_r = (fxc_sine(k->ph_r) + k->wander[1]) >> 1;
            }
        }
        int32_t shim = 0;
        if (celestial)
            shim = dn_fx_mz(fxc_shift(k->sh_buf, &sh, (from_l + from_r) >> 1), MR_SHIMMER);
        int32_t a = fxc_ap_swept(rg, p, MR_S_AP1L, MR_SZ(MR_B_AP1L, size), mod_l, in + shim + dn_fx_mz(from_r, dec),
                                 -22938);
        int32_t o = MR_RD(MR_S_D1L, MR_SZ(MR_B_D1L, size));
        MR_WR(MR_S_D1L, a);
        lp_l = o + dn_fx_mz(lp_l - o, damp);
        MR_WR(MR_S_D2L, fxc_ap(rg, p, MR_S_AP2L, MR_SZ(MR_B_AP2L, size), dn_fx_mz(fxc_dc(&dc_l, lp_l), dec), 16384));
        int32_t b = fxc_ap_swept(rg, p, MR_S_AP1R, MR_SZ(MR_B_AP1R, size), mod_r, in + shim + dn_fx_mz(from_l, dec),
                                 -22938);
        o = MR_RD(MR_S_D1R, MR_SZ(MR_B_D1R, size));
        MR_WR(MR_S_D1R, b);
        lp_r = o + dn_fx_mz(lp_r - o, damp);
        MR_WR(MR_S_D2R, fxc_ap(rg, p, MR_S_AP2R, MR_SZ(MR_B_AP2R, size), dn_fx_mz(fxc_dc(&dc_r, lp_r), dec), 16384));
#define MR_TAP(S, t) MR_RD(S, MR_SZ(MR_L(t), size) - 1u)
        int32_t yl = MR_TAP(MR_S_D1R, 266) + MR_TAP(MR_S_D1R, 2974) - MR_TAP(MR_S_AP2R, 1913) +
                     MR_TAP(MR_S_D2R, 1996) - MR_TAP(MR_S_D1L, 1990) - MR_TAP(MR_S_AP2L, 187) - MR_TAP(MR_S_D2L, 1066);
        int32_t yr = MR_TAP(MR_S_D1L, 353) + MR_TAP(MR_S_D1L, 3627) - MR_TAP(MR_S_AP2L, 1228) +
                     MR_TAP(MR_S_D2L, 2673) - MR_TAP(MR_S_D1R, 2111) - MR_TAP(MR_S_AP2R, 335) - MR_TAP(MR_S_D2R, 121);
#undef MR_TAP
        x[s] = ((yl >> 2) * MR_OUT_GAIN) >> MR_OUT_SH;
        r[s] = ((yr >> 2) * MR_OUT_GAIN) >> MR_OUT_SH;
        p = (p - 1u) & MR_MASK;
    }
    k->p = p, k->lp_l = lp_l, k->lp_r = lp_r, k->mod_l = mod_l, k->mod_r = mod_r, k->duck = duck;
    k->duck_g = dg, k->duck_step = dg_step, k->dc_l = dc_l, k->dc_r = dc_r, k->wander_n = chunk, k->sh = sh;
}

static void fxc_tank(dn_fxk_t *k, uint32_t type, const dn_fx_p_t *p, int32_t *x, int32_t *r, uint32_t n)
{
    int32_t kt = p->p[0] > 127u ? 127 : p->p[0], kd = p->p[1] > 127u ? 127 : p->p[1];
    switch (type) {
    case DN_FX_TUMULUS:                             /* the cavern: long and dark */
        fxc_tank_run(k, x, r, n, 16000 + kt * 128, 6000 + kd * 150, 0, 0, 256u);
        break;
    case DN_FX_DOLMEN:                              /* the cave: small, dense, the stone bright */
        fxc_tank_run(k, x, r, n, 14800 + kt * 95, kd * 120, 0, 0, 102u);
        break;
    case DN_FX_CELEST:                              /* the shimmer feeds the loop too: a shorter decay */
        fxc_tank_run(k, x, r, n, 8000 + kt * 185, kd * 200, 1, 1, 256u);
        break;
    default:                                        /* MOD */
        fxc_tank_run(k, x, r, n, 8000 + kt * 190, kd * 200, 1, 0, 256u);
        break;
    }
}

/* ---- SPRING: two springs after the dispersive models of spring tanks (Valimaki, Parker and Abel, JAES 2010): each a
 * loop of one trip's delay, a low-pass (DAMP), the decay (TIME) and 12 stretched allpasses (a + z^-3) / (1 + a z^-3),
 * which delay the lows more than the highs: every echo a falling chirp. Trips of 34.7 and 43.2 ms, one a side. The
 * loop is float (in int each stage's rounding kept a faint tone for ever); the lines int16, truncated toward zero. */
#define SP_MASK (SP_RING - 1u)
#define SP_D1 1531u
#define SP_D2 1907u
#define SP_S2 (SP_D1 + 1u)
#define SP_K 3
#define SP_A (-0.78f)
#define SP_IN_GAIN 0.2f
#define SP_OUT_GAIN 0.69f
#define SP_HP_C 0.0233f
#define SP_TINY 1e-4f
typedef char fxc_spring_fits[SP_S2 + SP_D2 + 1u <= SP_RING && SP_D1 >= DN_FX_BLK ? 1 : -1];
static inline float fxc_flush(float v) { return v < SP_TINY && v > -SP_TINY ? 0.f : v; }
static inline __attribute__((always_inline)) void fxc_pair(float *b, uint32_t n, float (*h)[SP_K])
{
    float x1 = h[0][0], x2 = h[0][1], x3 = h[0][2], m1 = h[1][0], m2 = h[1][1], m3 = h[1][2];
    float y1 = h[2][0], y2 = h[2][1], y3 = h[2][2];
    uint32_t s;
    for (s = 0; s < n; s++) {
        float x = b[s], m = x3 + SP_A * (x - m3), y = m3 + SP_A * (m - y3);
        x3 = x2, x2 = x1, x1 = x;
        m3 = m2, m2 = m1, m1 = m;
        y3 = y2, y2 = y1, y1 = y;
        b[s] = y;
    }
    h[0][0] = fxc_flush(x1), h[0][1] = fxc_flush(x2), h[0][2] = fxc_flush(x3);
    h[1][0] = fxc_flush(m1), h[1][1] = fxc_flush(m2), h[1][2] = fxc_flush(m3);
    h[2][0] = fxc_flush(y1), h[2][1] = fxc_flush(y2), h[2][2] = fxc_flush(y3);
}
static void fxc_spring_one(dn_fxs_t *sp, dn_fxspr_t *o1, const int32_t *in, float *out, uint32_t n, uint32_t start,
                           uint32_t d, float decay, float lp_c)
{
    float b[DN_FX_BLK], lp = o1->lp;
    uint32_t p = sp->p, s, k;
    for (s = 0; s < n; s++) {
        float o = (float)sp->ring[(p - s + start + d) & SP_MASK];
        out[s] = o;
        lp += lp_c * (o - lp);
        b[s] = (float)in[s] + decay * lp;
    }
    o1->lp = fxc_flush(lp);
    for (k = 0; k < DN_SP_STAGES; k += 2u)
        fxc_pair(b, n, o1->h[k / 2u]);
    for (s = 0; s < n; s++)
        sp->ring[(p - s + start) & SP_MASK] = (int16_t)dn_fx_c16((int32_t)b[s]);
}
static void fxc_spring(dn_fxs_t *sp, const dn_fx_p_t *p, int32_t *x, int32_t *r, uint32_t n)
{
    int32_t kt = p->p[0] > 127u ? 127 : p->p[0], kd = p->p[1] > 127u ? 127 : p->p[1];
    float decay = 0.52f + (float)kt * (0.44f / 127.f), lp_c = 0.51f - (float)kd * (0.32f / 127.f);
    float o1[DN_FX_BLK], o2[DN_FX_BLK], hp = sp->hp;
    uint32_t s;
    for (s = 0; s < n; s++) {                       /* the front: high-passed (a tank has little bass), scaled in */
        float v = (float)x[s];
        hp += SP_HP_C * (v - hp);
        x[s] = (int32_t)((v - hp) * SP_IN_GAIN);
    }
    sp->hp = fxc_flush(hp);
    fxc_spring_one(sp, &sp->sp[0], x, o1, n, 0u, SP_D1, decay, lp_c);
    fxc_spring_one(sp, &sp->sp[1], x, o2, n, SP_S2, SP_D2, decay, lp_c);
    sp->p = (sp->p - n) & SP_MASK;
    for (s = 0; s < n; s++) {
        x[s] = (int32_t)((o1[s] * 3.f + o2[s]) * SP_OUT_GAIN);
        r[s] = (int32_t)((o2[s] * 3.f + o1[s]) * SP_OUT_GAIN);
    }
}

/* ---- TOMB: the Felucca firmware's ROOM (4 damped combs into 2 allpasses, Freeverb-style), mono. Its multiplies
 * round toward zero and the allpasses' feedback halves toward zero: a tail ends in exact silence (no whine). */
static void fxc_tomb(dn_fxr_t *rm, const dn_fx_p_t *p, int32_t *x, int32_t *r, uint32_t n)
{
    const int32_t size = 25000 + (int32_t)p->p[0] * 50, damp = (int32_t)p->p[1] * 200;
    int16_t *const c0 = rm->comb, *const c1 = c0 + RM_COMB[0], *const c2 = c1 + RM_COMB[1], *const c3 = c2 + RM_COMB[2];
    int16_t *const a0 = rm->ap, *const a1 = a0 + RM_AP[0];
    uint32_t i0 = rm->ci[0], i1 = rm->ci[1], i2 = rm->ci[2], i3 = rm->ci[3], j0 = rm->ai[0], j1 = rm->ai[1], s;
    int32_t l0 = rm->clp[0], l1 = rm->clp[1], l2 = rm->clp[2], l3 = rm->clp[3];
    for (s = 0; s < n; s++) {
        int32_t in = dn_fx_mz(x[s], 2580), a = 0, o;
#define RM_COMB_(c, i, lp, len)                                                                                        \
    o = c[i];                                                                                                          \
    lp = o + dn_fx_mz(lp - o, damp);                                                                                   \
    c[i] = (int16_t)dn_fx_c16(in + dn_fx_mz(lp, size));                                                                \
    if (++i >= (len))                                                                                                  \
        i = 0;                                                                                                         \
    a += o;
        RM_COMB_(c0, i0, l0, 1116u)
        RM_COMB_(c1, i1, l1, 1188u)
        RM_COMB_(c2, i2, l2, 1277u)
        RM_COMB_(c3, i3, l3, 1356u)
#undef RM_COMB_
        a = (a + ((a >> 31) & 1)) >> 1;
#define RM_AP_(c, j, len)                                                                                              \
    o = c[j];                                                                                                          \
    c[j] = (int16_t)dn_fx_c16(a + ((o + ((o >> 31) & 1)) >> 1));                                                       \
    a = o - a;                                                                                                         \
    if (++j >= (len))                                                                                                  \
        j = 0;
        RM_AP_(a0, j0, 556u)
        RM_AP_(a1, j1, 441u)
#undef RM_AP_
        x[s] = a * 2;
        r[s] = a * 2;
    }
    rm->ci[0] = i0, rm->ci[1] = i1, rm->ci[2] = i2, rm->ci[3] = i3, rm->ai[0] = j0, rm->ai[1] = j1;
    rm->clp[0] = l0, rm->clp[1] = l1, rm->clp[2] = l2, rm->clp[3] = l3;
}

void dn_fxc_run(dn_fx_s_t *s, const dn_fx_p_t *p, const int32_t *mono, int32_t *wl, int32_t *wr, uint32_t n,
                uint32_t bpm)
{
    switch (s->type) {
    case DN_FX_TAPE:
        fxc_tape(&s->u.t, p, mono, wl, wr, n, bpm);
        break;
    case DN_FX_BBD:
        fxc_bbd(&s->u.t, p, mono, wl, wr, n, bpm);
        break;
    case DN_FX_SPRING:
        fxc_front(&s->u.s.f, p, mono, wl, n);
        fxc_spring(&s->u.s, p, wl, wr, n);
        break;
    case DN_FX_TOMB:
        fxc_front(&s->u.r.f, p, mono, wl, n);
        fxc_tomb(&s->u.r, p, wl, wr, n);
        break;
    default:
        fxc_front(&s->u.k.f, p, mono, wl, n);
        fxc_tank(&s->u.k, s->type, p, wl, wr, n);
        break;
    }
}
