#include <iostream>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <memory>
#include <array>

#ifdef __APPLE__
#  include <OpenCL/opencl.h>
#else
#  include <CL/cl.h>
#endif

#include <kotlib/macro.h>
#include <kotlib/OptionParser.hpp>
#include "oclErrorCode.h"


static constexpr cl_uint kNDefaultPlatformEntry = 16;
static constexpr cl_uint kNDefaultDeviceEntry = 16;
static const std::unordered_map<std::string, cl_int> kDeviceTypeMap{
  {"all", CL_DEVICE_TYPE_ALL},
  {"default", CL_DEVICE_TYPE_DEFAULT},
  {"cpu", CL_DEVICE_TYPE_CPU},
  {"gpu", CL_DEVICE_TYPE_GPU}
};


#define OCLC_CHECK_ERROR(errCode) \
  KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, std::string("[OpenCL] [") + std::to_string(errCode) + "] " + kErrorMessageMap.at(errCode))

#define OCLC_CHECK_ERROR_WITH_MSG(errCode, msg) \
  KOTLIB_THROW_IF(errCode != CL_SUCCESS, std::runtime_error, std::string("[OpenCL] [") + std::to_string(errCode) + "] " + kErrorMessageMap.at(errCode) + "\n" + msg)


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
  OCLC_CHECK_ERROR(errCode);
  platformIds.resize(nPlatform);
  return platformIds;
}


/*!
 * @brief  Get device IDs
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
  OCLC_CHECK_ERROR(errCode);
  deviceIds.resize(nDevice);
  return deviceIds;
}


/*!
 * @brief Read specified file as a text file and return std::string of the file
 * @param [in] filename  File name to read
 * @return  std::string which contains the specified file content
 */
static inline std::string
readSource(const std::string& filename)
{
  std::ifstream ifs(filename.c_str());
  KOTLIB_THROW_IF(!ifs.is_open(), std::runtime_error, "Failed to read file: " + filename);
  return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}


/*!
 * @brief Read specified files and return std::vector of std::string of the file
 * @param [in] filenames  Vector of file names to read
 * @return  std::vector which contains std::string of the files
 */
static inline std::vector<std::string>
readSource(const std::vector<std::string>& filenames)
{
  std::vector<std::string> srcs(filenames.size());
  for (decltype(srcs)::size_type i = 0; i < srcs.size(); i++) {
    srcs[i] = readSource(filenames[i]);
  }
  return srcs;
}


/*!
 * @brief Remove suffix of specified file name
 * @param [in] filename  FIle name which you want to remove suffix
 * @return  File name which removed suffix
 */
static inline std::string
removeSuffix(const std::string& filename) noexcept
{
  return filename.substr(0, filename.find_last_of("."));
}


/*!
 * @brief Show specified platform information
 * @param [in] platformId  Platform ID
 */
static inline void
showPlatformInfo(cl_platform_id platformId)
{
  std::array<char, 1024> info;
  std::size_t size;

  cl_int errCode = clGetPlatformInfo(platformId, CL_PLATFORM_NAME, info.size(), info.data(), &size);
  OCLC_CHECK_ERROR(errCode);
  std::cout << "  CL_PLATFORM_NAME: " << info.data() << std::endl;

  errCode = clGetPlatformInfo(platformId, CL_PLATFORM_VERSION, info.size(), info.data(), &size);
  OCLC_CHECK_ERROR(errCode);
  std::cout << "  CL_PLATFORM_VERSION: " << info.data() << std::endl;
}


/*!
 * @brief Show specified device information
 * @param [in] deviceId  Device ID
 */
static inline void
showDeviceInfo(cl_device_id deviceId)
{
  std::array<char, 1024> info;
  std::size_t size;

  cl_int errCode = clGetDeviceInfo(deviceId, CL_DEVICE_NAME, info.size(), info.data(), &size);
  OCLC_CHECK_ERROR(errCode);
  std::cout << "    CL_DEVICE_NAME: " << info.data() << std::endl;

  errCode = clGetDeviceInfo(deviceId, CL_DEVICE_VERSION, info.size(), info.data(), &size);
  OCLC_CHECK_ERROR(errCode);
  std::cout << "    CL_DEVICE_VERSION: " << info.data() << std::endl;
}


