#version 460 core

double M_PI = 3.14159265358979323846LF;
double M_2PI = M_PI * 2.0LF;
double M_PI2 = M_PI / 2.0LF;
double M_E = 2.71828182845904523536LF;
double M_EHALF = 1.6487212707001281469LF;

out vec4 fragColor;

uniform ivec2  screenSize;
uniform dvec2  center;
uniform double zoom;
uniform int    max_iters;
uniform float  spectrum_offset;
uniform float  iter_multiplier;
uniform int    continuous_coloring;
uniform int    normal_map_effect;
uniform vec3   set_color;
uniform dvec2  mousePos; // position in pixels
uniform dvec2  mouseCoord; // position in the complex plane
uniform double julia_zoom;
uniform int    julia_maxiters;
uniform int    blur;
uniform int    transfer_function;
//normal mapping (https://www.math.univ-toulouse.fr/~cheritat/wiki-draw/index.php/Mandelbrot_set#Normal_map_effect)
uniform float  height;
uniform float  angle;

uniform int    fractal;
uniform float  power;
uniform dvec2  initialz;

layout(std430, binding = 0) coherent buffer vertices {
    vec2 orbit[];
};
uniform int numVertices = 0;

layout(std430, binding = 1) readonly buffer spectrum {
    vec4 spec[];
};
uniform int span;

layout(std430, binding = 2) readonly buffer variables {
    float sliders[];
};

layout(std430, binding = 3) readonly buffer kernel {
    float weights[];
};
uniform int radius;

layout(std430, binding = 4) readonly buffer newton_roots {
    vec2 roots[];
};

layout(binding = 0) uniform sampler2D mandelbrotTex;
layout(binding = 1) uniform sampler2D postprocTex;
layout(binding = 2) uniform sampler2D finalTex;

uniform int op;

double atan2(double y, double x) {
    const double atan_tbl[] = {
        -3.333333333333333333333333333303396520128e-1LF,
         1.999999117496509842004185053319506031014e-1LF,
        -1.428514132711481940637283859690014415584e-1LF,
         1.110012236849539584126568416131750076191e-1LF,
        -8.993611617787817334566922323958104463948e-2LF,
         7.212338962134411520637759523226823838487e-2LF,
        -5.205055255952184339031830383744136009889e-2LF,
         2.938542391751121307313459297120064977888e-2LF,
        -1.079891788348568421355096111489189625479e-2LF,
         1.858552116405489677124095112269935093498e-3LF
    };

    double ax = abs(x);
    double ay = abs(y);
    double t0 = max(ax, ay);
    double t1 = min(ax, ay);

    double a = 1 / t0;
    a *= t1;

    double s = a * a;
    double p = atan_tbl[9];

    p = fma(fma(fma(fma(fma(fma(fma(fma(fma(fma(p, s,
        atan_tbl[8]), s,
        atan_tbl[7]), s,
        atan_tbl[6]), s,
        atan_tbl[5]), s,
        atan_tbl[4]), s,
        atan_tbl[3]), s,
        atan_tbl[2]), s,
        atan_tbl[1]), s,
        atan_tbl[0]), s * a, a);

    double r = ay > ax ? (1.57079632679489661923LF - p) : p;

    r = x < 0 ? 3.14159265358979323846LF - r : r;
    r = y < 0 ? -r : r;

    return r;
}

double dsin(double x) {
    int i;
    int counter = 0;
    double sum = x, t = x;
    double s = x;

    if (isnan(x) || isinf(x))
        return 0.0LF;

    while (abs(s) > 1.1) {
        s = s / 3.0;
        counter += 1;
    }

    sum = s;
    t = s;

    for (i = 1; i <= 3; i++)
    {
        t = (t * (-1.0) * s * s) / (2.0 * double(i) * (2.0 * double(i) + 1.0));
        sum = sum + t;
    }

    for (i = 0; i < counter; i++)
        sum = 3.0 * sum - 4.0 * sum * sum * sum;

    return sum;
}

double dcos(double x) {
    int i;
    int counter = 0;
    double sum = 1, t = 1;
    double s = x;

    if (isnan(x) || isinf(x))
        return 0.0LF;

    while (abs(s) > 1.1) {
        s = s / 3.0;
        counter += 1;
    }

    for (i = 1; i <= 3; i++)
    {
        t = t * (-1.0) * s * s / (2.0 * double(i) * (2.0 * double(i) - 1.0));
        sum = sum + t;
    }

    for (i = 0; i < counter; i++)
        sum = -3.0 * sum + 4.0 * sum * sum * sum;

    return sum;
}

