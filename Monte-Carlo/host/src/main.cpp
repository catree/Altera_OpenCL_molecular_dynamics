#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "CL/opencl.h"
#include <sys/timeb.h>
#include "parameters.h"
#ifdef ALTERA
    #include "AOCL_Utils.h"
    using namespace aocl_utils;
#endif
#ifdef NVIDIA
    #define VENDOR "NVIDIA Corporation"
    #include "gpu.h"
#endif
#ifdef IOCL
    #define VENDOR "Intel(R) Corporation"
    #include "gpu.h"
#endif

cl_platform_id platform = NULL;
cl_device_id device;
cl_context context = NULL;
cl_command_queue queue;
cl_program program = NULL;
cl_kernel kernel;
cl_mem nearest_buf;
cl_mem output_buf;

// Problem data(positions and energy)
cl_float3 input_a[size] = {};
cl_float3 nearest[size] = {};
float output[size] = {};
float max_deviation = 0.005;
double kernel_total_time = 0.;

// Function prototypes
bool init_opencl();
void init_problem();
void run();
void cleanup();
void mc();
void nearest_image();
float calculate_energy_lj();

// Entry point.
int main() {
    struct timeb start_total_time;
    ftime(&start_total_time);
    // Initialize OpenCL.
    if(!init_opencl()) {
      return -1;
    }
    // Initialize the problem data.
    init_problem();
    mc();
    // Free the resources allocated
    cleanup();
    struct timeb end_total_time;
    ftime(&end_total_time);
    printf("\nTotal execution time in ms =  %d\n", (int)((end_total_time.time - start_total_time.time) * 1000 + end_total_time.millitm - start_total_time.millitm));
    printf("\nKernel execution time in milliseconds = %0.3f ms\n", (kernel_total_time / 1000000.0) );
    return 0;
}

/////// HELPER FUNCTIONS ///////

// Initializes the OpenCL objects.
bool init_opencl() {
    cl_int status;

    printf("Initializing OpenCL\n");
    #ifdef ALTERA
        if(!setCwdToExeDir()) {
          return false;
        }
        platform = findPlatform("Altera");
    #else
        cl_uint num_platforms;
        cl_platform_id pls[MAX_PLATFORMS_COUNT];
        clGetPlatformIDs(MAX_PLATFORMS_COUNT, pls, &num_platforms);
        char vendor[128];
        for (int i = 0; i < MAX_PLATFORMS_COUNT; i++){
            clGetPlatformInfo (pls[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, NULL);
            if (!strcmp(VENDOR, vendor))
            {
                platform = pls[i];
                break;
            }
        }
    #endif
    if(platform == NULL) {
      printf("ERROR: Unable to find OpenCL platform.\n");
      return false;
    }

    #ifdef ALTERA
        scoped_array<cl_device_id> devices;
        cl_uint num_devices;
        devices.reset(getDevices(platform, CL_DEVICE_TYPE_ALL, &num_devices));
        // We'll just use the first device.
        device = devices[0];
    #else
        cl_uint num_devices;
        clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU , 1, &device, &num_devices);
    #endif

    context = clCreateContext(NULL, 1, &device, NULL, NULL, &status);
    checkError(status, "Failed to create context");

    queue = clCreateCommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &status);
    checkError(status, "Failed to create command queue");

    #ifdef ALTERA
        std::string binary_file = getBoardBinaryFile("mc", device);
        printf("Using AOCX: %s\n", binary_file.c_str());
        program = createProgramFromBinary(context, binary_file.c_str(), &device, 1);
    #else
        int MAX_SOURCE_SIZE  = 65536;
        FILE *fp;
        const char fileName[] = "./device/mc.cl";
        size_t source_size;
        char *source_str;
        try {
            fp = fopen(fileName, "r");
            if (!fp) {
                fprintf(stderr, "Failed to load kernel.\n");
                exit(1);
            }
            source_str = (char *)malloc(MAX_SOURCE_SIZE);
            source_size = fread(source_str, 1, MAX_SOURCE_SIZE, fp);
            fclose(fp);
        }
        catch (int a) {
            printf("%f", a);
        }
        program = clCreateProgramWithSource(context, 1, (const char **)&source_str, (const size_t *)&source_size, &status);
    #endif

    // Build the program that was just created.
    status = clBuildProgram(program, 0, NULL, "", NULL, NULL);
    checkError(status, "Failed to build program");

    const char *kernel_name = "mc";
    kernel = clCreateKernel(program, kernel_name, &status);
    checkError(status, "Failed to create kernel");

    // Input buffer.
    nearest_buf = clCreateBuffer(context, CL_MEM_READ_ONLY,
        size * sizeof(cl_float3), NULL, &status);
    checkError(status, "Failed to create buffer for input A");

    // Output buffer.
    output_buf = clCreateBuffer(context, CL_MEM_WRITE_ONLY,
        size * sizeof(float), NULL, &status);
    checkError(status, "Failed to create buffer for output");

    return true;
}

