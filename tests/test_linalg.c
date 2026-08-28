/* The vector arithmetic, held to the algebra -- and the two optical laws
 * that live in linalg.h held to angles worked out by hand: a 30-degree ray
 * into n=1.5 glass refracts to 19.47 degrees because Snell says so, and the
 * same interface run backwards past 41.81 degrees has no transmitted ray at
 * all. If these numbers drift, every image the engine will ever draw is
 * wrong, which is why they are checked here and not eyeballed there.
 *
 *   build.bat test
 */
#include <math.h>
#include <stdio.h>
#include "harness.h"
#include "linalg.h"

static void check_v3(HoloV3 got, float x, float y, float z, const char *what) {
    check_close(got.x, x, what);
    check_close(got.y, y, what);
    check_close(got.z, z, what);
}

static void test_arithmetic(void) {
    printf("linalg: arithmetic identities\n");
    HoloV3 a = hv3(1, 2, 3), b = hv3(4, -5, 6);

    check_v3(hv3_add(a, b), 5, -3, 9, "add");
    check_v3(hv3_sub(a, b), -3, 7, -3, "sub");
    check_v3(hv3_mul(a, b), 4, -10, 18, "mul");
    check_v3(hv3_scale(a, 2), 2, 4, 6, "scale");
    check_close(hv3_dot(a, b), 12.0f, "dot");
    /* cross(a,b) is perpendicular to both and follows the right hand */
    HoloV3 c = hv3_cross(a, b);
    check_v3(c, 27, 6, -13, "cross");
    check_close(hv3_dot(c, a), 0.0f, "cross perp a");
    check_close(hv3_dot(c, b), 0.0f, "cross perp b");
    check_v3(hv3_cross(hv3(1, 0, 0), hv3(0, 1, 0)), 0, 0, 1, "x cross y = z");

    check_close(hv3_len(hv3(3, 4, 0)), 5.0f, "3-4-5 length");
    check_close(hv3_len(hv3_norm(b)), 1.0f, "normalized length");
    check_v3(hv3_lerp(a, b, 0.5f), 2.5f, -1.5f, 4.5f, "lerp midpoint");
}

static void test_reflect(void) {
    printf("linalg: mirror reflection\n");
    /* 45 degrees onto a y-up floor bounces to 45 degrees out. */
    float s = sqrtf(0.5f);
    HoloV3 r = hv3_reflect(hv3(s, -s, 0), hv3(0, 1, 0));
    check_v3(r, s, s, 0, "45 in, 45 out");
    check_close(hv3_len(r), 1.0f, "reflection stays unit");

    /* Straight down bounces straight up. */
    check_v3(hv3_reflect(hv3(0, -1, 0), hv3(0, 1, 0)), 0, 1, 0, "normal incidence");
}

static void test_refract(void) {
    printf("linalg: Snell's law\n");
    HoloV3 n = hv3(0, 1, 0), t;

    /* Air to glass (n = 1.5) at 30 degrees: sin(t) = sin(30)/1.5, so the
       transmitted ray runs at 19.4712 degrees -- x = 1/3, y = -0.942809. */
    HoloV3 d = hv3(sinf(30 * 3.14159265f / 180), -cosf(30 * 3.14159265f / 180), 0);
    check_int(hv3_refract(d, n, 1.0f / 1.5f, &t), 1, "30 deg into glass refracts");
    check_v3(t, 1.0f / 3.0f, -0.942809f, 0, "at 19.47 degrees");
    check_close(hv3_len(t), 1.0f, "transmitted ray is unit");

    /* Normal incidence passes straight through, any eta. */
    check_int(hv3_refract(hv3(0, -1, 0), n, 1.0f / 1.5f, &t), 1, "normal incidence");
    check_v3(t, 0, -1, 0, "goes straight through");

    /* Glass to air: the critical angle is asin(1/1.5) = 41.8103 degrees.
       Just below it a ray still escapes (grazing along the surface); past
       it Snell has no answer and hv3_refract must say TIR. */
    float just_below = 41.0f * 3.14159265f / 180;
    float just_above = 43.0f * 3.14159265f / 180;
    d = hv3(sinf(just_below), -cosf(just_below), 0);
    check_int(hv3_refract(d, n, 1.5f, &t), 1, "41 deg escapes the glass");
    check_close(t.x, 1.5f * sinf(just_below), "at the Snell angle");
    d = hv3(sinf(just_above), -cosf(just_above), 0);
    check_int(hv3_refract(d, n, 1.5f, &t), 0, "43 deg is trapped: TIR");
}

