#include "gfx/Renderer.h"
#include "core/Noise.h"
#include "world/Block.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>

namespace qc {
void Camera::update() {
    vec3 f = forward();
    vec3 baseRight = normalize(cross(f, {0, 1, 0}), {1, 0, 0});
    vec3 baseUp = normalize(cross(baseRight, f), {0, 1, 0});
    vec3 up = baseUp * std::cos(roll) + baseRight * std::sin(roll);
    view = lookAt({0, 0, 0}, f, up);
    proj = perspective(fov, aspect, nearZ, farZ);
    viewProj = proj * view;
    frustum = Frustum::fromMatrix(viewProj);
}

namespace {
const char* kCommon = R"(
uniform vec3 uLightDir;
uniform vec3 uSunCol;
uniform vec3 uAmbCol;
uniform vec3 uFog;
uniform vec2 uFogRange;
uniform float uFogDensity;
uniform float uCamY;
uniform int uMedium;
uniform int uShadowOn;
uniform mat4 uShadowVP;
uniform sampler2DShadow uShadowMap;
uniform float uShadowTexel;
uniform int uPointCount;
uniform int uUseVol;
uniform sampler2D uLightData;
uniform usampler3D uClusterRanges;
uniform usampler2D uClusterIndices;
uniform mat4 uClusterView;
uniform vec2 uClusterTan;
uniform vec3 uClusterGrid;
uniform vec2 uClusterDepth;
uniform sampler2DArrayShadow uPointShadows;
uniform sampler3D uColorVol;
uniform float uSkyExposure;
uniform float uCausticTime;
uniform sampler3D uPointVolume;
uniform sampler3D uLightValidity;
uniform vec3 uPointCam;
uniform vec4 uPointWindow;
uniform int uUseLiveSky;
uniform sampler3D uMovingLight0;
uniform sampler3D uMovingLight1;
uniform sampler3D uMovingLight2;
uniform sampler3D uMovingLight3;
uniform vec3 uMovingLightOrigin0,uMovingLightSize0;
uniform vec3 uMovingLightOrigin1,uMovingLightSize1;
uniform vec3 uMovingLightOrigin2,uMovingLightSize2;
uniform vec3 uMovingLightOrigin3,uMovingLightSize3;
float movingSky(sampler3D vol,vec3 origin,vec3 size,vec3 p,float result) {
    if(size.x<0.5)return result;
    vec3 q=p-origin;
    if(any(lessThan(q,vec3(0.0)))||any(greaterThanEqual(q,size)))return result;
    vec4 L=texture(vol,q/size);
    return L.a>.001?min(result,clamp(L.r/L.a,0.0,1.0)):result;
}
float movingCompositeSky(vec3 rel,vec3 N,float fallback) {
    vec3 p=(rel+uPointCam)*2.0+N*0.6;
    float result=movingSky(uMovingLight0,uMovingLightOrigin0,uMovingLightSize0,p,fallback);
    result=movingSky(uMovingLight1,uMovingLightOrigin1,uMovingLightSize1,p,result);
    result=movingSky(uMovingLight2,uMovingLightOrigin2,uMovingLightSize2,p,result);
    return movingSky(uMovingLight3,uMovingLightOrigin3,uMovingLightSize3,p,result);
}
float atlasShadow(vec3 dir,float reach,int slot,float resolution) {
    vec3 a=abs(dir);vec2 uv;float z;int face;
    if(a.x>=a.y&&a.x>=a.z){z=a.x;face=dir.x>0.0?0:1;uv=vec2(dir.x>0.0?-dir.z:dir.z,-dir.y);}
    else if(a.y>=a.z){z=a.y;face=dir.y>0.0?2:3;uv=vec2(dir.x,dir.y>0.0?dir.z:-dir.z);}
    else {z=a.z;face=dir.z>0.0?4:5;uv=vec2(dir.z>0.0?dir.x:-dir.x,-dir.y);}
    if(z<=.105)return 1.0;
    uv=(uv/z*.5+.5)*resolution;
    uv=clamp(uv,vec2(.5/256.0),vec2(resolution-.5/256.0));
    float d=reach/(reach-.08)-(reach*.08)/((reach-.08)*max(.08,z-.035));
    return texture(uPointShadows,vec4(uv,float(slot*6+face),d));
}
float mappedPointShadow(int slot,vec3 delta,float reach,float resolution) {
    vec3 dir=-delta,a=abs(dir);vec2 uv;float z;int face;
    if(a.x>=a.y&&a.x>=a.z){z=a.x;face=dir.x>0.0?0:1;uv=vec2(dir.x>0.0?-dir.z:dir.z,-dir.y);}
    else if(a.y>=a.z){z=a.y;face=dir.y>0.0?2:3;uv=vec2(dir.x,dir.y>0.0?dir.z:-dir.z);}
    else {z=a.z;face=dir.z>0.0?4:5;uv=vec2(dir.z>0.0?dir.x:-dir.x,-dir.y);}
    if(z<=.105)return 1.0;
    uv=(uv/z*.5+.5)*resolution;
    float spread=max(.6/256.0,.0225/z*resolution),sum=0.0;
    float depth=reach/(reach-.08)-(reach*.08)/((reach-.08)*max(.08,z-.035));
    if(min(min(uv.x,uv.y),min(resolution-uv.x,resolution-uv.y))>spread+.5/256.0) {
        for(int k=0;k<4;++k)sum+=texture(uPointShadows,vec4(uv+vec2((k&1)==0?-spread:spread,(k&2)==0?-spread:spread),float(slot*6+face),depth));
    } else {
        vec3 axis=abs(normalize(dir).y)>.9?vec3(1,0,0):vec3(0,1,0);
        vec3 right=normalize(cross(dir,axis)),up=normalize(cross(right,dir));
        float width=max(.045,z*.0045);
        for(int k=0;k<4;++k)sum+=atlasShadow(dir+(right*((k&1)==0?-1.0:1.0)+up*((k&2)==0?-1.0:1.0))*width,reach,slot,resolution);
    }
    return sum*.25;
}
float pointVisibility(vec3 rel, vec3 N, vec3 delta, float distance) {
    if (uPointWindow.z < 1.0) return 1.0;
    vec3 start=(rel+uPointCam+N*.09)*2.0;
    vec3 target=(rel+uPointCam+delta)*2.0, ray=target-start;
    ivec3 cell=ivec3(floor(start)), source=ivec3(floor(target));
    ivec3 stepDir=ivec3(sign(ray));
    vec3 stride=1.0/max(abs(ray),vec3(0.000001));
    vec3 next=mix(fract(start),1.0-fract(start),step(vec3(0.0),ray))*stride;
    for(int j=0;j<48;++j) {
        if(all(equal(cell,source)))break;
        vec3 p=vec3(cell);
        if(p.x>=uPointWindow.x&&p.z>=uPointWindow.y&&p.x<uPointWindow.x+uPointWindow.z&&p.z<uPointWindow.y+uPointWindow.z&&p.y>=0.0&&p.y<uPointWindow.w) {
            vec4 voxel=texelFetch(uPointVolume,ivec3(mod(p,vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z))),0);
            if(voxel.b>.5 && texture(uLightValidity,(p+.5)/vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z)).r>.5)return 0.0;
        }
        float t=min(next.x,min(next.y,next.z));if(t>=1.0)break;
        vec3 advance=step(next,vec3(t+.000001));
        cell+=ivec3(advance)*stepDir;next+=advance*stride;
    }
    return 1.0;
}
vec3 sourceIrradiance(vec3 color,vec3 delta) {
    float travel=dot(abs(delta),vec3(2.0));
    const float floor=0.0656469;
    return max(vec3(0.0),(color+floor)*exp2(-0.321928095*travel)-floor);
}
vec3 pointLighting(vec3 rel,vec3 N,vec3 field) {
    if(uPointCount==0||dot(rel,rel)>=64.0*64.0)return field;
    vec3 view=(uClusterView*vec4(rel,1)).xyz;float z=max(.25,-view.z);
    vec2 tile=(view.xy/(z*uClusterTan)*.5+.5)*uClusterGrid.xy;
    int slice=int(floor(log(z/uClusterDepth.x)*uClusterDepth.y));
    ivec3 cell=clamp(ivec3(ivec2(floor(tile)),slice),ivec3(0),ivec3(uClusterGrid)-1);
    uvec2 range=texelFetch(uClusterRanges,cell,0).rg;
    vec3 expected=vec3(0.0),visible=vec3(0.0),covered=vec3(0.0);
    vec3 portable=vec3(0.0),portablePeak=vec3(0.0);
    for(uint j=0u;j<range.y;++j) {
        uint address=range.x+j;
        int i=int(texelFetch(uClusterIndices,ivec2(int(address%1024u),int(address/1024u)),0).r);
        vec4 source=texelFetch(uLightData,ivec2(i,0),0);
        vec3 delta=source.xyz-rel;float reach=source.w;
        if(dot(abs(delta),vec3(1.0))>=reach)continue;
        vec3 strength=sourceIrradiance(texelFetch(uLightData,ivec2(i,1),0).rgb,delta-N*.3);
        if(max(strength.r,max(strength.g,strength.b))<.004)continue;
        vec4 state=texelFetch(uLightData,ivec2(i,2),0);
        float visibility=state.x>=0.0?mappedPointShadow(int(state.x),delta-N*.035,reach,state.z):pointVisibility(rel,N,delta,length(delta));
        if(state.y>.5) {
            expected+=strength;visible+=strength*visibility;covered=max(covered,strength);
        } else {
            vec3 value=strength*visibility;portable+=value;portablePeak=max(portablePeak,value);
        }
    }
    float nearWeight=1.0-smoothstep(32.0,64.0,length(rel));
    vec3 blocked=1.0-clamp(visible/max(expected,vec3(.00001)),0.0,1.0);
    vec3 local=field-min(field,covered)*blocked*(.78*nearWeight);
    portable=portablePeak+min(max(vec3(0),portable-portablePeak),portablePeak*.8)*.35;
    return max(local,portable)+min(local,portable)*.35;
}
float lightCurve(float l) { return max(0.0,(pow(0.80,15.0*(1.0-l))-pow(0.80,15.0))/(1.0-pow(0.80,15.0))); }
float skyCurve(float l) { return max(0.0,(pow(0.845,15.0*(1.0-l))-pow(0.845,15.0))/(1.0-pow(0.845,15.0))); }
float shadowAt(vec3 rel, vec3 N) {
    if (uShadowOn == 0) return 1.0;
    vec4 s = uShadowVP * vec4(rel + N * 0.07, 1.0);
    vec3 c = s.xyz * 0.5 + 0.5;
    vec2 e = min(c.xy, 1.0 - c.xy);
    float edge = smoothstep(0.0, 0.08, min(e.x, e.y));
    if (edge <= 0.0 || c.z >= 1.0) return 1.0;
    float t = uShadowTexel;
    float sum = texture(uShadowMap, vec3(c.xy + vec2(-0.6, -0.6) * t, c.z)) + texture(uShadowMap, vec3(c.xy + vec2(0.6, -0.6) * t, c.z)) +
                texture(uShadowMap, vec3(c.xy + vec2(-0.6, 0.6) * t, c.z)) + texture(uShadowMap, vec3(c.xy + vec2(0.6, 0.6) * t, c.z));
    float lit = sum * 0.25;
    lit = smoothstep(0.15, 0.85, lit);
    return mix(1.0, lit, edge);
}
vec2 texelCentreStep(vec2 uv) {
    vec2 t = uv * 16.0;
    vec2 dx = dFdx(t), dy = dFdy(t);
    vec2 d = floor(t) + 0.5 - t;
    float det = dx.x * dy.y - dx.y * dy.x;
    if (abs(det) < 1e-7) return vec2(0.0);
    vec2 ab = vec2(d.x * dy.y - d.y * dy.x, dx.x * d.y - dx.y * d.x) / det;
    return clamp(ab, vec2(-6.0), vec2(6.0));
}
bool lightFootprintValid(vec3 p) {
    vec3 size=vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z);
    if(texture(uLightValidity,p/size).r<.5)return false;
    ivec3 lo=ivec3(floor((p-.5)/32.0)),hi=ivec3(floor((p+.5)/32.0));
    if(all(equal(lo,hi)))return true;
    ivec3 count=ivec3(size/32.0);
    for(int z=lo.z;z<=hi.z;++z)for(int y=lo.y;y<=hi.y;++y)for(int x=lo.x;x<=hi.x;++x) {
        ivec3 cell=ivec3((x%count.x+count.x)%count.x,clamp(y,0,count.y-1),(z%count.z+count.z)%count.z);
        if(texelFetch(uLightValidity,cell,0).r<.5)return false;
    }
    return true;
}
vec3 environmentLight(vec3 rel,vec3 N,inout float sky,vec3 fallback) {
    vec3 p=(rel+uPointCam)*2.0+N*0.6;
    if(uUseVol==0||uPointWindow.z<1.0||p.y<0.0||p.y>=uPointWindow.w||p.x<uPointWindow.x||p.z<uPointWindow.y||p.x>=uPointWindow.x+uPointWindow.z||p.z>=uPointWindow.y+uPointWindow.z)return fallback;
    vec3 uv=p/vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z);
    if(!lightFootprintValid(p))return fallback;
    vec4 light=texture(uPointVolume,uv);
    if(light.a<.001)return fallback;
    float edge=min(min(p.x-uPointWindow.x,p.z-uPointWindow.y),min(uPointWindow.x+uPointWindow.z-p.x,uPointWindow.y+uPointWindow.z-p.z));
    float weight=smoothstep(1.0,12.0,edge);
    sky=mix(sky,clamp(light.r/light.a,0.0,1.0),weight);
    vec3 live=light.g>0.0?clamp(texture(uColorVol,uv).rgb/light.a,0.0,1.0):vec3(0.0);
    return mix(fallback,live,weight);
}
float waterCaustics(vec3 rel, vec3 N) {
    vec3 p=(rel+uPointCam+N*.16)*2.0;
    if(uPointWindow.z<1.0||p.y<0.0||p.y>=uPointWindow.w||p.x<uPointWindow.x||p.z<uPointWindow.y||p.x>=uPointWindow.x+uPointWindow.z||p.z>=uPointWindow.y+uPointWindow.z)return 0.0;
    ivec3 cell=ivec3(mod(floor(p),vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z)));
    vec4 water=texelFetch(uPointVolume,cell,0);
    if(water.a<.99 || water.b<.20 || water.b>.30 || texture(uLightValidity,p/vec3(uPointWindow.z,uPointWindow.w,uPointWindow.z)).r<.5)return 0.0;
    vec3 q=floor((rel+uPointCam)*16.0)/16.0;
    vec2 uv=q.xz+q.y*vec2(.21,.13);
    float t=floor(uCausticTime*10.0)/10.0;
    float a=sin(uv.x*3.0+sin(uv.y*1.6+t*.35)*.65+t*.23);
    float b=sin(uv.y*3.3+sin(uv.x*1.8-t*.28)*.65-t*.19);
    return (1.0-smoothstep(.06,.23,abs(a+b)))*.14*max(.15,N.y);
}
vec3 shade(vec3 albedo, inout float sky, float blk, float face, float ao, vec3 N, vec3 rel, bool shadows,vec3 radiance,bool causticReceiver) {
    float liveSky=sky;
    vec3 rgb=environmentLight(rel,N,liveSky,max(radiance,vec3(0.0)));
    if(uUseLiveSky==1) {
        liveSky=movingCompositeSky(rel,N,liveSky);
        sky=liveSky;
    } else if(uUseLiveSky==2) {
        sky=movingCompositeSky(rel,N,sky);
    }
    float skyL = skyCurve(sky);
    float sunVis = smoothstep(0.5, 0.95, sky);
    float ndl = max(dot(N, uLightDir), 0.0);
    bool shTaken = shadows && ndl > 0.0 && sunVis > 0.0;
    float sh = shTaken ? shadowAt(rel, N) : 1.0;
    float hemi = mix(0.76, 1.0, N.y * 0.5 + 0.5);
    float faceSoft = mix(1.0, face, 0.4);
    vec3 amb = uAmbCol * skyL * hemi * faceSoft * ao;
    vec3 sun = uSunCol * (ndl * sh * sunVis);
    vec3 field=vec3(lightCurve(rgb.r),lightCurve(rgb.g),lightCurve(rgb.b))*1.80;
    vec3 torch=pointLighting(rel,N,field);
    vec3 L = amb + sun;
    L = max(L, torch) + min(L, torch) * 0.35;
    float caustic=causticReceiver&&(skyL>.02||max(torch.r,max(torch.g,torch.b))>.08)?waterCaustics(rel,N):0.0;
    if(caustic>0.0) {
        vec3 focus=uSunCol*skyL*.75*(shadows?(shTaken?sh:shadowAt(rel,N)):1.0)+amb*.30+torch*.28;
        L+=caustic*focus*vec3(.50,.73,.68);
    }
    vec3 cave=vec3(.15,.165,.18)*(1.0-smoothstep(.08,.65,sky))*hemi*ao*faceSoft;
    vec3 illumination=L+cave+vec3(.006,.007,.01);
    return albedo * illumination;
}
vec3 applyFog(vec3 col, vec3 rel, float sky, out float f) {
    float d = length(rel);
    float h = uCamY + rel.y * 0.5 - 63.0;
    float exposed = uMedium==1 ? 1.0 : max(smoothstep(0.0,0.45,uSkyExposure),smoothstep(0.0,0.6,sky));
    float dens = mix(0.002,uFogDensity * exp(-clamp(h,-20.0,80.0)*0.035),exposed);
    float expFog = 1.0 - exp(-d * dens);
    float outdoorGate = smoothstep(uFogRange.x * 0.42, uFogRange.x, d);
    expFog *= mix(1.0, outdoorGate, exposed);
    f = max(expFog, smoothstep(uFogRange.x, uFogRange.y, d));
    vec3 V = rel / max(d, 1e-3);
    float glow = pow(max(dot(V, uLightDir), 0.0), 8.0);
    return mix(col, mix(vec3(0.020,0.025,0.030),uFog + uSunCol * glow * 0.3,exposed), f);
}
)";

