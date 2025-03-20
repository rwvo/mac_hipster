# Mac Hipster: building HIP applications on macOS
## Imagine this...

You're at 30,000 ft, traveling about 500 mph on an 11-hour international flight. The entertainment system in row 42 is broken. You are in seat 42-B. Ah, just as well, the movie selection was substandard anyway. The guy next to you opens his Windows laptop, and starts watching Avatar. Avatar! On Windows! Could it get any worse? With a condescending arch of the brow, you open your MacBook and navigate to the folder where you illegally downloaded Krzysztof Kieślowski's _Trois Couleurs_–only to realize that you downloaded them in *.mkv format and the free version of Infuse doesn't support that. Serves you well, you cheapskate.

Your second biggest passion–after watching international arthouse films–is developing HIP applications. If only you could do that on your MacBook...

This repository has you covered. While running HIP applications on macOS is a bit challenging, given the lack of a "Team Red" GPU, compiling and linking is entirely possible. You just need to prepare the MacBook a little bit before leaving on your international trip. You do need access to a Linux machine with an AMD GPU and a working ROCm install, including the ROCm development packages; we'll copy them over to the MacBook, and then use llvm/clang to cross-compile for x86 with AMD GPU support.

Just follow along with the 5 steps below. Some of the steps may seem a bit daunting, but the total number of steps is less than half of those of the _Alcoholics Anonymous_ program. Easy!

## 1. Test Program for HIP Compilation

We start with a simple HIP test program that launches a single workgroup of 8 threads, each printing its `threadIdx.x`. See:
- [hip_hello.cpp](./hip_hello.cpp)
- [CMakeLists.txt](./CMakeLists.txt)

To verify that this is a valid and working HIP program, build this repository and run the resulting program on your **AMD GPU Linux** machine:
```bash
git clone git@github.com:rwvo/mac_hipster.git
cd mac_hipster
mkdir build && cd build
cmake ..
make
./hip_hello
```

On macOS, we can’t run it (no AMD GPU), but by the end of step 5, we can compile and link it. And we can even copy the executable over to our Linux machine and run it there. Nice!

---

## 2. Build a Custom LLVM with AMDGPU

Apple’s Clang or Homebrew’s LLVM typically **cannot** cross-compile HIP code for x86_64 Linux (missing AMDGPU support). We’ll build LLVM from source, including the AMDGPU backend and LLD.

1. **Install prerequisites on macOS**

   Install Homebrew if you don't have it yet. See https://brew.sh for instructions. Then:
   ```bash
   brew install cmake ninja
   ```
2. **Clone the LLVM repo**:
   ```bash
   git clone https://github.com/llvm/llvm-project.git
   cd llvm-project
   ```
3. **Configure (with AMDGPU + LLD)**:
   ```bash
   mkdir build && cd build
   cmake -G Ninja \
     -DCMAKE_BUILD_TYPE=Release \
     -DCMAKE_INSTALL_PREFIX=/opt/llvm-amdgpu \
     -DLLVM_ENABLE_PROJECTS="clang;lld;mlir" \
     -DLLVM_TARGETS_TO_BUILD="AArch64;X86;AMDGPU" \
     -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" \
     -DLIBCXXABI_USE_LLVM_UNWINDER=OFF \
     -DCMAKE_C_COMPILER=/usr/bin/clang \
     -DCMAKE_CXX_COMPILER=/usr/bin/clang++ \
     ../llvm
   ```
   - **`-DLLVM_TARGETS_TO_BUILD="AArch64;X86;AMDGPU"`** adds AMDGPU support.
   - **`-DLLVM_ENABLE_PROJECTS="clang;lld;mlir"`** includes LLD.
   - **`/opt/llvm-amdgpu`** is our install prefix.
4. **Build and install**:
   ```bash
   ninja -j$(sysctl -n hw.ncpu)
   sudo ninja install
   ```

After this, you have a custom LLVM in `/opt/llvm-amdgpu` with all necessary components. (Note: If you already have Homebrew’s LLVM installed, that’s fine—just make sure to reference `/opt/llvm-amdgpu/bin/clang++` in your compile commands.)

---

## 3. Copying ROCm from Linux

We need the ROCm libraries (e.g. `amdhip64`) to link HIP code. The ROCm installation directory
may be inversioned, e.g., `/opt/rocm`, or it may be versioned, e.g., `/opt/rocm-6.3.1`. In the examples
below, we'll assume the latter; adapt to whatever the situation is on your Linux machine.

1. **On the Linux machine**:
   ```bash
   sudo tar -cvpf - /opt/rocm-6.3.1 | xz -T0 -1 > /tmp/rocm_backup.tar.xz
   ```
   Note: the `-T0` argument to `xz` tells it to use all available CPU cores. The `-1` is the compression
   level, ranging from 0 (fast, lest compression) to 9 (slow, better compression). On my machine, `-1` took
   about 1 minute and resulted in a 2.2GB file, while `-9` took about 8 minutes and resulted in a 1.5GB file.
   Pick your poison.
2. **On the Mac**:
   ```bash
   # modify username and machine name as required in the next line
   scp user@linux-machine:/tmp/rocm_backup.tar.xz /tmp/
   sudo tar -xvpf /tmp/rocm_backup.tar.xz -C /
   ```
Now `/opt/rocm-6.3.1` is on macOS. The copy includes a whole bunch of files that are useless on macOS,
such as x86 executables; we'll prune that in a later iteration of these instructions.