double dlog(double x) {
    double
        Ln2Hi = 6.93147180369123816490e-01LF, /* 3fe62e42 fee00000 */
        Ln2Lo = 1.90821492927058770002e-10LF, /* 3dea39ef 35793c76 */
        L0 = 7.0710678118654752440e-01LF,  /* 1/sqrt(2) */
        L1 = 6.666666666666735130e-01LF,   /* 3FE55555 55555593 */
        L2 = 3.999999999940941908e-01LF,   /* 3FD99999 9997FA04 */
        L3 = 2.857142874366239149e-01LF,   /* 3FD24924 94229359 */
        L4 = 2.222219843214978396e-01LF,   /* 3FCC71C5 1D8E78AF */
        L5 = 1.818357216161805012e-01LF,   /* 3FC74664 96CB03DE */
        L6 = 1.531383769920937332e-01LF,   /* 3FC39A09 D078C69F */
        L7 = 1.479819860511658591e-01LF;   /* 3FC2F112 DF3E5244 */

    if (isinf(x))
        return 1.0 / 0.0; /* return +inf */
    if (isnan(x) || x < 0)
        return -0.0; /* nan */
    if (x == 0)
        return -1.0 / 0.0; /* return -inf */

    int ki;
    double f1 = frexp(x, ki);

    if (f1 < L0) {
        f1 *= 2.0;
        ki--;
    }

    double f = f1 - 1.0;
    double k = double(ki);

    double s = f / (2.0 + f);
    double s2 = s * s;
    double s4 = s2 * s2;
    double t1 = s2 * (L1 + s4 * (L3 + s4 * (L5 + s4 * L7)));
    double t2 = s4 * (L2 + s4 * (L4 + s4 * L6));
    double R = t1 + t2;
    double hfsq = 0.5 * f * f;

    return k * Ln2Hi - ((hfsq - (s * (hfsq + R) + k * Ln2Lo)) - f);
}

double exp_approx(double x) {
    double u = 3.5438786726672135e-7LF;
    u = u * x + 2.6579928825872315e-6LF;
    u = u * x + 2.4868626682939294e-5LF;
    u = u * x + 1.983843872760968e-4LF;
    u = u * x + 1.3888965369092271e-3LF;
    u = u * x + 8.3333320096674514e-3LF;
    u = u * x + 4.1666666809276345e-2LF;
    u = u * x + 1.6666666665771182e-1LF;
    u = u * x + 5.0000000000028821e-1LF;
    u = u * x + 9.9999999999999638e-1LF;
    u = u * x + 1.0LF;
    if (isnan(u) || isinf(u))
        return 0.0LF;
    return u;
}

double dexp(double x) {
    int i;
    int n;
    double f;
    double e_accum = M_E;
    double answer = 1.0LF;
    bool invert_answer = true;

    if (x < 0.0) {
        x = -x;
        invert_answer = true;
    }

    n = int(x);
    f = x - double(n);

    if (f > 0.5) {
        f -= 0.5;
        answer = M_EHALF;
    }

    for (i = 0; i < 8; i++) {
        if (((n >> i) & 1) == 1)
            answer *= e_accum;
        e_accum *= e_accum;
    }

    answer *= exp_approx(x);

    if (invert_answer)
        answer = 1.0 / answer;

    return answer;
}

double dpow(double x, double y) {
    return dexp(y * dlog(x));
}