/*!
 * @brief Show all information of platform and device information
 * @param [in] platformIds  Platform IDs
 * @param [in] deviceType   Device type which you want to know the information
 */
static inline void
showInfo(const std::vector<cl_platform_id>& platformIds, cl_int deviceType = CL_DEVICE_TYPE_DEFAULT)
{
  static constexpr int MAX_DEVICE_IDS = 8;

  std::cout << "============================= Platform Information =============================";
  for (std::remove_reference<decltype(platformIds)>::type::size_type i = 0; i < platformIds.size(); i++) {
    std::cout << "\nPlatform: " << i << std::endl;
    showPlatformInfo(platformIds[i]);

    std::vector<cl_device_id> deviceIds = getDeviceIds(platformIds[i], MAX_DEVICE_IDS, deviceType);
    for (decltype(deviceIds)::size_type j = 0; j < deviceIds.size(); j++) {
      std::cout << "  Device: " << j << std::endl;
      showDeviceInfo(deviceIds[j]);
    }
  }
  std::cout << "================================================================================" << std::endl;
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
  try {
    kot::OptionParser op(argv[0]);
    op.setOption("all", 'a', kot::OptionParser::NO_ARGUMENT, false, "Compile kernel program for all detected devices");
    op.setOption("list", 'l', kot::OptionParser::NO_ARGUMENT, false, "List up all platforms and devices");
    op.setOption("device-type", 't', kot::OptionParser::REQUIRED_ARGUMENT, "default",
        "Specify device type\n"
        "      all: CPU and GPU\n"
        "      cpu: CPU only\n"
        "      gpu: GPU only", "DEVICE_TYPE");
    op.setOption("output", 'o', kot::OptionParser::REQUIRED_ARGUMENT, "", "Specify output file name", "FILE_NAME");
    op.setOption("option", 'O', kot::OptionParser::REQUIRED_ARGUMENT, "", "Specify compile option", "COMPILE_OPTION");
    op.setOption("platform", 'p', kot::OptionParser::REQUIRED_ARGUMENT, 0, "Specify platform index", "PLATFORM_INDEX");
    op.setOption("device", 'd', kot::OptionParser::REQUIRED_ARGUMENT, 0, "Specify device index", "DEVICE_INDEX");
    op.setOption("fsyntax-only", kot::OptionParser::NO_ARGUMENT, false, "Check syntax only, not generate binary");
    op.setOption("help", 'h', kot::OptionParser::NO_ARGUMENT, false, "Show help and exit this program");
    op.parse(argc, argv);

    // Show help and exit this program
    if (op.get<bool>("help")) {
      op.showUsage();
      return EXIT_SUCCESS;
    }

    // Get platform information
    std::vector<cl_platform_id> platformIds = getPlatformIds(kNDefaultPlatformEntry);
    if (op.get<bool>("list")) {
      showInfo(platformIds, kDeviceTypeMap.at(op.get("device-type")));
      return EXIT_SUCCESS;
    }

    // Get source file
    std::vector<std::string> args = op.getArguments();
    if (args.size() < 1) {
      std::cerr << "Please specify only one or more source file" << std::endl;
      return EXIT_FAILURE;
    }

    std::size_t pi = op.get<std::size_t>("platform");
    std::size_t di = op.get<std::size_t>("device");

    // Get device information
    std::vector<cl_device_id> deviceIds = getDeviceIds(platformIds[pi], kNDefaultDeviceEntry, kDeviceTypeMap.at(op.get("device-type")));

    // Generate context
    cl_int errCode;
    std::unique_ptr<std::remove_pointer<cl_context>::type, decltype(&clReleaseContext)> context(
        clCreateContext(nullptr, static_cast<cl_uint>(deviceIds.size()), &deviceIds[di], nullptr, nullptr, &errCode), clReleaseContext);
    OCLC_CHECK_ERROR(errCode);

    std::vector<std::string> kernelSources = readSource(op.getArguments());
    std::pair<std::vector<const char*>, std::vector<std::string::size_type> > kernelSourcePairs;
    kernelSourcePairs.first.reserve(kernelSources.size());
    kernelSourcePairs.second.reserve(kernelSources.size());
    for (const auto& kernelSource : kernelSources) {
      kernelSourcePairs.first.emplace_back(kernelSource.c_str());
      kernelSourcePairs.second.emplace_back(kernelSource.length());
    }
    std::unique_ptr<std::remove_pointer<cl_program>::type, decltype(&clReleaseProgram)> program(
        clCreateProgramWithSource(
          context.get(),
          static_cast<cl_uint>(kernelSourcePairs.first.size()),
          kernelSourcePairs.first.data(),
          kernelSourcePairs.second.data(),
          &errCode),
        clReleaseProgram);
    OCLC_CHECK_ERROR(errCode);

    // Compile kernel source code
    errCode = clBuildProgram(program.get(), static_cast<cl_uint>(deviceIds.size()), &deviceIds[di], op.get("option").c_str(), nullptr, nullptr);
    switch (errCode) {
      case CL_SUCCESS:
        break;
      case CL_BUILD_PROGRAM_FAILURE:
        {
          std::array<char, 2048> buildLog;
          std::size_t logSize;
          clGetProgramBuildInfo(program.get(), deviceIds[di], CL_PROGRAM_BUILD_LOG, buildLog.size(), buildLog.data(), &logSize);
          OCLC_CHECK_ERROR_WITH_MSG(errCode, buildLog.data());
        }
        break;
      case CL_INVALID_BUILD_OPTIONS:
        OCLC_CHECK_ERROR(errCode);
        break;
      default:
        OCLC_CHECK_ERROR(errCode);
    }
    if (op.get<bool>("fsyntax-only")) {
      return EXIT_SUCCESS;
    }

    // figure out number of devices and the sizes of the binary for each device.
    cl_uint nDevice;
    errCode = clGetProgramInfo(program.get(), CL_PROGRAM_NUM_DEVICES, sizeof(nDevice), &nDevice, nullptr);
    OCLC_CHECK_ERROR(errCode);

    std::unique_ptr<std::size_t[]> binSizes(new std::size_t[nDevice]);
    errCode = clGetProgramInfo(program.get(), CL_PROGRAM_BINARY_SIZES, sizeof(std::size_t) * nDevice, binSizes.get(), nullptr);
    OCLC_CHECK_ERROR(errCode);

    // copy over all of the generated bins.
    std::vector<std::unique_ptr<char> > bins(nDevice);
    for (std::size_t i = 0; i < nDevice; i++) {
      bins[i] = std::unique_ptr<char>(binSizes[i] == 0 ? nullptr : new char[binSizes[i]]);
    }
    errCode = clGetProgramInfo(program.get(), CL_PROGRAM_BINARIES, sizeof(char*) * nDevice, bins.data(), nullptr);
    OCLC_CHECK_ERROR(errCode);

    std::string basename = removeSuffix(args[0]);
    for (std::size_t i = 0; i < nDevice; i++) {
      if (bins[i] == nullptr) {
        continue;
      }
      std::string filename = op.get("output") == "" ? (basename + ".bin") : op.get("output");
      if (nDevice > 1) {
        filename += "." + std::to_string(i);
      }
      std::ofstream ofs(filename, std::ios::binary);
      if (ofs.is_open()) {
        ofs.write(bins[i].get(), binSizes[i]);
      } else {
        std::cerr << "Failed to open: " << filename << std::endl;
      }
    }
  } catch (const std::exception& e) {
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
