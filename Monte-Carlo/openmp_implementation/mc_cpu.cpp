#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include <float.h>
#include <sys/timeb.h>
#include <time.h>
#include <omp.h>
#include <string.h>
#include "parameters.h"
#include "logger.h"

#define NUM_THREADS 8
#define COULOMB "--coulomb"

struct dim {
    double x;
    double y;
    double z;
};
typedef struct dim dim;

void nearest_image(dim *position_arr, dim *nearest);
void init_problem(dim *position_arr, int *charge);
void mc_method(dim *position_arr, dim *nearest, int *charge);
double calculate_energy_lj(dim *position_arr, dim *nearest, int *charge);
double calculate_energy_coulomb(dim *position_arr, dim *nearest, int *charge);

double max_deviation = 0.007;
double (*calculate_energy)(dim*, dim*, int*);
double final_energy = 0;

int main(int argc, char *argv[])
{
    calculate_energy = calculate_energy_lj;
    if (argc > 1){
        if (!strcmp(argv[1], COULOMB)){
            calculate_energy = calculate_energy_coulomb;
        }
    }
    struct timeb start_total_time;
    ftime(&start_total_time);
    time_t t;
    srand((unsigned)time(&t));
    dim *position_arr = (dim*)malloc(sizeof(dim) * particles_count);
    dim *nearest = (dim*)malloc(sizeof(dim) * particles_count);
    int *charge = (int*)malloc(sizeof(int) * particles_count);

    init_problem(position_arr, charge);
    mc_method(position_arr, nearest, charge);

    free(position_arr);
    free(nearest);
    free(charge);
    struct timeb end_total_time;
    ftime(&end_total_time);
    printf("Total execution time in ms =  %d", (int)((end_total_time.time - start_total_time.time) * 1000 + end_total_time.millitm - start_total_time.millitm));
    LOG_PRINT("Total execution time in ms =  %d", (int)((end_total_time.time - start_total_time.time) * 1000 + end_total_time.millitm - start_total_time.millitm));
    LOG_PRINT("Energy is %f", final_energy);
    return 0;
}

/////// HELPER FUNCTIONS ///////

void init_problem(dim *position_arr, int *charge) {
    int count = 0;
    for (double i = -(box_size - initial_dist_to_edge)/2; i < (box_size - initial_dist_to_edge)/2; i += initial_dist_by_one_axis) {
        for (double j = -(box_size - initial_dist_to_edge)/2; j < (box_size - initial_dist_to_edge)/2; j += initial_dist_by_one_axis) {
            for (double l = -(box_size - initial_dist_to_edge)/2; l < (box_size - initial_dist_to_edge)/2; l += initial_dist_by_one_axis) {
                if( count == particles_count){
                    return;
                }
                position_arr[count] = { i,j,l };
                if (calculate_energy == calculate_energy_coulomb){
                    if (count & 1)
                        charge[count] = 1;
                    else
                        charge[count] = -1;
                }
                count++;
            }
        }
    }
    if( count < particles_count ){
        printf("error decrease initial_dist parameter, count is %ld  particles_count is %ld \n", count, particles_count);
        exit(1);
    }
}

void nearest_image(dim *position_arr, dim *nearest){
    for (int i = 0; i < particles_count; i++){
        float x,y,z;
        if (position_arr[i].x  > 0){
            x = fmod(position_arr[i].x + half_box, box_size) - half_box;
        }
        else{
            x = fmod(position_arr[i].x - half_box, box_size) + half_box;
        }
        if (position_arr[i].y  > 0){
            y = fmod(position_arr[i].y + half_box, box_size) - half_box;
        }
        else{
            y = fmod(position_arr[i].y - half_box, box_size) + half_box;
        }
        if (position_arr[i].z  > 0){
            z = fmod(position_arr[i].z + half_box, box_size) - half_box;
        }
        else{
            z = fmod(position_arr[i].z - half_box, box_size) + half_box;
        }
        nearest[i] = (dim){ x, y, z};
    }
}

