#version 120
#extension GL_ARB_shader_texture_lod : enable
uniform sampler2D t; uniform samplerCube cm;
void main(){ gl_FragData[0] = texture2DLod(t, gl_TexCoord[0].xy, 1.0) + textureCube(cm, vec3(1.0)); gl_FragDepth = gl_FragCoord.z; }