const char* kTintFns = R"(
vec3 grassTint(vec2 cl) {
    vec3 c = mix(vec3(0.72, 0.91, 1.05), vec3(0.95, 1.10, 0.84), smoothstep(0.12, 0.48, cl.x));
    c = mix(c, vec3(1.52, 1.16, 0.67), smoothstep(0.57, 0.86, cl.x) * (1.0 - smoothstep(0.35, 0.72, cl.y)));
    return mix(c, vec3(0.73, 1.06, 0.76), smoothstep(0.7, 0.94, cl.y));
}
vec3 foliageTint(vec2 cl) { return mix(grassTint(cl), vec3(0.87, 1.0, 0.90), 0.3); }
vec3 waterTint(vec2 cl) {
    vec3 c = mix(vec3(0.65, 0.85, 1.22), vec3(0.61, 1.14, 0.96), cl.x);
    return mix(c, vec3(0.86, 0.96, 0.55), smoothstep(0.80, 0.97, cl.y) * smoothstep(0.38, 0.6, cl.x));
}
)";

const char* kTerrainVS = R"(#version 330 core
invariant gl_Position;
layout(location = 0) in uvec4 aData;
layout(location = 1) in vec4 aOrigin;
uniform mat4 uViewProj;
uniform mat4 uModel;
uniform vec3 uUVOffset;
uniform float uSkyScale;
uniform float uTime;
uniform vec2 uLightOverride;
out vec3 vUV;
out vec4 vWater;
flat out float vChunkFade;
flat out int vWaterFlags;
out vec3 vRadiance;
out vec3 vLight;
out float vShade;
out float vMaterial;
out vec3 vRel;
out vec2 vClim;
flat out int vMode;
flat out int vTint;
flat out vec3 vNormal;
void main() {
    uint a = aData.x, b = aData.y;
    vec3 p = vec3(float(a & 63u), float((a >> 6) & 63u), float((a >> 12) & 63u));
    p+=vec3(float((aData.w>>29)&1u),float((aData.w>>30)&1u),float((aData.w>>31)&1u))*64.0;
    p+=vec3(float((aData.w>>12)&15u),float((aData.w>>16)&15u),float((aData.w>>20)&15u))/16.0;
    uint face = (a >> 18) & 7u;
    float cu = float((a >> 21) & 1u), cv = float((a >> 22) & 1u);
    uint mode = (a >> 23) & 3u;
    uint sway = (a >> 25) & 1u;
    float yoff = float((a >> 26) & 15u);
    p.y -= yoff / 16.0;
    vec3 wp = aOrigin.xyz + uUVOffset + p;
    vec3 lp = (aOrigin.xyz + p) * 0.5;
    vChunkFade = aOrigin.w;
    // stepped on purpose, smooth looked off with the pixel lighting
    float animTime = floor(uTime * 15.0) / 15.0;
    float animFade=1.0-smoothstep(48.0,88.0,length((uModel*vec4(lp,1.0)).xyz));
    vec2 uv;
    if (mode == 2u) uv = vec2(cu, 1.0 - cv);
    else {
        if (face < 2u) uv = vec2(face == 0u ? -wp.z : wp.z, -wp.y) * 0.5;
        else if (face < 4u) uv = vec2(wp.x, face == 2u ? wp.z : -wp.z) * 0.5;
        else uv = vec2(face == 4u ? wp.x : -wp.x, -wp.y) * 0.5;
        if (mode == 1u) uv.y = (1.0 - cv) * 0.5;
        if (mode == 3u) {
            if (face == 2u) uv += vec2(animTime * 0.03, animTime * 0.05);
            else uv.y -= animTime * 0.6;
        }
    }
    if (((a >> 31) & 1u) == 1u)
        uv = vec2(float(aData.z & 0xFFFFu), float(aData.z >> 16)) / 4096.0 - 8.0;
    int wf=int(aData.z>>22);
    float oceanWeight=(wf&128)!=0?float((aData.z>>14)&255u)/255.0:0.0;
    vWater=vec4((vec2(float(aData.z&127u),float((aData.z>>7)&127u))-63.0)/63.0,oceanWeight,float(wf&1));
    vWaterFlags=wf;
    if(mode==3u && (wf&4)!=0) {
        float wave=sin(wp.x*0.48+wp.z*0.31-animTime*1.4)*0.7+sin(wp.x*0.19-wp.z*0.53-animTime*1.1)*0.3;
        float swell=sin(wp.x*.095+wp.z*.057-animTime*.85)*.36+sin(wp.x*.043-wp.z*.081-animTime*.57)*.10;
        lp.y+=((wf&128)!=0 ? floor(swell*oceanWeight*32.0+.5)/32.0 : wave*.028)*animFade-.012;
    }
    if (sway == 1u) {
        float s = sin(animTime * 1.6 + wp.x * 0.37 + wp.z * 0.23 + wp.y * 0.11);
        if (mode == 3u) lp.y -= 0.0;
        else { lp.x += s * 0.035*animFade; lp.z += cos(animTime * 1.3 + wp.x * 0.2 + wp.z * 0.31) * 0.035*animFade; }
    }
    vec4 world = uModel * vec4(lp, 1.0);
    gl_Position = uViewProj * world;
    vRel = world.xyz;
    vUV = vec3(uv, float(b & 255u));
    vLight = vec3(float((b >> 8) & 15u) / 15.0, float((b >> 12) & 15u) / 15.0, float((b >> 16) & 3u) / 3.0);
    vRadiance = vec3(float(aData.w&15u),float((aData.w>>4)&15u),float((aData.w>>8)&15u))/15.0;
    vTint = int((b >> 18) & 3u);
    vClim = vec2(float((b >> 20) & 63u), float((b >> 26) & 63u)) / 63.0;
    const vec3 normals[7] = vec3[7](vec3(1,0,0), vec3(-1,0,0), vec3(0,1,0), vec3(0,-1,0), vec3(0,0,1), vec3(0,0,-1), vec3(0,1,0));
    vNormal = normalize(mat3(uModel) * normals[face]);
    if (uLightOverride.x >= 0.0) vLight.xy = uLightOverride;
    vLight.x *= uSkyScale;
    const float shades[7] = float[7](0.72, 0.72, 1.0, 0.52, 0.86, 0.86, 0.92);
    vShade = shades[face];
    uint material=(aData.w>>24)&31u;
    vMaterial=material==0u?1.0:float(material)/16.0;
    vMode = int(mode);
}
)";

const char* kTerrainFS = R"(#version 330 core
in vec3 vUV;
flat in float vChunkFade;
in vec3 vLight;
in vec3 vRadiance;
in float vShade;
in float vMaterial;
in vec3 vRel;
in vec2 vClim;
flat in int vMode;
flat in int vTint;
flat in vec3 vNormal;
uniform sampler2DArray uTex;
uniform float uSun;
uniform int uTranslucent;
uniform float uTime;
in vec4 vWater;
flat in int vWaterFlags;
uniform float uWaterLayer, uLavaLayer;
uniform vec3 uSkyCol, uSunDir, uCamPosF;
uniform sampler2D uDepthTex;
uniform int uUseDepth, uUnderwater;
uniform vec2 uNearFar, uViewport;
uniform vec2 uDetailFade;
out vec4 frag;
#ifndef CHUNK_FADE
#define CHUNK_FADE 1
#endif
COMMON
TINTS
float linearZ(float d) {
    float z = d * 2.0 - 1.0;
    return 2.0 * uNearFar.x * uNearFar.y / (uNearFar.y + uNearFar.x - z * (uNearFar.y - uNearFar.x));
}
void main() {
    vec4 c = texture(uTex, vUV);
#if CUTOUT
    if (uTranslucent == 0 && c.a < 0.5) discard;
#endif
#if CUTOUT
    if(uDetailFade.y > 0.0) {
        float df = clamp((length(vRel) - uDetailFade.x) / max(uDetailFade.y - uDetailFade.x, 1.0), 0.0, 1.0);
        if(df > 0.0) {
            vec3 q=floor((vRel+uCamPosF)*16.0);
            float h=fract(sin(dot(q,vec3(12.9898,78.233,37.719)))*43758.5453);
            if(h < df) discard;
        }
    }
#endif
#if CHUNK_FADE
    if(vChunkFade < 0.999) {
        vec3 q=floor((vRel+uCamPosF)*16.0);
        float h=fract(sin(dot(q,vec3(12.9898,78.233,37.719)))*43758.5453);
        if(h > vChunkFade) discard;
    }
#endif
    float sky = vLight.x, blk = vLight.y;
    float fog;
#if WATER
    {
        vec2 faceUV=abs(vNormal.y)>0.5 ? (vRel.xz+uCamPosF.xz) :
                    abs(vNormal.x)>0.5 ? (vRel.zy+uCamPosF.zy) : (vRel.xy+uCamPosF.xy);
        vec2 ab=vec2(0.0);
        vec3 P=vRel;
        vec3 L=clamp(vLight,0.0,1.0);
        vec3 RGB=clamp(vRadiance,0.0,1.0);
        sky=L.x;blk=L.y;
        vec2 uvDx=dFdx(faceUV),uvDy=dFdy(faceUV);
        float footprint=max(length(uvDx),length(uvDy))*16.0;
        float waterLod=clamp(log2(max(footprint,1.0)),0.0,4.0);
        vec2 pixel=(floor(faceUV*16.0)+0.5)/16.0;
        vec2 d=pixel-faceUV;
        if(abs(vNormal.y)>0.5) P.xz+=d;
        else if(abs(vNormal.x)>0.5) P.zy+=d;
        else P.xy+=d;
        float waterDist=length(P);
        float farFade=smoothstep(64.0,112.0,waterDist);
        float detail=1.0-farFade;
        float tick=floor(uTime*8.0)/8.0;
        bool wash=(vWaterFlags&2)!=0;
        bool surface=abs(vNormal.y)>0.5;
        vec2 current=surface?vWater.xy:vec2(0,-max(0.6,length(vWater.xy)*1.5));
        if(!surface && vWater.w<0.5 && length(vWater.xy)<0.01)current=vec2(0);
        float ocean=surface?clamp(vWater.z,0.0,1.0):0.0;
        current+=vec2(.10,.06)*ocean;
        vec2 shift=floor(current*tick*5.0)/16.0;
        vec2 movingPixel=(floor((faceUV-shift)*16.0)+0.5)/16.0;
        vec4 pattern;
        if(farFade<=0.001) pattern=textureLod(uTex,vec3(movingPixel,vUV.z),waterLod);
        else if(farFade>=0.999) pattern=textureLod(uTex,vec3(pixel,vUV.z),waterLod);
        else {
            vec4 still=textureLod(uTex,vec3(pixel,vUV.z),waterLod);
            vec4 moving=textureLod(uTex,vec3(movingPixel,vUV.z),waterLod);
            pattern=mix(still,moving,detail);
        }
        float movingCoast=0.5+0.5*sin(uTime*1.4+(P.x+uCamPosF.x)*0.9+(P.z+uCamPosF.z)*0.65);
        float coastWave=mix(0.5,movingCoast,detail);
        float runup=0.12+0.70*coastWave*coastWave;
        vec2 cell=fract((P.xz+uCamPosF.xz)*2.0);
        int banks=(vWaterFlags>>3)&15;float edge=1.0;
        if((banks&1)!=0)edge=min(edge,1.0-cell.x);if((banks&2)!=0)edge=min(edge,cell.x);
        if((banks&4)!=0)edge=min(edge,1.0-cell.y);if((banks&8)!=0)edge=min(edge,cell.y);
        if(wash && edge>runup)discard;
#if WATER_DEPTH
        frag=vec4(0.0);return;
#endif
        vec3 V=normalize(P),N=vNormal;
        bool top=abs(vNormal.y)>0.5;
        float ripple=floor((sin(pixel.x*4.0+tick)+sin(pixel.y*3.0-tick))*2.0)/4.0*detail;
        if(top) {
            vec2 slope=(vec2(cos(dot(pixel,vec2(.19,.114))-tick*.85)) * vec2(.0684,.04104)
                      + vec2(cos(dot(pixel,vec2(.086,-.162))-tick*.57)) * vec2(.0086,-.0162))*detail;
            N=normalize(vec3(ripple*.025-slope.x*ocean,1.0,.02*sign(sin(pixel.y*6.0+tick))*detail-slope.y*ocean));
        }
        if(top && V.y>0.0)N=-N;
        vec3 lit=shade(vec3(1.0),sky,blk,1.0,1.0,vNormal,P,true,RGB,false);
        float thick=2.5;
        if(uUseDepth==1 && !wash && farFade<0.999) {
            float surface=gl_FragCoord.z+ab.x*dFdx(gl_FragCoord.z)+ab.y*dFdy(gl_FragCoord.z);
            float ls=linearZ(surface);
            float measured=0.0,wsum=0.0;
            for(int i=-2;i<=2;++i)for(int j=-2;j<=2;++j){
                vec2 o=vec2(float(i),float(j))*3.5;
                float w=exp(-0.08*float(i*i+j*j));
                float sd=texture(uDepthTex,clamp((gl_FragCoord.xy+ab+o)/uViewport,0.0,1.0)).r;
                measured+=min(max(linearZ(sd)-ls,0.0),9.0)*w;wsum+=w;
            }
            measured/=wsum;
            thick=mix(measured,2.5,farFade);
        }
        if(wash)thick=0.03;
        float absorb=1.0-exp(-thick*0.26);
        vec3 tint=waterTint(vClim);
        vec3 body=mix(vec3(0.16,0.39,0.43),vec3(0.045,0.17,0.23),absorb)*tint;
        body*=0.82+floor(pattern.b*6.0)/6.0*0.42+ripple*0.035;
        float fres=pow(1.0-max(dot(-V,N),0.0),3.0);
        vec3 col=body*lit;
        float alpha=mix(0.60,1.0,absorb);
        if(top && uUnderwater==0) {
            col=mix(col,uSkyCol*skyCurve(sky)*uSun,fres*0.14*detail);
            float sunBand=floor(pow(max(dot(reflect(V,N),normalize(uSunDir)),0.0),10.0)*4.0)/4.0;
            col+=vec3(0.035,0.031,0.018)*sunBand*step(0.66,pattern.b)*sky*uSun*detail;
            float shoreFoam=(1.0-smoothstep(0.10,0.46,edge))*coastWave;
            float turbulence=length(vWater.xy)*step(thick,0.8)+vWater.w*0.5;
            float foam=max(shoreFoam,turbulence*0.48)*step(0.57,pattern.g);
            float swellPhase=dot(pixel,vec2(.19,.114))-tick*.85;
            float crest=step(.83,sin(swellPhase))*step(.62,pattern.g)*ocean*.56;
            foam=max(foam,crest);
            if(wash)foam=max(foam,smoothstep(runup-0.10,runup,edge)*0.45);
            col=mix(col,vec3(0.65,0.79,0.70)*lit,clamp(foam*max(detail,0.35),0.0,0.62));
            if(wash)alpha=(0.30*(1.0-smoothstep(0.0,max(runup,0.05),edge))+foam*0.22)*smoothstep(runup,runup-0.04,edge);
            else alpha=mix(alpha,max(alpha,0.88),farFade);
        } else if(top) alpha=mix(0.60,0.88,farFade);
        else if(vWater.w>0.5) {
            float streak=step(0.60,pattern.g)*0.32;
            col=mix(col,vec3(0.60,0.77,0.72)*lit,streak);alpha=0.72;
        }
        col=applyFog(col,P,sky,fog);
        alpha=mix(alpha,1.0,fog);
        frag = vec4(col, alpha);
        return;
    }
#else
    if (vTint != 0) {
        vec3 tcol = vTint == 1 ? grassTint(vClim) : vTint == 2 ? foliageTint(vClim) : waterTint(vClim);
        if (vTint == 3 || (c.g > c.r * 1.05 && c.g > c.b)) {
            c.rgb *= tcol;
            
        }
    }
    vec3 L = vLight, P = vRel, RGB = vRadiance;
    {
        vec2 ab = texelCentreStep(vUV.xy);
        L += ab.x * dFdx(vLight) + ab.y * dFdy(vLight);
        RGB=clamp(RGB+ab.x*dFdx(vRadiance)+ab.y*dFdy(vRadiance),0.0,1.0);
        P += ab.x * dFdx(vRel) + ab.y * dFdy(vRel);
        L = clamp(L, 0.0, 1.0);
        float material=vMaterial+ab.x*dFdx(vMaterial)+ab.y*dFdy(vMaterial);
        c.rgb*=floor(clamp(material,.5,1.25)*16.0+.5)/16.0;
    }
    sky = L.x; blk = L.y;
    float ao = 0.84 + 0.16 * L.z;
    vec3 col = abs(vUV.z-uLavaLayer)<.5 ? c.rgb*1.15 : shade(c.rgb,sky,blk,vShade,ao,vNormal,P,true,RGB,true);
    col = applyFog(col, vRel, sky, fog);
    frag = vec4(col, uTranslucent == 1 ? mix(c.a, 1.0, fog) : 1.0);
#endif
}
)";

