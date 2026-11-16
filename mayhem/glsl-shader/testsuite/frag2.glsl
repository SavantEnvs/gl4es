uniform sampler2D a;
#if __VERSION__ >= 110
#define TX(x) texture2DProj(a, x)
#else
#define TX(x) texture2D(a, x.xy)
#endif
void main(){ gl_FragColor = TX(gl_TexCoord[0]) * gl_FrontMaterial.diffuse + gl_LightModel.ambient; }