double calculate_energy_lj(dim *position_arr, dim *nearest, int *charge){
    nearest_image(position_arr, nearest);
    double energy = 0;
    #pragma omp parallel for reduction(+:energy) num_threads(NUM_THREADS)
    for (int i = 0; i < particles_count; i++) {
        for (int j = 0; j < i; j++) {
            float x = nearest[j].x - nearest[i].x;
            float y = nearest[j].y - nearest[i].y;
            float z = nearest[j].z - nearest[i].z;
            if (x > half_box)
                x -= box_size;
            else {
                if (x < -half_box)
                    x += box_size;
            }
            if (y > half_box)
                y -= box_size;
            else {
                if (y < -half_box)
                    y += box_size;
            }
            if (z > half_box)
                z -= box_size;
            else {
                if (z < -half_box)
                    z += box_size;
            }
            float sq_dist = x * x + y * y + z * z;
            if (sq_dist < rc * rc) {
                double r6 = sq_dist * sq_dist * sq_dist;
                double r12 = r6 * r6;
                energy += 4 * (1 / r12 - 1 / r6);
            }
        }
    }
    return energy;
}

double calculate_energy_coulomb(dim *position_arr, dim *nearest, int *charge){
    nearest_image(position_arr, nearest);
    double energy = 0;
    #pragma omp parallel for reduction(+:energy) num_threads(NUM_THREADS)
    for (int i = 0; i < particles_count; i++) {
        for (int j = 0; j < i; j++) {
            float x = nearest[j].x - nearest[i].x;
            float y = nearest[j].y - nearest[i].y;
            float z = nearest[j].z - nearest[i].z;
            if (x > half_box)
                x -= box_size;
            else {
                if (x < -half_box)
                    x += box_size;
            }
            if (y > half_box)
                y -= box_size;
            else {
                if (y < -half_box)
                    y += box_size;
            }
            if (z > half_box)
                z -= box_size;
            else {
                if (z < -half_box)
                    z += box_size;
            }
            energy += charge[i] * charge[j] / sqrt(x * x + y * y + z * z);
        }
    }
    return energy;
}

void mc_method(dim *position_arr, dim *nearest, int *charge) {
    double *energy_ar = (double*)malloc(sizeof(double) * nmax);
    register int i = 0;
    register int good_iter = 0;
    int good_iter_hung = 0;
    double u1 = calculate_energy(position_arr, nearest, charge);
    while (1) {
        if ((good_iter == nmax) || (i == total_it)) {
            final_energy = energy_ar[good_iter-1] / particles_count;
            printf("energy is %f \ngood iters percent %f \n", energy_ar[good_iter-1]/particles_count, (float)good_iter/(float)i);
            break;
        }
        dim *tmp = (dim*)malloc(sizeof(dim)*particles_count);
        memcpy(tmp, position_arr, sizeof(dim)*particles_count);
        for (int particle = 0; particle < particles_count; particle++) {
            //ofsset between -max_deviation/2 and max_deviation/2
            double ex = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            double ey = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            double ez = (double)rand() / (double)RAND_MAX * max_deviation - max_deviation / 2;
            tmp[particle].x = tmp[particle].x + ex;
            tmp[particle].y = tmp[particle].y + ex;
            tmp[particle].z = tmp[particle].z + ex;
        }
        double u2 = calculate_energy(tmp, nearest, charge);
        double deltaU_div_T = (u1 - u2) / Temperature;
        double probability = exp(deltaU_div_T);
        double rand_0_1 = (double)rand() / (double)RAND_MAX;
        if ((u2 < u1) || (probability <= rand_0_1)) {
            u1 = u2;
            memcpy(position_arr, tmp, sizeof(dim)*particles_count);
            energy_ar[good_iter] = u2;
            good_iter++;
            good_iter_hung++;
        }
        i++;
        free(tmp);
    }
}