const char* kBoxVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aFace;
layout(location = 3) in vec4 iR0;
layout(location = 4) in vec4 iR1;
layout(location = 5) in vec4 iR2;
layout(location = 6) in vec4 iLayers;
layout(location = 7) in vec4 iParams;
layout(location = 8) in vec4 iTint;
uniform mat4 uViewProj;
out vec3 vUV;
out vec2 vLight;
out float vShade;
out vec4 vTint;
out float vDist;
out vec3 vRel;
out vec3 vNormal;
void main() {
    vec4 p = vec4(aPos, 1.0);
    vec3 w = vec3(dot(iR0, p), dot(iR1, p), dot(iR2, p));
    gl_Position = uViewProj * vec4(w, 1.0);
    vDist = length(w);
    int f = int(aFace + 0.5);
    float layer = f == 2 ? iLayers.x : (f == 3 ? iLayers.y : (f == 4 ? iLayers.w : iLayers.z));
    if (iParams.z == 0.0 && iParams.w > 0.5 && f == 5) layer = iParams.w - 1.0;
    vec2 uv = aUV;
    if (iParams.z > 0.0) uv = uv * iParams.z + vec2(iParams.w);
    if(iParams.z<0.0) {
        vec3 size=vec3(length(vec3(iR0.x,iR1.x,iR2.x)),length(vec3(iR0.y,iR1.y,iR2.y)),length(vec3(iR0.z,iR1.z,iR2.z)));
        uv*= f<2 ? size.zy : f<4 ? size.xz : size.xy;
    }
    vUV = vec3(uv, layer);
    vLight = iParams.xy / 15.0;
    vec3 n = f == 0 ? vec3(1,0,0) : f == 1 ? vec3(-1,0,0) : f == 2 ? vec3(0,1,0) : f == 3 ? vec3(0,-1,0) : f == 4 ? vec3(0,0,1) : vec3(0,0,-1);
    mat3 m = mat3(iR0.xyz, iR1.xyz, iR2.xyz);
    vec3 wn = normalize(n * m);
    vRel = w;
    vNormal = wn;
    vec3 n2 = wn * wn;
    vShade = n2.x * 0.72 + n2.z * 0.86 + n2.y * (wn.y > 0.0 ? 1.0 : 0.52);
    vTint = iTint;
}
)";

const char* kBoxFS = R"(#version 330 core
in vec3 vUV;
in vec2 vLight;
in float vShade;
in vec4 vTint;
in float vDist;
in vec3 vRel;
in vec3 vNormal;
uniform sampler2DArray uTex;
uniform int uUnlit;
uniform int uViewSpace;
uniform float uOutAlpha;
uniform float uAlphaCut;
out vec4 frag;
COMMON
void main() {
    vec4 c = vUV.z < 0.0 ? vec4(1.0) : texture(uTex, vUV);
    c *= vTint;
    if (c.a < uAlphaCut) discard;
    vec3 col;
    float sky=vLight.x;
    float fog = 0.0;
    if (uUnlit == 1) col = c.rgb * vShade;
    else if (uViewSpace == 1) col = shade(c.rgb, sky, vLight.y, vShade, 1.0, normalize(vec3(0.2, 0.9, 0.3)), vec3(0.0), false, vec3(-1.0),false);
    else {
        vec2 L = vLight;
        vec3 P = vRel;
        if (vUV.z >= 0.0) {
            vec2 ab = texelCentreStep(vUV.xy);
            L = clamp(L + ab.x * dFdx(vLight) + ab.y * dFdy(vLight), 0.0, 1.07);
            P += ab.x * dFdx(vRel) + ab.y * dFdy(vRel);
        }
        sky=L.x;col = shade(c.rgb, sky, L.y, vShade, 1.0, vNormal, P, true, vec3(-1.0),true);
    }
    if (vLight.y > 1.01) col = c.rgb;
    if (uViewSpace == 0 && uUnlit == 0) col = applyFog(col, vRel, sky, fog);
    frag = vec4(col, uOutAlpha >= 0.0 ? uOutAlpha : c.a);
}
)";

const char* kShadowFS = R"(#version 330 core
in vec3 vUV;
in vec3 vRel;
uniform sampler2DArray uTex;
uniform int uSkipSource;
uniform vec3 uSkipPoint;
void main() {
    if(uSkipSource==1 && length(vRel-uSkipPoint)<0.46)discard;
#if CUTOUT
    if (texture(uTex, vUV).a < 0.5) discard;
#endif
}
)";

const char* kVolFS = R"(#version 330 core
uniform sampler2D uDepth;
uniform sampler2DShadow uShadowMap;
uniform mat4 uShadowVP;
uniform mat4 uInvViewProj;
uniform vec2 uSize;
uniform float uTime;
out vec4 frag;
float ign(vec2 p) { return fract(52.9829189 * fract(dot(p, vec2(0.06711056, 0.00583715)))); }
void main() {
    vec2 uv = gl_FragCoord.xy / uSize;
    float d = texture(uDepth, uv).r;
    vec4 wp = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
    vec3 P = wp.xyz / wp.w;
    float dist = length(P);
    vec3 dir = P / max(dist, 1e-4);
    float maxD = min(dist, 110.0);
    const int N = 16;
    float j = ign(gl_FragCoord.xy + fract(uTime * 7.31) * 97.0);
    float lit = 0.0;
    for (int i = 0; i < N; ++i) {
        float t = (float(i) + j) / float(N);
        t = t * t * maxD;
        vec3 c = (uShadowVP * vec4(dir * t, 1.0)).xyz * 0.5 + 0.5;
        lit += (c.x > 0.0 && c.x < 1.0 && c.y > 0.0 && c.y < 1.0 && c.z < 1.0) ? texture(uShadowMap, c) : 1.0;
    }
    frag = vec4(lit / float(N));
}
)";

const char* kPostVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";
const char* kPostFS = R"(#version 330 core
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform sampler2D uVol;
uniform vec2 uVolTexel;
uniform mat4 uInvViewProj;
uniform vec3 uLightDir, uSunCol;
uniform float uTime, uFogDensity;
uniform int uVolumetric, uUnderwater;
uniform int uAspect43;
uniform vec2 uOutSize, uSceneSize;
out vec4 frag;
vec3 filmic(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec2 outUV = gl_FragCoord.xy / uOutSize;
    vec2 uv = outUV;
    if (uAspect43 == 1) {
        float outAspect = uOutSize.x / max(uOutSize.y, 1.0);
        const float target = 4.0 / 3.0;
        if (outAspect > target) {
            float span = target / outAspect;
            float x0 = (1.0 - span) * 0.5;
            if (outUV.x < x0 || outUV.x > x0 + span) { frag = vec4(0,0,0,1); return; }
            uv.x = (outUV.x - x0) / span;
        } else if (outAspect < target) {
            float span = outAspect / target;
            float y0 = (1.0 - span) * 0.5;
            if (outUV.y < y0 || outUV.y > y0 + span) { frag = vec4(0,0,0,1); return; }
            uv.y = (outUV.y - y0) / span;
        }
    }
    vec2 tp = uv * uSceneSize;
    vec2 fl = floor(tp);
    float upscale = max(uOutSize.y / max(uSceneSize.y, 1.0), 1.0);
    vec2 f = clamp((tp - fl - 0.5) * upscale + 0.5, 0.0, 1.0);
    vec4 sc = textureLod(uScene, (fl + f) / uSceneSize, 0.0);
    vec3 col = sc.rgb;
    if (uVolumetric == 1 && sc.a > 0.5) {
        float d = texture(uDepth, uv).r;
        vec4 wp = uInvViewProj * vec4(uv * 2.0 - 1.0, d * 2.0 - 1.0, 1.0);
        vec3 P = wp.xyz / wp.w;
        float dist = length(P);
        vec3 dir = P / max(dist, 1e-4);
        float maxD = min(dist, 110.0);
        vec2 o = uVolTexel * 0.75;
        float lit = (texture(uVol, uv + vec2(-o.x, -o.y)).r + texture(uVol, uv + vec2(o.x, -o.y)).r +
                     texture(uVol, uv + vec2(-o.x, o.y)).r + texture(uVol, uv + vec2(o.x, o.y)).r) * 0.25;
        float cosT = dot(dir, uLightDir);
        const float g = 0.6;
        float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * cosT, 1.5) * 0.08;
        float amount = 1.0 - exp(-maxD * uFogDensity * 3.0);
        col += uSunCol * lit * (phase + 0.025) * amount * (uUnderwater == 1 ? 0.3 : 1.0);
    }
    {
        vec2 t1 = 1.5 / uSceneSize, t2 = 4.0 / uSceneSize;
        vec3 g = textureLod(uScene, uv, 2.0).rgb * 0.30;
        g += textureLod(uScene, uv + vec2(t1.x, 0.0), 3.0).rgb * 0.14 + textureLod(uScene, uv - vec2(t1.x, 0.0), 3.0).rgb * 0.14;
        g += textureLod(uScene, uv + vec2(0.0, t1.y), 3.0).rgb * 0.14 + textureLod(uScene, uv - vec2(0.0, t1.y), 3.0).rgb * 0.14;
        g += textureLod(uScene, uv + t2, 4.0).rgb * 0.035 + textureLod(uScene, uv - t2, 4.0).rgb * 0.035;
        float gl = dot(g, vec3(0.299, 0.587, 0.114));
        vec3 bright = g * smoothstep(0.38, 1.05, gl);
        col += bright * 0.62;
        col = mix(col, max(col, g * 0.92), 0.10);
    }
    col = filmic(col * 1.05);
    float gradeLum = dot(col, vec3(0.299,0.587,0.114));
    col = max(mix(vec3(gradeLum), col, 1.12), vec3(0.0));
    {
        float l2 = dot(col, vec3(0.299,0.587,0.114));
        col *= mix(vec3(0.94,0.99,1.06), vec3(1.05,1.01,0.94), smoothstep(0.15, 0.85, l2));
        col = mix(col, col * col * (3.0 - 2.0 * col), 0.22);
    }
    vec2 q = uv - 0.5;
    col *= 1.0 - dot(q, q) * 0.24;
    frag = vec4(max(col, vec3(0.0)), 1.0);
}
)";

const char* kLineVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aCol;
uniform mat4 uViewProj;
out vec4 vCol;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); vCol = aCol; }
)";
const char* kLineFS = R"(#version 330 core
in vec4 vCol;
out vec4 frag;
void main() { frag = vCol; }
)";

const char* kSkyVS = R"(#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = p;
    gl_Position = vec4(p, 0.9999, 1.0);
}
)";
const char* kSkyFS = R"(#version 330 core
in vec2 vNdc;
uniform mat4 uInvViewProj;
uniform vec3 uTop, uHorizon, uFog, uSunDir;
uniform float uStars;
uniform int uUnderwater;
out vec4 frag;
float hash(vec3 p) { p = fract(p * 0.3183099 + 0.1); p *= 17.0; return fract(p.x * p.y * p.z * (p.x + p.y + p.z)); }
void main() {
    vec4 w = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 d = normalize(w.xyz / w.w);
    float h = d.y;
    vec3 col = mix(uHorizon, uTop, clamp(h * 1.6, 0.0, 1.0));
    col = mix(col, uFog, 1.0 - smoothstep(0.0, 0.22, h));
    if (h < 0.0) col = uFog;
    vec3 sd = normalize(uSunDir);
    float ds = dot(d, sd);
    vec3 right=normalize(cross(sd,vec3(0,0,1))),up=cross(right,sd);
    vec2 sq=vec2(dot(d,right),dot(d,up))/max(ds,.001);
    float sunDisc=ds>0.0?1.0-step(.027,max(abs(sq.x),abs(sq.y))):0.0;
    col += vec3(1.0, 0.98, 0.92) * (pow(max(ds, 0.0), 300.0) * 0.12 + pow(max(ds, 0.0), 18.0) * 0.035) * (1.0 - uStars);
    col = mix(col, vec3(1.55, 1.42, 1.08), sunDisc);
    float dm = dot(d, -sd);
    if (dm > 0.99) {
        vec3 right = normalize(cross(sd, vec3(0.0, 0.0, 1.0)));
        vec3 up = cross(right, sd);
        vec2 q = vec2(dot(d, right), dot(d, up)) / dm;
        float disc = 1.0-step(0.0375,max(abs(q.x),abs(q.y)));
        float crater = hash(floor(vec3(q * 210.0, 3.0))) * 0.12 + hash(floor(vec3(q * 75.0, 7.0))) * 0.1;
        col = mix(col, vec3(0.52, 0.61, 0.72) - crater, disc * 0.9);
    }
    col += vec3(0.8, 0.82, 0.86) * pow(max(dm, 0.0), 60.0) * 0.025 * uStars;
    if (uStars > 0.0 && h > 0.0) {
        vec3 c = floor(d * 220.0);
        float s = hash(c);
        if (s > 0.996) col += vec3(uStars * (s - 0.996) * 220.0);
    }
    if (uUnderwater == 1) col = uFog;
    frag = vec4(col, 1.0);
}
)";

const char* kCloudVS = R"(#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in float aShade;
uniform mat4 uViewProj;
uniform vec3 uCam;
uniform float uHeight,uTime;
out vec3 vRel;
out float vShade;
void main(){vRel=aPos+vec3(-uTime*1.2,uHeight,0)-uCam;vShade=aShade;gl_Position=uViewProj*vec4(vRel,1);}
)";
const char* kCloudFS = R"(#version 330 core
in vec3 vRel;
in float vShade;
uniform vec3 uFog;
uniform float uSun,uOvercast,uRadius;
out vec4 frag;
void main(){
    float day=clamp((uSun-0.2)/0.8,0.0,1.0);
    vec3 col=mix(vec3(0.027,0.037,0.061),vec3(0.91,0.90,0.84),day)*vShade;
    col=mix(col,mix(uFog*0.90,col*0.64,0.4),uOvercast);
    float haze=smoothstep(uRadius*0.36,uRadius*0.88,length(vRel));
    frag=vec4(mix(col,uFog,haze),1.0);
}
)";

const char* kTintVS = R"(#version 330 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    gl_Position = vec4(p, 0.0, 1.0);
}
)";
const char* kTintFS = R"(#version 330 core
uniform vec4 uColor;
out vec4 frag;
void main() { frag = uColor; }
)";

std::string withCommon(const char* src, const char* defs = "#define CUTOUT 1\n") {
    std::string s = src;
    size_t nl = s.find('\n');
    if (nl != std::string::npos) s.insert(nl + 1, defs);
    auto sub = [&](const char* key, const char* text) {
        size_t p = s.find(key);
        if (p != std::string::npos) s.replace(p, std::strlen(key), text);
    };
    sub("COMMON", kCommon);
    sub("TINTS", kTintFns);
    return s;
}

void cubeVerts(std::vector<float>& v) {
    struct F {
        float n[3];
        float a[3], b[3], c[3], d[3];
    };
    const float P[6][4][3] = {
        {{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}},
        {{0, 1, 1}, {1, 1, 1}, {1, 1, 0}, {0, 1, 0}}, {{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}},
    };
    const float UV[4][2] = {{0, 1}, {1, 1}, {1, 0}, {0, 0}};
    const int idx[6] = {0, 1, 2, 2, 3, 0};
    for (int f = 0; f < 6; ++f)
        for (int k : idx) {
            v.push_back(P[f][k][0]);
            v.push_back(P[f][k][1]);
            v.push_back(P[f][k][2]);
            v.push_back(UV[k][0]);
            v.push_back(UV[k][1]);
            v.push_back(float(f));
        }
}

}

