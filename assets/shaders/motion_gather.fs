#version 330

// MOTION BLUR'S GATHER: the reconstruction filter of McGuire, Hennessy, Bukowski and Osman ("A Reconstruction Filter
// for Plausible Motion Blur", I3D 2012). Each pixel gathers taps along the longest streak that can reach it (the
// neighbour tiles), and weighs each tap by whether the tap's own streak, or the pixel's, covers the distance between
// them, and by which of the two is nearer: a nearer tap smears over the pixel by the tap's streak, a farther one shows
// through by the pixel's own. Streaks here are FULL lengths, centred on the pixel, so a pixel's streak reaches half its
// length each way. Embedded (CMakeLists.txt).
//
// Exact where nothing moves: a pixel no streak of half a pixel or more can reach, and an exempt one, is copied as it
// is. An exempt tap is never gathered.

in vec2 uv;
out vec4 o;

uniform sampler2D scene;
uniform sampler2D motion;
uniform sampler2D neighbourMax;
uniform int tile;
uniform int samples;
uniform ivec2 size;

// How sure it is that a distance `a` along the view is NEARER than `b`: 1 at or in front, falling to 0 over a twentieth
// of the nearer's distance behind -- a soft edge that scales with the view, as depth precision does.
float Nearer(float a, float b)
{
    return clamp(1.0 - (a - b)/(0.05*min(a, b) + 1.0e-4), 0.0, 1.0);
}

// Whether a streak reaching `reach` covers a tap `d` away: falling off along it (the cone) ...
float Cone(float d, float reach)
{
    return clamp(1.0 - d/max(reach, 1.0e-4), 0.0, 1.0);
}

// ... or all along it, with a soft end (the cylinder).
float Cylinder(float d, float reach)
{
    return 1.0 - smoothstep(0.95*reach, 1.05*reach, d);
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 colour = texelFetch(scene, p, 0);
    vec4 here = texelFetch(motion, p, 0);
    vec2 longest = texelFetch(neighbourMax, p/tile, 0).xy;
    float reachLongest = 0.5*length(longest);
    if (reachLongest < 0.25 || here.b > 0.5) {
        o = colour;
        return;
    }
    float reachHere = 0.5*length(here.xy);
    // The pixel's own weight: as McGuire's, inversely its streak, so a long streak lets the taps outweigh it.
    float weight = 1.0/max(reachHere, 1.0);
    vec4 sum = colour*weight;
    // Taps across the whole streak, -1 to 1 of its half length, offset a quarter of a step on alternate pixels so the
    // steps do not show as bands.
    float offset = ((p.x + p.y) & 1) == 0 ? 0.25 : -0.25;
    for (int i = 0; i < samples; ++i) {
        float t = ((float(i) + 0.5 + offset)/float(samples))*2.0 - 1.0;
        vec2 at = gl_FragCoord.xy + 0.5*longest*t;
        ivec2 q = clamp(ivec2(floor(at)), ivec2(0), size - 1);
        if (q == p)
            continue;
        vec4 there = texelFetch(motion, q, 0);
        if (there.b > 0.5)
            continue;
        float d = abs(t)*reachLongest;
        float reachThere = 0.5*length(there.xy);
        float w = Nearer(there.a, here.a)*Cone(d, reachThere) + Nearer(here.a, there.a)*Cone(d, reachHere) +
                  Cylinder(d, reachThere)*Cylinder(d, reachHere)*2.0;
        sum += texelFetch(scene, q, 0)*w;
        weight += w;
    }
    o = sum/weight;
}