void init_problem() {
    int count = 0;
    for (double i = -(box_size - initial_dist_to_edge)/2; i < (box_size - initial_dist_to_edge)/2; i += initial_dist_by_one_axis) {
        for (double j = -(box_size - initial_dist_to_edge)/2; j < (box_size - initial_dist_to_edge)/2; j += initial_dist_by_one_axis) {
            for (double l = -(box_size - initial_dist_to_edge)/2; l < (box_size - initial_dist_to_edge)/2; l += initial_dist_by_one_axis) {
                if( count == size){
                    return; //it is not balanced grid but we can use it
                }
                input_a[count] = (cl_float3){ i, j, l };
                count++;
            }
        }
    }
    if( count < size ){
        printf("error decrease initial_dist parameter, count is %ld  size is %ld \n", count, size);
        exit(1);
    }
}

float calculate_energy_lj() {
    nearest_image();
    memset(output, 0, sizeof(output));
    run();
    float total_energy = 0;
    for (unsigned i = 0; i < size; i++)
        total_energy+=output[i];
    total_energy/=2;
    return total_energy;
}

void mc() {
    int i = 0;
    int good_iter = 0;
    int good_iter_hung = 0;
    float energy_ar[nmax] = {};
    float u1 = calculate_energy_lj();
    printf("energy is %f\n", u1/size);
    while (1) {
        if ((good_iter == nmax) || (i == total_it)) {
            printf("\nenergy is %f \ngood iters percent %f \n", energy_ar[good_iter-1]/size, (float)good_iter/(float)total_it);
            printf("\nKernel execution time in milliseconds per iters = %0.3f ms\n", (kernel_total_time / (1000000.0 * i)) );
            break;
        }
        cl_float3 tmp[size];
        memcpy(tmp, input_a, sizeof(cl_float3)*size);
        for (int particle = 0; particle < size; particle++) {
            //ofsset between -max_deviation/2 and max_deviation/2
            double ex = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            double ey = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            double ez = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            input_a[particle].x = input_a[particle].x + ex;
            input_a[particle].y = input_a[particle].y + ex;
            input_a[particle].z = input_a[particle].z + ex;
        }
        double u2 = calculate_energy_lj();
        double deltaU_div_T = (u1 - u2) / Temperature;
        double probability = exp(deltaU_div_T);
        double rand_0_1 = (double)rand() / (double)RAND_MAX;
        if ((u2 < u1) || (probability <= rand_0_1)) {
            u1 = u2;
            energy_ar[good_iter] = u2;
            good_iter++;
            good_iter_hung++;
        }
        else {
            memcpy(input_a, tmp, sizeof(cl_float3) * size);
        }
        i++;
    }
}
void run() {
    cl_int status;
    cl_event kernel_event;
    cl_event finish_event;
    cl_ulong time_start, time_end;
    double total_time;

    cl_event write_event;
    status = clEnqueueWriteBuffer(queue, nearest_buf, CL_FALSE,
        0, size * sizeof(cl_float3), nearest, 0, NULL, &write_event);
    checkError(status, "Failed to transfer input A");

    unsigned argi = 0;

    size_t global_work_size[1] = {size};
    size_t local_work_size[1] = {size};
    status = clSetKernelArg(kernel, argi++, sizeof(cl_mem), &nearest_buf);
    checkError(status, "Failed to set argument nearest");

    status = clSetKernelArg(kernel, argi++, sizeof(cl_mem), &output_buf);
    checkError(status, "Failed to set argument output");

    status = clEnqueueNDRangeKernel(queue, kernel, 1, NULL,
        global_work_size, local_work_size, 1, &write_event, &kernel_event);
    checkError(status, "Failed to launch kernel");

    status = clEnqueueReadBuffer(queue, output_buf, CL_FALSE,
        0, size * sizeof(float), output, 1, &kernel_event, &finish_event);

    // Release local events.
    clReleaseEvent(write_event);

    // Wait for all devices to finish.
    clWaitForEvents(1, &finish_event);

    clGetEventProfilingInfo(kernel_event, CL_PROFILING_COMMAND_START, sizeof(time_start), &time_start, NULL);
    clGetEventProfilingInfo(kernel_event, CL_PROFILING_COMMAND_END, sizeof(time_end), &time_end, NULL);
    total_time = time_end - time_start;
    kernel_total_time += total_time;

    // Release all events.
    clReleaseEvent(kernel_event);
    clReleaseEvent(finish_event);
}

void nearest_image(){
    for (int i = 0; i < size; i++){
        float x,y,z;
        if (input_a[i].x  > 0){
            x = fmod(input_a[i].x + half_box, box_size) - half_box;
        }
        else{
            x = fmod(input_a[i].x - half_box, box_size) + half_box;
        }
        if (input_a[i].y  > 0){
            y = fmod(input_a[i].y + half_box, box_size) - half_box;
        }
        else{
            y = fmod(input_a[i].y - half_box, box_size) + half_box;
        }
        if (input_a[i].z  > 0){
            z = fmod(input_a[i].z + half_box, box_size) - half_box;
        }
        else{
            z = fmod(input_a[i].z - half_box, box_size) + half_box;
        }
        nearest[i] = (cl_float3){ x, y, z};
    }
}
// Free the resources allocated during initialization
void cleanup() {
    if(kernel) {
      clReleaseKernel(kernel);
    }
    if(queue) {
      clReleaseCommandQueue(queue);
    }
    if(nearest_buf) {
      clReleaseMemObject(nearest_buf);
    }
    if(output_buf) {
      clReleaseMemObject(output_buf);
    }
    if(program) {
    clReleaseProgram(program);
    }
    if(context) {
    clReleaseContext(context);
    }
}