dvec2 cexp(dvec2 z) {
    return dexp(z.x) * dvec2(dcos(z.y), dsin(z.y));
}
dvec2 cconj(dvec2 z) {
    return dvec2(z.x, -z.y);
}
double carg(dvec2 z) {
    return atan2(z.y, z.x);
}
dvec2 cmultiply(dvec2 a, dvec2 b) {
    return dvec2(a.x * b.x - a.y * b.y, a.x * b.y + a.y * b.x);
}
dvec2 cdivide(dvec2 a, dvec2 b) {
    return dvec2((a.x * b.x + a.y * b.y), (a.y * b.x - a.x * b.y)) / (b.x * b.x + b.y * b.y);
}
dvec2 clog(dvec2 z) {
    return dvec2(dlog(length(z)), carg(z));
}
dvec2 cpow(dvec2 z, float p) {
    double xsq, ysq;
    if (p == 0.f)
        return dvec2(1.f, 0.f);
    if (p == 1.f)
        return z;
    if (floor(p) == p) {
        xsq = z.x * z.x;
        ysq = z.y * z.y;
    }
    if (p == 2.f)
        return dvec2(xsq - ysq, 2 * z.x * z.y);
    if (p == 3.f)
        return dvec2(xsq * z.x - 3 * z.x * ysq, 3 * xsq * z.y - ysq * z.y);
    if (p == 4.f)
        return dvec2(xsq * xsq + ysq * ysq - 6 * xsq * ysq, 4 * xsq * z.x * z.y - 4 * z.x * ysq * z.y);
    if (p == 5.f)
        return dvec2(xsq * xsq * z.x + 5 * z.x * ysq * ysq - 10 * xsq * z.x * ysq,
            5 * xsq * xsq * z.y + ysq * ysq * z.y - 10 * xsq * ysq * z.y);
    vec2 c = vec2(z);
    float theta = atan(c.y, c.x);
    return pow(float(length(z)), p) * dvec2(cos(p * theta), sin(p * theta));
}
dvec2 cpow(dvec2 a, dvec2 b) {
    double r = length(a);
    if (r == 0.) return dvec2(0.);
    double t = carg(a);
    dvec2 loga = dvec2(dlog(r), t);
    return cexp(cmultiply(b, loga));
}
dvec2 csin(dvec2 z) {
    vec2 c = vec2(z);
    return dvec2(sin(c.x) * cosh(c.y), cos(c.x) * sinh(c.y));
}
dvec2 ccos(dvec2 z) {
    vec2 c = vec2(z);
    return dvec2(cos(c.x) * cosh(c.y), -sin(c.x) * sinh(c.y));
}
dvec2 csqrt(dvec2 z) {
    double r = length(z);
    return sqrt(r) * (z + dvec2(0.f, r)) / length(z + dvec2(0.f, r));
}

vec3 color(float i) {
    if (i < 0.f) return set_color;
    switch (transfer_function) {
    case 1:
        i = sqrt(i);
        break;
    case 2:
        i = pow(i, 1.f/3.f);
        break;
    case 3:
        i = log(i);
        break;
    }
    i = mod(i * iter_multiplier + spectrum_offset, span) / span;
    if (i < 1e-3 || i > 0.999) {
        return spec[0].rgb;
    }
    for (int v = 0; v < spec.length(); v++) {
        if (spec[v].w > i) {
            vec4 v2 = spec[v];
            vec4 v1;
            if (v > 0.f) v1 = spec[v - 1];
            else v2 = v1;
            vec4 dv = v2 - v1;
            return v1.rgb + (dv.rgb * (i - v1.w)) / dv.w;
        }
    }
    return vec3(0.f);
}

dvec2 advance(dvec2 z, dvec2 c, dvec2 prevz, double xsq, double ysq) {
    return %s;
}

dvec2 differentiate(dvec2 z, dvec2 der) {
    der = cmultiply(cpow(z, power - 1.f), der) * power + 1.0;
    return der;
}

bool is_experimental() {
    int res = %i;
    res |= int(power != 2.f);
    return true;
}

dvec2 complex_to_pixel(dvec2 complexCoord) {
    ivec2 ss = screenSize / blur;
    dvec2 normalizedCoord = (complexCoord - dvec2(center.x, center.y));
    normalizedCoord /= dvec2(zoom, (ss.y * zoom) / ss.x);
    dvec2 pixelCoordNormalized = normalizedCoord + dvec2(0.5, 0.5);
    return dvec2(pixelCoordNormalized.x * ss.x, pixelCoordNormalized.y * ss.y);
}

