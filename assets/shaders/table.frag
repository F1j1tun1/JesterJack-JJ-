#version 330 core

in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vTexCoord;
out vec4 fragColor;

uniform sampler2D uTexture;
uniform vec3 uColor;
uniform vec2 uUvScale;
uniform vec3 uCameraPosition;
uniform vec3 uLightPosition;
uniform float uLightIntensity;
uniform float uShininess;
uniform float uSpecular;
uniform float uEmission;
uniform float uOpacity;
uniform float uVelvet;
uniform float uRoomLight;
uniform int uMaterial;
uniform vec3 uFogColor;
uniform bool uUseTexture;
uniform bool uLighting;

uniform mat4 uPalaceInverse;
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



// Bounded analytic soft shadows. These use the dimensions of the visible portal
// columns and lantern posts; the theatre's lighting path below is unaffected.
float exteriorVisibility(vec3 p, vec3 source) {
    vec3 ray=source-p;
    float visibility=1.0;
    if(abs(ray.z)>.0001) {
        float t=(2.11-p.z)/ray.z;
        if(t>.005 && t<.995) {
            vec3 q=p+ray*t;
            float penumbra=.025+.09*t;
            float portal=1.0-smoothstep(.12,.12+penumbra,abs(abs(q.x)-1.05));
            portal *= smoothstep(.20,.35,q.y)*(1.0-smoothstep(2.82,2.96,q.y));
            float pilaster=1.0-smoothstep(.08,.08+penumbra,
                min(abs(abs(q.x)-2.60),abs(abs(q.x)-4.34)));
            pilaster *= smoothstep(.2,.4,q.y)*(1.0-smoothstep(4.43,4.53,q.y));
            visibility*=1.0-.84*max(portal,pilaster);
        }
    }
    // Ray/cylinder distance gives soft pole shadows which follow each light.
    for(int i=0;i<4;++i) {
        vec2 pole=vec2(i<2?-3.1:3.1,mod(float(i),2.0)<.5?4.3:5.95);
        float t=clamp(dot(pole-p.xz,ray.xz)/max(dot(ray.xz,ray.xz),.0001),.0,1.0);
        vec3 q=p+ray*t;
        float shade=(1.0-smoothstep(.043,.085+.08*t,length(q.xz-pole)))
            *smoothstep(.10,.18,q.y)*(1.0-smoothstep(1.95,2.02,q.y));
        if(t>.005 && t<.995) visibility*=1.0-.72*shade;
    }
    return visibility;
}

vec3 exteriorPoint(vec3 p, vec3 n, vec3 v, vec3 source, vec3 surface,
                   float power, float roughness, bool windowLight) {
    vec3 delta=source-p;
    float d2=dot(delta,delta);
    if(d2>100.0) return vec3(0);
    vec3 l=normalize(delta), h=normalize(l+v);
    float diffuse=max(dot(n,l),0.0);
    float shine=pow(max(dot(n,h),0.0),roughness)*uSpecular;
    float falloff=power/(1.0+d2*.72);
    falloff*=1.0-smoothstep(60.0,100.0,d2);
    if(windowLight) {
        // Window apertures illuminate the forecourt and their own stone reveals.
        falloff*=mix(.28,1.0,smoothstep(-.38,.10,p.z-source.z));
        shine*=.55;
    } else {
        // The opaque shade blocks light above the lantern; glass lights below it.
        falloff*=1.0-smoothstep(.03,.55,p.y-source.y);
    }
    if(falloff<.0001) return vec3(0);
    float shadow=exteriorVisibility(p+n*.012,source);
    vec3 radiance=(surface*diffuse+vec3(shine))*vec3(1.0,.81,.57)*falloff*shadow;
    if(uMaterial==19 && !windowLight) {
        // Rough wet asphalt stretches the mirrored physical lantern along the
        // view direction. The reflection moves with the camera and is depth-tested.
        vec3 eye=(uPalaceInverse*vec4(uCameraPosition,1)).xyz;
        float t=eye.y/max(eye.y+source.y,.01);
        vec2 reflection=mix(eye.xz,source.xz,t);
        vec2 axis=normalize(eye.xz-source.xz);
        vec2 delta=p.xz-reflection;
        vec2 stretch=vec2(dot(delta,vec2(-axis.y,axis.x))/.19,dot(delta,axis)/2.5);
        float streak=exp(-dot(stretch,stretch)*1.6);
        float ripples=.75+.25*noise(p.xz*vec2(13.0,61.0));
        radiance+=vec3(1.0,.73,.40)*streak*ripples*.27*shadow;
    }
    return radiance;
}

