#version 330 core
out vec4 FragColor;
in vec2 TexCoords;

// Camera and scene uniforms
uniform vec3 uCamPos;
uniform mat3 uCamRot;
uniform float uTime;
uniform bool uDenoise; // Toggle for denoising
uniform bool uGI;      // Toggle for global illumination
uniform bool uSkybox;  // Toggle for using the skybox
uniform sampler2D uSkyboxTex; // HDR skybox texture (equirectangular)

// Maximum number of bounces for reflections/gi
const int maxBounces = 3;

// --------------------------------------------------------
// 1. Sphere Intersection
// --------------------------------------------------------
float intersectSphere(vec3 ro, vec3 rd, vec3 center, float radius, out vec3 normal) {
    vec3 oc = ro - center;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - radius * radius;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    h = sqrt(h);
    float t = -b - h;
    if (t < 0.0) t = -b + h;
    if (t > 0.0) {
        vec3 hitPos = ro + t * rd;
        normal = normalize(hitPos - center);
        return t;
    }
    return -1.0;
}

// --------------------------------------------------------
// 2. Finite Plane Intersection
//    (plane at y=planeY, with half-size in X and Z)
// --------------------------------------------------------
float intersectFinitePlane(vec3 ro, vec3 rd, float planeY, float halfSize, out vec3 normal) {
    // If the ray is nearly parallel to the plane, no intersection
    if (abs(rd.y) < 0.0001) return -1.0;

    // Solve for t in plane equation y=planeY
    float t = (planeY - ro.y) / rd.y;
    if (t > 0.0) {
        // Check (x,z) within halfSize
        vec3 hitPos = ro + t * rd;
        if (abs(hitPos.x) <= halfSize && abs(hitPos.z) <= halfSize) {
            normal = vec3(0.0, 1.0, 0.0);
            return t;
        }
    }
    return -1.0;
}

