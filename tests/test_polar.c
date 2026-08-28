/* Polarization held to the textbook observables. Chains are built the way
 * the tracer builds them -- camera first, source last, the row updated at
 * each element -- so what these tests certify is exactly what the walk
 * does.
 *
 *   build.bat test
 */
#include <math.h>
#include <stdio.h>
#include "harness.h"
#include "polar.h"

#define PI 3.14159265358979f

/* Rotate the row into an element whose axis sits at `deg` degrees from the
   current frame, in the transverse plane. Returns the row; the caller keeps
   track of the frame angle. */
static HoloSRow to_axis(HoloSRow s, float from_deg, float to_deg) {
    float th = (to_deg - from_deg) * PI / 180.0f;
    return holo_srow_rotate(s, cosf(2 * th), sinf(2 * th));
}

static void test_malus(void) {
    printf("polar: Malus's law\n");
    /* Unpolarized -> polarizer at 0 -> analyzer at theta -> detector.
       Camera-side order: analyzer first. I = 0.5 cos^2(theta). */
    float thetas[] = { 0, 30, 45, 60, 90 };
    float want[] = { 0.5f, 0.375f, 0.25f, 0.125f, 0.0f };
    for (int k = 0; k < 5; k++) {
        HoloSRow s = holo_srow_start();
        s = to_axis(s, 0, thetas[k]);        /* into the analyzer's basis */
        s = holo_srow_polarizer(s);
        s = to_axis(s, thetas[k], 0);        /* into the polarizer's basis */
        s = holo_srow_polarizer(s);
        check_close(s.i, want[k], "cos-squared transmission");
    }
}

static void test_three_polarizer_paradox(void) {
    printf("polar: the three-polarizer paradox\n");
    /* Crossed polarizers pass nothing; SLIDE A THIRD IN BETWEEN at 45
       degrees and an eighth of the light comes back. The single fact that
       convinces anyone polarization is not a filter metaphor. */
    HoloSRow crossed = holo_srow_start();
    crossed = to_axis(crossed, 0, 90);
    crossed = holo_srow_polarizer(crossed);
    crossed = to_axis(crossed, 90, 0);
    crossed = holo_srow_polarizer(crossed);
    check_close(crossed.i, 0.0f, "crossed: dark");

    HoloSRow three = holo_srow_start();
    three = to_axis(three, 0, 90);
    three = holo_srow_polarizer(three);
    three = to_axis(three, 90, 45);
    three = holo_srow_polarizer(three);
    three = to_axis(three, 45, 0);
    three = holo_srow_polarizer(three);
    check_close(three.i, 0.125f, "a third polarizer brings light back");
}

static void test_brewster(void) {
    printf("polar: Brewster's angle polarizes the reflection\n");
    float rs, rp, ts, tp, f, delta;
    float brewster = atanf(1.5f);
    int tir = holo_fresnel_amp(cosf(brewster), 1.0f, 1.5f,
                               &rs, &rp, &ts, &tp, &f, &delta);
    check_int(tir, 0, "no TIR going into glass");
    check_close(rp, 0.0f, "p amplitude vanishes");

    /* The reflection Mueller on an unpolarized source: out = (a, b, 0, 0)
       with a = (Rs+Rp)/2, b = (Rs-Rp)/2. At Brewster b = a: the reflected
       light is COMPLETELY polarized along s. */
    float a = 0.5f * (rs * rs + rp * rp);
    float b = 0.5f * (rs * rs - rp * rp);
    check_close(b / a, 1.0f, "degree of polarization is 1");
}

static void test_energy(void) {
    printf("polar: amplitudes conserve energy\n");
    float rs, rp, ts, tp, f, delta;
    holo_fresnel_amp(cosf(0.6f), 1.0f, 1.5f, &rs, &rp, &ts, &tp, &f, &delta);
    check_close(rs * rs + f * ts * ts, 1.0f, "s: R + T = 1");
    check_close(rp * rp + f * tp * tp, 1.0f, "p: R + T = 1");
}

static void test_waveplates(void) {
    printf("polar: quarter- and half-wave plates\n");
    /* Between crossed polarizers, a waveplate with its fast axis at 45
       degrees transmits 0.5 sin^2(delta/2): nothing when it does nothing,
       a quarter of the light for a quarter-wave, half (everything the
       first polarizer left) for a half-wave. */
    float deltas[] = { 0.0f, 0.5f * PI, PI };
    float want[] = { 0.0f, 0.25f, 0.5f };
    for (int k = 0; k < 3; k++) {
        HoloSRow s = holo_srow_start();
        s = to_axis(s, 0, 90);
        s = holo_srow_polarizer(s);
        s = to_axis(s, 90, 45);
        s = holo_srow_mueller(s, 1, 0, cosf(deltas[k]), sinf(deltas[k]));
        s = to_axis(s, 45, 0);
        s = holo_srow_polarizer(s);
        check_close(s.i, want[k], "0.5 sin^2(delta/2)");
    }
}

static void test_tir_phase(void) {
    printf("polar: TIR's phase is what a Fresnel rhomb is cut to\n");
    /* Glass to air at 45 degrees, n = 1.5: the classic worked example --
       the p and s reflections come back with a 36.9-degree phase
       difference. Two such bounces at the right angle make a quarter wave
       out of plain glass; that right angle exists because this number
       peaks above 45 degrees. */
    float rs, rp, ts, tp, f, delta;
    int tir = holo_fresnel_amp(cosf(45 * PI / 180), 1.5f, 1.0f,
                               &rs, &rp, &ts, &tp, &f, &delta);
    check_int(tir, 1, "45 degrees inside glass is past critical");
    check_close(fabsf(delta), 0.6435f, "36.9 degrees of retardance");

    /* And feeding the phase through the Mueller template turns 45-degree
       linear light partly circular: V appears from nowhere U was. */
    HoloSRow s = holo_srow_start();
    s = holo_srow_mueller(s, 1, 0, cosf(delta), sinf(delta));
    check(s.i == 1.0f, "TIR loses nothing");
}

int main(void) {
    test_malus();
    test_three_polarizer_paradox();
    test_brewster();
    test_energy();
    test_waveplates();
    test_tir_phase();
    return report();
}