vec4 exteriorSurface(vec3 surface) {
    vec3 p=(uPalaceInverse*vec4(vWorldPosition,1)).xyz;
    vec3 n=normalize(mat3(uPalaceInverse)*vNormal);
    vec3 eye=(uPalaceInverse*vec4(uCameraPosition,1)).xyz;
    vec3 v=normalize(eye-p);
    // The entrance throat is intentionally outside every ambient/fog calculation.
    if(uMaterial==18) {
        float visibility=pow(clamp((p.z-1.10)/.95,0.0,1.0),3.0);
        return vec4(surface*visibility*.12,uOpacity);
    }
    if(uMaterial==23) {
        vec2 q=(vTexCoord-.5)*2.0;
        float halo=exp(-dot(q,q)*5.0)*(1.0-smoothstep(.8,1.0,max(abs(q.x),abs(q.y))));
        return vec4(vec3(1.0,.61,.22),halo*uOpacity*(uLighting?1.0:0.0));
    }
    if(uMaterial==20) {
        float grain=.94+.045*vTexCoord.y+.004*sin(vTexCoord.x*79.0)*sin(vTexCoord.y*93.0);
        return vec4(vec3(1.0,.69,.33)*grain,uOpacity);
    }
    if(uMaterial==17) {
        vec2 tile=p.xz*1.45;
        float checker=mod(floor(tile.x)+floor(tile.y),2.0);
        surface=mix(vec3(.22,.22,.235),vec3(.15,.155,.17),checker);
        vec2 seam=min(fract(tile),1.0-fract(tile));
        surface*=mix(.65,1.0,smoothstep(.0,.015,min(seam.x,seam.y)));
    }
    if(uMaterial==19) {
        float grain=sin(p.x*97.0+p.z*61.0)*sin(p.z*117.0-p.x*53.0);
        surface*=.96+grain*.04;
    }
    if(uMaterial==22) {
        float leaf=sin(p.x*19.0+sin(p.y*11.0))*sin(p.z*17.0+p.y*23.0);
        surface*=.90+.10*leaf;
    }
    if(uMaterial==21) {
        float slate=fract(vTexCoord.y*14.0);
        surface*=.77+.23*smoothstep(.0,.07,slate);
    }
    vec3 result=surface;
    if(uLighting) {
        vec3 moon=normalize(vec3(-.55,.85,.25));
        result=surface*(vec3(.19,.22,.29)+vec3(.24,.28,.38)*max(dot(n,moon),0.0));
        result+=surface*uEmission;
        for(int i=0;i<4;++i) {
            vec3 lamp=vec3(i<2?-3.1:3.1,2.22,mod(float(i),2.0)<.5?4.3:5.95);
            result+=exteriorPoint(p,n,v,lamp,surface,4.9, uMaterial==19?40.0:uShininess,false)*uLightIntensity;
        }
        for(int i=0;i<8;++i) {
            float x=i<4?(i<2?-3.45:-1.80):(i<6?1.80:3.45);
            float y=mod(float(i),2.0)<.5?1.44:3.52;
            result+=exteriorPoint(p,n,v,vec3(x,y,2.16),surface,1.65,uShininess,true)*uLightIntensity;
        }
        // Soft contact occlusion at the foot of the facade and under the sills.
        if(p.y<.22 && abs(p.x)<4.55)
            result*=mix(.26,1.0,smoothstep(1.8,2.65,p.z));
        if(p.z>1.82 && p.z<2.15) {
            float sill=min(abs(p.y-.75),abs(p.y-2.83));
            result*=.78+.22*smoothstep(.02,.16,sill);
        }
    }
    float peak=max(result.r,max(result.g,result.b));
    if(peak>.82) result*=(.82+.18*(1.0-exp(-(peak-.82)/.18)))/peak;
    if(uMaterial==16 || uMaterial==17 || uMaterial==19 || uMaterial==22) {
        // Independent of camera movement: no darkening of the palace itself.
        float extent=length(vec2(p.x/24.0,(p.z-3.0)/27.0));
        float fade=1.0-exp(-pow(extent,3.0));
        result=mix(result,nightSky(gl_FragCoord.xy/uResolution),fade);
    }
    return vec4(result,uOpacity);
}