---

## 4. Creating a Linux Sysroot

We also need a minimal sysroot with standard C++ libraries, etc. Here's an example for creating one on
**Ubuntu 22.04 (jammy)**. Adapt to taste for your Linux version.

### A. Generate the Sysroot on Ubuntu

```bash
sudo apt-get update
sudo apt-get install debootstrap
mkdir ~/sysroot_jammy

sudo debootstrap \
    --arch=amd64 \
    jammy \
    ~/sysroot_jammy \
    http://archive.ubuntu.com/ubuntu/

# Install dev packages in the chroot:
sudo chroot ~/sysroot_jammy
apt-get update
apt-get install -y build-essential linux-libc-dev libc6-dev
exit

cd ~
sudo tar -cvpf - sysroot_jammy | xz -T0 -1 > /tmp/sysroot_jammy.tar.xz
```
Note: see the comment about in Section 3.1 about the `xz` arguments.

### B. Transfer + Extract on macOS
```bash
# modify username and machine name as required in the next line
scp user@linux-machine:/tmp/sysroot_jammy.tar.xz /tmp/
sudo tar -xvpf /tmp/sysroot_jammy.tar.xz -C /opt
```
Now `/opt/sysroot_jammy` contains your minimal Linux development environment.

### C. Fix the Loader Symlink
Fix the symlink `/opt/sysroot_jammy/lib64/ld-linux-x86.so.2`:
```bash
cd /opt/sysroot_jammy/lib64
sudo rm ld-linux-x86-64.so.2 # points to /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2
# make it point to the appropriate path under our sysroot instead
sudo ln -s ../lib/x86_64-linux-gnu/ld-linux-x86-64.so.2 ld-linux-x86-64.so.2
```

(This applies to Ubuntu 22.04; it may or may not apply to other distros. Symlinks with absolute paths
are generally problematic, since we created them in a chroot environment, and paths starting with `/`
in the chroot environment end up in `/opt/sysroot_jammy` on macOS.)

---

## 5. Cross-Compiling the HIP Test Program

Assuming:
- **LLVM** in `/opt/llvm-amdgpu` (with AMDGPU + LLD)
- **ROCm** in `/opt/rocm-6.3.1`
- **Sysroot** in `/opt/sysroot_jammy`
- You have `hip_hello.cpp` from this repo.

Compile:
```bash
/opt/llvm-amdgpu/bin/clang++ \
  -target x86_64-linux-gnu \
  -fuse-ld=/opt/llvm-amdgpu/bin/ld.lld \
  --sysroot=/opt/sysroot_jammy \
  --gcc-toolchain=/opt/sysroot_jammy \
  -stdlib=libstdc++ \
  -x hip \
  --rocm-path=/opt/rocm-6.3.1 \
  -O2 \
  -mcpu=gfx906 \
  -isystem /opt/rocm-6.3.1/include \
  -L/opt/rocm-6.3.1/lib \
  -lamdhip64 \
  hip_hello.cpp -o hip_hello
```

### Explanation of some of the compiler flags
- **`-target x86_64-linux-gnu`**: Compiles for a 64-bit Linux platform.
- **`-fuse-ld=/opt/llvm-amdgpu/bin/ld.lld`**: Uses LLVM’s LLD linker, which understands Linux flags (instead of Apple’s `ld`).
- **`--sysroot=/opt/sysroot_jammy`**: Points Clang to the Ubuntu sysroot for headers/libs.
- **`--gcc-toolchain=/opt/sysroot_jammy`**: Uses the GCC libraries in the sysroot instead of macOS libs.

### Verify the ELF Output
```bash
file hip_hello
```
Should say something like:
```
hip_hello: ELF 64-bit LSB pie executable, x86-64, ...
```

### Transfer + Run on Linux
Copy `hip_hello` to the Linux machine:
```bash
./hip_hello
```
Each thread prints:
```
Hello from threadIdx.x = 0
Hello from threadIdx.x = 1
...
```

## Common Pitfalls
1. **Missing `<cmath>` or `<cstdlib>`**: Ensure `build-essential` & `libc6-dev` are installed in the sysroot.
2. **Undefined `__ockl_*`**: Omit `-nogpulib` so Clang includes device bitcode.
3. **Apple’s `ld`** complains about `--sysroot`: Use `-fuse-ld=/opt/llvm-amdgpu/bin/ld.lld`.
4. **`ld-linux-x86-64.so.2` not found**: Symlink it inside `/opt/sysroot_jammy/lib64`.


Future plans
Being able to compile simple HIP codes on macOS is a nice first step. It would be nice to also be able to compile more complex CMake-based HIP projects. Ideally, we'd just pass some extra flags to CMake to point it to our cross-compiler, and otherwise configure as usual. Initial attempts to use CMake failed, even for the simple test project covered above. `find_package(HIP REQUIRED)` fails; I haven't spent any serious time on fixing that yet. This would be the next thing on my list.

Copying the whole ROCm directory over from the Linux machine to our MacBook is a bit excessive. We don't need any ROCm exectutables, such as `roc-obj-ls`; these are x86 binaries that won't run on our MacBook. On the other hand, leaving out the whole `/opt/rocm/bin` directory may overdoing it; not everything in that directory is an executable; there are a bunch of scripts too, and we may need or want some of them. How much we can or should prune the rocm installation directory is still to be decided.