bool Renderer::init(int texLayers, const std::vector<uint32_t>& texels) {
    bool ok = true;
    ok &= terrain_.build("terrain", withCommon(kTerrainVS).c_str(), withCommon(kTerrainFS).c_str());
    ok &= terrainOpaque_.build("terrain-opaque", withCommon(kTerrainVS).c_str(),
                               withCommon(kTerrainFS, "#define CUTOUT 0\n#define CHUNK_FADE 0\n").c_str());
    ok &= terrainOpaqueFade_.build("terrain-opaque-fade", withCommon(kTerrainVS).c_str(),
                                   withCommon(kTerrainFS, "#define CUTOUT 0\n").c_str());
    ok &=
        terrainWater_.build("water", withCommon(kTerrainVS).c_str(), withCommon(kTerrainFS, "#define CUTOUT 0\n#define WATER 1\n").c_str());
    ok &= waterDepth_.build("water-depth", withCommon(kTerrainVS).c_str(),
                            withCommon(kTerrainFS, "#define CUTOUT 0\n#define WATER 1\n#define WATER_DEPTH 1\n").c_str());
    ok &= box_.build("box", withCommon(kBoxVS).c_str(), withCommon(kBoxFS).c_str());
    ok &= line_.build("line", kLineVS, kLineFS);
    ok &= sky_s_.build("sky", kSkyVS, kSkyFS);
    ok &= cloud_.build("cloud", kCloudVS, kCloudFS);
    ok &= tint_.build("tint", kTintVS, kTintFS);
    ok &= shadow_.build("shadow", withCommon(kTerrainVS).c_str(), withCommon(kShadowFS).c_str());
    ok &= shadowOpaque_.build("shadow-opaque", withCommon(kTerrainVS).c_str(), withCommon(kShadowFS, "#define CUTOUT 0\n").c_str());
    ok &= post_.build("post", kPostVS, kPostFS);
    ok &= vol_.build("shafts", kPostVS, kVolFS);
    if (!ok) return false;

    gl::GenTextures(1, &texArray_);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
    gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, gl::RGBA8, 16, 16, texLayers, 0, gl::RGBA, gl::UNSIGNED_BYTE, texels.data());
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MIN_FILTER, gl::NEAREST_MIPMAP_NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_S, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_WRAP_T, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_MAX_LEVEL, 4);
    gl::GenerateMipmap(gl::TEXTURE_2D_ARRAY);

    {
        std::vector<uint8_t> c(64 * 64);
        std::vector<float> v(64 * 64);
        Noise n(777);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                float a = x / 64.0f * 6.2831853f, b = y / 64.0f * 6.2831853f;
                v[size_t(y * 64 + x)] =
                    n.noise3(std::cos(a) * 1.6f, std::sin(a) * 1.6f + std::cos(b) * 1.6f, std::sin(b) * 1.6f) * 0.7f +
                    n.noise3(std::cos(a) * 4.0f + 9, std::sin(a) * 4.0f, std::cos(b) * 4.0f + std::sin(b) * 2.5f) * 0.3f;
            }
        std::vector<float> sorted = v;
        std::nth_element(sorted.begin(), sorted.begin() + sorted.size() * 62 / 100, sorted.end());
        float thr = sorted[sorted.size() * 62 / 100];
        for (size_t i = 0; i < v.size(); ++i) c[i] = v[i] > thr ? 255 : 0;
        gl::GenTextures(1, &cloudTex_);
        gl::BindTexture(gl::TEXTURE_2D, cloudTex_);
        gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R8, 64, 64, 0, gl::RED, gl::UNSIGNED_BYTE, c.data());
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::REPEAT);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::REPEAT);
        gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
    }

    gl::GenBuffers(1, &ebo_);
    ensureIndices(16384);

    mdi_ = gl::MultiDrawElementsIndirect != nullptr && !std::getenv("QC_NO_MULTIDRAW");
    gl::GenVertexArrays(1, &arenaVao_);
    gl::GenBuffers(1, &arenaVbo_);
    gl::GenBuffers(1, &originBuf_);
    gl::GenBuffers(1, &indirectBuf_);
    arenaGrow(1u << 18);
    gl::BindVertexArray(arenaVao_);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, ebo_);
    if (mdi_) {
        gl::BindBuffer(gl::ARRAY_BUFFER, originBuf_);
        gl::BufferData(gl::ARRAY_BUFFER, 4096 * 16, nullptr, gl::STREAM_DRAW);
        originCap_ = 4096 * 16;
        gl::EnableVertexAttribArray(1);
        gl::VertexAttribPointer(1, 4, gl::FLOAT, gl::FALSE_, 16, (void*)0);
        gl::VertexAttribDivisor(1, 1);
    }
    gl::BindVertexArray(0);
    QC_LOG("terrain draws: %s", mdi_ ? "multi-draw indirect" : "per section");

    for (int f = 0; f < kRing; ++f) gl::GenQueries(kMarks, queries_[f]);

    std::vector<float> cube;
    cubeVerts(cube);
    gl::GenVertexArrays(1, &boxVao_);
    gl::BindVertexArray(boxVao_);
    gl::GenBuffers(1, &boxVbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, boxVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(cube.size() * sizeof(float)), cube.data(), gl::STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, 24, (void*)0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, gl::FLOAT, gl::FALSE_, 24, (void*)12);
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, gl::FLOAT, gl::FALSE_, 24, (void*)20);
    gl::GenBuffers(1, &boxInst_);
    gl::BindBuffer(gl::ARRAY_BUFFER, boxInst_);
    for (int i = 0; i < 6; ++i) {
        gl::EnableVertexAttribArray(3 + i);
        gl::VertexAttribPointer(3 + i, 4, gl::FLOAT, gl::FALSE_, sizeof(BoxInstance), (void*)(size_t(i) * 16));
        gl::VertexAttribDivisor(3 + i, 1);
    }
    gl::GenVertexArrays(1, &lineVao_);
    gl::BindVertexArray(lineVao_);
    gl::GenBuffers(1, &lineVbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, sizeof(LineVertex), (void*)0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 4, gl::UNSIGNED_BYTE, gl::TRUE_, sizeof(LineVertex), (void*)12);
    gl::GenVertexArrays(1, &emptyVao_);
    gl::GenVertexArrays(1, &cloudVao_);
    gl::BindVertexArray(cloudVao_);
    gl::GenBuffers(1, &cloudVbo_);
    gl::BindBuffer(gl::ARRAY_BUFFER, cloudVbo_);
    gl::BufferData(gl::ARRAY_BUFFER, 0, nullptr, gl::DYNAMIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, gl::FLOAT, gl::FALSE_, 16, (void*)0);
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 1, gl::FLOAT, gl::FALSE_, 16, (void*)12);
    gl::BindVertexArray(0);
    return true;
}

void Renderer::shutdown() {
    sections_.clear();
    free_.clear();
    for (auto& [k, g] : meshes_) destroyMesh(g);
    meshes_.clear();
    if (arenaVbo_) {
        gl::DeleteBuffers(1, &arenaVbo_);
        arenaVbo_ = 0;
    }
    if (arenaVao_) {
        gl::DeleteVertexArrays(1, &arenaVao_);
        arenaVao_ = 0;
    }
    if (originBuf_) {
        gl::DeleteBuffers(1, &originBuf_);
        originBuf_ = 0;
    }
    if (indirectBuf_) {
        gl::DeleteBuffers(1, &indirectBuf_);
        indirectBuf_ = 0;
    }
    for (int f = 0; f < kRing; ++f) gl::DeleteQueries(kMarks, queries_[f]);
    if (fbo_) {
        gl::DeleteFramebuffers(1, &fbo_);
        gl::DeleteTextures(1, &fboColor_);
        gl::DeleteRenderbuffers(1, &fboDepth_);
        fbo_ = 0;
    }
    if (depthFbo_) {
        gl::DeleteFramebuffers(1, &depthFbo_);
        gl::DeleteTextures(1, &depthTex_);
        depthFbo_ = 0;
    }
    if (volFbo_) {
        gl::DeleteFramebuffers(1, &volFbo_);
        gl::DeleteTextures(1, &volTex_);
        volFbo_ = 0;
    }
    if (lightVol_) {
        gl::DeleteTextures(1, &lightVol_);
        lightVol_ = 0;
    }
    if (colorVol_) {
        gl::DeleteTextures(1, &colorVol_);
        colorVol_ = 0;
    }
    if (lightValidity_) {
        gl::DeleteTextures(1, &lightValidity_);
        lightValidity_ = 0;
    }
    gl::DeleteTextures(1, &texArray_);
    gl::DeleteTextures(1, &cloudTex_);
    if (pointShadowFbo_) {
        gl::DeleteFramebuffers(1, &pointShadowFbo_);
        pointShadowFbo_ = 0;
    }
    for (GLuint* t : {&pointShadowAtlas_, &pointStaticAtlas_, &clusterRanges_, &clusterIndices_, &lightData_}) {
        if (*t) gl::DeleteTextures(1, t);
        *t = 0;
    }
    if (pointStaticFbo_) gl::DeleteFramebuffers(1, &pointStaticFbo_);
    if (shadowFbo_) {
        gl::DeleteFramebuffers(1, &shadowFbo_);
        gl::DeleteTextures(1, &shadowTex_);
        shadowFbo_ = 0;
    }
    if (shadowStaticFbo_) {
        gl::DeleteFramebuffers(1, &shadowStaticFbo_);
        gl::DeleteTextures(1, &shadowStaticTex_);
        shadowStaticFbo_ = 0;
    }
    for (Shader* s : {&terrain_, &terrainOpaque_, &terrainOpaqueFade_, &terrainWater_, &waterDepth_, &box_, &line_, &sky_s_, &cloud_,
                      &tint_, &shadow_, &shadowOpaque_, &post_, &vol_})
        s->destroy();
}

void Renderer::resize(int w, int h, float renderScale, int forceW, int forceH) {
    w_ = std::max(w, 1);
    h_ = std::max(h, 1);
    scale_ = std::clamp(renderScale, 0.25f, 1.0f);
    int rw = forceW > 0 ? std::min(std::max(forceW, 1), w_) : std::max(1, int(w_ * scale_));
    int rh = forceH > 0 ? std::min(std::max(forceH, 1), h_) : std::max(1, int(h_ * scale_));
    if (fbo_ && rw == rw_ && rh == rh_) return;
    rw_ = rw;
    rh_ = rh;
    if (!fbo_) {
        gl::GenFramebuffers(1, &fbo_);
        gl::GenTextures(1, &fboColor_);
        gl::GenRenderbuffers(1, &fboDepth_);
    }
    gl::BindTexture(gl::TEXTURE_2D, fboColor_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA8, rw_, rh_, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::BindRenderbuffer(gl::RENDERBUFFER, fboDepth_);
    gl::RenderbufferStorage(gl::RENDERBUFFER, gl::DEPTH_COMPONENT24, rw_, rh_);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, fboColor_, 0);
    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, fboDepth_);
    if (!depthFbo_) {
        gl::GenFramebuffers(1, &depthFbo_);
        gl::GenTextures(1, &depthTex_);
    }
    gl::BindTexture(gl::TEXTURE_2D, depthTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::DEPTH_COMPONENT24, rw_, rh_, 0, gl::DEPTH_COMPONENT, gl::UNSIGNED_INT, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::BindFramebuffer(gl::FRAMEBUFFER, depthFbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::TEXTURE_2D, depthTex_, 0);
    gl::DrawBuffer(gl::NONE);
    gl::ReadBuffer(gl::NONE);
    vw_ = std::max(1, rw_ / 2);
    vh_ = std::max(1, rh_ / 2);
    if (!volFbo_) {
        gl::GenFramebuffers(1, &volFbo_);
        gl::GenTextures(1, &volTex_);
    }
    gl::BindTexture(gl::TEXTURE_2D, volTex_);
    gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R8, vw_, vh_, 0, gl::RED, gl::UNSIGNED_BYTE, nullptr);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::BindFramebuffer(gl::FRAMEBUFFER, volFbo_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, volTex_, 0);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
}

void Renderer::setFancyWater(bool on) { fancyWater_ = on; }

void Renderer::setShadows(int size, float radius, bool volumetric) {
    if (size > 0 && gl::GetIntegerv) {
        int layers = 0;
        gl::GetIntegerv(gl::MAX_ARRAY_TEXTURE_LAYERS, &layers);
        pointCacheCapacity_ = std::min(MaxPointCaches, std::max(1, layers / 6));
    }
    if (size != shadowSize_ || radius != shadowRadius_) shadowValid_ = false;
    shadowSize_ = size;
    shadowRadius_ = radius;
    volumetric_ = volumetric && size > 0;
    if (size <= 0 || size == shadowAlloc_) return;
    auto make = [&](GLuint& fbo, GLuint& tex, bool compare) {
        if (!fbo) {
            gl::GenFramebuffers(1, &fbo);
            gl::GenTextures(1, &tex);
        }
        gl::BindTexture(gl::TEXTURE_2D, tex);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::DEPTH_COMPONENT24, size, size, 0, gl::DEPTH_COMPONENT, gl::UNSIGNED_INT, nullptr);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, compare ? gl::LINEAR : gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, compare ? gl::LINEAR : gl::NEAREST);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        if (compare) {
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_COMPARE_MODE, gl::COMPARE_REF_TO_TEXTURE);
            gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_COMPARE_FUNC, gl::LEQUAL);
        }
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo);
        gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::TEXTURE_2D, tex, 0);
        gl::DrawBuffer(gl::NONE);
        gl::ReadBuffer(gl::NONE);
    };
    make(shadowFbo_, shadowTex_, true);
    make(shadowStaticFbo_, shadowStaticTex_, true);
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    shadowAlloc_ = size;
    shadowValid_ = false;
}

void Renderer::setPointLights(std::vector<PointLight> lights) {
    ++lightFrame_;
    pointUpdates_ = pointTerrainUpdates_ = 0;
    for (auto& l : lights)
        if (!l.id)
            l.id = l.priority ? lightIdentity(1, 0, 0, 0)
                              : lightIdentity(2, int(std::lround(l.pos.x * 1024)), int(std::lround(l.pos.y * 1024)),
                                              int(std::lround(l.pos.z * 1024)));
    auto score = [&](const PointLight& l) {
        float distance = lengthSq(l.pos - cam_.pos);
        float value = l.radius * l.radius / (distance + 4);
        if (l.priority) value += 10000;
        for (const auto& cache : pointCache_)
            if (cache.id == l.id) {
                value *= 1.15f;
                break;
            }
        return value;
    };
    std::stable_sort(lights.begin(), lights.end(), [&](const auto& a, const auto& b) {
        float x = score(a), y = score(b);
        return x == y ? a.id < b.id : x > y;
    });
    if (lights.size() > LightClusters::MaxLights) lights.resize(LightClusters::MaxLights);
    pointLights_ = std::move(lights);
    pointSlots_.assign(pointLights_.size(), -1);
    std::array<bool, MaxPointCaches> used{};
    int count = pointShadowCount();
    for (int i = 0; i < count; ++i)
        for (int k = 0; k < pointCacheCapacity_; ++k)
            if (pointCache_[k].id == pointLights_[i].id) {
                pointSlots_[i] = k;
                used[k] = true;
                pointCache_[k].lastUsed = lightFrame_;
                break;
            }
    for (int i = 0; i < count; ++i)
        if (pointSlots_[i] < 0) {
            int slot = -1;
            for (int k = 0; k < pointCacheCapacity_; ++k)
                if (!used[k] && (slot < 0 || pointCache_[k].lastUsed < pointCache_[slot].lastUsed)) slot = k;
            if (slot < 0) break;
            pointCache_[slot] = PointCache{};
            pointCache_[slot].id = pointLights_[i].id;
            pointCache_[slot].lastUsed = lightFrame_;
            pointSlots_[i] = slot;
            used[slot] = true;
        }
    clusters_.build(pointLights_, cam_.view, cam_.pos, std::tan(cam_.fov * .5f), cam_.aspect);
    stats.clusteredLights = int(pointLights_.size());
    stats.clusterMax = clusters_.maxPerCell;
    stats.clusterReferences = int(clusters_.indices.size());
}

void Renderer::uploadClusterLights() {
    if (!lightData_) {
        gl::GenTextures(1, &lightData_);
        gl::BindTexture(gl::TEXTURE_2D, lightData_);
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::RGBA32F, LightClusters::MaxLights, 3, 0, gl::RGBA, gl::FLOAT, nullptr);
        for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER}) gl::TexParameteri(gl::TEXTURE_2D, p, gl::NEAREST);
        gl::GenTextures(1, &clusterRanges_);
        gl::BindTexture(gl::TEXTURE_3D, clusterRanges_);
        gl::TexImage3D(gl::TEXTURE_3D, 0, gl::RG32UI, LightClusters::X, LightClusters::Y, LightClusters::Z, 0, gl::RG_INTEGER,
                       gl::UNSIGNED_INT, nullptr);
        for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER}) gl::TexParameteri(gl::TEXTURE_3D, p, gl::NEAREST);
        gl::GenTextures(1, &clusterIndices_);
        gl::BindTexture(gl::TEXTURE_2D, clusterIndices_);
        for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER}) gl::TexParameteri(gl::TEXTURE_2D, p, gl::NEAREST);
    }
    std::array<vec4, LightClusters::MaxLights * 3> data{};
    pointShadowValid_ = 0;
    for (size_t i = 0; i < pointLights_.size(); ++i) {
        const auto& l = pointLights_[i];
        vec3 p = l.pos - cam_.pos, c = emissionPeak(l.color, l.emission);
        data[i] = {p.x, p.y, p.z, l.radius};
        data[i + LightClusters::MaxLights] = {c.x, c.y, c.z, 0};
        int slot = pointSlots_[i];
        bool valid = slot >= 0 && pointCache_[slot].valid && pointCache_[slot].liveValid && lengthSq(pointCache_[slot].pos - l.pos) < 1e-8f;
        if (valid) ++pointShadowValid_;
        data[i + LightClusters::MaxLights * 2] = {float(valid ? slot : -1), l.stationary ? 1.0f : 0.0f,
                                                  valid ? float(pointCache_[slot].resolution) / pointShadowSize_ : 1.0f, 0};
    }
    gl::ActiveTexture(gl::TEXTURE0 + 13);
    gl::BindTexture(gl::TEXTURE_2D, lightData_);
    gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, LightClusters::MaxLights, 3, gl::RGBA, gl::FLOAT, data.data());
    gl::ActiveTexture(gl::TEXTURE0 + 14);
    gl::BindTexture(gl::TEXTURE_3D, clusterRanges_);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, 0, 0, 0, LightClusters::X, LightClusters::Y, LightClusters::Z, gl::RG_INTEGER, gl::UNSIGNED_INT,
                      clusters_.ranges.data());
    int rows = std::max(1, int((clusters_.indices.size() + 1023) / 1024));
    clusters_.indices.resize(size_t(rows) * 1024, 0);
    gl::ActiveTexture(gl::TEXTURE0 + 15);
    gl::BindTexture(gl::TEXTURE_2D, clusterIndices_);
    if (rows > clusterIndexRows_) {
        clusterIndexRows_ = rows;
        gl::TexImage2D(gl::TEXTURE_2D, 0, gl::R32UI, 1024, rows, 0, gl::RED_INTEGER, gl::UNSIGNED_INT, clusters_.indices.data());
    } else
        gl::TexSubImage2D(gl::TEXTURE_2D, 0, 0, 0, 1024, rows, gl::RED_INTEGER, gl::UNSIGNED_INT, clusters_.indices.data());
    gl::ActiveTexture(gl::TEXTURE0);
}

