// GBA LCD Screen Simulation Shader
// Simulates the characteristic look of the Game Boy Advance LCD display:
// - Sub-pixel grid structure (RGB stripe pattern of the GBA LCD)
// - Greenish/grayish color tint
// - Muted color response
// - Vignette (edge darkening)
//
// Settings:
//   u_setting.x = Grid intensity (0.0 = off, 1.0 = full grid)
//   u_setting.y = Tint strength (0.0 = off, 1.0 = full GBA tint)

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform vec2 u_texelDelta;
uniform vec2 u_pixelDelta;
uniform vec4 u_setting;

void main()
{
	// Sample the input pixel
	vec4 color = texture2D(sampler0, v_texcoord0);

	// === 1. GBA Color Tint Matrix ===
	// The GBA LCD had a distinct greenish-gray tint.
	// Apply a color transform to simulate the GBA screen's color response.
	// GBA colors were slightly muted with a green bias.
	vec3 gbaTint = vec3(
		color.r * 0.88 + color.g * 0.04 + color.b * 0.03,
		color.r * 0.05 + color.g * 0.92 + color.b * 0.04,
		color.r * 0.02 + color.g * 0.06 + color.b * 0.78
	);
	// Slight brightness boost to simulate GBA's backlight behavior
	float brightness = 1.05;
	gbaTint = gbaTint * brightness;

	// Mix tint based on setting
	vec3 tinted = mix(color.rgb, gbaTint, u_setting.y);

	// === 2. Muted Color Curve ===
	// GBA had less saturated colors than modern displays.
	// Apply a soft desaturation curve.
	float luminance = dot(tinted, vec3(0.299, 0.587, 0.114));
	tinted = mix(tinted, vec3(luminance), u_setting.y * 0.15);

	// === 3. LCD Sub-pixel Grid ===
	// The GBA LCD had visible RGB sub-pixel structure.
	// Draw thin lines between pixels using fractional texel coordinates.
	vec2 texelCoord = v_texcoord0 / u_texelDelta;
	vec2 gridPos = fract(texelCoord);
	// Grid lines at pixel boundaries: |x-0.5| < threshold for each pixel
	vec2 gridLines = abs(gridPos - 0.5);
	vec2 gridThreshold = vec2(0.48, 0.48);  // Width of visible pixel area
	vec2 gridMask = step(gridLines, gridThreshold);
	float pixelVisible = gridMask.x * gridMask.y;

	// Invert: grid lines = dark lines between pixels
	float gridDarkening = mix(0.4, 1.0, pixelVisible);

	// Sub-pixel RGB stripes: simulate the horizontal RGB stripe pattern
	float subPixelPos = fract(texelCoord.x * 3.0);
	float subPixelR = step(subPixelPos, 0.333);
	float subPixelG = step(0.333, subPixelPos) * step(subPixelPos, 0.667);
	float subPixelB = step(0.667, subPixelPos);

	// Very subtle sub-pixel mask (only visible on close inspection)
	vec3 subPixelMask = vec3(
		0.97 + 0.03 * subPixelR,
		0.97 + 0.03 * subPixelG,
		0.97 + 0.03 * subPixelB
	);

	// === 4. Vignette ===
	// Subtle darkening at screen edges (GBA LCD had some backlight falloff)
	vec2 vignetteCenter = v_texcoord0 - 0.5;
	float vignette = 1.0 - dot(vignetteCenter, vignetteCenter) * 0.3;
	vignette = clamp(vignette, 0.85, 1.0);

	// === 5. Combine all effects ===
	vec3 finalColor = tinted;

	// Apply grid (only when enabled)
	float gridAmount = u_setting.x;
	finalColor *= mix(1.0, gridDarkening, gridAmount);

	// Apply sub-pixel mask
	finalColor *= subPixelMask;

	// Apply vignette
	finalColor *= vignette;

	// Ensure alpha is 1.0
	gl_FragColor.rgba = vec4(finalColor, 1.0);
}
