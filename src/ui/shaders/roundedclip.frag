#version 440

// RoundedClip's layer drawn with anti-aliased rounded corners, the coverage
// worked out here (as in roundedcover.frag) instead of read from a second,
// mask layer.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // The item's size and corner radius, in item units.
    vec2 itemSize;
    float cornerRadius;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    const vec4 color = texture(source, qt_TexCoord0);

    // Signed distance to the rounded rectangle; its screen-space rate of
    // change gives a one-pixel soft edge at any scale.
    const vec2 point = (qt_TexCoord0 - 0.5) * itemSize;
    const vec2 corner = abs(point) - (itemSize * 0.5 - vec2(cornerRadius));
    const float distance = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    const float edge = max(fwidth(distance), 1e-4);
    const float coverage = clamp(0.5 - distance / edge, 0.0, 1.0);

    fragColor = color * (coverage * qt_Opacity);
}