void Renderer::setLightingUniforms(Shader& s) {
    s.set("uUseLiveSky", 1);
    auto bindMoving = [&](int slot, int unit, const char* sampler, const char* originName, const char* sizeName) {
        GLuint tex = 0;
        vec3 origin(0), size(0);
        int id = bodyLightMeshes_[size_t(slot)];
        auto it = meshes_.find(id);
        if (id && it != meshes_.end() && it->second.bodyLight) {
            tex = it->second.bodyLight;
            const auto& o = it->second.bodyLightOrigin;
            const auto& z = it->second.bodyLightSize;
            origin = vec3(float(o[0]), float(o[1]), float(o[2]));
            size = vec3(float(z[0]), float(z[1]), float(z[2]));
        }
        s.set(sampler, unit);
        s.set(originName, origin);
        s.set(sizeName, size);
        gl::ActiveTexture(gl::TEXTURE0 + unit);
        gl::BindTexture(gl::TEXTURE_3D, tex);
    };
    bindMoving(0, 9, "uMovingLight0", "uMovingLightOrigin0", "uMovingLightSize0");
    bindMoving(1, 10, "uMovingLight1", "uMovingLightOrigin1", "uMovingLightSize1");
    bindMoving(2, 6, "uMovingLight2", "uMovingLightOrigin2", "uMovingLightSize2");
    bindMoving(3, 8, "uMovingLight3", "uMovingLightOrigin3", "uMovingLightSize3");
    s.set("uLightValidity", 12);
    gl::ActiveTexture(gl::TEXTURE0 + 12);
    gl::BindTexture(gl::TEXTURE_3D, lightValidity_);
    s.set("uColorVol", 11);
    gl::ActiveTexture(gl::TEXTURE0 + 11);
    gl::BindTexture(gl::TEXTURE_3D, colorVol_);
    s.set("uSkyExposure", sky_.skyExposure);
    s.set("uCausticTime", time_);
    s.set("uPointVolume", 2);
    s.set("uPointCam", cam_.pos);
    s.set("uPointWindow", vec4(float(lvMinX_), float(lvMinZ_), float(lightVol_ ? lvN_ : 0), float(WH)));
    s.set("uPointCount", int(pointLights_.size()));
    s.set("uPointShadows", 7);
    gl::ActiveTexture(gl::TEXTURE0 + 7);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, pointShadowPass_ ? 0 : pointShadowAtlas_);
    s.set("uLightData", 13);
    gl::ActiveTexture(gl::TEXTURE0 + 13);
    gl::BindTexture(gl::TEXTURE_2D, lightData_);
    s.set("uClusterRanges", 14);
    gl::ActiveTexture(gl::TEXTURE0 + 14);
    gl::BindTexture(gl::TEXTURE_3D, clusterRanges_);
    s.set("uClusterIndices", 15);
    gl::ActiveTexture(gl::TEXTURE0 + 15);
    gl::BindTexture(gl::TEXTURE_2D, clusterIndices_);
    s.set("uClusterView", cam_.view);
    float tangent = std::tan(cam_.fov * .5f);
    s.set("uClusterTan", vec2(tangent * cam_.aspect, tangent));
    s.set("uClusterGrid", vec3(LightClusters::X, LightClusters::Y, LightClusters::Z));
    s.set("uClusterDepth", vec2(LightClusters::Near, LightClusters::Z / std::log(LightClusters::Far / LightClusters::Near)));
    s.set("uLightDir", sky_.lightDir);
    s.set("uSunCol", sky_.sunCol);
    s.set("uAmbCol", sky_.ambCol);
    s.set("uFog", sky_.fogColor);
    s.set("uFogRange", vec2(sky_.fogStart, sky_.fogEnd));
    s.set("uFogDensity", sky_.fogDensity);
    s.set("uCamY", cam_.pos.y);
    s.set("uMedium", sky_.underwater || sky_.inLava ? 1 : 0);
    bool on = shadowsActive() && shadowTex_;
    s.set("uShadowOn", on ? 1 : 0);
    s.set("uShadowVP", shadowVP_);
    s.set("uShadowMap", 4);
    s.set("uShadowTexel", 1.0f / float(std::max(shadowSize_, 1)));
    gl::ActiveTexture(gl::TEXTURE4);
    gl::BindTexture(gl::TEXTURE_2D, on ? sunShadowTexture() : 0);
    gl::ActiveTexture(gl::TEXTURE0);
}

int Renderer::shadowNear(vec3 c) const {
    if (!shadowValid_) return 0;
    float dx = c.x - shadowCam_.x, dz = c.z - shadowCam_.z, d2 = dx * dx + dz * dz;
    float near = shadowRadius_ * 0.6f + 14.0f, reach = shadowRadius_ * 1.25f + 14.0f;
    return d2 < near * near ? 2 : d2 < reach * reach ? 1 : 0;
}

void Renderer::terrainChanged(vec3 c) {
    for (auto& cache : pointCache_)
        if (lengthSq(cache.pos - c) < (cache.radius + 14) * (cache.radius + 14)) cache.valid = false;
    int n = shadowNear(c);
    if (n == 2)
        ++terrainEpoch_;
    else if (n == 1)
        shadowSoftDirty_ = true;
}

void Renderer::renderStaticShadow() {
    vec3 L = sky_.lightDir;
    L = normalize(vec3(L.x, std::max(L.y, 0.2f), L.z));
    vec3 f = L * -1.0f;
    vec3 right = normalize(cross(f, vec3(0, 1, 0)), {1, 0, 0});
    vec3 up = cross(right, f);
    const float R = shadowRadius_, D = 170.0f;
    float texel = 2.0f * R / float(std::max(shadowSize_, 1));
    float cx = dot(cam_.pos, right), cy = dot(cam_.pos, up);
    float ox = cx - std::floor(cx / texel) * texel, oy = cy - std::floor(cy / texel) * texel;
    mat4 m;
    m.m[0] = right.x / R;
    m.m[1] = up.x / R;
    m.m[2] = f.x / D;
    m.m[3] = 0;
    m.m[4] = right.y / R;
    m.m[5] = up.y / R;
    m.m[6] = f.y / D;
    m.m[7] = 0;
    m.m[8] = right.z / R;
    m.m[9] = up.z / R;
    m.m[10] = f.z / D;
    m.m[11] = 0;
    m.m[12] = ox / R;
    m.m[13] = oy / R;
    m.m[14] = 0;
    m.m[15] = 1;
    shadowStaticVP_ = m;
    shadowVP_ = m;
    shadowCam_ = cam_.pos;
    shadowLight_ = L;
    shadowEpoch_ = terrainEpoch_;
    shadowValid_ = true;
    shadowSoftDirty_ = false;
    shadowTime_ = time_;
    stats.shadowRenders++;

    gl::BindFramebuffer(gl::FRAMEBUFFER, shadowStaticFbo_);
    gl::Viewport(0, 0, shadowSize_, shadowSize_);
    gl::Clear(gl::DEPTH_BUFFER_BIT);
    shadowList_.clear();
    const float r = CS * VOX * 0.87f;
    for (auto& [k, g] : sections_) {
        if (!g.opaque && !g.cutout) continue;
        vec3 rel = g.center - cam_.pos;
        float lx = dot(rel, right) + ox, ly = dot(rel, up) + oy, lz = dot(rel, f);
        if (std::fabs(lx) > R + r || std::fabs(ly) > R + r || std::fabs(lz) > D + r) continue;
        shadowList_.push_back(&g);
    }
    mat4 model;
    model.m[12] = -cam_.pos.x;
    model.m[13] = -cam_.pos.y;
    model.m[14] = -cam_.pos.z;
    const int keepDrawn = stats.drawn, keepQuads = stats.quads;
    for (Shader* s : {&shadowOpaque_, &shadow_}) {
        s->use();
        s->set("uSkipSource", 0);
        s->set("uViewProj", shadowVP_);
        s->set("uTime", time_);
        s->set("uTex", 0);
        s->set("uLightOverride", vec2(-1, -1));
        s->set("uSkyScale", 1.0f);
        s->set("uUseVol", 0);
        s->set("uUVOffset", vec3(0, 0, 0));
        s->set("uModel", model);
        drawSections(shadowList_, s == &shadowOpaque_ ? P_OPAQUE : P_CUTOUT, false);
        if (s == &shadow_) {
            detailList_.clear();
            for (const auto* g : shadowList_)
                if (g->detail && lengthSq(g->center - cam_.pos) < detailFar_ * detailFar_) detailList_.push_back(g);
            drawSections(detailList_, P_DETAIL, false);
        }
    }
    stats.drawn = keepDrawn;
    stats.quads = keepQuads;
}

void Renderer::initPointAtlas() {
    if (pointShadowAtlas_) return;
    int maxLayers = 0;
    gl::GetIntegerv(gl::MAX_ARRAY_TEXTURE_LAYERS, &maxLayers);
    pointCacheCapacity_ = std::min(MaxPointCaches, std::max(1, maxLayers / 6));
    gl::GenFramebuffers(1, &pointShadowFbo_);
    gl::GenFramebuffers(1, &pointStaticFbo_);
    auto allocate = [&](GLuint& texture, bool compare) {
        gl::GenTextures(1, &texture);
        gl::BindTexture(gl::TEXTURE_2D_ARRAY, texture);
        gl::TexImage3D(gl::TEXTURE_2D_ARRAY, 0, gl::DEPTH_COMPONENT24, pointShadowSize_, pointShadowSize_, pointCacheCapacity_ * 6, 0,
                       gl::DEPTH_COMPONENT, gl::FLOAT, nullptr);
        for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER})
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, p, compare ? gl::LINEAR : gl::NEAREST);
        for (auto p : {gl::TEXTURE_WRAP_S, gl::TEXTURE_WRAP_T}) gl::TexParameteri(gl::TEXTURE_2D_ARRAY, p, gl::CLAMP_TO_EDGE);
        if (compare) {
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_COMPARE_MODE, gl::COMPARE_REF_TO_TEXTURE);
            gl::TexParameteri(gl::TEXTURE_2D_ARRAY, gl::TEXTURE_COMPARE_FUNC, gl::LEQUAL);
        }
    };
    allocate(pointStaticAtlas_, false);
    allocate(pointShadowAtlas_, true);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
}

bool Renderer::pointShadowNeedsUpdate(int light, const std::vector<BoxInstance>& boxes, uint64_t bodyStamp) {
    int slot = pointSlots_[light];
    if (slot < 0) return false;
    const auto& l = pointLights_[light];
    auto& cache = pointCache_[slot];
    uint64_t stamp = bodyStamp;
    auto add = [&](int64_t v) { stamp ^= uint64_t(v) + 0x9e3779b97f4a7c15ull + (stamp << 6) + (stamp >> 2); };
    for (const auto& b : boxes) {
        vec3 mid(b.r0.w + (b.r0.x + b.r0.y + b.r0.z) * .5f, b.r1.w + (b.r1.x + b.r1.y + b.r1.z) * .5f,
                 b.r2.w + (b.r2.x + b.r2.y + b.r2.z) * .5f);
        float distance = lengthSq(mid + cam_.pos - l.pos);
        if (b.params.y > 15 || distance < .25f || distance > (l.radius + 3) * (l.radius + 3)) continue;
        for (int axis = 0; axis < 3; ++axis) {
            const vec4& row = axis == 0 ? b.r0 : axis == 1 ? b.r1 : b.r2;
            add(std::llround((row.w + cam_.pos[axis]) * 4096));
            add(std::llround(row.x * 65536));
            add(std::llround(row.y * 65536));
            add(std::llround(row.z * 65536));
        }
        add(int(b.layers.x));
        add(int(b.layers.y));
        add(int(b.layers.z));
        add(int(b.layers.w));
    }
    bool moved = lengthSq(cache.pos - l.pos) > 1e-8f || cache.radius != l.radius;
    bool refine = cache.valid && cache.resolution < pointShadowSize_ && lengthSq(l.pos - cam_.pos) < 12 * 12;
    bool terrain = !cache.valid || moved || refine;
    bool update = terrain || !cache.liveValid || cache.movingStamp != stamp;
    if (!update) {
        ++stats.shadowCacheHits;
        return false;
    }
    bool urgent = l.priority || (!refine && lengthSq(l.pos - cam_.pos) < 8 * 8);
    if (!urgent && ((terrain && pointTerrainUpdates_ >= 3) || pointUpdates_ >= 8)) {
        ++stats.shadowDeferred;
        return false;
    }
    if (refine) cache.resolution = pointShadowSize_;
    if (l.priority) cache.resolution = pointShadowSize_;
    cache.refresh = terrain;
    cache.movingStamp = stamp;
    cache.liveValid = true;
    cache.pos = l.pos;
    cache.radius = l.radius;
    cache.valid = true;
    ++pointUpdates_;
    if (terrain) ++pointTerrainUpdates_;
    return true;
}

void Renderer::beginPointShadow(int light, int face) {
    initPointAtlas();
    pointShadowPass_ = true;
    pointShadowIndex_ = light;
    ++stats.pointLivePasses;
    int slot = pointSlots_[light];
    auto& cache = pointCache_[slot];
    int size = cache.resolution;
    gl::BindFramebuffer(gl::FRAMEBUFFER, pointStaticFbo_);
    gl::FramebufferTextureLayer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, pointStaticAtlas_, 0, slot * 6 + face);
    gl::DrawBuffer(gl::NONE);
    gl::ReadBuffer(gl::NONE);
    gl::Viewport(0, 0, size, size);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::CULL_FACE);
    gl::Enable(gl::POLYGON_OFFSET_FILL);
    gl::PolygonOffset(1.1f, 1.5f);
    if (cache.refresh) gl::Clear(gl::DEPTH_BUFFER_BIT);
    const vec3 dirs[] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
    const vec3 ups[] = {{0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0}};
    const auto& l = pointLights_[light];
    vec3 source = l.pos - cam_.pos;
    shadowVP_ = perspective(PI * 0.5f, 1, 0.08f, l.radius) * lookAt(source, source + dirs[face], ups[face]);
    Frustum view = Frustum::fromMatrix(shadowVP_);
    pointFaceFrustum_ = view;
    shadowList_.clear();
    if (cache.refresh) {
        ++stats.pointTerrainPasses;
        for (auto& [key, g] : sections_) {
            if ((g.opaque || g.cutout) && lengthSq(g.center - l.pos) < (l.radius + 14) * (l.radius + 14) &&
                view.sphereVisible(g.center - cam_.pos, 14))
                shadowList_.push_back(&g);
        }
        mat4 model;
        model.m[12] = -cam_.pos.x;
        model.m[13] = -cam_.pos.y;
        model.m[14] = -cam_.pos.z;
        const int drawn = stats.drawn, quads = stats.quads;
        for (Shader* s : {&shadowOpaque_, &shadow_}) {
            s->use();
            s->set("uViewProj", shadowVP_);
            s->set("uTime", time_);
            s->set("uTex", 0);
            s->set("uSkipSource", 1);
            s->set("uSkipPoint", source);
            s->set("uUseVol", 0);
            s->set("uLightOverride", vec2(-1, -1));
            s->set("uSkyScale", 1.0f);
            s->set("uUVOffset", vec3());
            s->set("uModel", model);
            drawSections(shadowList_, s == &shadowOpaque_ ? P_OPAQUE : P_CUTOUT, false);
        }
        stats.drawn = drawn;
        stats.quads = quads;
    }
    gl::BindFramebuffer(gl::READ_FRAMEBUFFER, pointStaticFbo_);
    gl::BindFramebuffer(gl::DRAW_FRAMEBUFFER, pointShadowFbo_);
    gl::FramebufferTextureLayer(gl::DRAW_FRAMEBUFFER, gl::DEPTH_ATTACHMENT, pointShadowAtlas_, 0, slot * 6 + face);
    gl::DrawBuffer(gl::NONE);
    gl::BlitFramebuffer(0, 0, size, size, 0, 0, size, size, gl::DEPTH_BUFFER_BIT, gl::NEAREST);
    gl::BindFramebuffer(gl::FRAMEBUFFER, pointShadowFbo_);
}
void Renderer::endPointShadows() {
    pointShadowPass_ = false;
    uploadClusterLights();
    gl::Disable(gl::POLYGON_OFFSET_FILL);
    gl::Enable(gl::CULL_FACE);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, rw_, rh_);
}

