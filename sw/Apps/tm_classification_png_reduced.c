/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * License); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * AS IS BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*
 * Copyright (c) 2020, OPEN AI LAB
 * Author: qtang@openailab.com
 */

/*
 * 2024 Modified by Filip Masar
 * Run classification on reduced part (100 per class) of CIFAR-10 dataset
 * CPU Float
 */

#include <stdlib.h>
#include <stdio.h>

#include "common.h"
#include "tengine/c_api.h"
#include "tengine_operations.h"

#define DEFAULT_IMG_H        224
#define DEFAULT_IMG_W        224
#define DEFAULT_SCALE1       0.017f
#define DEFAULT_SCALE2       0.017f
#define DEFAULT_SCALE3       0.017f
#define DEFAULT_MEAN1        104.007
#define DEFAULT_MEAN2        116.669
#define DEFAULT_MEAN3        122.679
#define DEFAULT_LOOP_COUNT   1
#define DEFAULT_THREAD_COUNT 1
#define DEFAULT_CPU_AFFINITY 255

#define NUMBER_OF_CLASSES 10
#define NUMBER_OF_SAMPLES 100

int get_top_result(float* data, int total_num)
{
    float max = FLT_MIN;
    int max_id = -1;
    for (int i = 0; i < total_num; i++)
    {
        if (data[i] > max)
        {
            max = data[i];
            max_id = i;
        }
    }

    return max_id;
}

int tengine_classify(const char* model_file, const char* collection_path, int img_h, int img_w, const float* mean,
                     const float* scale, int loop_count, int num_thread, int affinity, int use_opencl)
{
    /* set runtime options */
    struct options opt;
    opt.num_thread = num_thread;
    opt.cluster = TENGINE_CLUSTER_ALL;
    opt.precision = TENGINE_MODE_FP32;
    opt.affinity = affinity;

    /* inital tengine */
    if (init_tengine() != 0)
    {
        fprintf(stderr, "Initial tengine failed.\n");
        return -1;
    }
    fprintf(stderr, "tengine-lite library version: %s\n", get_tengine_version());
    graph_t graph;
    if (use_opencl)
    {
        context_t ocl_context = create_context("ocl", 1);
        int rtt = add_context_device(ocl_context, "OCL");
        if (0 > rtt)
        {
            fprintf(stderr, "add_context_device OpenCL failed.\n");
            return -1;
        }

        /* create graph, load tengine model xxx.tmfile */
        graph = create_graph(ocl_context, "tengine", model_file);
    }
    else
    {
        /* create graph, load tengine model xxx.tmfile */
        graph = create_graph(NULL, "tengine", model_file);
    }

    if (NULL == graph)
    {
        fprintf(stderr, "Create graph failed.\n");
        return -1;
    }

    /* set the shape, data buffer of input_tensor of the graph */
    int img_size = img_h * img_w * 3;
    int dims[] = {1, 3, img_h, img_w}; // nchw
    float* input_data = (float*)malloc(img_size * sizeof(float));
    if (input_data == NULL)
    {
        return -1;
    }

    tensor_t input_tensor = get_graph_input_tensor(graph, 0, 0);
    if (input_tensor == NULL)
    {
        fprintf(stderr, "Get input tensor failed\n");
        return -1;
    }

    if (set_tensor_shape(input_tensor, dims, 4) < 0)
    {
        fprintf(stderr, "Set input tensor shape failed\n");
        return -1;
    }

    if (set_tensor_buffer(input_tensor, input_data, img_size * sizeof(float)) < 0)
    {
        fprintf(stderr, "Set input tensor buffer failed\n");
        return -1;
    }

    /* prerun graph, set work options(num_thread, cluster, precision) */
    if (prerun_graph_multithread(graph, opt) < 0)
    {
        fprintf(stderr, "Prerun multithread graph failed.\n");
        return -1;
    }

    /* prepare process input data, set the data mem to input tensor */

    /* run graph */
    double min_time = DBL_MAX;
    double max_time = DBL_MIN;
    double total_time = 0.;
    int success_count = 0;
    int failed_count = 0;
    char image_file[500];
    for (int class = 0; class < NUMBER_OF_CLASSES; class ++)
    {
        for (int sample = 1; sample <= NUMBER_OF_SAMPLES; sample++)
        {
            switch (class)
            {
            case 0:
                sprintf(image_file, "%s%s%04d%s", collection_path, "airplane/", sample, ".png");
                break;
            case 1:
                sprintf(image_file, "%s%s%04d%s", collection_path, "automobile/", sample, ".png");
                break;
            case 2:
                sprintf(image_file, "%s%s%04d%s", collection_path, "bird/", sample, ".png");
                break;
            case 3:
                sprintf(image_file, "%s%s%04d%s", collection_path, "cat/", sample, ".png");
                break;
            case 4:
                sprintf(image_file, "%s%s%04d%s", collection_path, "deer/", sample, ".png");
                break;
            case 5:
                sprintf(image_file, "%s%s%04d%s", collection_path, "dog/", sample, ".png");
                break;
            case 6:
                sprintf(image_file, "%s%s%04d%s", collection_path, "frog/", sample, ".png");
                break;
            case 7:
                sprintf(image_file, "%s%s%04d%s", collection_path, "horse/", sample, ".png");
                break;
            case 8:
                sprintf(image_file, "%s%s%04d%s", collection_path, "ship/", sample, ".png");
                break;
            case 9:
                sprintf(image_file, "%s%s%04d%s", collection_path, "truck/", sample, ".png");
                break;
            }

            get_input_data(image_file, input_data, img_h, img_w, mean, scale);

            double start = get_current_time();
            if (run_graph(graph, 1) < 0)
            {
                fprintf(stderr, "Run graph failed\n");
                return -1;
            }
            double end = get_current_time();
            double cur = end - start;
            total_time += cur;
            if (min_time > cur)
                min_time = cur;
            if (max_time < cur)
                max_time = cur;
            tensor_t output_tensor = get_graph_output_tensor(graph, 0, 0);
            float* output = (float*)get_tensor_buffer(output_tensor);
            int output_size = get_tensor_buffer_size(output_tensor) / sizeof(float);

            int top = get_top_result(output, output_size);
            if (top == class)
                success_count++;
            else
                failed_count++;
            release_graph_tensor(output_tensor);
        }
    }
    fprintf(stderr, "Success: %d Failed: %d Rate %f %%\n", success_count, failed_count, (float)success_count / (NUMBER_OF_CLASSES * NUMBER_OF_SAMPLES) * 100);

    fprintf(stderr, "\nmodel file : %s\n", model_file);
    fprintf(stderr, "image file : %s\n", image_file);
    fprintf(stderr, "img_h, img_w, scale[3], mean[3] : %d %d , %.3f %.3f %.3f, %.1f %.1f %.1f\n", img_h, img_w,
            scale[0], scale[1], scale[2], mean[0], mean[1], mean[2]);
    fprintf(stderr, "Repeat %d times, thread %d, avg time %.2f ms, max_time %.2f ms, min_time %.2f ms\n", loop_count,
            num_thread, total_time / loop_count, max_time, min_time);
    fprintf(stderr, "--------------------------------------\n");

    /* get the result of classification */
    fprintf(stderr, "--------------------------------------\n");

    /* release tengine */
    free(input_data);
    postrun_graph(graph);
    destroy_graph(graph);
    release_tengine();

    return 0;
}

