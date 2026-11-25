#version 330

// MOTION BLUR'S TILES (McGuire et al. 2012): the longest streak in each tile of tile x tile pixels, found in two passes
// -- across each tile's rows, then down its column -- so each fragment reads `tile` texels and the GPU has many to run;
// then, with pass 2, the longest of each tile's own and its eight neighbours', so a pixel knows a streak can reach it
// from as far as a tile away. An exempt pixel's streak is no streak. Embedded (CMakeLists.txt).

in vec2 uv;
out vec4 o;

uniform sampler2D source;
// 0: source is the motion target; each output texel is the longest of `tile` pixels along a row.
// 1: source is pass 0's; each output texel is the longest of `tile` of them down a column -- one tile.
// 2: source is the tiles; each output texel is the longest of the 3x3 of them around it.
// (Not `step`: that is a built-in function's name, which GLSL ES does not let a variable hide.)
uniform int pass;
uniform int tile;
uniform ivec2 sourceSize;

void main()
{
    ivec2 t = ivec2(gl_FragCoord.xy);
    vec2 longest = vec2(0.0);
    float most = -1.0;
    if (pass < 2) {
        ivec2 along = pass == 0 ? ivec2(1, 0) : ivec2(0, 1);
        ivec2 start = pass == 0 ? ivec2(t.x*tile, t.y) : ivec2(t.x, t.y*tile);
        for (int i = 0; i < tile; ++i) {
            ivec2 p = start + along*i;
            if (p.x >= sourceSize.x || p.y >= sourceSize.y)
                break;
            vec4 m = texelFetch(source, p, 0);
            vec2 streak = m.b > 0.5 ? vec2(0.0) : m.xy;
            float l = dot(streak, streak);
            if (l > most) {
                most = l;
                longest = streak;
            }
        }
    } else {
        for (int y = -1; y <= 1; ++y) {
            for (int x = -1; x <= 1; ++x) {
                ivec2 p = clamp(t + ivec2(x, y), ivec2(0), sourceSize - 1);
                vec2 m = texelFetch(source, p, 0).xy;
                float l = dot(m, m);
                if (l > most) {
                    most = l;
                    longest = m;
                }
            }
        }
    }
    o = vec4(longest, 0.0, 1.0);
}
