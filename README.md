# Mandelbrot Voyage 2
A fully interactive open-source GPU-based fully customizable fractal zoom program aimed at creating artistic and high quality images & videos.

![Screenshot of the UI](https://github.com/Yilmaz4/MV2/assets/77583632/d8a478c7-7a6f-42c0-b0f2-89a93d4702dc)

Mandelbrot set is a set defined in the complex plane, and consists of all complex numbers which satisfy $|Z_n| < 2$ for all $n$ under iteration of $Z_{n+1}=Z_n^2+c$ where $c$ is the particular point in the Mandelbrot set and $Z_0=0$.

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
Burning ship fractal $Z_{n+1}=(|\Re(Z_n)| + i|\Im(Z_n)|)^2+c$\
![Screenshot 2024-05-18 165422](https://github.com/Yilmaz4/MV2/assets/77583632/d9fb9d98-52c5-44bc-aeed-2c875a807411)\
Tricorn fractal $Z_{n+1}=\bar{Z_n}^2+c$
![Screenshot 2024-05-18 170237](https://github.com/Yilmaz4/MV2/assets/77583632/6798c9d6-5fcc-4fb8-b767-88d4f29863f0)

## Preview & gallery
![2024-05-16 00-02-40](https://github.com/Yilmaz4/MV2/assets/77583632/62a251ba-33af-4b81-8e86-50531adbc114)\
Fractional order Mandelbrot sets in real time\
![2024-05-16 00-02-40_2](https://github.com/Yilmaz4/MV2/assets/77583632/10c6e49f-1dd6-4937-9d55-b0eeb6a8e5f5)\
7 second 20 FPS zoom sequence rendered at 640x480 in 49 seconds, with RTX 3070

## Limitations
- Any order ≠ 2 will be limited to single-precision floating point, limiting amount of zoom to 10^4
- Maximum zoom is 10^14 due to finite precision
