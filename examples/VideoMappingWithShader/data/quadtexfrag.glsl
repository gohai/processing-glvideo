#ifdef GL_ES
precision mediump float;
precision mediump int;
#endif

uniform sampler2D texture;

varying vec4 vertColor;
varying vec3 vertTexCoord;

void main() {
  gl_FragColor = texture2D(texture, vertTexCoord.st / vertTexCoord.p) * vertColor;
}
