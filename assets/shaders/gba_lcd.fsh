// [PPSSPP-FORK] MultiCore: GBA LCD Screen Simulation Shader
// Simulates the characteristic look of different GBA LCD models.
// Profiles (u_setting.z):
//   0 = AGB-001 (original reflective) — greenish tint, strong grid, strong vignette
//   1 = AGS-001 (SP frontlit) — lighter tint, moderate grid+ vignette
//   2 = AGS-101 (SP backlit) — minimal tint, faint grid, minimal vignette
// Gamma correction (u_setting.w):
//   1.0 = off, >1.0 applies pow(color, 1.0/gamma) for sRGB compensation
//
// Settings:
//   u_setting.x = Grid intensity (0.0 = off, 1.0 = full grid)
//   u_setting.y = Tint strength (0.0 = off, 1.0 = full tint)
//   u_setting.z = LCD Profile (0=AGB-001, 1=AGS-001, 2=AGS-101)
//   u_setting.w = Gamma (1.0 = off, 2.2 = full sRGB compensation)

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform vec2 u_texelDelta;
uniform vec4 u_setting;

void main()
{
	// Sample the input pixel
	vec4 color = texture2D(sampler0, v_texcoord0);

	// === Profile parameters ===
	// Each GBA model has different LCD characteristics
	int profile = int(round(u_setting.z));
	float tintStrength = u_setting.y;
	float gridAmount = u_setting.x;

	// Per-profile parameters
	vec3 profileTintMatrixR, profileTintMatrixG, profileTintMatrixB;
	float profileBrightness;
	float profileDesaturation;
	float profileGridThreshold;
	float profileSubPixelStrength;
	float profileVignetteStrength;

	if (profile == 1) {
		// AGS-001 (SP frontlit) — lighter tint, moderate effects
		profileTintMatrixR = vec3(0.91, 0.03, 0.02);
		profileTintMatrixG = vec3(0.04, 0.94, 0.03);
		profileTintMatrixB = vec3(0.02, 0.05, 0.85);
		profileBrightness = 1.03;
		profileDesaturation = 0.10;
		profileGridThreshold = 0.46;
		profileSubPixelStrength = 0.02;
		profileVignetteStrength = 0.25;
	} else if (profile == 2) {
		// AGS-101 (SP backlit) — minimal tint, very subtle effects
		profileTintMatrixR = vec3(0.96, 0.02, 0.01);
		profileTintMatrixG = vec3(0.02, 0.97, 0.02);
		profileTintMatrixB = vec3(0.01, 0.03, 0.92);
		profileBrightness = 1.0;
		profileDesaturation = 0.05;
		profileGridThreshold = 0.44;
		profileSubPixelStrength = 0.01;
		profileVignetteStrength = 0.15;
	} else {
		// Default: AGB-001 (original reflective) — strong greenish tint, strong effects
		profileTintMatrixR = vec3(0.88, 0.04, 0.03);
		profileTintMatrixG = vec3(0.05, 0.92, 0.04);
		profileTintMatrixB = vec3(0.02, 0.06, 0.78);
		profileBrightness = 1.05;
		profileDesaturation = 0.15;
		profileGridThreshold = 0.48;
		profileSubPixelStrength = 0.03;
		profileVignetteStrength = 0.30;
	}

	// === 1. GBA Color Tint Matrix ===
	// Simulate the GBA screen's characteristic color response
	vec3 gbaTint = vec3(
		dot(color.rgb, profileTintMatrixR),
		dot(color.rgb, profileTintMatrixG),
		dot(color.rgb, profileTintMatrixB)
	);
	gbaTint = gbaTint * profileBrightness;

	// Mix tint based on setting
	vec3 tinted = mix(color.rgb, gbaTint, tintStrength);

	// === 2. Muted Color Curve ===
	// GBA had less saturated colors than modern displays
	float luminance = dot(tinted, vec3(0.299, 0.587, 0.114));
	tinted = mix(tinted, vec3(luminance), tintStrength * profileDesaturation);

	// === 3. LCD Sub-pixel Grid ===
	// Visible grid structure between pixels
	vec2 texelCoord = v_texcoord0 / u_texelDelta;
	vec2 gridPos = fract(texelCoord);
	vec2 gridLines = abs(gridPos - 0.5);
	vec2 gridThreshold = vec2(profileGridThreshold);
	vec2 gridMask = step(gridLines, gridThreshold);
	float pixelVisible = gridMask.x * gridMask.y;

	// Invert: grid lines = dark lines between pixels
	float gridDarkening = mix(0.4, 1.0, pixelVisible);

	// Sub-pixel RGB stripes (horizontal RGB stripe pattern)
	float subPixelPos = fract(texelCoord.x * 3.0);
	vec3 subPixelMask = vec3(
		1.0 - profileSubPixelStrength + profileSubPixelStrength * step(subPixelPos, 0.333),
		1.0 - profileSubPixelStrength + profileSubPixelStrength * step(0.333, subPixelPos) * step(subPixelPos, 0.667),
		1.0 - profileSubPixelStrength + profileSubPixelStrength * step(0.667, subPixelPos)
	);

	// === 4. Vignette ===
	// Edge darkening from backlight falloff / reflective screen
	vec2 vignetteCenter = v_texcoord0 - 0.5;
	float vignette = 1.0 - dot(vignetteCenter, vignetteCenter) * profileVignetteStrength;
	vignette = clamp(vignette, 1.0 - profileVignetteStrength * 0.5, 1.0);

	// === 5. Combine all effects ===
	vec3 finalColor = tinted;

	// Apply grid
	finalColor *= mix(1.0, gridDarkening, gridAmount);

	// Apply sub-pixel mask
	finalColor *= subPixelMask;

	// Apply vignette
	finalColor *= vignette;

	// === 6. Gamma Correction ===
	// Compensate for modern display's sRGB gamma
	float gamma = u_setting.w;
	if (gamma > 1.01) {
		float invGamma = 1.0 / gamma;
		finalColor = pow(finalColor, vec3(invGamma));
	}

	gl_FragColor.rgba = vec4(finalColor, 1.0);
}