// --------------------------------------------------------
// 3. Trace a ray through the scene with up to maxBounces
//    Now includes:
//    - finite plane
//    - distance accumulation for fog
// --------------------------------------------------------
vec3 traceRay(vec3 ro, vec3 rd) {
    vec3 accColor = vec3(0.0);
    vec3 attenuation = vec3(1.0);

    // Keep track of how far the ray has traveled (for fog)
    float totalDistance = 0.0;

    for (int bounce = 0; bounce < maxBounces; bounce++) {
        float t = 1e20;
        vec3 hitNormal;
        vec3 baseColor;
        bool hit = false;

        // --- Sphere: example red sphere at (0,0,5) radius=1 ---
        vec3 sphereCenter = vec3(0.0, 0.0, 5.0);
        float sphereRadius = 1.0;
        vec3 nSphere;
        float tSphere = intersectSphere(ro, rd, sphereCenter, sphereRadius, nSphere);
        if (tSphere > 0.0 && tSphere < t) {
            t = tSphere;
            hitNormal = nSphere;
            baseColor = vec3(1.0, 0.0, 0.0); // red
            hit = true;
        }

        // --- Finite Plane: large �floor� at y=-1, �50 in X,Z ---
        float planeY = -1.0;
        float halfSize = 50.0; // big enough to look large, but not infinite
        vec3 nPlane;
        float tPlane = intersectFinitePlane(ro, rd, planeY, halfSize, nPlane);
        if (tPlane > 0.0 && tPlane < t) {
            t = tPlane;
            hitNormal = nPlane;

            // Checkerboard pattern
            vec3 hitPos = ro + t * rd;
            float scale = 2.0;
            float checker = mod(floor(hitPos.x * scale) + floor(hitPos.z * scale), 2.0);
            baseColor = (checker < 1.0) ? vec3(1.0) : vec3(0.2);

            hit = true;
        }

        // --- If nothing hit, sample background/skybox ---
        if (!hit) {
            if (uSkybox) {
                vec3 d = normalize(rd);
                float uCoord = atan(d.z, d.x) / (2.0 * 3.1415926) + 0.5;
                float vCoord = asin(d.y) / 3.1415926 + 0.5;
                accColor += attenuation * texture(uSkyboxTex, vec2(uCoord, vCoord)).rgb;
            }
            else {
                accColor += attenuation * vec3(0.5, 0.7, 1.0); // plain sky
            }
            break;
        }

        // We have a hit; the ray traveled 't' more units
        totalDistance += t;

        // --- Local diffuse shading ---
        vec3 hitPos = ro + t * rd;
        vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
        float diffuse = max(dot(hitNormal, lightDir), 0.0);
        vec3 localColor = baseColor * (0.2 + 0.8 * diffuse);

        // Some reflectivity (glossy or GI)
        float reflectivity = 0.7;

        // Accumulate local shading (blended with reflectivity)
        accColor += attenuation * mix(localColor, vec3(0.0), reflectivity);

        // Decide bounce type based on GI toggle
        if (uGI) {
            // Global Illumination: random diffuse bounce
            vec2 seed = hitPos.xz + vec2(uTime, uTime * 0.5);
            float r1 = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
            float r2 = fract(sin(dot(seed, vec2(39.3467, 11.135))) * 12345.6789);
            float phi = 2.0 * 3.1415926 * r1;
            float cosTheta = sqrt(1.0 - r2);
            float sinTheta = sqrt(r2);

            // Build an orthonormal basis around hitNormal
            vec3 tangent = normalize(abs(hitNormal.x) < 0.5
                ? cross(hitNormal, vec3(1.0, 0.0, 0.0))
                : cross(hitNormal, vec3(0.0, 1.0, 0.0)));
            vec3 bitangent = cross(hitNormal, tangent);

            vec3 diffuseDir = normalize(
                tangent * cos(phi) * sinTheta +
                bitangent * sin(phi) * sinTheta +
                hitNormal * cosTheta
            );
            rd = diffuseDir;
        }
        else {
            // Glossy reflection: reflect + small random perturbation
            vec3 refl = reflect(rd, hitNormal);
            float roughness = 0.2;
            vec2 seed = hitPos.xz + vec2(uTime);
            float r1 = fract(sin(dot(seed, vec2(12.9898, 78.233))) * 43758.5453);
            float r2 = fract(sin(dot(seed, vec2(39.3467, 11.135))) * 12345.6789);
            float angle = roughness * 6.2831853 * r1;
            float offset = roughness * r2;

            vec3 tangent = normalize(
                abs(refl.x) > 0.1
                ? cross(refl, vec3(0.0, 1.0, 0.0))
                : cross(refl, vec3(1.0, 0.0, 0.0))
            );
            vec3 bitangent = cross(refl, tangent);
            vec3 perturbed = normalize(refl + offset * (cos(angle) * tangent + sin(angle) * bitangent));
            rd = perturbed;
        }

        // Offset ray origin to avoid self-intersection
        ro = hitPos + hitNormal * 0.001;

        // Next bounce is further attenuated by reflectivity
        attenuation *= reflectivity;
    }

    // --------------------------------------------------------
    // Fog based on the total distance traveled
    // --------------------------------------------------------
    float nearFog = 10.0;
    float farFog = 50.0;
    float fogFactor = clamp((totalDistance - nearFog) / (farFog - nearFog), 0.0, 1.0);

    // If totalDistance < nearFog, fogFactor = 0 => no fog
    // If totalDistance > farFog, fogFactor = 1 => full fog

    vec3 fogColor = vec3(0.9, 0.9, 1.0);
    accColor = mix(accColor, fogColor, fogFactor);

    return accColor;
}

// --------------------------------------------------------
// 4. Main Entry
// --------------------------------------------------------
void main() {
    // Convert TexCoords [0..1] to [-1..1]
    vec2 uv = TexCoords * 2.0 - 1.0;

    // Camera params
    float fov = radians(45.0);
    float aspect = 1280.0 / 720.0; // match your window aspect ratio

    // Build the base ray direction from UV
    vec3 rayDir = normalize(uCamRot * vec3(
        uv.x * aspect * tan(fov / 2.0),
        uv.y * tan(fov / 2.0),
        1.0
    ));

    vec3 color;

    if (uDenoise) {
        // Example: multi-sample approach
        int samples = 80;
        vec3 acc = vec3(0.0);
        for (int i = 0; i < samples; i++) {
            float jitterX = (
                fract(sin(dot(uv + vec2(float(i), uTime),
                    vec2(12.9898, 78.233))) * 43758.5453)
                - 0.5
                ) / 500.0;
            float jitterY = (
                fract(sin(dot(uv + vec2(float(i) + 1.0, uTime),
                    vec2(93.9898, 67.345))) * 43758.5453)
                - 0.5
                ) / 500.0;
            vec2 uvOffset = uv + vec2(jitterX, jitterY);

            vec3 rayDirOffset = normalize(uCamRot * vec3(
                uvOffset.x * aspect * tan(fov / 2.0),
                uvOffset.y * tan(fov / 2.0),
                1.0
            ));

            acc += traceRay(uCamPos, rayDirOffset);
        }
        color = acc / float(samples);
    }
    else {
        // Single-sample path
        color = traceRay(uCamPos, rayDir);
    }

    FragColor = vec4(color, 1.0);
}
