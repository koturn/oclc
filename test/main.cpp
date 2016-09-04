#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#ifdef __APPLE__
#  include <OpenCL/opencl.h>
#else
#  include <CL/cl.h>
#endif

#include <kotlib/macro.h>
#include <kotlib/OptionParser.hpp>


static constexpr cl_uint kNDefaultPlatformEntry = 16;
static constexpr cl_uint kNDefaultDeviceEntry = 16;


/*!
 * @brief Allocate aligned memory
 * @param [in] size       Memory size
 * @param [in] alignment  Alignment (Must be power of 2)
 * @return  Allocated aligned memory
 */
template<typename T=void*, typename std::enable_if<std::is_pointer<T>::value, std::nullptr_t>::type=nullptr>
static inline T
alignedMalloc(std::size_t size, std::size_t alignment) noexcept
{
#if defined(_MSC_VER) || defined(__MINGW32__)
  return reinterpret_cast<T>(_aligned_malloc(size, alignment));
#else
  void* p;
  return reinterpret_cast<T>(posix_memalign(&p, alignment, size) == 0 ? p : nullptr);
#endif  // defined(_MSC_VER) || defined(__MINGW32__)
}


/*!
 * @brief Free aligned memory
 * @param [in] ptr  Aligned memory
 */
static inline void
alignedFree(void* ptr) noexcept
{
#if defined(_MSC_VER) || defined(__MINGW32__)
  _aligned_free(ptr);
#else
  std::free(ptr);
#endif  // defined(_MSC_VER) || defined(__MINGW32__)
}


/*!
 * @brief Custom deleter for std::unique_ptr and std::shared_ptr which holds
 *        aligned memory
 */
struct AlignedDeleter
{
  /*!
   * @brief operator() for delete processs
   * @param [in,out] p  Pointer to aligned memory
   */
  void
  operator()(void* p) const noexcept
  {
    alignedFree(p);
  }
};


/*!
 * @brief Get platform IDs
 * @param [in] nPlatformEntry  Max number of platform IDs to get
 * @return  std::vector which contains obtained platform IDs
 */
static inline std::vector<cl_platform_id>
getPlatformIds(cl_uint nPlatformEntry = kNDefaultPlatformEntry)
{
  std::vector<cl_platform_id> platformIds(nPlatformEntry);
  cl_uint nPlatform;
  cl_int errCode = clGetPlatformIDs(nPlatformEntry, platformIds.data(), &nPlatform);
  KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clGetPlatformIDs() failed");
  platformIds.resize(nPlatform);
  return platformIds;
}


/*!
 * @brief Get device IDs
 * @param [in] platformId    One platform ID
 * @param [in] nDeviceEntry  Max number of device IDs to get
 * @param [in] deviceType    Device type which means CPU, GPU, or both.
 * @return  std::vector which contains obtained device IDs
 */
static inline std::vector<cl_device_id>
getDeviceIds(const cl_platform_id& platformId, cl_uint nDeviceEntry = kNDefaultDeviceEntry, cl_int deviceType = CL_DEVICE_TYPE_DEFAULT)
{
  std::vector<cl_device_id> deviceIds(nDeviceEntry);
  cl_uint nDevice;
  cl_int errCode = clGetDeviceIDs(platformId, deviceType, nDeviceEntry, deviceIds.data(), &nDevice);
  KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clGetDeviceIDs() failed");
  deviceIds.resize(nDevice);
  return deviceIds;
}



/*!
 * @brief Implementation of setting arguments to kernel function
 * @param [in] kernel  Kernel object of OpenCL
 * @param [in] idx     Argument index
 * @param [in] first   One argument
 * @param [in] rest    Rest arguments
 * @return  Error code of OpenCL
 */
template<typename First, typename... Rest>
static inline cl_uint
setKernelArgsImpl(const cl_kernel& kernel, int idx, const First& first, const Rest&... rest) noexcept
{
  cl_uint errCode = clSetKernelArg(kernel, idx, sizeof(first), &first);
  return errCode == CL_SUCCESS ? setKernelArgsImpl(kernel, idx + 1, rest...) : errCode;
}


/*!
 * @brief Set the last argument to the kernel function
 * @param [in] kernel  Kernel object of OpenCL
 * @param [in] idx     Argument index
 * @param [in] last    THe last argument
 * @return  Error code of OpenCL
 */
template<typename Last>
static inline cl_uint
setKernelArgsImpl(const cl_kernel& kernel, int idx, const Last& last) noexcept
{
  return clSetKernelArg(kernel, idx, sizeof(last), &last);
}


/*!
 * @brief Interface of setting arguments to kernel function
 * @param [in] kernel  Kernel object of OpenCL
 * @param [in] args    All arguments for kernel function
 * @return  Error code of OpenCL
 */
template<typename... Args>
static inline cl_uint
setKernelArgs(const cl_kernel& kernel, const Args&... args) noexcept
{
  return setKernelArgsImpl(kernel, 0, args...);
}




/*!
 * @brief The entry point of this program
 * @param [in] argc  Number of command-line arguments
 * @param [in] argv  Command-line arguments
 * @return Exit-status
 */
