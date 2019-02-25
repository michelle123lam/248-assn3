//
// Parameters that control fragment shader behavior. Different materials
// will set these flags to true/false for different looks
//

uniform bool useTextureMapping;     // true if basic texture mapping (diffuse) should be used
uniform bool useNormalMapping;      // true if normal mapping should be used
uniform bool useEnvironmentMapping; // true if environment mapping should be used
uniform bool useMirrorBRDF;         // true if mirror brdf should be used (default: phong)

//
// texture maps
//

uniform sampler2D diffuseTextureSampler;
uniform sampler2D normalTextureSampler;
uniform sampler2D environmentTextureSampler;

uniform sampler2D shadowTextureSampler0;
uniform sampler2D shadowTextureSampler1;


//
// lighting environment definition. Scenes may contain directional
// and point light sources, as well as an environment map
//

#define MAX_NUM_LIGHTS 10
uniform int  num_directional_lights;
uniform vec3 directional_light_vectors[MAX_NUM_LIGHTS];

uniform int  num_point_lights;
uniform vec3 point_light_positions[MAX_NUM_LIGHTS];

uniform int   num_spot_lights;
uniform vec3  spot_light_positions[MAX_NUM_LIGHTS];
uniform vec3  spot_light_directions[MAX_NUM_LIGHTS];
uniform vec3  spot_light_intensities[MAX_NUM_LIGHTS];
uniform float spot_light_angles[MAX_NUM_LIGHTS];


//
// material-specific uniforms
//

// parameters to Phong BRDF
uniform float spec_exp;

// values that are varying per fragment (computed by the vertex shader)

varying vec3 position;     // surface position
varying vec3 normal;       // surface normal
varying vec2 texcoord;     // surface texcoord (uv)
varying vec3 dir2camera;   // vector from surface point to camera
varying mat3 tan2world;    // tangent space to world space transform
varying vec3 vertex_diffuse_color; // surface color

varying vec4 position_shadowlight0; // surface position in light space
varying vec4 position_shadowlight1; // surface position in light space

#define PI 3.14159265358979323846


//
// Simple diffuse brdf
//
// L -- direction to light
// N -- surface normal at point being shaded
//
vec3 Diffuse_BRDF(vec3 L, vec3 N, vec3 diffuseColor) {
    return diffuseColor * max(dot(N, L), 0.);
}

//
// Phong_BRDF --
//
// Evaluate phong reflectance model according to the given parameters
// L -- direction to light
// V -- direction to camera (view direction)
// N -- surface normal at point being shaded
//
vec3 Phong_BRDF(vec3 L, vec3 V, vec3 N, vec3 diffuse_color, vec3 specular_color, float specular_exponent)
{

    //
    // TODO CS248: PART 1: implement diffuse and specular terms of the Phong
    // reflectance model here.
    // 

    vec3 R = 2. * dot(L, N) * N - L;
    R = normalize(R);

    if (dot((V + L), N) < 0. || dot(L, N) < 0. || dot(R, V) < 0.) {
        // return diffuse_color;
        return diffuse_color * dot(L, N);
    }

    diffuse_color = (diffuse_color * dot(L, N)) + (specular_color * pow(dot(R, V), specular_exponent));

    return diffuse_color;
}

//
// SampleEnvironmentMap -- returns incoming radiance from specified direction
//
// D -- world space direction (outward from scene) from which to sample radiance
// 
vec3 SampleEnvironmentMap(vec3 D)
{    

     //
     // TODO CS248 PART 3: sample environment map in direction D.  This requires
     // converting D into spherical coordinates where Y is the polar direction
     // (warning: in our scene, theta is angle with Y axis, which differs from
     // typical convention in physics)
     //

     // Tips:
     //
     // (1) See GLSL documentation of acos(x) and atan(x, y)
     //
     // (2) atan() returns an angle in the range -PI to PI, so you'll have to
     //     convert negative values to the range 0 - 2PI
     //
     // (3) How do you convert theta and phi to normalized texture
     //     coordinates in the domain [0,1]^2?


    float phi = atan(D[0], D[2]);
    if (phi < 0.) {
        phi += (2. * PI);
    }
    float r = sqrt(pow(D[0], 2.) + pow(D[1], 2.) + pow(D[2], 2.));
    float theta = acos(D[1] / r);
    vec2 envCoord = vec2((phi / (2. * PI)), (theta / PI));

    vec3 envColor = texture2D(environmentTextureSampler, envCoord).rgb;
    return envColor;

}