void main() {
    dvec2 nv = cexp(dvec2(0.f, angle * 2.f * M_PI / 360.f));

    if (op == 3) {
        dvec2 c = (dvec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(julia_zoom, julia_zoom);
        dvec2 z = c;
        dvec2 prevz = z;

        dvec2 der = dvec2(1.0, 0.0);

        double xsq = z.x * z.x;
        double ysq = z.y * z.y;

        for (int i = 1; i < julia_maxiters; i++) {
            if (%s) {
                float t = 0;
                if (normal_map_effect == 1) {
                    dvec2 u = cdivide(z, der);
                    u = u / length(u);
                    t = float(u.x * nv.x + u.y * nv.y + height) / (1.f + height);
                    if (t < 0) t = 0;
                }
                float s = i + 1 - log2(log2(float(length(z)))) / log2(power);
                float final = (continuous_coloring == 1 && s >= 1 && s < julia_maxiters) ? s : (i - 1);
                fragColor = vec4(mix(vec3(0.f), vec3(color((continuous_coloring == 1 ? final : i))), normal_map_effect == 1 ? pow(t, 1.f / 1.8f) : 1.f), 1.f);
                return;
            }
            if (normal_map_effect == 1)
                der = differentiate(z, der);
            prevz = z;
            z = advance(z, mouseCoord, prevz, xsq, ysq);
            xsq = z.x * z.x;
            ysq = z.y * z.y;
        }
        fragColor = vec4(set_color, 1.0);
    }

    if (op == 2) {
        dvec2 c = center + (dvec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (screenSize.y * zoom) / screenSize.x);
        dvec2 z = %s;
        dvec2 prevz = dvec2(0.0);

        dvec2 der = dvec2(1.0, 0.0);

        double xsq = z.x * z.x;
        double ysq = z.y * z.y;

        double p = xsq - z.x / 2.0 + 0.0625 + ysq;
        int i;
        if (is_experimental() || !is_experimental() && (4.0 * p * (p + (z.x - 0.25)) > ysq && (xsq + ysq + 2 * z.x + 1) > 0.0625)) {
            for (i = 1; i < max_iters; i++) {
                if (%s) {
                    double t = 0;
                    if (normal_map_effect == 1) {
                        dvec2 u = cdivide(z, der);
                        u = u / length(u);
                        t = (u.x * nv.x + u.y * nv.y + height) / (1.f + height);
                        if (t < 0) t = 0;
                    }

                    if (continuous_coloring == 1 && i > 1) {
                        float s = i + 1 - log2(log(float(length(z)))) / log2(power);
                        if (s < 1 || s >= max_iters) {
                            s = i - 1;
                        }
                        fragColor = vec4(s, i, t, 0.f);
                    }
                    else {
                        fragColor = vec4(i, i, t, 0.f);
                    }
                    return;
                }
                if (normal_map_effect == 1)
                    der = differentiate(z, der);
                prevz = z;
                z = advance(z, c, prevz, xsq, ysq);
                xsq = z.x * z.x;
                ysq = z.y * z.y;
            }
        }
        fragColor = vec4(-1.f, -1.f, 0.f, 0.f);
    }
    if (op == 1) {
        if (blur == 1) {
            vec4 data = texture(mandelbrotTex, vec2(gl_FragCoord.x / screenSize.x, gl_FragCoord.y / screenSize.y));
            fragColor = vec4(mix(vec3(0.f), color(data.x), normal_map_effect == 1 ? pow(data.z, 1.f / 1.8f) : 1.f), 1.f);
        }
        else {
            vec3 blurredColor = vec3(0.0);
            int kernelSize = 2 * radius + 1;
            for (int i = -radius; i <= radius; i++) {
                for (int j = -radius; j <= radius; j++) {
                    vec4 d = texture(mandelbrotTex, (gl_FragCoord.xy + vec2(i, j)) / screenSize);
                    vec3 s = color(d.x);
                    blurredColor += mix(vec3(0.f), s, normal_map_effect == 1 ? pow(d.z, 1.f / 1.8f) : 1.f) * weights[(i + radius) * kernelSize + (j + radius)];
                }
            }
            fragColor = vec4(blurredColor, 1.f);
        }
        if (ivec2(gl_FragCoord.xy) == ivec2(mousePos)) {
            ivec2 ss = screenSize / blur;
            dvec2 c = center + (dvec2(gl_FragCoord.x / ss.x, gl_FragCoord.y / ss.y) - dvec2(0.5, 0.5)) * dvec2(zoom, (ss.y * zoom) / ss.x);
            dvec2 z = %s;
            dvec2 prevz = dvec2(0.0);
            double xsq = z.x * z.x;
            double ysq = z.y * z.y;
            
            for (int i = 0; i < numVertices; i++) {
                orbit[i].x = float(complex_to_pixel(z).x);
                orbit[i].y = float(complex_to_pixel(z).y);
                prevz = z;
                z = advance(z, c, prevz, xsq, ysq);
                xsq = z.x * z.x;
                ysq = z.y * z.y;
            }
        }
    }
    if (op == 0) {
        vec4 texel = texture(postprocTex, gl_FragCoord.xy / screenSize);
        for (int i = 1; i < numVertices - 1; i++) {
            float m = (orbit[i].y - orbit[i-1].y) / (orbit[i].x - orbit[i-1].x);
            float c = orbit[i-1].y - m * orbit[i-1].x;
            if (pow(gl_FragCoord.x - orbit[i].x, 2) + pow(gl_FragCoord.y - orbit[i].y, 2) < 16.f ||
                abs(m * gl_FragCoord.x - gl_FragCoord.y + c) / sqrt(m * m + 1) < 0.5f &&
                dot(gl_FragCoord.xy - orbit[i], gl_FragCoord.xy - orbit[i-1]) < 0)
            {
                fragColor = 1.f - texel;
                return;
            }
        }
        fragColor = texel;
    }
}