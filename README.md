# Mandelbrot Voyage 2
A fully interactive GPU-accelerated customizable fractal zoom program aimed at creating artistic and high quality images & videos.

![snapshot_2025-07-17_11-28-00](https://github.com/user-attachments/assets/4dd5d5e4-7db7-411e-885b-816ea37e16ab)

Mandelbrot set is a set defined in the complex plane, and consists of all complex numbers which satisfy $|Z_n| < 2$ for all $n$ under iteration of $Z_{n+1}=Z_n^2+c$ where $c$ is the complex number in question and $Z_0=0$.

Points inside the set are colored black, while points outside the set are colored based on $n$ (how many iterations it took for the point to diverge).

## Features
- Fully customizable equation in GLSL syntax
- Perturbation theory for zooming further than you'll have the patience for
- 11 built-in unique fractals
- Smooth coloring
- Normal mapping for shadows
- TAA (temporal anti-aliasing) and SSAA (super sampling anti-aliasing)
- Customizable color palette with up to 16 colors
- Hold right-click to see the orbit, the corresponding Julia set, and hear the sound waves of the orbit for any point
- Zoom video creation to uncompressed AVI

https://github.com/user-attachments/assets/3195f7ae-b438-4f13-83b6-38fc45c53397

Zoom video demonstrating perturbation theory using one central reference orbit, 70K iterations, taking 3 minutes to render

https://github.com/Yilmaz4/MV2/assets/77583632/b6c11774-b2cd-4895-8eef-0fd47954e4ff

## Custom equations
Users can write custom equations to draw fractals in the complex plane. The following functions are available in addition to the standard functions in GLSL:

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

## Building

Clone the repository with `--recurse-submodules`, then go into the directory
```
git clone --recurse-submodules https://github.com/Yilmaz4/MV2.git
cd MV2
```

<details>
  <summary><b>Windows</b></summary>

<br>

Install MSYS2 to `C:\msys64`, and from a MSYS2 UCRT64 terminal, run 
```
pacman -Syu mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-ninja mingw-w64-ucrt-x86_64-cmake
```
Then add `C:\msys64\ucrt64\bin` to PATH. Note that you have to run the cmake commands in the UCRT64 terminal as well.
</details>

<details>
  <summary><b>On Linux</b></summary>

<br>

**Arch Linux**
```
sudo pacman -S base-devel cmake ninja
```
**Debian/Ubuntu**
```
sudo apt install build-essential cmake ninja-build
```
**Fedora**
```
sudo dnf install gcc-c++ cmake ninja-build pkg-config
```
**openSUSE**
```
sudo zypper in -t pattern devel-basis
sudo zypper install cmake ninja
```
</details>

Finally, generate the build files with CMake and build
```
cmake -S . -B build -G Ninja
cmake --build build
```

You can then find the binary in the `bin` directory

## Limitations
- Any custom equation utilizing `dvec2 cpow(dvec2, float)` where the second argument $\not\in \{2, 3, 4\}$ will be limited to single-precision floating point, therefore limiting amount of zoom to $10^4$.
- Maximum zoom for all fractals other than the Mandelbrot set is $10^{14}$ due to finite precision. Perturbation can be enabled for the Mandelbrot set to zoom further.

## Known issues
- Shader linkage takes very long on Intel iGPUs with Mesa drivers on Linux, causing the program to open only after several minutes, I have no idea why
- Enabling perturbation causes "glitches" to appear such as same-color blobs or noise. This can be minimized with glitch detection algorithms and more reference orbits as described [here](https://mathr.co.uk/blog/2021-05-14_deep_zoom_theory_and_practice.html). TODO: implement glitch detection.
- The zoom videos do not play in VLC or Windows Media Player, even though they do in MPV. TODO: Use ffmpeg instead.

## Contributing
Contributions are highly welcome, it could be anything from a typo correction to a completely new feature, feel free to create a pull request or raise an issue!
