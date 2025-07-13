# Mandelbrot Voyage 2
A fully interactive open-source GPU-based fully customizable fractal zoom program aimed at creating artistic and high quality images & videos.

![mv2](https://github.com/Yilmaz4/MV2/assets/77583632/f9763dd7-e527-441b-a5b4-3eae7c4a7695)

Mandelbrot set is a set defined in the complex plane, and consists of all complex numbers which satisfy $|Z_n| < 2$ for all $n$ under iteration of $Z_{n+1}=Z_n^2+c$ where $c$ is the particular point in the Mandelbrot set and $Z_0=0$.

Points inside the set are colored black, and points outside the set are colored based on $n$.

## Features
- Smooth coloring with $n^{\prime}=n-\log_P\left(\log|Z_n|\right)$ where $n$ is the first iteration number after $|Z_n| \geq 2$
- Fully customizable equation in GLSL syntax, supporting 10 different complex defined functions
- Normal vector calculation for Lambert lighting
- Super-sampling anti aliasing (SSAA)
- Customizable color palette with up to 16 colors
- Hold right-click to see the orbit and the corresponding Julia set for any point
- Zoom sequence creation

https://github.com/Yilmaz4/MV2/assets/77583632/bc77921c-2139-464b-84e5-ba0f5cb2a3ce

https://github.com/Yilmaz4/MV2/assets/77583632/b6c11774-b2cd-4895-8eef-0fd47954e4ff

## Custom equations
The expression in the inputs are directly substituted into the GLSL shader code. Because double-precision bivectors are used, most of the built-in GLSL functions are unavailable; and because vector arithmetic such as multiplication or division are component-wise, the following list of custom implemented functions have to be used instead:

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

<details>
  <summary>Local variables</summary>

  You can use these variables in the custom equation however you want
  | Name | Description |
  | --- | --- |
  | `dvec2 c` | Corresponding point in the complex plane of the current pixel |
  | `dvec2 z` | $Z_n$ |
  | `dvec2 prevz` | $Z_{n-1}$ |
  | `int i` | Number of iterations so far |
  | `dvec2 xsq` | $\Re^2(Z_n)$, for optimization purposes |
  | `dvec2 ysq` | $\Im^2(Z_n)$, for optimization purposes |
  | `float degree` | Uniform variable of type float, adjustable from the UI |
  | `int max_iters` | Maximum number of iterations before point is considered inside the set |
  | `double zoom` | Length of a single pixel in screen space in the complex plane |
</details>

The first input (`dvec2`) is the new value of $Z_{n+1}$ in each next iteration. The second input (`bool`) is the condition which when true the current pixel will be considered inside the set. The third input (`dvec2`) is $Z_0$.

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

The project has been tested on Windows and Linux.

## Limitations
- Any custom equation utilizing `dvec2 cpow(dvec2, float)` where the second argument $\not\in [1,4] \cap \mathbb{N}$ will be limited to single-precision floating point, therefore limiting amount of zoom to $10^4$.
- Most of the double-precision transcendental functions are software emulated, which means performance will be severely impacted.
- Maximum zoom is $10^{14}$ due to finite precision.

## Contributing
Contributions are highly welcome, it could be anything from a typo correction to a completely new feature, feel free to create a pull request or raise an issue!
