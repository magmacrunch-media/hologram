/* Wavelengths in, sRGB out, and the dispersion in between. The CIE fits
 * are sanity-checked by shape, the weights by their defining property
 * (a flat spectrum is exact white), and the Cauchy model by the number an
 * optical engineer would reach for first: BK7's Abbe number, which had
 * better come out near 64.
 *
 *   build.bat test
 */
#include <math.h>
#include <stdio.h>
#include "harness.h"
#include "spectrum.h"

static void test_lambdas(void) {
    printf("spectrum: the sample ladder\n");
    check_close(holo_lambda(0), 0.42f, "starts at 420nm");
    check_close(holo_lambda(HOLO_WAVELENGTHS - 1), 0.68f, "ends at 680nm");
    check(holo_lambda(3) < holo_lambda(4), "and climbs");
}

static void test_weights(void) {
    printf("spectrum: flat spectrum, exact white\n");
    HoloV3 sum = hv3(0, 0, 0);
    for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
        sum = hv3_add(sum, holo_spectral_weight(i));
    }
    check_close(sum.x, 1.0f, "weights sum r");
    check_close(sum.y, 1.0f, "weights sum g");
    check_close(sum.z, 1.0f, "weights sum b");

    /* Shape: a short wavelength should push blue, a long one red. */
    HoloV3 blue = holo_spectral_weight(1);   /* ~444nm */
    HoloV3 red = holo_spectral_weight(10);   /* ~656nm */
    check(blue.z > blue.x, "444nm weighs blue over red");
    check(red.x > red.z, "656nm weighs red over blue");
}

static void test_albedo_bands(void) {
    printf("spectrum: albedos read through bands\n");
    /* Neutral colors are exact at every wavelength -- the bands partition
       unity, which is what keeps gray scenes identical in RGB and
       spectral renders. */
    for (int i = 0; i < HOLO_WAVELENGTHS; i++) {
        check_close(holo_albedo_at(hv3(0.6f, 0.6f, 0.6f), holo_lambda(i)),
                    0.6f, "gray is gray at every lambda");
    }
    check_close(holo_albedo_at(hv3(1, 0, 0), 0.65f), 1.0f, "red reflects deep red");
    check_close(holo_albedo_at(hv3(1, 0, 0), 0.44f), 0.0f, "red absorbs blue");
    check_close(holo_albedo_at(hv3(0, 0, 1), 0.44f), 1.0f, "blue reflects blue");
}

static void test_cauchy(void) {
    printf("spectrum: Cauchy dispersion earns its Abbe number\n");
    /* BK7: n_d 1.5168, B 0.0042 um^2. The model must return the quoted
       index at the D line exactly, and the Abbe number
       (n_d - 1) / (n_F - n_C) should land near BK7's catalog 64. */
    float n_d = holo_ior_at(1.5168f, 0.0042f, 0.5893f);
    check_close(n_d, 1.5168f, "the D line is the anchor");

    float n_f = holo_ior_at(1.5168f, 0.0042f, 0.4861f);
    float n_c = holo_ior_at(1.5168f, 0.0042f, 0.6563f);
    check(n_f > n_d && n_d > n_c, "blue bends more than red");
    float abbe = (n_d - 1.0f) / (n_f - n_c);
    check(fabsf(abbe - 64.4f) < 0.5f, "Abbe number lands on BK7's 64");
}

static void test_dispersion_splits(void) {
    printf("spectrum: two wavelengths part ways at one interface\n");
    /* The same 45-degree ray into dispersive glass: the blue transmitted
       ray hugs the normal harder than the red one. This single comparison
       is the whole mechanism behind every fringe M5 draws. */
    float s = sqrtf(0.5f);
    HoloV3 d = hv3(s, -s, 0), n = hv3(0, 1, 0), t_blue, t_red;
    float n_blue = holo_ior_at(1.6f, 0.02f, 0.44f);
    float n_red = holo_ior_at(1.6f, 0.02f, 0.66f);
    hv3_refract(d, n, 1.0f / n_blue, &t_blue);
    hv3_refract(d, n, 1.0f / n_red, &t_red);
    check(n_blue > n_red, "n falls with wavelength");
    check(t_blue.x < t_red.x, "so blue lands nearer the normal");
}

int main(void) {
    test_lambdas();
    test_weights();
    test_albedo_bands();
    test_cauchy();
    test_dispersion_splits();
    return report();
}
