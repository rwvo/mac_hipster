#include <hip/hip_runtime.h>
#include <iostream>

__global__ void hello_kernel() {
    printf("Hello from threadIdx.x = %d\n", threadIdx.x);
}

int main() {
    constexpr int threadsPerBlock = 8;
    constexpr int blocksPerGrid = 1;
    
    hello_kernel<<<blocksPerGrid, threadsPerBlock>>>();
    
    hipError_t err = hipDeviceSynchronize();
    if (err != hipSuccess) {
        std::cerr << "HIP error: " << hipGetErrorString(err) << std::endl;
        return 1;
    }
    
    return 0;
}