//
// Fragment shader main entry point
//
void main(void)
{

    //////////////////////////////////////////////////////////////////////////
	// Pattern generation. Compute parameters to BRDF 
    //////////////////////////////////////////////////////////////////////////
    
	vec3 diffuseColor = vec3(1.0, 1.0, 1.0);
    vec3 specularColor = vec3(1.0, 1.0, 1.0);
    float specularExponent = spec_exp;

    if (useTextureMapping) {
        diffuseColor = texture2D(diffuseTextureSampler, texcoord).rgb;
    } else {
        diffuseColor = vertex_diffuse_color;
    }

    /////////////////////////////////////////////////////////////////////////
    // Evaluate lighting and surface BRDF 
    /////////////////////////////////////////////////////////////////////////

    // perform normal map lookup if required
    vec3 N = vec3(0);
    if (useNormalMapping) {

       // TODO: CS248 PART 2: use tan2World in the normal map to compute the
       // world space normal baaed on the normal map.

       // Note that values from the texture should be scaled by 2 and biased
       // by negative -1 to covert positive values from the texture fetch, which
       // lie in the range (0-1), to the range (-1,1).
       //
       // In other words:   tangent_space_normal = texture_value * 2.0 - 1.0;

       // replace this line with your implementation
        vec3 normalVal = texture2D(normalTextureSampler, texcoord).rgb * 2.0 - 1.0;
        normalVal = normalize(normalVal);
        N = normalize(tan2world * normalVal);

    } else {
       N = normalize(normal);
    }

    vec3 V = normalize(dir2camera);
    // initialize "L out" with reflectance due to ambient lighting
    vec3 Lo = vec3(0.2 * diffuseColor);   

    if (useMirrorBRDF) {
        //
        // TODO: CS248 PART 3: compute perfect mirror reflection direction here.
        // You'll also need to implement environment map sampling in SampleEnvironmentMap()
        vec3 w_i = normalize(dir2camera);
        vec3 R = -(w_i) + 2. * dot(w_i, N) * N;

        // sample environment map
        vec3 envColor = SampleEnvironmentMap(R);
        
        // this is a perfect mirror material, so we'll just return the light incident
        // from the reflection direction
        gl_FragColor = vec4(envColor, 1);
        return;
    }

	// for simplicity, assume all lights (other than spot lights) have unit magnitude
	float light_magnitude = 1.0;

	// for all directional lights
	for (int i = 0; i < num_directional_lights; ++i) {
	    vec3 L = normalize(-directional_light_vectors[i]);
		vec3 brdf_color = Phong_BRDF(L, V, N, diffuseColor, specularColor, specularExponent);
	    Lo += light_magnitude * brdf_color;
    }

    // for all point lights
    for (int i = 0; i < num_point_lights; ++i) {
		vec3 light_vector = point_light_positions[i] - position;
        vec3 L = normalize(light_vector);
        float distance = length(light_vector);
        vec3 brdf_color = Phong_BRDF(L, V, N, diffuseColor, specularColor, specularExponent);
        float falloff = 1.0 / (0.01 + distance * distance);
        Lo += light_magnitude * falloff * brdf_color;
    }

    // for all spot lights
	for (int i = 0; i < num_spot_lights; ++i) {
    
        vec3 intensity = spot_light_intensities[i];   // intensity of light: this is intensity in RGB
        vec3 light_pos = spot_light_positions[i];     // location of spotlight
        float cone_angle = spot_light_angles[i];      // spotlight falls off to zero in directions whose
                                                      // angle from the light direction is grester than
                                                      // cone angle. Caution: this value is in units of degrees!

        vec3 dir_to_surface = position - light_pos;
        float angle = acos(dot(normalize(dir_to_surface), spot_light_directions[i])) * 180.0 / PI;

        //
        // CS248 TODO: Part 4: compute the attenuation of the spotlight due to two factors:
        // (1) distance from the spot light (D^2 falloff)
        // (2) attentuation due to being outside the spotlight's cone 
        //
        // Here is a description of what to compute:
        //
        // 1. Modulate intensity by a factor of 1/D^2, where D is the distance from the
        //    spotlight to the current surface point.  For robustness, it's common to use 1/(1 + D^2)
        //    to never multiply by a value greather than 1.
        //
        // 2. Modulate the resulting intensity based on whether the surface point is in the cone of
        //    illumination.  To achieve a smooth falloff, consider the following rules
        //    
        //    -- Intensity should be zero if angle between the spotlight direction and the vector from
        //       the light position to the surface point is greater than (1.0 + SMOOTHING) * cone_angle
        //
        //    -- Intensity should not be further attentuated if the angle is less than (1.0 - SMOOTHING) * cone_angle
        //
        //    -- For all other angles between these extremes, interpolate linearly from unattenuated
        //       to zero intensity. 
        //
        //    -- The reference solution uses SMOOTHING = 0.1, so 20% of the spotlight region is the smoothly
        //       facing out area.  Smaller values of SMOOTHING will create hard spotlights.

        float smoothing = 0.1;
        if (angle > (cone_angle * (1.0 + smoothing))) {
            // Outside spotlight
            intensity = vec3(0., 0., 0.);
        } else {
            // Within spotlight
            float D = sqrt(
                pow(light_pos[0] - position[0], 2.) + 
                pow(light_pos[1] - position[1], 2.) +
                pow(light_pos[2] - position[2], 2.)
            );
            intensity *= 1. /(1. + pow(D, 2.));

            if (angle >= (cone_angle * (1.0 - smoothing))) {
                // With attenuation
                float cone_angle_per = (angle / cone_angle);
                float attenuation_factor = 1. - ((cone_angle_per - 0.9) / 0.2);
                intensity *= attenuation_factor;
            }
        }

        if (i<2) {

           if (i == 0) {
                // CS248 TODO: Part 4: comute shadowing for spotlight 0 here 
                float pcf_step_size = 256.;
                float num_in_shadow = 0.;
                for (int j=-2; j<=2; j++) {
                  for (int k=-2; k<=2; k++) {
                     vec2 offset = vec2(j,k) / pcf_step_size;
                     // sample shadow map at shadow_uv + offset
                     // and test if the surface is in shadow according to this sample
                    vec2 shadow_uv = position_shadowlight0.xy / position_shadowlight0.w;
                    float shadowVal = texture2D(shadowTextureSampler0, shadow_uv + offset).z;
                    if (((position_shadowlight0.z - 0.05) / position_shadowlight0.w) > shadowVal) {
                        num_in_shadow += 1.;
                    }
                  }
                }
                // record the fraction (out of 25) of shadow tests that are in shadow
                // and attenuate illumination accordingly
                float shadow_frac = num_in_shadow / 25.;
                intensity = intensity * (1. - shadow_frac);

           } else if (i == 1) {
                // CS248 TODO: Part 4: comute shadowing for spotlight 1 here 
                float pcf_step_size = 256.;
                float num_in_shadow = 0.;
                for (int j=-2; j<=2; j++) {
                  for (int k=-2; k<=2; k++) {
                     vec2 offset = vec2(j,k) / pcf_step_size;
                     // sample shadow map at shadow_uv + offset
                     // and test if the surface is in shadow according to this sample
                    vec2 shadow_uv = position_shadowlight1.xy / position_shadowlight1.w;
                    float shadowVal = texture2D(shadowTextureSampler1, shadow_uv + offset).z;
                    if (((position_shadowlight1.z - 0.05) / position_shadowlight1.w) > shadowVal) {
                        num_in_shadow += 1.;
                    }
                  }
                }
                // record the fraction (out of 25) of shadow tests that are in shadow
                // and attenuate illumination accordingly
                float shadow_frac = num_in_shadow / 25.;
                intensity = intensity * (1. - shadow_frac);
           }
        }

	    vec3 L = normalize(-spot_light_directions[i]);
		vec3 brdf_color = Phong_BRDF(L, V, N, diffuseColor, specularColor, specularExponent);

	    Lo += intensity * brdf_color;
    }

    gl_FragColor = vec4(Lo, 1);
}



