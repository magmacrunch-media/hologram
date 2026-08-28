#ifndef HOLO_POLAR_H
#define HOLO_POLAR_H

/* Polarization: Stokes vectors, Mueller matrices, and the trick that makes
 * them affordable.
 *
 * Every source in hologram is unpolarized, so a camera path never needs the
 * full 4x4 Mueller product -- only its first row. Each spectral ray carries
 * that row (a HoloSRow) and a reference frame (the transverse direction its
 * Q axis means), and every optical element updates the row in place:
 * srow' = srow * M. When the path reaches a source of intensity S, the
 * camera sees S * srow.i. Crossed polarizers extinguish because the product
 * of their matrices has a zero corner, not because anything special-cases
 * them.
 *
 * Conventions: frames are unit vectors perpendicular to propagation; Q > 0
 * means polarized along the frame; rotations use double angles computed
 * from dot products (no atan in the hot path); the p amplitude follows the
 * same Fresnel convention linalg.c uses, under which a perfect mirror is a
 * scalar and the handedness bookkeeping is absorbed by keeping the s vector
 * as the frame across every reflection. The tests pin the observables:
 * Malus's law, the three-polarizer paradox, Brewster's polarizing angle,
 * the quarter- and half-wave plates, and the TIR phase a Fresnel rhomb
 * is cut to exploit.
 */

#include "linalg.h"

typedef struct {
    float i, q, u, v;
} HoloSRow;

/* The camera's own row: measure intensity, no analyzer. */
HoloSRow holo_srow_start(void);

/* srow * R(theta), theta given as its double angle (cos2t, sin2t). */
HoloSRow holo_srow_rotate(HoloSRow s, float c2, float s2);

/* srow * M for the interface template
   [a b 0 0; b a 0 0; 0 0 c d; 0 0 -d c] -- Fresnel reflection and
   refraction, retarders, and mirrors are all this shape in their own
   basis. */
HoloSRow holo_srow_mueller(HoloSRow s, float a, float b, float c, float d);

/* srow * M for an ideal linear polarizer along the frame. */
HoloSRow holo_srow_polarizer(HoloSRow s);

HoloSRow holo_srow_scale(HoloSRow s, float k);

/* The double angle rotating `frame` onto `target` about `dir`; all three
   unit, frames perpendicular to dir. */
void holo_frame_rot(HoloV3 frame, HoloV3 target, HoloV3 dir,
                    float *c2, float *s2);

/* Fresnel amplitude coefficients (linalg.c's holo_fresnel squares these).
   Below the critical angle: real rs/tp amplitudes, *f the power-projection
   factor (n2 cos_t)/(n1 cos_i) so that f*ts*ts = 1 - rs*rs, and *delta = 0.
   Past it: returns 1, rs = rp = 1 in magnitude, and *delta = the TIR phase
   difference delta_p - delta_s -- the number a Fresnel rhomb is cut to. */
int holo_fresnel_amp(float cos_i, float n1, float n2,
                     float *rs, float *rp, float *ts, float *tp,
                     float *f, float *delta);

#endif