void Renderer::beginShadow() {
    if (!shadowsActive() || !shadowFbo_) return;
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    gl::Enable(gl::POLYGON_OFFSET_FILL);
    gl::PolygonOffset(1.6f, 3.0f);
    vec3 L = sky_.lightDir;
    L = normalize(vec3(L.x, std::max(L.y, 0.2f), L.z));
    vec3 moved = cam_.pos - shadowCam_;
    bool stale = !shadowValid_ || shadowEpoch_ != terrainEpoch_ || dot(L, shadowLight_) < 0.999996f ||
                 moved.x * moved.x + moved.z * moved.z > (shadowRadius_ * 0.1f) * (shadowRadius_ * 0.1f) || std::fabs(moved.y) > 8.0f ||
                 (shadowSoftDirty_ && time_ - shadowTime_ > 0.5f) || time_ < shadowTime_;
    if (stale)
        renderStaticShadow();
    else {
        mat4 t;
        t.m[12] = moved.x;
        t.m[13] = moved.y;
        t.m[14] = moved.z;
        shadowVP_ = shadowStaticVP_ * t;
    }
    shadowLive_ = false;
    gl::BindFramebuffer(gl::FRAMEBUFFER, shadowFbo_);
    gl::Viewport(0, 0, shadowSize_, shadowSize_);
    gl::BindVertexArray(0);
}

void Renderer::ensureLiveShadow() {
    if (shadowLive_ || pointShadowPass_) return;
    gl::BindFramebuffer(gl::READ_FRAMEBUFFER, shadowStaticFbo_);
    gl::BindFramebuffer(gl::DRAW_FRAMEBUFFER, shadowFbo_);
    gl::BlitFramebuffer(0, 0, shadowSize_, shadowSize_, 0, 0, shadowSize_, shadowSize_, gl::DEPTH_BUFFER_BIT, gl::NEAREST);
    gl::BindFramebuffer(gl::FRAMEBUFFER, shadowFbo_);
    gl::Viewport(0, 0, shadowSize_, shadowSize_);
    shadowLive_ = true;
}

bool Renderer::bodyVisible(vec3 center, float radius, bool shadowPass) const {
    if (!shadowPass) return cam_.frustum.sphereVisible(center, radius);
    return (pointShadowPass_ ? pointFaceFrustum_ : Frustum::fromMatrix(shadowVP_)).sphereVisible(center, radius);
}

void Renderer::drawMeshShadow(int id, const mat4& model, vec3 uvOrigin) {
    if (!pointShadowPass_ && (!shadowsActive() || !shadowFbo_)) return;
    auto it = meshes_.find(id);
    if (it == meshes_.end() || !it->second.solidQuads) return;
    ensureLiveShadow();
    shadow_.use();
    shadow_.set("uSkipSource", pointShadowPass_ ? 1 : 0);
    if (pointShadowPass_) shadow_.set("uSkipPoint", pointLights_[pointShadowIndex_].pos - cam_.pos);
    shadow_.set("uViewProj", shadowVP_);
    shadow_.set("uTime", time_);
    shadow_.set("uTex", 0);
    shadow_.set("uLightOverride", vec2(-1, -1));
    shadow_.set("uSkyScale", 1.0f);
    shadow_.set("uUseVol", 0);
    shadow_.set("uUVOffset", uvOrigin);
    shadow_.set("uModel", model);
    gl::VertexAttrib3f(1, 0, 0, 0);
    gl::BindVertexArray(it->second.vao);
    gl::DrawElements(gl::TRIANGLES, it->second.solidQuads * 6, gl::UNSIGNED_INT, nullptr);
    gl::BindVertexArray(0);
}

void Renderer::drawBoxesShadow(const std::vector<BoxInstance>& boxes) {
    if ((!pointShadowPass_ && (!shadowsActive() || !shadowFbo_)) || boxes.empty()) return;
    if (pointShadowPass_) {
        pointBoxes_.clear();
        const auto& light = pointLights_[pointShadowIndex_];
        vec3 origin = light.pos - cam_.pos;
        for (const auto& b : boxes) {
            vec3 mid(b.r0.w + (b.r0.x + b.r0.y + b.r0.z) * 0.5f, b.r1.w + (b.r1.x + b.r1.y + b.r1.z) * 0.5f,
                     b.r2.w + (b.r2.x + b.r2.y + b.r2.z) * 0.5f);
            if (b.params.y > 15 || lengthSq(mid - origin) < 0.25f || lengthSq(mid - origin) > (light.radius + 3) * (light.radius + 3))
                continue;
            float radius =
                0.5f * (length(vec3(b.r0.x, b.r1.x, b.r2.x)) + length(vec3(b.r0.y, b.r1.y, b.r2.y)) + length(vec3(b.r0.z, b.r1.z, b.r2.z)));
            if (pointFaceFrustum_.sphereVisible(mid, radius)) pointBoxes_.push_back(b);
        }
        drawBoxes(pointBoxes_, false, &shadowVP_, true, 1);
    } else {
        pointBoxes_.clear();
        Frustum f = Frustum::fromMatrix(shadowVP_);
        float reach = shadowRadius_ + 5.0f;
        for (const auto& b : boxes) {
            vec3 mid(b.r0.w + (b.r0.x + b.r0.y + b.r0.z) * 0.5f, b.r1.w + (b.r1.x + b.r1.y + b.r1.z) * 0.5f,
                     b.r2.w + (b.r2.x + b.r2.y + b.r2.z) * 0.5f);
            if (mid.x * mid.x + mid.z * mid.z > reach * reach) continue;
            float radius =
                0.5f * (length(vec3(b.r0.x, b.r1.x, b.r2.x)) + length(vec3(b.r0.y, b.r1.y, b.r2.y)) + length(vec3(b.r0.z, b.r1.z, b.r2.z)));
            if (f.sphereVisible(mid, radius)) pointBoxes_.push_back(b);
        }
        if (pointBoxes_.empty()) return;
        ensureLiveShadow();
        drawBoxes(pointBoxes_, false, &shadowVP_, true, 1);
    }
}

void Renderer::endShadow() {
    gl::Disable(gl::POLYGON_OFFSET_FILL);
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, rw_, rh_);
    gl::Enable(gl::CULL_FACE);
    gl::Enable(gl::DEPTH_TEST);
    gpuMark(1);
}

void Renderer::initLightVolume(int size) {
    if (lightVol_ && lvN_ == size) return;
    if (lightVol_) gl::DeleteTextures(1, &lightVol_);
    if (colorVol_) gl::DeleteTextures(1, &colorVol_);
    if (lightValidity_) gl::DeleteTextures(1, &lightValidity_);
    gl::GenTextures(1, &lightValidity_);
    gl::BindTexture(gl::TEXTURE_3D, lightValidity_);
    int columns = size / CS;
    std::vector<uint8_t> valid(size_t(columns) * columns * NSEC * 4, 0);
    gl::TexImage3D(gl::TEXTURE_3D, 0, gl::RGBA8, columns, NSEC, columns, 0, gl::RGBA, gl::UNSIGNED_BYTE, valid.data());
    for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER}) gl::TexParameteri(gl::TEXTURE_3D, p, gl::NEAREST);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_S, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_R, gl::REPEAT);
    gl::GenTextures(1, &colorVol_);
    gl::BindTexture(gl::TEXTURE_3D, colorVol_);
    gl::TexImage3D(gl::TEXTURE_3D, 0, gl::RGB, size, WH, size, 0, gl::RGB, gl::UNSIGNED_BYTE, nullptr);
    std::vector<uint8_t> black(size_t(size) * WH * 3, 0);
    for (int z = 0; z < size; ++z) gl::TexSubImage3D(gl::TEXTURE_3D, 0, 0, 0, z, size, WH, 1, gl::RGB, gl::UNSIGNED_BYTE, black.data());
    for (auto p : {gl::TEXTURE_MIN_FILTER, gl::TEXTURE_MAG_FILTER}) gl::TexParameteri(gl::TEXTURE_3D, p, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_S, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_R, gl::REPEAT);
    lvN_ = size;
    gl::GenTextures(1, &lightVol_);
    gl::BindTexture(gl::TEXTURE_3D, lightVol_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexImage3D(gl::TEXTURE_3D, 0, gl::RGBA8, size, WH, size, 0, gl::RGBA, gl::UNSIGNED_BYTE, nullptr);
    std::vector<uint8_t> slab(size_t(size) * WH * 4);
    for (int z = 0; z < size; ++z) gl::TexSubImage3D(gl::TEXTURE_3D, 0, 0, 0, z, size, WH, 1, gl::RGBA, gl::UNSIGNED_BYTE, slab.data());
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_S, gl::REPEAT);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_R, gl::REPEAT);
    gl::BindTexture(gl::TEXTURE_3D, 0);
}

void Renderer::invalidateLightColumn(int slotX, int slotZ) {
    if (!lightValidity_) return;
    uint8_t invalid[NSEC * 4] = {};
    gl::BindTexture(gl::TEXTURE_3D, lightValidity_);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, slotX, 0, slotZ, 1, NSEC, 1, gl::RGBA, gl::UNSIGNED_BYTE, invalid);
    gl::BindTexture(gl::TEXTURE_3D, 0);
}

void Renderer::uploadLightBlock(int tx, int ty, int tz, const uint8_t* rg, const uint8_t* rgb) {
    if (!lightVol_) return;
    gl::BindTexture(gl::TEXTURE_3D, lightVol_);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, tx, ty, tz, CS, CS, CS, gl::RGBA, gl::UNSIGNED_BYTE, rg);
    gl::BindTexture(gl::TEXTURE_3D, colorVol_);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, tx, ty, tz, CS, CS, CS, gl::RGB, gl::UNSIGNED_BYTE, rgb);
    uint8_t valid[4] = {255, 255, 255, 255};
    gl::BindTexture(gl::TEXTURE_3D, lightValidity_);
    gl::TexSubImage3D(gl::TEXTURE_3D, 0, tx / CS, ty / CS, tz / CS, 1, 1, 1, gl::RGBA, gl::UNSIGNED_BYTE, valid);
    gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
    gl::BindTexture(gl::TEXTURE_3D, 0);
}

void Renderer::setVolumeUniforms(Shader& s, bool use) {
    s.set("uUseVol", use && lightVol_ ? 1 : 0);
    s.set("uLightValidity", 12);
    gl::ActiveTexture(gl::TEXTURE0 + 12);
    gl::BindTexture(gl::TEXTURE_3D, lightValidity_);
    s.set("uLightVol", 2);
    s.set("uCamPos", cam_.pos);
    s.set("uVolWindow", vec4(float(lvMinX_), float(lvMinZ_), float(lvN_), float(WH)));
    gl::ActiveTexture(gl::TEXTURE2);
    gl::BindTexture(gl::TEXTURE_3D, lightVol_);
    gl::ActiveTexture(gl::TEXTURE0);
}

void Renderer::ensureIndices(int quads) {
    if (quads <= eboQuads_) return;
    int n = std::max(quads, eboQuads_ * 2);
    std::vector<uint32_t> idx(size_t(n) * 6);
    for (int q = 0; q < n; ++q) {
        uint32_t b = uint32_t(q) * 4;
        uint32_t* o = &idx[size_t(q) * 6];
        o[0] = b;
        o[1] = b + 1;
        o[2] = b + 2;
        o[3] = b + 2;
        o[4] = b + 3;
        o[5] = b;
    }
    gl::BindVertexArray(0);
    gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, ebo_);
    gl::BufferData(gl::ELEMENT_ARRAY_BUFFER, GLsizeiptr(idx.size() * 4), idx.data(), gl::STATIC_DRAW);
    eboQuads_ = n;
}

constexpr size_t QUAD_BYTES = 4 * VERT_WORDS * 4;

void Renderer::arenaGrow(uint32_t need) {
    uint32_t newCap = std::max(arenaCap_ * 2, arenaCap_ + need);
    GLuint nb = 0;
    gl::GenBuffers(1, &nb);
    gl::BindVertexArray(0);
    gl::BindBuffer(gl::COPY_WRITE_BUFFER, nb);
    gl::BufferData(gl::COPY_WRITE_BUFFER, GLsizeiptr(size_t(newCap) * QUAD_BYTES), nullptr, gl::STATIC_DRAW);
    if (arenaCap_) {
        gl::BindBuffer(gl::COPY_READ_BUFFER, arenaVbo_);
        gl::CopyBufferSubData(gl::COPY_READ_BUFFER, gl::COPY_WRITE_BUFFER, 0, 0, GLsizeiptr(size_t(arenaCap_) * QUAD_BYTES));
    }
    if (arenaVbo_) gl::DeleteBuffers(1, &arenaVbo_);
    arenaVbo_ = nb;
    uint32_t start = arenaCap_, len = newCap - arenaCap_;
    if (!free_.empty()) {
        auto last = std::prev(free_.end());
        if (last->first + last->second == arenaCap_) {
            start = last->first;
            len += last->second;
            free_.erase(last);
        }
    }
    free_[start] = len;
    stats.gpuBytes += size_t(newCap - arenaCap_) * QUAD_BYTES;
    arenaCap_ = newCap;
    gl::BindVertexArray(arenaVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, arenaVbo_);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribIPointer(0, 4, gl::UNSIGNED_INT, 4 * VERT_WORDS, (void*)0);
    gl::BindVertexArray(0);
}

uint32_t Renderer::arenaAlloc(uint32_t n) {
    for (;;) {
        for (auto it = free_.begin(); it != free_.end(); ++it) {
            if (it->second < n) continue;
            uint32_t off = it->first, len = it->second;
            free_.erase(it);
            if (len > n) free_[off + n] = len - n;
            return off;
        }
        arenaGrow(n);
    }
}

void Renderer::arenaFree(uint32_t off, uint32_t n) {
    if (!n) return;
    auto next = free_.lower_bound(off);
    if (next != free_.end() && off + n == next->first) {
        n += next->second;
        next = free_.erase(next);
    }
    if (next != free_.begin()) {
        auto prev = std::prev(next);
        if (prev->first + prev->second == off) {
            prev->second += n;
            return;
        }
    }
    free_[off] = n;
}

void Renderer::releaseSection(GpuSec& g) {
    arenaFree(g.off, g.cap);
    g.cap = 0;
}

void Renderer::createMesh(GpuMesh& g, const MeshOut& m) {
    int solidQ = int((m.solid.size() + m.cutout.size() + m.detail.size()) / (4 * VERT_WORDS)),
        transQ = int(m.trans.size() / (4 * VERT_WORDS));
    ensureIndices(std::max(solidQ, transQ));
    if (!g.vao) {
        gl::GenVertexArrays(1, &g.vao);
        gl::GenBuffers(1, &g.vbo);
        gl::BindVertexArray(g.vao);
        gl::BindBuffer(gl::ARRAY_BUFFER, g.vbo);
        gl::EnableVertexAttribArray(0);
        gl::VertexAttribIPointer(0, 4, gl::UNSIGNED_INT, 4 * VERT_WORDS, (void*)0);
        gl::BindBuffer(gl::ELEMENT_ARRAY_BUFFER, ebo_);
    } else {
        gl::BindVertexArray(g.vao);
        gl::BindBuffer(gl::ARRAY_BUFFER, g.vbo);
    }
    size_t bytes = (m.solid.size() + m.cutout.size() + m.detail.size() + m.trans.size()) * 4;
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(bytes), nullptr, gl::STATIC_DRAW);
    size_t o = 0;
    for (const std::vector<uint32_t>* part : {&m.solid, &m.cutout, &m.detail, &m.trans}) {
        if (!part->empty()) gl::BufferSubData(gl::ARRAY_BUFFER, GLintptr(o), GLsizeiptr(part->size() * 4), part->data());
        o += part->size() * 4;
    }
    gl::BindVertexArray(0);
    if (!m.bodyLight.empty()) {
        if (!g.bodyLight) gl::GenTextures(1, &g.bodyLight);
        g.bodyLightSize = m.bodyLightSize;
        g.bodyLightOrigin = m.bodyLightOrigin;
        gl::ActiveTexture(gl::TEXTURE0 + 9);
        gl::BindTexture(gl::TEXTURE_3D, g.bodyLight);
        gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MIN_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
        gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
        gl::TexParameteri(gl::TEXTURE_3D, gl::TEXTURE_WRAP_R, gl::CLAMP_TO_EDGE);
        gl::PixelStorei(gl::UNPACK_ALIGNMENT, 1);
        gl::TexImage3D(gl::TEXTURE_3D, 0, gl::RGBA8, g.bodyLightSize[0], g.bodyLightSize[1], g.bodyLightSize[2], 0, gl::RGBA,
                       gl::UNSIGNED_BYTE, m.bodyLight.data());
        gl::PixelStorei(gl::UNPACK_ALIGNMENT, 4);
        gl::ActiveTexture(gl::TEXTURE0);
        bytes += m.bodyLight.size();
    } else if (g.bodyLight) {
        gl::DeleteTextures(1, &g.bodyLight);
        g.bodyLight = 0;
        g.bodyLightSize = {};
        g.bodyLightOrigin = {};
    }
    stats.gpuBytes += bytes;
    stats.gpuBytes -= g.bytes;
    g.bytes = bytes;
    g.solidQuads = solidQ;
    g.transQuads = transQ;
}

