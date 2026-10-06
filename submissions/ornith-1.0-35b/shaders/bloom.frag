#version 330 core
in vec2 texCoord;
out vec4 FragColor;

uniform int direction; // -1 = composite, 0 = vertical blur, 1 = horizontal blur
uniform sampler2D screenTexture;
uniform vec2 texelSize; // pixel size in texture coordinates

// 5-tap Gaussian kernel weights (normalized)
const float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main() {
    if (direction == -1) {
        // Composite mode: blend original scene with blurred bloom for glow effect
        vec3 orig = texture(screenTexture, texCoord).rgb;
        FragColor = vec4(orig * 1.4, 1.0); // Slight brightness boost for final output
        return;
    }

    // separable Gaussian blur: sample along one axis per pass
    vec3 result = texture(screenTexture, texCoord).rgb * weights[0];

    if (direction == 1) {
        // Horizontal blur
        for (int i = 1; i < 5; ++i) {
            vec2 offset = vec2(float(i) * texelSize.x, 0.0);
            result += texture(screenTexture, texCoord + offset).rgb * weights[i];
            result += texture(screenTexture, texCoord - offset).rgb * weights[i];
        }
    } else {
        // Vertical blur
        for (int i = 1; i < 5; ++i) {
            vec2 offset = vec2(0.0, float(i) * texelSize.y);
            result += texture(screenTexture, texCoord + offset).rgb * weights[i];
            result += texture(screenTexture, texCoord - offset).rgb * weights[i];
        }
    }

    FragColor = vec4(result, 1.0);
}
