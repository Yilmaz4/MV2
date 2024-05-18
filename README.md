# Mandelbrot Voyage 2
A fully interactive open-source GPU-based fractal zoom program aimed at creating artistic and high quality images & videos.

![Screenshot of the UI](https://github.com/Yilmaz4/MV2/assets/77583632/d8a478c7-7a6f-42c0-b0f2-89a93d4702dc)

## Features
- Automatically increased iteration count based on user-chosen coefficient
- Changable bailout radius, degree, iteration multiplier, spectrum offset and SSAA factor for the best looks without sacrificing performance
- Fully customizable equation
- Normal vector calculation for Lambert lighting
- Customizable color palette with up to 16 colors
- Hold right-click to see the corresponding Julia set for any point
- Zoom sequence creation

## Planned features
- GPU arbitrary precision for zooming without limits

## Preview & gallery
![2024-05-16 00-02-40](https://github.com/Yilmaz4/MV2/assets/77583632/62a251ba-33af-4b81-8e86-50531adbc114)<br />
Fractional order Mandelbrot sets in real time<br />
![2024-05-16 00-02-40_2](https://github.com/Yilmaz4/MV2/assets/77583632/10c6e49f-1dd6-4937-9d55-b0eeb6a8e5f5)<br />
7 second 20 FPS zoom sequence rendered at 640x480 in 49 seconds, with RTX 3070

# Custom equations
![Screenshot 2024-05-18 165422](https://github.com/Yilmaz4/MV2/assets/77583632/d9fb9d98-52c5-44bc-aeed-2c875a807411)
![Screenshot 2024-05-18 170237](https://github.com/Yilmaz4/MV2/assets/77583632/6798c9d6-5fcc-4fb8-b767-88d4f29863f0)
![Screenshot 2024-05-18 170546](https://github.com/Yilmaz4/MV2/assets/77583632/fd72e377-1b83-44dc-80b6-4d3d43f81ad4)



## Limitations
- Any order ≠ 2 will be limited to single-precision floating point, limiting amount of zoom to 10^4
- Maximum zoom is 10^14 due to finite precision