static void test_fresnel(void) {
    printf("linalg: the Fresnel equations\n");
    float rs, rp;

    /* Normal incidence on n = 1.5 glass: both polarizations reflect exactly
       ((n1 - n2) / (n1 + n2))^2 = 4%. */
    holo_fresnel(1.0f, 1.0f, 1.5f, &rs, &rp);
    check_close(rs, 0.04f, "normal incidence rs");
    check_close(rp, 0.04f, "normal incidence rp");

    /* Brewster's angle, atan(1.5) = 56.31 degrees: the p-polarization
       vanishes -- the reflection is perfectly polarized. rs works out to
       0.1479 there, so unpolarized light reflects about 7.4%. */
    float brewster = atanf(1.5f);
    holo_fresnel(cosf(brewster), 1.0f, 1.5f, &rs, &rp);
    check_close(rp, 0.0f, "p vanishes at Brewster");
    check_close(rs, 0.1479f, "s at Brewster");

    /* Reciprocity: the same interface crossed the other way at the paired
       Snell angle reflects identically. */
    float theta_t = asinf(sinf(brewster) / 1.5f);
    float rs2, rp2;
    holo_fresnel(cosf(theta_t), 1.5f, 1.0f, &rs2, &rp2);
    check_close(rs2, rs, "reciprocal rs");
    check_close(rp2, 0.0f, "reciprocal rp");

    /* Past the critical angle from the dense side, everything reflects. */
    holo_fresnel(cosf(43.0f * 3.14159265f / 180), 1.5f, 1.0f, &rs, &rp);
    check_close(rs, 1.0f, "TIR rs");
    check_close(rp, 1.0f, "TIR rp");
}

static void test_grating(void) {
    printf("linalg: the grating equation, conical form\n");
    HoloV3 n = hv3(0, 0, 1), groove = hv3(0, 1, 0), out;

    /* m = 0 is exactly specular, at any skew. */
    HoloV3 d = hv3_norm(hv3(0.3f, 0.4f, -0.85f));
    check_int(holo_grating_order(d, n, groove, 0.0f, &out), 1, "0th propagates");
    HoloV3 spec = hv3_reflect(d, n);
    check_close(out.x, spec.x, "0th is specular x");
    check_close(out.y, spec.y, "y");
    check_close(out.z, spec.z, "z");

    /* The conical invariant: every order keeps the groove component. */
    check_int(holo_grating_order(d, n, groove, 0.31f, &out), 1, "+1 propagates");
    check_close(hv3_dot(out, groove), hv3_dot(d, groove),
                "the groove component is conserved");
    check_close(hv3_len(out), 1.0f, "and the direction stays unit");

    /* Littrow: in-plane at sin(theta) = lambda/2d, order -1 goes straight
       back where it came from -- the alignment every grating lab uses. */
    float sin_lit = 0.31f;   /* lambda/2d */
    HoloV3 lit = hv3(sin_lit, 0, -sqrtf(1 - sin_lit * sin_lit));
    check_int(holo_grating_order(lit, n, groove, -2.0f * sin_lit, &out), 1,
              "Littrow order propagates");
    check_close(out.x, -lit.x, "retroreflected x");
    check_close(out.z, -lit.z, "retroreflected z");

    /* Longer wavelengths diffract harder, and far enough they stop
       propagating at all. */
    HoloV3 out_red;
    HoloV3 axial = hv3(0, 0, -1);
    holo_grating_order(axial, n, groove, 0.45f, &out);
    holo_grating_order(axial, n, groove, 0.65f, &out_red);
    check(out_red.x > out.x, "red leaves at the wider angle");
    check_int(holo_grating_order(axial, n, groove, 1.1f, &out), 0,
              "past 90 degrees the order is evanescent");
}

int main(void) {
    test_arithmetic();
    test_reflect();
    test_refract();
    test_fresnel();
    test_grating();
    return report();
}
