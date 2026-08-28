#ifndef HOLO_LINALG_H
#define HOLO_LINALG_H

/* Vectors and the optics that is really just vector arithmetic.
 *
 * Pure functions over floats -- float rather than double deliberately, so the
 * CPU oracle computes in the same precision the GPU will, and an image diff
 * between them measures the tracing, not the word size.
 *
 * Conventions, engine-wide:
 *   - direction vectors are unit length unless a comment says otherwise
 *   - surface normals point out of the surface, against the arriving ray
 *   - hv3_refract's eta is n_from / n_to, and the incoming direction points
 *     INTO the surface (as a traced ray does), not away from it
 */

typedef struct {
    float x, y, z;
} HoloV3;

HoloV3 hv3(float x, float y, float z);
HoloV3 hv3_add(HoloV3 a, HoloV3 b);
HoloV3 hv3_sub(HoloV3 a, HoloV3 b);
HoloV3 hv3_mul(HoloV3 a, HoloV3 b);      /* component-wise, for colors */
HoloV3 hv3_scale(HoloV3 a, float s);
float  hv3_dot(HoloV3 a, HoloV3 b);
HoloV3 hv3_cross(HoloV3 a, HoloV3 b);
float  hv3_len(HoloV3 a);
HoloV3 hv3_norm(HoloV3 a);               /* a itself if its length is ~0 */
HoloV3 hv3_lerp(HoloV3 a, HoloV3 b, float t);

/* Mirror reflection: d arrives at a surface with normal n, the result leaves
   it. r = d - 2(d.n)n, and |r| = |d|. */
HoloV3 hv3_reflect(HoloV3 d, HoloV3 n);

/* Snell's law. d arrives (unit, pointing into the surface), n is the surface
   normal on d's side (n.d < 0), eta = n_from / n_to. Writes the transmitted
   direction (unit) and returns 1 -- or returns 0 on total internal
   reflection, when Snell has no answer and the caller must reflect. */
int hv3_refract(HoloV3 d, HoloV3 n, float eta, HoloV3 *out);

#endif
