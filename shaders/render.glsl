#version 430 core

#extension GL_ARB_gpu_shader_fp64 : enable
#extension GL_NV_uniform_buffer_std430_layout : enable

double M_PI = 3.14159265358979323846LF;
double M_2PI = M_PI * 2.0LF;
double M_PI2 = M_PI / 2.0LF;
double M_E = 2.71828182845904523536LF;
double M_EHALF = 1.6487212707001281469LF;

out vec4 fragColor;

uniform ivec2  screenSize;
uniform dvec2  offset;
uniform double zoom;
uniform int    max_iters;
uniform float  spectrum_offset;
uniform float  iter_multiplier;
uniform double bailout_radius;
uniform int    continuous_coloring;
uniform int    normal_map_effect;
uniform vec3   set_color;
uniform dvec2  mousePos; // position in pixels
uniform dvec2  mouseCoord; // position in the complex plane
uniform double julia_zoom;
uniform int    julia_maxiters;
uniform int    blur;

uniform float  height;
uniform float  angle;

//experimental
uniform float  degree;
uniform double const_coeff;

layout(binding = 0) uniform sampler2D mandelbrotTex;
layout(binding = 1) uniform sampler2D postprocTex;
layout(binding = 2) uniform sampler2D finalTex;

uniform int protocol;

dvec2 cexp(vec2 z) {
    return exp(z.x) * dvec2(cos(z.y), sin(z.y));
}
dvec2 cconj(dvec2 z) {
    return dvec2(z.x, -z.y);
}
dvec2 cmultiply(dvec2 a, dvec2 b) {
    return dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}
dvec2 cdivide(dvec2 a, dvec2 b) {
    return dvec2(a.x * b.x + a.y * b.y, a.y * b.x - a.x * b.y) / (b.x * b.x + b.y * b.y);
}
dvec2 cpow(dvec2 z, float p) {
    vec2 c = vec2(z);
    float r = sqrt(c.x * c.x + c.y * c.y);
    float theta = atan(c.y, c.x);
    return pow(r, p) * dvec2(cos(p * theta), sin(p * theta));
}

uniform mat3 weight = mat3(
    0.0751136, 0.123841, 0.0751136,
    0.1238410, 0.204180, 0.1238410,
    0.0751136, 0.123841, 0.0751136
);

layout(std430, binding = 1) readonly buffer spectrum {
    vec4 spec[];
};
uniform int span;

vec3 color(float i) {
    if (i < 0.f) return set_color;
    i = mod(i + spectrum_offset, span) / span;
    for (int v = 0; v < spec.length(); v++) {
        if (spec[v].w >= i) {
            vec4 v2 = spec[v];
            vec4 v1;
            if (v > 0.f) v1 = spec[v - 1];
            else v2 = v1;
            vec4 dv = v2 - v1;
            return v1.rgb + (dv.rgb * (i - v1.w)) / dv.w;
        }
    }
    return vec3(i, i, i);
}

dvec2 advance(dvec2 z, dvec2 c) {
    if (degree == 2.f) z = dvec2(z.x * z.x - z.y * z.y, 2 * z.x * z.y) + const_coeff * c;
    else z = cpow(z, degree) + const_coeff * c;
    return z;
}

dvec2 differentiate(dvec2 z, dvec2 der) {
    der = cmultiply(cpow(z, degree - 1.f), der) * degree + 1.0;
    return der;
}

bool is_experimental() {
    int res = 0;
    res |= int(const_coeff != 1.0);
    res |= int(degree != 2.f);
    return bool(res);
}

void main() {
    if (protocol == 4) {
        dvec2 c = dvec2(julia_zoom, julia_zoom) * (dvec2(gl_FragCoord.x / screenSize.x, (screenSize.y - gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5));
        dvec2 z = c;

        for (int i = 1; i < julia_maxiters; i++) {
            double xx = z.x * z.x;
            double yy = z.y * z.y;
            if (xx + yy >= bailout_radius) {
                fragColor = vec4(color(iter_multiplier * (i + 1 - log2(log2(float(length(z)))) / log2(degree))), 1.f);
                return;
            }
            z = advance(z, mouseCoord);
        }
        fragColor = vec4(set_color, 1.0);
    }

    if (protocol == 3) {
        dvec2 nv = cexp(vec2(0.f, angle * 2.f * M_PI / 360.f));

        dvec2 c = offset + (dvec2(gl_FragCoord.x / screenSize.x, (gl_FragCoord.y) / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (screenSize.y * zoom) / screenSize.x);
        dvec2 z = c;

        dvec2 dc = dvec2(1.0, 0.0);
        dvec2 der = dc;

        double xx = z.x * z.x;
        double yy = z.y * z.y;

        double p = xx - z.x / 2.0 + 0.0625 + yy;
        if (is_experimental() || !is_experimental() && (4.0 * p * (p + (z.x - 0.25)) > yy && (xx + yy + 2 * z.x + 1) > 0.0625)) {
            for (int i = 1; i < max_iters; i++) {
                if (xx + yy > bailout_radius) {
                    double t;
                    if (normal_map_effect == 1) {
                        dvec2 u = cdivide(z, der);
                        u = u / length(u);
                        t = (u.x * nv.x + u.y * nv.y + height) / (1.f + height);
                        if (t < 0) t = 0;
                    }
                    else t = 0;

                    if (continuous_coloring == 1) {
                        fragColor = vec4(i + 2 - log2(log2(float(length(z)))) / log2(degree), i, t, 0.f);
                    }
                    else {
                        fragColor = vec4(i, i, t, 0.f);
                    }
                    return;
                }
                if (normal_map_effect == 1)
                    der = differentiate(z, der);
                z = advance(z, c);
                xx = z.x * z.x;
                yy = z.y * z.y;
            }
        }
        fragColor = vec4(-1.f, -1.f, 0.f, 0.f);
    }
    if (protocol == 2) {
        vec4 data = texture(mandelbrotTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
        if (blur == 1) {
            fragColor = vec4(mix(color(data.x * iter_multiplier), vec3(0.f), normal_map_effect * data.z), 1.f);
            return;
        }
        vec3 blurredColor = vec3(0.0);
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                vec4 d = texture(mandelbrotTex, vec2((gl_FragCoord.x + float(i)) / screenSize.x, (gl_FragCoord.y + float(j)) / screenSize.y));
                vec3 s = color(d.x * iter_multiplier);
                blurredColor += mix(s, vec3(0.f), normal_map_effect * d.z) * weight[i + 1][j + 1];
            }
        }
        fragColor = vec4(blurredColor, 1.f);
    }
    if (protocol == 1) {
        fragColor = texture(postprocTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
    }
    if (protocol == 0) {
        fragColor = texture(finalTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
    }
}