void Renderer::destroyMesh(GpuMesh& g) {
    if (g.vao) gl::DeleteVertexArrays(1, &g.vao);
    if (g.vbo) gl::DeleteBuffers(1, &g.vbo);
    if (g.bodyLight) gl::DeleteTextures(1, &g.bodyLight);
    stats.gpuBytes -= g.bytes;
    g = GpuMesh{};
}

void Renderer::uploadSection(int cx, int cz, int sy, const MeshOut& m) {
    int64_t k = sectionKey(cx, cz, sy);
    sectionVer_[k]++;
    vec3 center((cx * CS + CS / 2) * VOX, (sy * CS + CS / 2) * VOX, (cz * CS + CS / 2) * VOX);
    uint64_t shadowHash = 1469598103934665603ull;
    auto add = [&](uint32_t v) {
        shadowHash ^= v;
        shadowHash *= 1099511628211ull;
    };
    for (const auto* part : {&m.solid, &m.cutout, &m.detail}) {
        add(uint32_t(part->size()));
        for (size_t i = 0; i < part->size(); i += VERT_WORDS) {
            add((*part)[i]);
            add((*part)[i + 1] & 255u);
            if ((*part)[i] & (1u << 31)) add((*part)[i + 2]);
            add((*part)[i + 3] & 0xe0fff000u);
        }
    }
    auto previous = sections_.find(k);
    const bool firstUpload = previous == sections_.end();
    if (firstUpload || previous->second.shadowHash != shadowHash) terrainChanged(center);
    secVis_[k] = m.visibility;
    if (m.empty()) {
        auto it = sections_.find(k);
        if (it != sections_.end()) {
            releaseSection(it->second);
            sections_.erase(it);
        }
        return;
    }
    const uint32_t wq = 4 * VERT_WORDS;
    uint32_t nO = uint32_t(m.solid.size() / wq), nC = uint32_t(m.cutout.size() / wq), nT = uint32_t(m.trans.size() / wq),
             nD = uint32_t(m.detail.size() / wq);
    uint32_t total = nO + nC + nT + nD;
    ensureIndices(int(std::max({nO, nC, nT, nD})));
    GpuSec& g = sections_[k];
    if (firstUpload) g.appearAt = time_;
    g.shadowHash = shadowHash;
    if (g.cap < total || g.cap > total * 2 + 64) {
        releaseSection(g);
        g.cap = total + total / 8 + 8;
        g.off = arenaAlloc(g.cap);
    }
    g.opaque = nO;
    g.cutout = nC;
    g.trans = nT;
    g.detail = nD;
    g.opaqueFaces = m.solidFaces;
    g.cutoutFaces = m.cutoutFaces;
    uint32_t n = 0;
    for (auto q : g.opaqueFaces) n += q;
    if (n != nO) {
        g.opaqueFaces = {};
        g.opaqueFaces[6] = nO;
    }
    n = 0;
    for (auto q : g.cutoutFaces) n += q;
    if (n != nC) {
        g.cutoutFaces = {};
        g.cutoutFaces[6] = nC;
    }
    g.center = center;
    gl::BindVertexArray(0);
    gl::BindBuffer(gl::ARRAY_BUFFER, arenaVbo_);
    size_t o = size_t(g.off) * QUAD_BYTES;
    for (const std::vector<uint32_t>* part : {&m.solid, &m.cutout, &m.trans, &m.detail}) {
        if (!part->empty()) gl::BufferSubData(gl::ARRAY_BUFFER, GLintptr(o), GLsizeiptr(part->size() * 4), part->data());
        o += part->size() * 4;
    }
}

void Renderer::freeSection(int cx, int cz, int sy) {
    int64_t k = sectionKey(cx, cz, sy);
    sectionVer_[k]++;
    secVis_.erase(k);
    auto it = sections_.find(k);
    if (it == sections_.end()) return;
    terrainChanged(it->second.center);
    releaseSection(it->second);
    sections_.erase(it);
}

void Renderer::freeColumn(int cx, int cz) {
    for (int s = 0; s < NSEC; ++s) freeSection(cx, cz, s);
}
bool Renderer::hasSection(int cx, int cz, int sy) const { return sections_.count(sectionKey(cx, cz, sy)) != 0; }

int Renderer::uploadMesh(const MeshOut& m) {
    int id = nextMesh_++;
    createMesh(meshes_[id], m);
    return id;
}

void Renderer::freeMesh(int id) {
    auto it = meshes_.find(id);
    if (it == meshes_.end()) return;
    for (int& active : bodyLightMeshes_)
        if (active == id) active = 0;
    destroyMesh(it->second);
    meshes_.erase(it);
}

void Renderer::setBodyLightMeshes(const std::vector<int>& ids) {
    bodyLightMeshes_ = {};
    size_t n = 0;
    for (int id : ids) {
        auto it = meshes_.find(id);
        if (id && it != meshes_.end() && it->second.bodyLight) {
            bodyLightMeshes_[n++] = id;
            if (n == bodyLightMeshes_.size()) break;
        }
    }
}

void Renderer::drawSections(const std::vector<const GpuSec*>& list, Part part, bool reverse, bool directionCull) {
    cmds_.clear();
    origins_.clear();
    const size_t n = list.size();
    for (size_t i = 0; i < n; ++i) {
        const GpuSec* g = list[reverse ? n - 1 - i : i];
        uint32_t start = part == P_OPAQUE   ? 0
                         : part == P_CUTOUT ? g->opaque
                         : part == P_TRANS  ? g->opaque + g->cutout
                                            : g->opaque + g->cutout + g->trans;
        uint32_t quads = part == P_OPAQUE ? g->opaque : part == P_CUTOUT ? g->cutout : part == P_TRANS ? g->trans : g->detail;
        if (!quads) continue;
        vec3 o = (g->center / VOX) - vec3(CS / 2.0f);
        auto append = [&](uint32_t first, uint32_t count) {
            if (!count) return;
            cmds_.push_back({count * 6, 1, 0, int32_t((g->off + first) * 4), uint32_t(cmds_.size())});
            float fade = std::clamp((time_ - g->appearAt) / 0.32f, 0.0f, 1.0f);
            fade = fade * fade * (3.0f - 2.0f * fade);
            origins_.push_back(std::round(o.x));
            origins_.push_back(std::round(o.y));
            origins_.push_back(std::round(o.z));
            origins_.push_back(fade);
            stats.quads += int(count);
        };
        if (directionCull && mdi_ && part != P_TRANS) {
            const auto& faces = part == P_OPAQUE ? g->opaqueFaces : g->cutoutFaces;
            vec3 eye = cam_.pos - g->center;
            float axis[] = {eye.x, eye.y, eye.z};
            float limit = CS * VOX * .5f + .08f + .08f * (std::fabs(eye.x) + std::fabs(eye.y) + std::fabs(eye.z));
            uint32_t first = start;
            for (int f = 0; f < 7; ++f) {
                bool back = f < 6 && axis[f / 2] * (f % 2 ? -1.0f : 1.0f) < -limit;
                if (!back) append(first, faces[f]);
                first += faces[f];
            }
        } else
            append(start, quads);
    }
    if (cmds_.empty()) return;
    stats.drawn += int(cmds_.size());
    gl::BindVertexArray(arenaVao_);
    if (mdi_) {
        size_t ob = origins_.size() * 4, cb = cmds_.size() * sizeof(DrawCmd);
        gl::BindBuffer(gl::ARRAY_BUFFER, originBuf_);
        originCap_ = std::max(originCap_, ob);
        gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(originCap_), nullptr, gl::STREAM_DRAW);
        gl::BufferSubData(gl::ARRAY_BUFFER, 0, GLsizeiptr(ob), origins_.data());
        gl::BindBuffer(gl::DRAW_INDIRECT_BUFFER, indirectBuf_);
        cmdCap_ = std::max(cmdCap_, cb);
        gl::BufferData(gl::DRAW_INDIRECT_BUFFER, GLsizeiptr(cmdCap_), nullptr, gl::STREAM_DRAW);
        gl::BufferSubData(gl::DRAW_INDIRECT_BUFFER, 0, GLsizeiptr(cb), cmds_.data());
        gl::MultiDrawElementsIndirect(gl::TRIANGLES, gl::UNSIGNED_INT, nullptr, GLsizei(cmds_.size()), 0);
        gl::BindBuffer(gl::DRAW_INDIRECT_BUFFER, 0);
        stats.drawCalls++;
    } else {
        for (size_t i = 0; i < cmds_.size(); ++i) {
            gl::VertexAttrib4f(1, origins_[i * 4], origins_[i * 4 + 1], origins_[i * 4 + 2], origins_[i * 4 + 3]);
            gl::DrawElementsBaseVertex(gl::TRIANGLES, GLsizei(cmds_[i].count), gl::UNSIGNED_INT, nullptr, cmds_[i].baseVertex);
        }
        stats.drawCalls += int(cmds_.size());
    }
    gl::BindVertexArray(0);
}

void Renderer::gpuMark(int i) {
    while (marked_ <= i && marked_ < kMarks) {
        gl::QueryCounter(queries_[qFrame_][marked_], gl::TIMESTAMP);
        ++marked_;
    }
}

void Renderer::gpuFrameEnd() {
    if (marked_ == 0) return;
    gpuMark(kMarks - 1);
    qIssued_[qFrame_] = true;
    marked_ = 0;
    qFrame_ = (qFrame_ + 1) % kRing;
    if (!qIssued_[qFrame_]) return;
    GLint avail = 0;
    gl::GetQueryObjectiv(queries_[qFrame_][kMarks - 1], gl::QUERY_RESULT_AVAILABLE, &avail);
    if (!avail) return;
    uint64_t t[kMarks];
    for (int i = 0; i < kMarks; ++i) gl::GetQueryObjectui64v(queries_[qFrame_][i], gl::QUERY_RESULT, &t[i]);
    qIssued_[qFrame_] = false;
    auto ms = [&](int a, int b) { return t[b] > t[a] ? float(double(t[b] - t[a]) * 1e-6) : 0.0f; };
    auto ease = [&](float& v, float x) { v = gpu.valid ? v * 0.9f + x * 0.1f : x; };
    ease(gpu.shadow, ms(0, 1));
    ease(gpu.terrain, ms(1, 2));
    ease(gpu.entities, ms(2, 3));
    ease(gpu.water, ms(3, 4));
    ease(gpu.post, ms(4, 5));
    ease(gpu.ui, ms(5, 6));
    ease(gpu.total, ms(0, 6));
    gpu.valid = true;
}

void Renderer::beginFrame(const Camera& cam, const SkyState& sky, float time) {
    cam_ = cam;
    sky_ = sky;
    time_ = time;
    marked_ = 0;
    gpuMark(0);
    if (shadowValid_) {
        vec3 moved = cam_.pos - shadowCam_;
        mat4 t;
        t.m[12] = moved.x;
        t.m[13] = moved.y;
        t.m[14] = moved.z;
        shadowVP_ = shadowStaticVP_ * t;
    }
    stats.sections = int(sections_.size());
    stats.drawn = 0;
    stats.quads = 0;
    stats.drawCalls = 0;
    stats.meshes = int(meshes_.size());
    gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    gl::Viewport(0, 0, rw_, rh_);
    gl::Enable(gl::DEPTH_TEST);
    gl::DepthFunc(gl::LEQUAL);
    gl::DepthMask(gl::TRUE_);
    gl::Enable(gl::CULL_FACE);
    gl::CullFace(gl::BACK);
    gl::Disable(gl::BLEND);
    gl::ClearColor(sky.fogColor.x, sky.fogColor.y, sky.fogColor.z, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
}

void Renderer::drawSky() {
    gpuMark(1);
    sky_s_.use();
    sky_s_.set("uInvViewProj", inverse(cam_.viewProj));
    sky_s_.set("uTop", sky_.skyTop);
    sky_s_.set("uHorizon", sky_.skyHorizon);
    sky_s_.set("uFog", sky_.fogColor);
    sky_s_.set("uSunDir", sky_.sunDir);
    sky_s_.set("uStars", sky_.starAlpha);
    sky_s_.set("uUnderwater", sky_.underwater || sky_.inLava ? 1 : 0);
    gl::DepthMask(gl::FALSE_);
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);
    gl::DepthMask(gl::TRUE_);
}

void Renderer::setTerrainUniforms(Shader& s) {
    s.use();
    s.set("uViewProj", cam_.viewProj);
    s.set("uTime", time_);
    s.set("uTex", 0);
    s.set("uSun", sky_.sunLight);
    s.set("uLightOverride", vec2(-1, -1));
    s.set("uSkyScale", 1.0f);
    s.set("uWaterLayer", float(T_WATER));
    s.set("uLavaLayer", float(T_LAVA));
    s.set("uSkyCol", (sky_.skyHorizon + sky_.skyTop) * 0.5f);
    s.set("uSunDir", sky_.sunDir);
    s.set("uCamPosF", cam_.pos);
    s.set("uUnderwater", sky_.underwater ? 1 : 0);
    s.set("uNearFar", vec2(cam_.nearZ, cam_.farZ));
    s.set("uViewport", vec2(float(rw_), float(rh_)));
    s.set("uDepthTex", 3);
    s.set("uUseDepth", 0);
    s.set("uUVOffset", vec3(0, 0, 0));
    mat4 model;
    model.m[12] = -cam_.pos.x;
    model.m[13] = -cam_.pos.y;
    model.m[14] = -cam_.pos.z;
    s.set("uModel", model);
    setVolumeUniforms(s, true);
    setLightingUniforms(s);
}

void Renderer::floodVisible() {
    reached_.clear();
    const int camX = int(std::floor(cam_.pos.x / VOX)), camY = int(std::floor(cam_.pos.y / VOX)), camZ = int(std::floor(cam_.pos.z / VOX));
    const int scx = camX >= 0 ? camX / CS : (camX - CS + 1) / CS, scz = camZ >= 0 ? camZ / CS : (camZ - CS + 1) / CS;
    int ssy = camY >= 0 ? camY / CS : -1;
    const float r = CS * VOX * 0.87f;
    const int radius = int(std::ceil(sky_.fogEnd / (CS * VOX))) + 1;
    const int DX[6] = {1, -1, 0, 0, 0, 0}, DY[6] = {0, 0, 1, -1, 0, 0}, DZ[6] = {0, 0, 0, 0, 1, -1};
    auto opp = [](int f) { return f ^ 1; };
    struct Node {
        int cx, sy, cz;
        uint8_t entry, dirs;
    };
    std::vector<Node> q;
    q.reserve(4096);
    if (ssy >= NSEC) {
        ssy = NSEC - 1;
    } else if (ssy < 0)
        ssy = 0;
    q.push_back({scx, ssy, scz, 255, 0});
    reached_.insert(sectionKey(scx, scz, ssy));
    for (size_t head = 0; head < q.size(); ++head) {
        Node n = q[head];
        uint64_t vis = ~0ull;
        auto it = secVis_.find(sectionKey(n.cx, n.cz, n.sy));
        if (it != secVis_.end()) vis = it->second;
        for (int f = 0; f < 6; ++f) {
            if (n.dirs & (1 << opp(f))) continue;
            if (n.entry != 255 && !(vis & (1ull << (n.entry * 6 + f)))) continue;
            int nx = n.cx + DX[f], ny = n.sy + DY[f], nz = n.cz + DZ[f];
            if (ny < 0 || ny >= NSEC) continue;
            if (std::abs(nx - scx) > radius || std::abs(nz - scz) > radius) continue;
            int64_t key = sectionKey(nx, nz, ny);
            if (reached_.count(key)) continue;
            vec3 c((nx * CS + CS / 2) * VOX, (ny * CS + CS / 2) * VOX, (nz * CS + CS / 2) * VOX);
            if (!cam_.frustum.sphereVisible(c - cam_.pos, r)) continue;
            reached_.insert(key);
            q.push_back({nx, ny, nz, uint8_t(opp(f)), uint8_t(n.dirs | (1 << f))});
        }
    }
}

