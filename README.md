# Mandelbrot Voyage 2
Utilize the full power of your GPU with Mandelbrot Voyage 2, a fully interactive open-source and free fractal zoom program aimed at creating the highest quality images ever. Customize your color palette however you want! Anti-aliasing with super sampling and continuous coloring to avoid bands of color.

This is a project I started randomly while learning OpenGL for a completely different reason, but here I am.

![Screenshot 2024-02-03 213620](https://github.com/Yilmaz4/MV2/assets/77583632/73a71d0a-652d-4e68-a0c7-82cbe16d41b0)

## Features
- Automatically increased iteration count based on user-chosen coefficient
- Changable bailout radius, degree, iteration multiplier, spectrum offset and SSAA factor for the best looks without sacrificing performance
- Customizable color palette with up to 8 colors
- Explore 3rd degree and beyond Mandelbrot Set variants
- Hold right-click to see the corresponding Julia set for any point
- Zoom sequence creation

## Planned features
- 3D-like shadows
- GPU arbitrary precision for zooming without limits
- Non-integer degree Mandelbrot sets
- Burning ship fractal

## A screenshot I took with this that I'm currently using as my wallpaper
![Screenshot 2024-02-03 211204](https://github.com/Yilmaz4/MV2/assets/77583632/b1e42990-d045-450c-8ff7-d4a6e00b47fc)

\* The image still looks somewhat aliased, this is because the supersampled texture is not blurred before downsampling. Implementing that requires me to greatly change the flow of my program, so that'll be delayed for a while... not like anybody cares anyway lol