vec4 finishSurface(vec3 color) {
    // Soft highlight shoulder preserves ink and brass at the upper light setting.
    float peak=max(color.r,max(color.g,color.b));
    if(uRoomLight>0.0 && peak>.75) color *= (.75+.25*(1.0-exp(-(peak-.75)/.25)))/peak;
    return vec4(color, uOpacity);
}

void main() {
    vec3 surface = uColor;
    if (uUseTexture) {
        surface *= texture(uTexture, vTexCoord * uUvScale).rgb;
    }
    if (uRoomLight > 0.0 && uMaterial == 0 && uVelvet > 0.55) {
        // Broad, low-contrast changes in the nap of the old table felt.
        vec2 p = vWorldPosition.xz;
        float wear = sin(p.x * 1.7 + sin(p.y * 0.8)) * sin(p.y * 2.1 - p.x * 0.4);
        surface *= 0.94 + wear * 0.06;
        surface = mix(surface, vec3(0.10, 0.115, 0.07), max(wear, 0.0) * 0.10);
    }
    if (uMaterial == 1 || uMaterial == 8) {
        vec2 p = vTexCoord * 2.0 - 1.0;
        float eyeY = 0.20 + 0.10 * (abs(p.x) - 0.39);
        vec2 eye = vec2((abs(p.x) - 0.39) / 0.225, (p.y - eyeY) / 0.085);
        float eyeDistance = dot(eye, eye);
        if (eyeDistance < 0.84) discard;
        float mouthY = -0.43 + 0.68 * p.x * p.x;
        float mouth = abs(p.y - mouthY);
        float goldEye = 1.0 - smoothstep(1.01, 1.18, eyeDistance);
        float edge = smoothstep(0.955, 0.985, abs(p.x));
        float cheekDiamond = abs(abs(p.x) - 0.61) + abs(p.y + 0.15) * 0.65;
        float goldCheek = 1.0 - smoothstep(0.055, 0.075, cheekDiamond);
        if (uMaterial == 8) {
            float paintedDiamond = abs((p.x + 0.39) / 0.27) + abs((p.y - 0.20) / 0.43);
            float paint = 1.0 - smoothstep(0.94, 1.01, paintedDiamond);
            surface = mix(surface, vec3(0.34, 0.018, 0.035), paint * 0.90);
            float cheekAccent = abs((p.x + 0.58) / 0.095) + abs((p.y + 0.23) / 0.15);
            surface = mix(surface, vec3(0.028, 0.025, 0.028),
                (1.0 - smoothstep(0.94, 1.03, cheekAccent)) * 0.85);
        }
        surface = mix(surface, vec3(0.63, 0.43, 0.18), clamp(goldEye + edge + goldCheek * 0.60, 0.0, 1.0));
        float smile = (1.0 - smoothstep(0.008, 0.024, mouth))
            * (1.0 - smoothstep(0.24, 0.29, abs(p.x)));
        surface = mix(surface, vec3(0.28, 0.075, 0.09), smile * 0.82);
    } else if (uMaterial == 2 || uMaterial == 4) {
        if (uMaterial == 4)
            surface = mix(vec3(0.26, 0.024, 0.06), vec3(0.115, 0.030, 0.20),
                          smoothstep(0.22, 0.28, vTexCoord.x));
        float diamond = abs(fract(vTexCoord.x * 12.0) - 0.5)
            + abs(fract(vTexCoord.y * 9.0) - 0.5);
        float stitch = 1.0 - smoothstep(0.008, 0.026, abs(diamond - 0.50));
        surface *= 0.97 + 0.025 * sin(vTexCoord.x * 110.0) * sin(vTexCoord.y * 150.0);
        surface = mix(surface, vec3(0.40, 0.25, 0.095), stitch * 0.055);
    } else if (uMaterial == 3) {
        vec2 tile = vWorldPosition.xz / 1.3;
        float checker = mod(floor(tile.x) + floor(tile.y), 2.0);
        surface = mix(vec3(0.055, 0.071, 0.073), vec3(0.075, 0.089, 0.089), checker);
        float grout = min(fract(tile.x), fract(tile.y));
        surface *= mix(0.70, 1.0, smoothstep(0.0, 0.012, grout));
    } else if (uMaterial == 5) {
        // Diagonal panes follow the curved, slack fabric of each hat point.
        vec2 cloth = vec2(vTexCoord.x * 5.0, vTexCoord.y * 3.5);
        float pane = mod(floor(cloth.x + cloth.y) + floor(cloth.x - cloth.y), 2.0);
        surface = mix(surface, vec3(0.014, 0.014, 0.018), pane * 0.84);
        surface *= 0.97 + 0.025 * sin(vTexCoord.x * 100.0) * sin(vTexCoord.y * 140.0);
        float tipBand = smoothstep(0.935, 0.947, vTexCoord.y)
            * (1.0 - smoothstep(0.965, 0.978, vTexCoord.y));
        surface = mix(surface, vec3(0.48, 0.32, 0.12), tipBand * 0.8);
    } else if (uMaterial == 6 || uMaterial == 10) {
        float stripe = smoothstep(0.45, 0.53, fract(vTexCoord.x * 12.0 + sin(vTexCoord.y * 5.0) * 0.035));
        vec3 panes = mix(uColor, vec3(0.78, 0.725, 0.62), stripe * 0.88);
        if (uMaterial == 10) {
            float blackSleeve = smoothstep(0.32, 0.37, vTexCoord.y);
            surface = mix(panes, vec3(0.027, 0.018, 0.047), blackSleeve);
            float braid = 1.0 - smoothstep(0.008, 0.020, abs(vTexCoord.y - 0.35));
            surface = mix(surface, vec3(0.52, 0.34, 0.12), braid * 0.90);
        } else surface = panes;
        surface *= 0.98 + 0.02 * sin(vTexCoord.x * 100.0) * sin(vTexCoord.y * 140.0);
    } else if (uMaterial == 7) {
        vec2 lace = vec2(vTexCoord.x * 36.0, vTexCoord.y * 5.0);
        vec2 hole = fract(lace + vec2(mod(floor(lace.y), 2.0) * 0.5, 0.0)) - 0.5;
        float eyelet = dot(hole / vec2(0.21, 0.23), hole / vec2(0.21, 0.23));
        if (vTexCoord.y > 0.14 && vTexCoord.y < 0.90 && eyelet < 0.72) discard;
        float embroidery = 1.0 - smoothstep(0.90, 1.35, eyelet);
        surface *= 0.92 + embroidery * 0.08;
        float edgeBraid = smoothstep(0.94, 0.98, vTexCoord.y);
        surface = mix(surface, vec3(0.55, 0.39, 0.19), edgeBraid * 0.45);
    } else if (uMaterial == 9) {
        float a = vTexCoord.x * 6.2831853;
        surface = mix(uColor, vec3(0.18, 0.055, 0.31), smoothstep(-0.035, 0.035, cos(a)));
        if (uUseTexture) surface *= texture(uTexture, vTexCoord * uUvScale).rgb;
        float height = clamp((vTexCoord.y - 0.45) / 0.43, 0.0, 1.0);
        float x = abs(cos(a));
        float lapelCenter = 0.09 + height * 0.55;
        float lapelWidth = 0.10 + height * 0.06;
        float lapel = (1.0 - smoothstep(lapelWidth - 0.015, lapelWidth + 0.015, abs(x - lapelCenter)))
            * smoothstep(0.43, 0.47, vTexCoord.y) * (1.0 - smoothstep(0.88, 0.92, vTexCoord.y))
            * smoothstep(0.0, 0.2, sin(a));
        surface = mix(surface, vec3(0.020, 0.024, 0.030), lapel);
        float belt = smoothstep(0.264, 0.276, vTexCoord.y)
            * (1.0 - smoothstep(0.382, 0.394, vTexCoord.y));
        surface = mix(surface, vec3(0.018, 0.028, 0.030), belt);
        surface *= 0.975 + 0.025 * sin(vTexCoord.x * 145.0) * sin(vTexCoord.y * 120.0);
    }
    if (uMaterial == 11) {
        // Broad plaster patina, fixed in world space, with no animated noise.
        vec3 p = vWorldPosition;
        float stain = sin(p.x*.61 + sin(p.z*.29))*sin(p.y*.87+p.z*.23);
        surface *= .91 + .09*stain;
        surface = mix(surface, surface*vec3(.72,.77,.75), smoothstep(.35,.85,stain)*.18);
    } else if (uMaterial == 12) {
        vec2 p = vWorldPosition.xz;
        float plank = fract(p.x/.62);
        float seam = smoothstep(0.0,.018,plank)*(1.0-smoothstep(.982,1.0,plank));
        surface *= mix(.58,1.0,seam) * (.94+.06*sin(p.y*1.7+sin(p.x*7.0)));
    }
    if (uMaterial == 15) {
        // Real tiny 3D grains scatter the local pendant and stage lights. Their locations
        // do not depend on the camera; soft silhouettes avoid hard pinprick sprites.
        vec3 toPendant = uLightPosition - vWorldPosition;
        float pendantCone = smoothstep(.22, .84, dot(normalize(-toPendant), vec3(0,-1,0)));
        float illumination = .035 + pendantCone * uLightIntensity * 1.8 / (1.0 + dot(toPendant,toPendant) * .045);
        for (int i = 0; i < 2; ++i) {
            vec3 lamp = vec3(i == 0 ? -8.7 : 8.7, 11.089, -39.475);
            vec3 aim = normalize(vec3(i == 0 ? 2.9 : -2.9, 1.534, -45.5) - lamp);
            vec3 delta = vWorldPosition - lamp;
            float cone = smoothstep(.65, .91, dot(normalize(delta), aim));
            illumination += cone * 1.4 / (1.0 + dot(delta,delta) * .012);
        }
        float silhouette = pow(max(0.0, dot(normalize(vNormal), normalize(uCameraPosition-vWorldPosition))), .7);
        fragColor = vec4(surface * (.50 + min(illumination, 1.25)),
            uOpacity * silhouette * clamp((illumination - .035) * 1.5, 0.0, 1.0));
        return;
    }
    if (uRoomLight < .5) {
        fragColor = exteriorSurface(surface);
        return;
    }
    if (!uLighting) {
        fragColor = finishSurface(surface);
        return;
    }

    vec3 normal = normalize(vNormal);
    vec3 toLight = uLightPosition - vWorldPosition;
    float distanceToLight = length(toLight);
    vec3 lightDirection = normalize(toLight);
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    vec3 halfway = normalize(lightDirection + viewDirection);
    float diffuse = max(dot(normal, lightDirection), 0.0);
    float specular = diffuse > 0.0
        ? pow(max(dot(normal, halfway), 0.0), uShininess) * uSpecular
        : 0.0;
    float attenuation = 1.0 / (1.0 + 0.012 * distanceToLight * distanceToLight);
    if (uRoomLight > 0.0) {
        // The opaque shade sends the pendant light down onto the playing area.
        attenuation *= smoothstep(.30,.75,dot(-lightDirection,vec3(0,-1,0)));
    }
    vec3 warmLight = vec3(1.0, 0.94, 0.82);
    vec3 result = surface * ((uRoomLight > 0.0 ? 0.075 : 0.34) + uEmission)
        + (surface * diffuse + vec3(specular))
          * warmLight * uLightIntensity * attenuation;
    float fibers = pow(1.0 - abs(dot(normal, viewDirection)), 3.0);
    result += surface * fibers * uVelvet * 0.32;
    float rim = pow(1.0 - max(dot(normal, viewDirection), 0.0), 4.0);
    result += vec3(0.34, 0.22, 0.11) * rim * uVelvet * uRoomLight * 0.16;
    // Warm stage spots originate at visible truss-mounted luminaires.
    for (int i = 0; i < 2; ++i) {
        vec3 lamp = vec3(i == 0 ? -8.7 : 8.7, 11.089, -39.475);
        vec3 aim = normalize(vec3(i == 0 ? 2.9 : -2.9, 1.534, -45.5) - lamp);
        vec3 delta = lamp - vWorldPosition;
        float cone = smoothstep(.65,.91,dot(normalize(-delta),aim));
        float falloff = 1.0/(1.0+dot(delta,delta)*.0045);
        result += surface*vec3(1.0,.84,.60)*cone*falloff*
            (.3+max(dot(normal,normalize(delta)),0.0)*2.5)*uRoomLight*1.45;
    }
    // Broad footlight wash stays around the stage, away from the playing cards.
    float depth=(vWorldPosition.z+42.375)/8.75;
    float across=abs(vWorldPosition.x)/17.4;
    float stageWash=exp(-depth*depth)
        * exp(-pow(across,4.0));
    result += surface*vec3(1.0,.82,.55)*stageWash*uRoomLight*.67;

    if (uRoomLight > 0.0 && (uMaterial == 11 || uMaterial == 12 ||
        abs(vWorldPosition.x) > 10.0)) {
        // Suppress auditorium fill, retaining a local warm stage and gentle floor pool.
        float tablePool=exp(-dot(vWorldPosition.xz,vWorldPosition.xz)*.018);
        float visibility=.10 + .38*tablePool + .90*stageWash;
        result *= visibility;
        if(uMaterial == 11) result += surface*vec3(.035,.040,.045)*(1.0-stageWash);
        if(uMaterial == 12) result += surface*vec3(.24,.21,.16)*tablePool;
    }
    if (uRoomLight > 0.0 && uMaterial == 12 && vWorldPosition.y < -.5) {
        // The oak floor reflects the actual pendant, without auditorium fill suppression.
        // Keep only a small ambient base outside its downward cone.
        result = surface * .012 + (surface * diffuse + vec3(specular))
            * warmLight * uLightIntensity * attenuation;
        // Soft analytic table shadow projected from the pendant onto the floor.
        vec2 footprint=(vWorldPosition.xz-vec2(0,.231))/vec2(5.27,3.78);
        result *= mix(.22,1.0,smoothstep(.92,1.20,length(footprint)));
    }
    // Thin paper edges scatter light; retain a readable neutral body even on vertical sides.
    // This material is used only on the deck layers, never on illustrated card backs.
    if(uMaterial == 13) result = surface * (vec3(.075)
        + warmLight * uLightIntensity * (.46 + diffuse * attenuation * .25));
    // The visible diffuser follows the same bounded lamp intensity as its light.
    if(uMaterial == 14) result = surface * ((.15 + .65*uLightIntensity)/(1.0+.15*uLightIntensity));
    // Sparse warm haze lives in the distant stage volume, never over cards or UI.
    float stageHaze = smoothstep(12.0,28.0,-vWorldPosition.z)*exp(-pow((vWorldPosition.z+40.0)/18.0,2.0));
    result = mix(result,vec3(.12,.095,.065),stageHaze*uRoomLight*.035);
    // Auditorium haze preserves stage depth; retain the separate menu treatment.
    float viewDistance = length(uCameraPosition - vWorldPosition);
    float fog = uRoomLight > 0.0 ? smoothstep(30.0, 90.0, viewDistance)
                                : smoothstep(9.0, 20.0, viewDistance);
    result = mix(result, uFogColor, fog * (uRoomLight > 0.0 ? 0.20 : 0.30));
    if (uMaterial == 3)
        result *= exp(-max(length(uCameraPosition - vWorldPosition) - 7.0, 0.0) * 0.14);
    fragColor = finishSurface(result);
}
