# Mandelbrot Voyage 2
A fully interactive open-source GPU-based fully customizable fractal zoom program aimed at creating artistic and high quality images & videos.

![Screenshot of the UI](https://github.com/Yilmaz4/MV2/assets/77583632/d8a478c7-7a6f-42c0-b0f2-89a93d4702dc)

Mandelbrot set is a set defined in the complex plane, and consists of all complex numbers which satisfy $|Z_n| < 2$ for all $n$ under iteration of $Z_{n+1}=Z_n^2+c$ where $c$ is the particular point in the Mandelbrot set and $Z_0=0$.

Points inside the set are colored black, and points outside the set are colored based on $n$.

## Features
- Continuous (smooth) coloring with $n^{\prime}=n-\log_P\left(\log|Z_n|\right)$ where $n$ is the first iteration number where $|Z_n| > 2$
- Fully customizable equation in GLSL syntax, supporting 10 different complex defined functions
- Normal vector calculation for Lambert lighting
- Super-sampling anti aliasing (SSAA)
- Customizable color palette with up to 16 colors
- Hold right-click to see the corresponding Julia set for any point
- Zoom sequence creation

## Planned features
- GPU arbitrary precision for zooming without limits

# Custom equations
The expression in the first input is directly substituted into the GLSL shader code. Because double-precision bivectors are used, most of the built-in GLSL functions are unavailable, therefore the following list of custom implemented functions must be used instead:

<details>
<summary>Custom functions reference</summary>

### Double-precision transcendental functions
| Function | Definition |
| --- | --- |
`double atan2(double, double)` | $\tan^{-1}(x/y)$ |
`double dsin(double)` | $\sin(x)$ |
`double dcos(double)` | $\cos(x)$ |
`double dlog(double)` | $\ln(x)$ |
`double dexp(double)` | $e^x$ |
`double dpow(double, double)` | $x^y$ |

### Complex-defined double-precision functions
| Function | Definition |
| --- | --- |
`dvec2 cexp(dvec2)` | $e^z $|
`dvec2 cconj(dvec2)` | $\bar{z} $|
`double carg(dvec2)` | $\arg{(z)}$|
`dvec2 cmultiply(dvec2, dvec2)` | $z\cdot w$|
`dvec2 cdivide(dvec2, dvec2)` | $\{z}/{w} $|
`dvec2 clog(dvec2)` | $\ln{(z)}$ |
`dvec2 cpow(dvec2, float)` | $z^x, x \in \mathbb{R}$|
`dvec2 cpow(dvec2, dvec2)` | $z^w, w \in \mathbb{C}$|
`dvec2 csin(dvec2)` | $\sin(z)$|
`dvec2 ccos(dvec2)` | $\cos(z)$|

</details>

## Examples
![Screenshot 2024-05-18 165422](https://github.com/Yilmaz4/MV2/assets/77583632/d9fb9d98-52c5-44bc-aeed-2c875a807411)\
Burning ship fractal $Z_{n+1}=(|\Re(Z_n)| + i|\Im(Z_n)|)^2+c$

![Screenshot 2024-05-18 170237](https://github.com/Yilmaz4/MV2/assets/77583632/6798c9d6-5fcc-4fb8-b767-88d4f29863f0)\
Tricorn fractal $Z_{n+1}=\bar{Z_n}^2+c$

## Preview & gallery
![2024-05-16 00-02-40](https://github.com/Yilmaz4/MV2/assets/77583632/62a251ba-33af-4b81-8e86-50531adbc114)\
Fractional order Mandelbrot sets in real time\
![2024-05-16 00-02-40_2](https://github.com/Yilmaz4/MV2/assets/77583632/10c6e49f-1dd6-4937-9d55-b0eeb6a8e5f5)\
7 second 20 FPS zoom sequence rendered at 640x480 in 49 seconds, with RTX 3070

## Limitations
- Any custom equation utilizing `dvec2 cpow(dvec2, float)` will be limited to single-precision floating point, limiting amount of zoom to 10^4
- Most of the double-precision transcendental functions are software emulated, which means performance will be severely impacted
- Maximum zoom is 10^14 due to finite precision of double
