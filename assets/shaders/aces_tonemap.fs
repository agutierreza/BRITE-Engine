#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Output fragment color
out vec4 finalColor;

// ACES tonemapping curve fit
vec3 ACESFilm(vec3 x)
{
    float a = 2.51f;
    float b = 0.03f;
    float c = 2.43f;
    float d = 0.59f;
    float e = 0.14f;
    return clamp((x*(a*x+b))/(x*(c*x+d)+e), 0.0, 1.0);
}

void main()
{
    vec4 texelColor = texture(texture0, fragTexCoord);
    vec3 color = texelColor.rgb;
    
    // Apply ACES tonemapping
    color = ACESFilm(color);
    
    // Optional Gamma correction, Raylib might already be handling sRGB output, 
    // but just in case we need it, we can apply 1.0 / 2.2 here.
    // color = pow(color, vec3(1.0 / 2.2));
    
    finalColor = vec4(color, texelColor.a);
}
