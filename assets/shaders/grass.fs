#version 100

precision mediump float;

varying vec3 fragPosition;

void main() {
    float stripe = sin(fragPosition.x * 10.0) * cos(fragPosition.z * 10.0);
    vec3 grassColor = vec3(0.1, 0.5 + 0.1 * stripe, 0.15);
    gl_FragColor = vec4(grassColor, 1.0);
}