void Renderer::drawTerrain(bool translucent) {
    if (!translucent) {
        gpuMark(1);
        const float r = CS * VOX * 0.87f;
        const float maxD2 = (sky_.fogEnd + r) * (sky_.fogEnd + r);
        static const bool noCull = std::getenv("QC_NOCAVECULL") != nullptr;
        const bool cull = caveCull_ && !noCull;
        if (cull) floodVisible();
        for (auto& bucket : visibleBuckets_) bucket.clear();
        for (auto& [k, g] : sections_) {
            if (cull && !reached_.count(k)) continue;
            vec3 rel = g.center - cam_.pos;
            if (!cam_.frustum.sphereVisible(rel, r)) continue;
            float d = lengthSq(rel);
            if (d > maxD2) continue;
            int bucket = std::clamp(int((d / maxD2) * float(visibleBuckets_.size() - 1)), 0, int(visibleBuckets_.size() - 1));
            visibleBuckets_[size_t(bucket)].push_back(&g);
        }
        visible_.clear();
        for (auto& bucket : visibleBuckets_) visible_.insert(visible_.end(), bucket.begin(), bucket.end());
        const int keepDrawn = stats.drawn;
        gl::Disable(gl::BLEND);
        gl::DepthMask(gl::TRUE_);
        settled_.clear();
        fading_.clear();
        for (const auto* g : visible_) (time_ - g->appearAt >= 0.32f ? settled_ : fading_).push_back(g);
        setTerrainUniforms(terrainOpaque_);
        terrainOpaque_.set("uTranslucent", 0);
        drawSections(settled_, P_OPAQUE, false, true);
        if (!fading_.empty()) {
            setTerrainUniforms(terrainOpaqueFade_);
            terrainOpaqueFade_.set("uTranslucent", 0);
            drawSections(fading_, P_OPAQUE, false, true);
        }
        size_t foliageQuads = 0;
        for (const auto* g : visible_) foliageQuads += g->cutout;
        bool prepass = foliageQuads > 2048;
        if (prepass) {
            const int q = stats.quads, d = stats.drawn;
            gl::ColorMask(gl::FALSE_, gl::FALSE_, gl::FALSE_, gl::FALSE_);
            setTerrainUniforms(shadow_);
            shadow_.set("uSkipSource", 0);
            drawSections(visible_, P_CUTOUT, false, true);
            stats.quads = q;
            stats.drawn = d;
            gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
            gl::DepthFunc(gl::EQUAL);
            gl::DepthMask(gl::FALSE_);
        }
        setTerrainUniforms(terrain_);
        terrain_.set("uTranslucent", 0);
        drawSections(visible_, P_CUTOUT, false, true);
        if (prepass) {
            gl::DepthFunc(gl::LEQUAL);
            gl::DepthMask(gl::TRUE_);
        }
        detailList_.clear();
        for (const auto* g : visible_)
            if (g->detail && lengthSq(g->center - cam_.pos) < (detailFar_ + r) * (detailFar_ + r)) detailList_.push_back(g);
        if (!detailList_.empty()) {
            terrain_.set("uDetailFade", vec2(detailNear_, detailFar_));
            drawSections(detailList_, P_DETAIL, false);
            terrain_.set("uDetailFade", vec2(0, 0));
        }
        stats.drawn = keepDrawn + int(visible_.size());
        gpuMark(2);
        return;
    }
    gpuMark(3);
    setTerrainUniforms(terrainWater_);
    terrainWater_.set("uTranslucent", 1);
    if (fbo_ && depthFbo_) {
        gl::BindFramebuffer(gl::READ_FRAMEBUFFER, fbo_);
        gl::BindFramebuffer(gl::DRAW_FRAMEBUFFER, depthFbo_);
        gl::BlitFramebuffer(0, 0, rw_, rh_, 0, 0, rw_, rh_, gl::DEPTH_BUFFER_BIT, gl::NEAREST);
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
        gl::ActiveTexture(gl::TEXTURE3);
        gl::BindTexture(gl::TEXTURE_2D, depthTex_);
        gl::ActiveTexture(gl::TEXTURE0);
        terrainWater_.set("uUseDepth", fancyWater_ ? 1 : 0);
    }
    gl::ColorMask(gl::FALSE_, gl::FALSE_, gl::FALSE_, gl::FALSE_);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::CULL_FACE);
    setTerrainUniforms(waterDepth_);
    {
        const int drawn = stats.drawn, quads = stats.quads;
        drawSections(visible_, P_TRANS, false);
        stats.drawn = drawn;
        stats.quads = quads;
    }
    setTerrainUniforms(terrainWater_);
    terrainWater_.set("uTranslucent", 1);
    terrainWater_.set("uUseDepth", fancyWater_ && fbo_ && depthFbo_ ? 1 : 0);
    gl::DepthFunc(gl::EQUAL);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::FALSE_);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::DepthMask(gl::FALSE_);
    gl::Disable(gl::CULL_FACE);
    {
        const int keepDrawn = stats.drawn;
        drawSections(visible_, P_TRANS, true);
        stats.drawn = keepDrawn;
    }
    gl::Enable(gl::CULL_FACE);
    gl::DepthFunc(gl::LEQUAL);
    gl::DepthMask(gl::TRUE_);
    gl::Disable(gl::BLEND);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
    if (fbo_ && depthFbo_) {
        gl::BindFramebuffer(gl::READ_FRAMEBUFFER, depthFbo_);
        gl::BindFramebuffer(gl::DRAW_FRAMEBUFFER, fbo_);
        gl::BlitFramebuffer(0, 0, rw_, rh_, 0, 0, rw_, rh_, gl::DEPTH_BUFFER_BIT, gl::NEAREST);
        gl::BindFramebuffer(gl::FRAMEBUFFER, fbo_);
    }
    gl::BindVertexArray(0);
    gpuMark(4);
}

void Renderer::drawMesh(int id, const mat4& model, vec3 uvOrigin, float skyScale) {
    auto it = meshes_.find(id);
    if (it == meshes_.end() || !it->second.solidQuads) return;
    setTerrainUniforms(terrain_);
    setVolumeUniforms(terrain_, true);
    terrain_.set("uTranslucent", 0);
    terrain_.set("uUVOffset", uvOrigin);
    terrain_.set("uModel", model);
    terrain_.set("uSkyScale", skyScale);
    if (it->second.bodyLight) {
        const auto& o = it->second.bodyLightOrigin;
        const auto& z = it->second.bodyLightSize;
        terrain_.set("uMovingLight0", 9);
        terrain_.set("uMovingLightOrigin0", vec3(float(o[0]), float(o[1]), float(o[2])));
        terrain_.set("uMovingLightSize0", vec3(float(z[0]), float(z[1]), float(z[2])));
        terrain_.set("uMovingLightSize1", vec3(0));
        terrain_.set("uMovingLightSize2", vec3(0));
        terrain_.set("uMovingLightSize3", vec3(0));
        gl::ActiveTexture(gl::TEXTURE0 + 9);
        gl::BindTexture(gl::TEXTURE_3D, it->second.bodyLight);
        gl::ActiveTexture(gl::TEXTURE0);
        terrain_.set("uUseLiveSky", 2);
    } else
        terrain_.set("uUseLiveSky", 0);
    gl::VertexAttrib3f(1, 0, 0, 0);
    gl::BindVertexArray(it->second.vao);
    gl::DrawElements(gl::TRIANGLES, it->second.solidQuads * 6, gl::UNSIGNED_INT, nullptr);
    gl::BindVertexArray(0);
}

void Renderer::drawBoxes(const std::vector<BoxInstance>& boxes, bool blend, const mat4* vpOverride, bool depth, int unlit) {
    if (boxes.empty()) return;
    const std::vector<BoxInstance>* draw = &boxes;
    if (!vpOverride && depth && boxes.size() > 48) {
        boxCull_.clear();
        boxCull_.reserve(boxes.size());
        for (const auto& b : boxes) {
            vec3 mid(b.r0.w + (b.r0.x + b.r0.y + b.r0.z) * .5f, b.r1.w + (b.r1.x + b.r1.y + b.r1.z) * .5f,
                     b.r2.w + (b.r2.x + b.r2.y + b.r2.z) * .5f);
            float radius =
                .5f * (length(vec3(b.r0.x, b.r1.x, b.r2.x)) + length(vec3(b.r0.y, b.r1.y, b.r2.y)) + length(vec3(b.r0.z, b.r1.z, b.r2.z)));
            float reach = sky_.fogEnd + radius;
            if (lengthSq(mid) > reach * reach || !cam_.frustum.sphereVisible(mid, radius)) continue;
            boxCull_.push_back(b);
        }
        if (boxCull_.empty()) return;
        draw = &boxCull_;
    }
    box_.use();
    box_.set("uViewProj", vpOverride ? *vpOverride : cam_.viewProj);
    box_.set("uTex", 0);
    box_.set("uSun", sky_.sunLight);
    box_.set("uFog", sky_.fogColor);
    box_.set("uFogRange", vpOverride ? vec2(1e6f, 2e6f) : vec2(sky_.fogStart, sky_.fogEnd));
    box_.set("uUnlit", unlit >= 0 ? unlit : (vpOverride ? 1 : 0));
    box_.set("uAlphaCut", blend ? 0.02f : 0.3f);
    box_.set("uViewSpace", vpOverride ? 1 : 0);
    box_.set("uOutAlpha", (vpOverride && unlit == 0) ? 0.0f : -1.0f);
    setVolumeUniforms(box_, vpOverride == nullptr);
    setLightingUniforms(box_);
    gl::BindVertexArray(boxVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, boxInst_);
    size_t bytes = draw->size() * sizeof(BoxInstance);
    boxInstCap_ = std::max(boxInstCap_, bytes);
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(boxInstCap_), nullptr, gl::STREAM_DRAW);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, GLsizeiptr(bytes), draw->data());
    if (blend) {
        gl::Enable(gl::BLEND);
        gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
        gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::FALSE_);
        gl::DepthMask(gl::FALSE_);
    }
    if (!depth) gl::Disable(gl::DEPTH_TEST);
    gl::DrawArraysInstanced(gl::TRIANGLES, 0, 36, GLsizei(draw->size()));
    if (blend) {
        gl::Disable(gl::BLEND);
        gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
        gl::DepthMask(gl::TRUE_);
    }
    if (!depth) gl::Enable(gl::DEPTH_TEST);
    gl::BindVertexArray(0);
}

void Renderer::drawLines(const std::vector<LineVertex>& lines, bool depth) {
    if (lines.empty()) return;
    line_.use();
    line_.set("uViewProj", cam_.viewProj);
    gl::BindVertexArray(lineVao_);
    gl::BindBuffer(gl::ARRAY_BUFFER, lineVbo_);
    size_t bytes = lines.size() * sizeof(LineVertex);
    lineCap_ = std::max(lineCap_, bytes);
    gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(lineCap_), nullptr, gl::STREAM_DRAW);
    gl::BufferSubData(gl::ARRAY_BUFFER, 0, GLsizeiptr(bytes), lines.data());
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    if (!depth) gl::Disable(gl::DEPTH_TEST);
    gl::DrawArrays(gl::LINES, 0, GLsizei(lines.size()));
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);
}

void Renderer::drawClouds() {
    if (!sky_.clouds || sky_.underwater || sky_.inLava) return;
    int cx = int(std::floor((cam_.pos.x + time_ * 1.2f) / 32)) * 4, cz = int(std::floor(cam_.pos.z / 32)) * 4;
    if (cx != cloudCellX_ || cz != cloudCellZ_) {
        cloudCellX_ = cx;
        cloudCellZ_ = cz;
        static const Noise noise(0xC10D);
        constexpr int R = 40, S = 2 * R + 3;
        int heights[S][S];
        for (int z = 0; z < S; ++z)
            for (int x = 0; x < S; ++x) {
                float n = noise.noise2((cx + x - R - 1) * 0.11f, (cz + z - R - 1) * 0.11f);
                heights[z][x] = n > 0.10f ? std::clamp(1 + int((n - 0.1f) * 5), 1, 4) : 0;
            }
        std::vector<float> v;
        auto quad = [&](vec3 a, vec3 b, vec3 c, vec3 d, float shade) {
            for (auto p : {a, b, c, c, d, a}) {
                v.push_back(p.x);
                v.push_back(p.y);
                v.push_back(p.z);
                v.push_back(shade);
            }
        };
        for (int z = 1; z < S - 1; ++z)
            for (int x = 1; x < S - 1; ++x) {
                int h = heights[z][x];
                if (!h) continue;
                float X = float(cx + x - R - 1) * 8, Z = float(cz + z - R - 1) * 8, H = float(h) * 2;
                quad({X, H, Z}, {X, H, Z + 8}, {X + 8, H, Z + 8}, {X + 8, H, Z}, 1.0f);
                quad({X, 0, Z}, {X + 8, 0, Z}, {X + 8, 0, Z + 8}, {X, 0, Z + 8}, 0.76f);
                float b = float(heights[z][x - 1]) * 2;
                if (b < H) quad({X, b, Z}, {X, b, Z + 8}, {X, H, Z + 8}, {X, H, Z}, 0.86f);
                b = float(heights[z][x + 1]) * 2;
                if (b < H) quad({X + 8, b, Z + 8}, {X + 8, b, Z}, {X + 8, H, Z}, {X + 8, H, Z + 8}, 0.91f);
                b = float(heights[z - 1][x]) * 2;
                if (b < H) quad({X + 8, b, Z}, {X, b, Z}, {X, H, Z}, {X + 8, H, Z}, 0.84f);
                b = float(heights[z + 1][x]) * 2;
                if (b < H) quad({X, b, Z + 8}, {X + 8, b, Z + 8}, {X + 8, H, Z + 8}, {X, H, Z + 8}, 0.90f);
            }
        cloudCount_ = int(v.size() / 4);
        gl::BindBuffer(gl::ARRAY_BUFFER, cloudVbo_);
        gl::BufferData(gl::ARRAY_BUFFER, GLsizeiptr(v.size() * sizeof(float)), v.data(), gl::DYNAMIC_DRAW);
    }
    cloud_.use();
    cloud_.set("uViewProj", cam_.viewProj);
    cloud_.set("uCam", cam_.pos);
    cloud_.set("uHeight", sky_.cloudHeight);
    float radius = std::min(340.0f, std::max(sky_.fogEnd * 2.2f, 160.0f));
    cloud_.set("uRadius", radius);
    cloud_.set("uTime", time_);
    cloud_.set("uFog", sky_.fogColor);
    cloud_.set("uSun", sky_.sunLight);
    cloud_.set("uOvercast", sky_.overcast);
    cloud_.set("uCloud", 1);
    gl::ActiveTexture(gl::TEXTURE1);
    gl::BindTexture(gl::TEXTURE_2D, cloudTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::Disable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::Disable(gl::CULL_FACE);
    gl::DepthMask(gl::TRUE_);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::FALSE_);
    gl::BindVertexArray(cloudVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, cloudCount_);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
    gl::DepthMask(gl::TRUE_);
    gl::Enable(gl::CULL_FACE);
    gl::Disable(gl::BLEND);
    gl::BindVertexArray(0);
}

void Renderer::drawFullscreenTint(vec4 color) {
    tint_.use();
    tint_.set("uColor", color);
    gl::Enable(gl::BLEND);
    gl::BlendFunc(gl::SRC_ALPHA, gl::ONE_MINUS_SRC_ALPHA);
    gl::Disable(gl::DEPTH_TEST);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::FALSE_);
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);
    gl::ColorMask(gl::TRUE_, gl::TRUE_, gl::TRUE_, gl::TRUE_);
    gl::Enable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
}

void Renderer::endWorld() {
    gpuMark(4);
    bool shafts = sky_.skyExposure > 0.01f && volumetric_ && shadowsActive() && shadowTex_ && fbo_;
    gl::Disable(gl::DEPTH_TEST);
    gl::Disable(gl::BLEND);
    gl::Disable(gl::CULL_FACE);
    mat4 invVP = inverse(cam_.viewProj);
    if (shafts) {
        gl::BindFramebuffer(gl::FRAMEBUFFER, volFbo_);
        gl::Viewport(0, 0, vw_, vh_);
        vol_.use();
        vol_.set("uDepth", 3);
        vol_.set("uShadowMap", 4);
        vol_.set("uShadowVP", shadowVP_);
        vol_.set("uInvViewProj", invVP);
        vol_.set("uSize", vec2(float(vw_), float(vh_)));
        vol_.set("uTime", time_);
        gl::ActiveTexture(gl::TEXTURE3);
        gl::BindTexture(gl::TEXTURE_2D, depthTex_);
        gl::ActiveTexture(gl::TEXTURE4);
        gl::BindTexture(gl::TEXTURE_2D, sunShadowTexture());
        gl::BindVertexArray(emptyVao_);
        gl::DrawArrays(gl::TRIANGLES, 0, 3);
    }
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, w_, h_);
    if (!fbo_) return;
    post_.use();
    post_.set("uScene", 0);
    post_.set("uDepth", 3);
    post_.set("uVol", 5);
    post_.set("uVolTexel", vec2(1.0f / float(vw_), 1.0f / float(vh_)));
    post_.set("uInvViewProj", invVP);
    post_.set("uLightDir", sky_.lightDir);
    post_.set("uSunCol", sky_.sunCol);
    post_.set("uTime", time_);
    post_.set("uFogDensity", sky_.fogDensity);
    post_.set("uVolumetric", shafts ? 1 : 0);
    post_.set("uUnderwater", sky_.underwater ? 1 : 0);
    post_.set("uOutSize", vec2(float(w_), float(h_)));
    gl::ActiveTexture(gl::TEXTURE3);
    gl::BindTexture(gl::TEXTURE_2D, depthTex_);
    gl::ActiveTexture(gl::TEXTURE5);
    gl::BindTexture(gl::TEXTURE_2D, volTex_);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D, fboColor_);
    gl::GenerateMipmap(gl::TEXTURE_2D);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MIN_FILTER, gl::LINEAR_MIPMAP_LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_MAG_FILTER, gl::LINEAR);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_S, gl::CLAMP_TO_EDGE);
    gl::TexParameteri(gl::TEXTURE_2D, gl::TEXTURE_WRAP_T, gl::CLAMP_TO_EDGE);
    post_.set("uAspect43", aspect43_ ? 1 : 0);
    post_.set("uSceneSize", vec2(float(rw_), float(rh_)));
    gl::BindVertexArray(emptyVao_);
    gl::DrawArrays(gl::TRIANGLES, 0, 3);
    gl::BindVertexArray(0);
    gl::Enable(gl::DEPTH_TEST);
    gl::Enable(gl::CULL_FACE);
    gl::ActiveTexture(gl::TEXTURE0);
    gl::BindTexture(gl::TEXTURE_2D_ARRAY, texArray_);
    gpuMark(5);
}

}
