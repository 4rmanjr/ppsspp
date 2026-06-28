// [PPSSPP-FORK] MultiCore: GBA Gamma Correction Shader
// Compensates for sRGB gamma on modern displays vs GBA LCD's linear response.
// GBA games were designed for a direct-drive LCD panel (~1.0 gamma).
// Modern sRGB displays apply ~2.2 gamma, making midtones too dark.
//
// This shader pre-compensates: color = pow(color, 1.0 / gamma)
// Gamma=1.0: no change (for modern HDR/linear displays)
// Gamma=1.5: mild correction (default — good middle ground)
// Gamma=2.2: full sRGB compensation (matches original GBA look)
//
// Settings:
//   u_setting.x = Gamma value (1.0-2.5)

#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D sampler0;
varying vec2 v_texcoord0;

uniform vec4 u_setting;

void main()
{
	vec4 color = texture2D(sampler0, v_texcoord0);

	// Gamma correction: pow(color, 1.0/gamma)
	// This pre-compensates so that after display's sRGB gamma, colors look correct
	float gamma = u_setting.x;
	if (gamma > 0.99 && gamma < 1.01) {
		// Skip if gamma is ~1.0 (no correction needed)
		gl_FragColor = color;
	} else {
		float invGamma = 1.0 / gamma;
		color.rgb = pow(color.rgb, vec3(invGamma));
		gl_FragColor = color;
	}
}
