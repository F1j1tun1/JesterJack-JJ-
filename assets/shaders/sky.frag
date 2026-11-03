#version 330 core
out vec4 fragColor;
uniform vec2 uResolution;

float hash(vec2 p) { return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453); }
float noise(vec2 p) {
    vec2 i=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),
               mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
vec3 nightSky(vec2 uv) {

    vec3 ground=vec3(.012,.016,.027);
    vec3 sky=mix(vec3(.060,.075,.112),vec3(.014,.020,.047),smoothstep(.4,1.0,uv.y));
    float cloud=noise(vec2(uv.x*4.0,uv.y*9.0));
    cloud+=noise(vec2(uv.x*9.0,uv.y*19.0))*.35;
    sky+=vec3(.012,.014,.021)*smoothstep(.5,.95,cloud)*smoothstep(.5,.8,uv.y);
    // Fine, sparse stars stay behind the architecture; no large screen glow.
    vec2 cells=uv*vec2(uResolution.x/uResolution.y,1)*250.0;
    vec2 star=fract(cells)-.5;
    float point=(1.0-smoothstep(.025,.12,length(star)))*step(.996,hash(floor(cells)));
    sky+=vec3(.20,.22,.27)*point*smoothstep(.60,.85,uv.y);
    // Quiet layered hills create depth without a cut-off polygon horizon.
    for(int layer=0;layer<3;++layer) {
        float l=float(layer);
        float ridge=.615+l*.025+noise(vec2(uv.x*(4.0+l*2.0),l*3.0))*(.035+l*.010);
        float mountain=1.0-smoothstep(ridge-.012,ridge+.018,uv.y);
        sky=mix(sky,mix(ground,vec3(.049,.062,.090),l*.20),mountain*.62);
    }
    sky=mix(ground,sky,smoothstep(.40,.70,uv.y));
    return sky;
}

void main() { fragColor=vec4(nightSky(gl_FragCoord.xy/uResolution),1); }