void show_usage()
{
    fprintf(
        stderr,
        "[Usage]:  [-h]\n    [-m model_file] [-i image_file]\n [-g img_h,img_w] [-s scale[0],scale[1],scale[2]] [-w "
        "mean[0],mean[1],mean[2]] [-r loop_count] [-t thread_count] [-a cpu_affinity]\n");
    fprintf(
        stderr,
        "\nresnet18 example: \n    ./classification -m /path/to/resnet18.tmfile -i /path/to/CIFAR10/ -g 32,32 -s "
        "0.017,0.017,0.017 -w 104.007,116.669,122.679\n");
}

int main(int argc, char* argv[])
{
    int loop_count = DEFAULT_LOOP_COUNT;
    int num_thread = DEFAULT_THREAD_COUNT;
    int cpu_affinity = DEFAULT_CPU_AFFINITY;
    char* model_file = NULL;
    char* image_file = NULL;
    float img_hw[2] = {0.f};
    int img_h = 0;
    int img_w = 0;
    float mean[3] = {-1.f, -1.f, -1.f};
    float scale[3] = {0.f, 0.f, 0.f};
    int use_opencl = 0;

    int res;
    while ((res = getopt(argc, argv, "m:i:l:g:s:w:r:t:a:h:l")) != -1)
    {
        switch (res)
        {
        case 'm':
            model_file = optarg;
            break;
        case 'i':
            image_file = optarg;
            break;
        case 'g':
            split(img_hw, optarg, ",");
            img_h = (int)img_hw[0];
            img_w = (int)img_hw[1];
            break;
        case 's':
            split(scale, optarg, ",");
            break;
        case 'w':
            split(mean, optarg, ",");
            break;
        case 'r':
            loop_count = atoi(optarg);
            break;
        case 't':
            num_thread = atoi(optarg);
            break;
        case 'a':
            cpu_affinity = atoi(optarg);
            break;
        case 'l':
            use_opencl = atoi(optarg);
            break;
        case 'h':
            show_usage();
            return 0;
        default:
            break;
        }
    }

    /* check files */
    if (model_file == NULL)
    {
        fprintf(stderr, "Error: Tengine model file not specified!\n");
        show_usage();
        return -1;
    }

    if (img_h == 0)
    {
        img_h = DEFAULT_IMG_H;
        fprintf(stderr, "Image height not specified, use default %d\n", img_h);
    }

    if (img_w == 0)
    {
        img_w = DEFAULT_IMG_W;
        fprintf(stderr, "Image width not specified, use default  %d\n", img_w);
    }

    if (scale[0] == 0.f || scale[1] == 0.f || scale[2] == 0.f)
    {
        scale[0] = DEFAULT_SCALE1;
        scale[1] = DEFAULT_SCALE2;
        scale[2] = DEFAULT_SCALE3;
        fprintf(stderr, "Scale value not specified, use default  %.1f, %.1f, %.1f\n", scale[0], scale[1], scale[2]);
    }

    if (mean[0] == -1.0 || mean[1] == -1.0 || mean[2] == -1.0)
    {
        mean[0] = DEFAULT_MEAN1;
        mean[1] = DEFAULT_MEAN2;
        mean[2] = DEFAULT_MEAN3;
        fprintf(stderr, "Mean value not specified, use default   %.1f, %.1f, %.1f\n", mean[0], mean[1], mean[2]);
    }

    if (tengine_classify(model_file, image_file, img_h, img_w, mean, scale, loop_count, num_thread, cpu_affinity, use_opencl) < 0)
        return -1;

    return 0;
}

