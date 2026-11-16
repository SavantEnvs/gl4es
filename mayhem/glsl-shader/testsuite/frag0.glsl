#version 120
uniform sampler2D tex; uniform sampler2DShadow sh; varying vec3 n;
void main(){
  vec4 c = texture2D(tex, gl_TexCoord[0].st) * gl_Color;
  float s = shadow2D(sh, gl_TexCoord[1].xyz).r;
  float f = clamp((gl_Fog.end - gl_FogFragCoord) * gl_Fog.scale, 0.0, 1.0);
  gl_FragColor = mix(gl_Fog.color, c * s, f);
}
