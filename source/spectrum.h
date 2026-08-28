#ifndef HOLO_SPECTRUM_H
#define HOLO_SPECTRUM_H

/* Wavelengths, and the two ends of a spectral render: how a material
 * responds at one wavelength, and how a pile of single-wavelength
 * intensities becomes an sRGB pixel.
 *
 * The tracer samples HOLO_WAVELENGTHS fixed wavelengths across the visible
 * band -- fixed, not random, because the CPU and GPU must trace exactly the
 * same rays for the oracle diff to mean anything. Each sample traces the
 * whole scene with n(lambda) in the glass; the results are weighted by the
 * CIE 1931 color matching functions (the Wyman-Sloan-Shirley analytic fits)
 * and mapped to sRGB, normalized so a flat spectrum lands on exact white --
 * hue structure from the human eye, white point from the engine.
 *
 * Scene colors stay RGB: an albedo is read at a wavelength through three
 * smooth bands (holo_albedo_at). That round trip is not colorimetry -- a
 * red albedo will not survive to the exact same red -- but neutral colors
 * are exact, and the physics being showcased (dispersion angles) does not
 * pass through it at all.
 */

#include "linalg.h"

#define HOLO_WAVELENGTHS 12

/* Sample i's wavelength in micrometers, evenly spaced 0.42 to 0.68. */
float holo_lambda(int i);

/* The CIE-derived sRGB weight of sample i: sum(I_i * weight_i) is the
   pixel. The weights sum to exactly (1,1,1) across i. */
HoloV3 holo_spectral_weight(int i);

/* A material's (or the sky's) reflectance at one wavelength, read from its
   RGB color through three smooth bands. Neutral colors are exact. */
float holo_albedo_at(HoloV3 rgb, float lambda_um);

/* Cauchy dispersion: the index at lambda for a glass quoted as ior at the
   sodium D line (0.5893 um) with coefficient B in um^2 -- so n(D) == ior
   for every B, and B = 0 is achromatic glass. BK7 is roughly ior 1.5168,
   B 0.0042; dense flints run several times that. */
float holo_ior_at(float ior_d, float cauchy_b, float lambda_um);

#endif
