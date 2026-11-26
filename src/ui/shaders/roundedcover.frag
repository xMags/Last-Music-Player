#version 440

// A cover picture filling its item like Image.PreserveAspectCrop, with
// anti-aliased rounded corners, in one texture read. Drawing the picture
// straight from its own texture avoids the second resample a masked layer
// adds when the item does not sit on whole pixels.

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    // The item's size and corner radius, in item units.
    vec2 itemSize;
    float cornerRadius;
    // The part of the picture shown along each axis (1 = all of it), centred.
    vec2 cropScale;
};

layout(binding = 1) uniform sampler2D source;

void main()
{
    const vec2 uv = (qt_TexCoord0 - 0.5) * cropScale + 0.5;
    const vec4 color = texture(source, uv);

    // Signed distance to the rounded rectangle; its screen-space rate of
    // change gives a one-pixel soft edge at any scale.
    const vec2 point = (qt_TexCoord0 - 0.5) * itemSize;
    const vec2 corner = abs(point) - (itemSize * 0.5 - vec2(cornerRadius));
    const float distance = length(max(corner, 0.0)) + min(max(corner.x, corner.y), 0.0) - cornerRadius;
    const float edge = max(fwidth(distance), 1e-4);
    const float coverage = clamp(0.5 - distance / edge, 0.0, 1.0);

    fragColor = color * (coverage * qt_Opacity);
}
