# Mandelbrot Voyage 2
A fully interactive GPU-accelerated customizable fractal zoom program aimed at creating artistic and high quality images & videos.

![snapshot_2025-07-17_11-28-00](https://github.com/user-attachments/assets/4dd5d5e4-7db7-411e-885b-816ea37e16ab)

Mandelbrot set is a set defined in the complex plane, and consists of all complex numbers which satisfy $|Z_n| < 2$ for all $n$ under iteration of $Z_{n+1}=Z_n^2+c$ where $c$ is any point in the complex plane and $Z_0=0$.

Points inside the set are colored black, while points outside the set are colored based on $n$ (how many iterations it took for the point to diverge).

## Features
- Fully customizable equation in GLSL syntax
- Smooth coloring
- Normal mapping for shadows
- Super-sampling anti aliasing (SSAA)
- Customizable color palette with up to 16 colors
- Hold right-click to see the orbit and the corresponding Julia set for any point
- Zoom video rendering

https://github.com/Yilmaz4/MV2/assets/77583632/bc77921c-2139-464b-84e5-ba0f5cb2a3ce

https://github.com/Yilmaz4/MV2/assets/77583632/b6c11774-b2cd-4895-8eef-0fd47954e4ff

## Custom equations
Users can define custom equations to draw fractals in the complex plane. The following functions are available in addition to the standard functions in GLSL:

<details>
<summary>Custom functions reference</summary>
  
### Double-precision transcendental functions
| Function | Definition |
| --- | --- |
| `double atan2(double, double)` | $\tan^{-1}(x/y)$ |
| `double dsin(double)` | $\sin(x)$ |
| `double dcos(double)` | $\cos(x)$ |
| `double dlog(double)` | $\ln(x)$ |
| `double dexp(double)` | $e^x$ |
| `double dpow(double, double)` | $x^y$ |

### Complex-defined double-precision functions
| Function | Definition |
| --- | --- |
| `dvec2 cexp(dvec2)` | $e^z $|
| `dvec2 cconj(dvec2)` | $\bar{z} $|
| `double carg(dvec2)` | $\arg{(z)}$|
| `dvec2 cmultiply(dvec2, dvec2)` | $z\cdot w$|
| `dvec2 cdivide(dvec2, dvec2)` | $\{z}/{w} $|
| `dvec2 clog(dvec2)` | $\ln{(z)}$ |
| `dvec2 cpow(dvec2, float)` | $z^x, x \in \mathbb{R}$|
| `dvec2 csin(dvec2)` | $\sin(z)$|
| `dvec2 ccos(dvec2)` | $\cos(z)$|
</details>

Furthermore, the following variables are also exposed to the user:
<details>
  <summary>Local variables</summary>

  | Name | Description |
  | --- | --- |
  | `dvec2 c` | Corresponding point in the complex plane of the current pixel |
  | `dvec2 z` | $Z_n$ |
  | `dvec2 prevz` | $Z_{n-1}$ |
  | `int i` | Number of iterations so far |
  | `dvec2 xsq` | $\Re^2(Z_n)$, for optimization purposes |
  | `dvec2 ysq` | $\Im^2(Z_n)$, for optimization purposes |
  | `float power` | Uniform variable of type float, adjustable from the UI |
  | `int max_iters` | Maximum number of iterations before point is considered inside the set |
  | `double zoom` | Length of a single pixel in screen space in the complex plane |
  | `dvec2 center` | Center point of the window in the complex plane |
  | `dvec2 mouseCoord` | Point in the complex plane that the mouse cursor is on |
  | `dvec2 initialz` | Initial value of $Z_n$ |
</details>

The first input (the main equation, must evaluate to a `dvec2`) is the new value of $Z_{n+1}$ after each iteration. The second input (bailout condition, must evaluate to a `bool`) is the condition which, when true, the current pixel will be considered inside the set. The third input (initial Z, must evaluate to a `dvec2`) is $Z_0$.

User-controlled variables can also be defined, which can then be used in the equation and adjusted in real time using the sliders below. "Power" is a default slider that cannot be deleted and corresponds to the `power` variable above. 

> [!NOTE]  
> Due to limitations in GLSL, zoom is limited to $10^4$ for any non-integer or bigger than 4 power. See [Limitations](#limitations) for more information. Click "Round" to round the power to the nearest integer to zoom further.

## Examples
![Screenshot 2024-05-31 222113](https://github.com/Yilmaz4/MV2/assets/77583632/8f5d49f5-45ef-4627-8025-a6455f71d1dd)\
Burning ship fractal $Z_{n+1}=(|\Re(Z_n)| + i|\Im(Z_n)|)^2+c \quad Z_0=c \quad \text{Bailout: } |Z_n| > 100$

![Screenshot 2024-05-31 222339](https://github.com/Yilmaz4/MV2/assets/77583632/d062e30d-ea10-4dfa-a246-74d45ad732fc)\
Nova fractal $Z_{n+1}=Z_n-\frac{Z_n^3-1}{3Z_n^2}+c \quad Z_0=1 \quad \text{Bailout: } |Z_n-Z_{n-1}| < 10^{-4}$

![image](https://github.com/Yilmaz4/MV2/assets/77583632/cd16be5b-8a45-4d93-8911-dfbe28167162)\
Magnet 1 fractal $Z_{n+1}=\bigg(\dfrac{Z_n^2+c-1}{2Z_n+c-2}\bigg)^2 \quad Z_0=0 \quad \text{Bailout: } |Z_n| > 100 \lor |Z_n-1| < 10^{-4}$

## Building

### Dependencies

- Git
- CMake version >= 3.21
- C++ build system (Make, Ninja, MSBuild, etc.)
- C++ compiler (GCC, Clang, MSVC, etc.)

Clone the repository with `--recurse-submodules`, then go into the directory
```
git clone --recurse-submodules https://github.com/Yilmaz4/MV2.git
cd MV2
```

Generate the build files with CMake and build
```
cmake -S . -B build
cmake --build build
```

You can then find the binary in the `bin` directory

This project has been tested on Windows and Linux.

## Limitations
- Any custom equation utilizing `dvec2 cpow(dvec2, float)` where the second argument $\not\in [1,4] \cap \mathbb{N}$ will be limited to single-precision floating point, therefore limiting amount of zoom to $10^4$.
- Most of the double-precision transcendental functions are software emulated, which means performance will be severely impacted.
- Maximum zoom is $10^{14}$ due to finite precision.

## Known issues
- Shader linkage takes too long on Intel iGPUs with Mesa drivers on Linux, causing the program to open only after several minutes, I have no idea why
- Taking a screenshot results in a blurry image regardless of how high SSAA is set

## Contributing
Contributions are highly welcome, it could be anything from a typo correction to a completely new feature, feel free to create a pull request or raise an issue!