int
main(int argc, char* argv[])
{
  static constexpr int ALIGN = 4096;
  static constexpr std::size_t N = 65536;

  if (argc < 2) {
    std::cerr << "Please specify only one kernel binary file" << std::endl;
    return EXIT_FAILURE;
  }

  std::unique_ptr<float[], AlignedDeleter> hostX(alignedMalloc<float*>(N * sizeof(float), ALIGN));
  std::unique_ptr<float[], AlignedDeleter> hostY(alignedMalloc<float*>(N * sizeof(float), ALIGN));
  std::unique_ptr<float[], AlignedDeleter> hostZ(alignedMalloc<float*>(N * sizeof(float), ALIGN));

  std::mt19937 mt((std::random_device())());
  for (std::size_t i = 0; i < N; i++) {
    hostX[i] = static_cast<float>(mt());
    hostY[i] = static_cast<float>(mt());
  }
  std::fill_n(hostZ.get(), N, 0.0f);

  try {
    std::vector<cl_platform_id> platformIds = getPlatformIds(1);
    std::vector<cl_device_id> deviceIds = getDeviceIds(platformIds[0], 1, CL_DEVICE_TYPE_DEFAULT);

    cl_int errCode;
    std::unique_ptr<std::remove_pointer<cl_context>::type, decltype(&clReleaseContext)> context(
        clCreateContext(nullptr, 1, &deviceIds[0], nullptr, nullptr, &errCode), clReleaseContext);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clCreateContext() failed");
    std::unique_ptr<std::remove_pointer<cl_command_queue>::type, decltype(&clReleaseCommandQueue)> cmdQueue(
        clCreateCommandQueue(context.get(), deviceIds[0], 0, &errCode), clReleaseCommandQueue);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clCreateCommandQueue() failed");

    std::unique_ptr<std::remove_pointer<cl_mem>::type, decltype(&clReleaseMemObject)> deviceX(
        clCreateBuffer(context.get(), CL_MEM_READ_WRITE, N * sizeof(float), nullptr, &errCode), clReleaseMemObject);
    std::unique_ptr<std::remove_pointer<cl_mem>::type, decltype(&clReleaseMemObject)> deviceY(
        clCreateBuffer(context.get(), CL_MEM_READ_WRITE, N * sizeof(float), nullptr, &errCode), clReleaseMemObject);
    std::unique_ptr<std::remove_pointer<cl_mem>::type, decltype(&clReleaseMemObject)> deviceZ(
        clCreateBuffer(context.get(), CL_MEM_READ_WRITE, N * sizeof(float), nullptr, &errCode), clReleaseMemObject);

    errCode = clEnqueueWriteBuffer(cmdQueue.get(), deviceX.get(), CL_TRUE, 0, N * sizeof(float), hostX.get(), 0, nullptr, nullptr);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clEnqueueWriteBuffer() failed");
    errCode = clEnqueueWriteBuffer(cmdQueue.get(), deviceY.get(), CL_TRUE, 0, N * sizeof(float), hostY.get(), 0, nullptr, nullptr);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clEnqueueWriteBuffer() failed");
    errCode = clEnqueueWriteBuffer(cmdQueue.get(), deviceZ.get(), CL_TRUE, 0, N * sizeof(float), hostZ.get(), 0, nullptr, nullptr);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clEnqueueWriteBuffer() failed");

    // Read kernel binary
    std::ifstream ifs(argv[1], std::ios::binary);
    if (!ifs.is_open()) {
      std::cerr << "Failed to kernel binary: " << argv[1] << std::endl;
      std::exit(EXIT_FAILURE);
    }
    std::string kernelBin((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    const unsigned char* kbin = reinterpret_cast<const unsigned char*>(kernelBin.c_str());
    std::size_t kbinSize = kernelBin.size();
    cl_int binStatus;
    std::unique_ptr<std::remove_pointer<cl_program>::type, decltype(&clReleaseProgram)> program(
        clCreateProgramWithBinary(context.get(), 1, &deviceIds[0], &kbinSize, &kbin, &binStatus, &errCode), clReleaseProgram);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clCreateProgramWithBinary() failed");

    // errCode = clBuildProgram(program.get(), 1, &deviceIds[0], nullptr, nullptr, nullptr);

    std::unique_ptr<std::remove_pointer<cl_kernel>::type, decltype(&clReleaseKernel)> kernel(
        clCreateKernel(program.get(), "vecAdd", &errCode), clReleaseKernel);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clCreateKernel() failed");

    errCode = setKernelArgs(kernel.get(), deviceZ.get(), deviceX.get(), deviceY.get(), static_cast<int>(N));
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "setKernelArgs() failed");
    errCode = clEnqueueTask(cmdQueue.get(), kernel.get(), 0, nullptr, nullptr);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clEnqueueTask() failed");

    errCode = clFlush(cmdQueue.get());
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clFlush() failed");
    errCode = clFinish(cmdQueue.get());
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clFinish() failed");

    errCode = clEnqueueReadBuffer(cmdQueue.get(), deviceZ.get(), CL_TRUE, 0, N * sizeof(float), hostZ.get(), 0, nullptr, nullptr);
    KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, "clEnqueueReadBuffer() failed");
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  for (std::size_t i = 0; i < N; i++) {
    if (std::abs(hostX[i] + hostY[i] - hostZ[i]) > 1.0e-5) {
      std::cerr << "Result verification failed at element " << i << "!" << std::endl;
      return EXIT_FAILURE;
    }
  }
  std::cout << "Test PASSED" << std::endl;

  return EXIT_SUCCESS;
}

