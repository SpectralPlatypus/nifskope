#version 120

uniform sampler2D BaseMap;
uniform sampler2D NormalMap;
uniform sampler2D GlowMap;
uniform sampler2D HeightMap;
uniform sampler2D LightMask;
uniform sampler2D BacklightMap;
uniform sampler2D EnvironmentMap;
uniform samplerCube CubeMap;

uniform vec3 specColor;
uniform float specStrength;
uniform float specGlossiness;

uniform bool hasGlowMap;
uniform vec3 glowColor;
uniform float glowMult;

uniform float alpha;

uniform vec3 tintColor;

uniform bool hasHeightMap;
uniform vec2 uvScale;
uniform vec2 uvOffset;

uniform bool hasEmit;
uniform bool hasSoftlight;
uniform bool hasBacklight;
uniform bool hasRimlight;
uniform bool hasTintColor;
uniform bool hasCubeMap;
uniform bool hasEnvMask;

uniform float lightingEffect1;
uniform float lightingEffect2;

uniform float envReflection;

uniform mat4 worldMatrix;

varying vec3 LightDir;
varying vec3 ViewDir;

varying vec4 A;
varying vec4 C;
varying vec4 D;

varying mat3 tbnMatrix;


vec3 tonemap(vec3 x)
{
      float _A = 0.15;
      float _B = 0.50;
      float _C = 0.10;
      float _D = 0.20;
      float _E = 0.02;
      float _F = 0.30;

      return ((x*(_A*x+_C*_B)+_D*_E)/(x*(_A*x+_B)+_D*_F))-_E/_F;
}

vec3 toGrayscale(vec3 color)
{
      return vec3(dot(vec3(0.3, 0.59, 0.11), color));
}

void main( void )
{
      vec2 offset = gl_TexCoord[0].st * uvScale + uvOffset;

      vec3 E = normalize(ViewDir);

      if ( hasHeightMap ) {
            float height = texture2D( HeightMap, offset ).r;
            offset += E.xy * (height * 0.08 - 0.04);
      }

      vec4 baseMap = texture2D( BaseMap, offset );
      vec4 normalMap = texture2D( NormalMap, offset );
      vec4 glowMap = texture2D( GlowMap, offset );

      vec3 normal = normalize(tbnMatrix * (normalMap.rgb * 2.0 - 1.0));

      vec3 L = normalize(LightDir);
      vec3 R = reflect(-L, normal);
      vec3 H = normalize( L + E );

      float NdotL = max( dot(normal, L), 0.0 );
      float NdotH = max( dot(normal, H), 0.0 );
      float EdotN = max( dot(normal, E), 0.0 );
      float NdotNegL = max( dot(normal, -L), 0.0 );

      vec3 reflected = reflect( -E, normal );
      vec3 reflectedVS = reflected;
      vec3 reflectedWS = vec3( worldMatrix * (gl_ModelViewMatrixInverse * vec4( reflectedVS, 0.0 )) );


      vec4 color;
      vec3 albedo = baseMap.rgb * C.rgb;
      vec3 diffuse = A.rgb + (D.rgb * NdotL);


      // Environment
      if ( hasCubeMap ) {
            vec4 cube = textureCube( CubeMap, reflectedWS );
            cube.rgb *= envReflection;

            if ( hasEnvMask ) {
                  vec4 env = texture2D( EnvironmentMap, offset );
                  cube.rgb *= env.r;
            } else {
                  cube.rgb *= normalMap.a;
            }


            albedo += cube.rgb;
      }

      // Emissive & Glow
      vec3 emissive = vec3(0.0);
      if ( hasEmit ) {
            emissive += glowColor * glowMult;

            if ( hasGlowMap ) {
                  emissive *= glowMap.rgb;
            }
      }

      // Specular
      vec3 spec = clamp( specColor * specStrength * normalMap.a * pow(NdotH, specGlossiness), 0.0, 1.0 );
      spec *= D.rgb;

      vec3 backlight = vec3(0.0);
      if ( hasBacklight ) {
            backlight = texture2D( BacklightMap, offset ).rgb;
            backlight *= NdotNegL;

            emissive += backlight * D.rgb;
      }

      vec4 mask = vec4(0.0);
      if ( hasRimlight || hasSoftlight ) {
            mask = texture2D( LightMask, offset );
      }

      vec3 rim = vec3(0.0);
      if ( hasRimlight ) {
            rim = mask.rgb * pow(vec3((1.0 - EdotN)), vec3(lightingEffect2));
            rim *= smoothstep( -0.2, 1.0, dot(-L, E) );

            emissive += rim * D.rgb;
      }

      vec3 soft = vec3(0.0);
      if ( hasSoftlight ) {
            float wrap = (dot(normal, L) + lightingEffect1) / (1.0 + lightingEffect1);

            soft = max( wrap, 0.0 ) * mask.rgb * smoothstep( 1.0, 0.0, NdotL );
            soft *= sqrt( clamp( lightingEffect1, 0.0, 1.0 ) );

            emissive += soft * D.rgb;
      }

      if ( hasTintColor ) {
            albedo *= tintColor;
      }

      color.rgb = albedo * (diffuse + emissive) + spec;
      color.rgb = tonemap( color.rgb ) / tonemap( vec3(1.0) );
      color.a = C.a * baseMap.a;

      gl_FragColor = color;
      gl_FragColor.a *= alpha;
}
