#version 330

// Input vertex attributes (from vertex shader)
in vec2 fragTexCoord;
in vec4 fragColor;

// Input uniform values
uniform sampler2D texture0;
uniform vec4 colDiffuse;

// Custom uniforms
uniform vec2 resolution = vec2(1280.0, 720.0);

// Output fragment color
out vec4 finalColor;

// Gaussian weights
const float weight[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
    // Simple single-pass bloom: Extract bright areas, blur, and add.
    // In a real engine this is typically multi-pass (downsample, blur horizontal, blur vertical, upsample/add).
    // For now, this is a simplified 9-tap blur plus bright-pass in one step.
    
    vec2 tex_offset = 1.0 / resolution; // gets size of single texel
    vec3 result = texture(texture0, fragTexCoord).rgb; // current fragment's contribution
    
    // Bright pass
    vec3 brightColor = result;
    float brightness = dot(brightColor, vec3(0.2126, 0.7152, 0.0722));
    if(brightness < 1.0) {
        brightColor = vec3(0.0);
    }
    
    // Blur bright regions (simple cross pattern)
    vec3 blurColor = brightColor * weight[0];
    for(int i = 1; i < 5; ++i)
    {
        // sample horizontal & vertical
        vec3 h1 = texture(texture0, fragTexCoord + vec2(tex_offset.x * i, 0.0)).rgb;
        if (dot(h1, vec3(0.2126, 0.7152, 0.0722)) >= 1.0) blurColor += h1 * weight[i];
        
        vec3 h2 = texture(texture0, fragTexCoord - vec2(tex_offset.x * i, 0.0)).rgb;
        if (dot(h2, vec3(0.2126, 0.7152, 0.0722)) >= 1.0) blurColor += h2 * weight[i];
        
        vec3 v1 = texture(texture0, fragTexCoord + vec2(0.0, tex_offset.y * i)).rgb;
        if (dot(v1, vec3(0.2126, 0.7152, 0.0722)) >= 1.0) blurColor += v1 * weight[i];
        
        vec3 v2 = texture(texture0, fragTexCoord - vec2(0.0, tex_offset.y * i)).rgb;
        if (dot(v2, vec3(0.2126, 0.7152, 0.0722)) >= 1.0) blurColor += v2 * weight[i];
    }
    
    // Add blurred bright areas to original image
    finalColor = vec4(result + blurColor, 1.0);
}
