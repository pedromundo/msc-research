#include <libfreenect/libfreenect_sync.h>

#include <opencv2/opencv.hpp>
#include <sstream>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <omp.h>
#include <unistd.h>

//Escape closes the application
#define CLOSE 27
//Image capture keys
#define DEPTH 'q'
#define SR 'w'
#define COLOR 'e'
#define ALL 'A'
#define UP 'U'
#define DOWN 'J'
//Mesh capture keys
#define DEPTH_MESH 'Q'
#define SR_MESH 'W'

//Constants
#define MESH_TYPE_REGULAR 1
#define MESH_TYPE_SR 2
#define SR_SIZE 7

using namespace cv;

static int freenect_angle = 0;

static int count_depth = 0;
static int count_color = 0;
static int count_sr = 0;
static int count_depth_mesh = 0;
static int count_sr_mesh = 0;

static std::string capture_name = "estatua";
static int capture_step = 45;

//Depth thresholds in mm
int depth_min = 0;
int depth_max = 850;

//OpenCV variables
static Size kinect;

//TODO: I dont need all of these since conversion works in-place
static Mat depth_mat_filtered;      //filtered depth data
static Mat depth_mat_float;         //depth data converted to float
static Mat depth_mat_float_filtered; //filtered depth data converted to float

/**********************************************************************************************************

########     ################
# main # --- # start_kinect #
########     ################
                    |
             #######################     #################     ##############	  ########
             # freenect_threadfunc # --- # depthCallback # --- # show_depth # --- # lerp #
             #######################     #################     ##############	  ########
                    |                           |
                    |                           |              ##############
                    |                           -------------- # save_depth #
                    |                                          ##############
                    |
                    |                ###############    ############
                    ---------------- # rgbCallback # -- # show_rgb #
                                     ###############    ############
                                         |
                                         |              ############
                                         -------------- # save_ply #
                                         |				############
                                         |
                                         |              ############
                                         -------------- # save_rgb #
                                                        ############

**********************************************************************************************************/

inline double lerp(double a, double b, double f)
{
    return (a * (1.0 - f)) + (b * f);
}

// ---------------------------------------------------------
// Name:		show_depth
// ---------------------------------------------------------
// Description:	transform a depth frame information into color
// 				and show as a video
// ---------------------------------------------------------

void show_depth (uint16_t *depth) {
    Mat depth_mat = Mat(kinect,CV_16UC1, depth);
    Mat depth_mat_tmp = Mat(kinect, CV_8UC3);
    int i, j;
    for ( i = 0; i < kinect.height; i++)
        for ( j = 0; j < kinect.width; j++) {
            uint16_t depth_in_mm = depth_mat.at<uint16_t>(i,j);
            if (depth_in_mm != 0 && depth_in_mm >= depth_min && depth_in_mm <= depth_max) {
                uint16_t color = (uint16_t) lerp(255.0,32.0,(double) depth_in_mm/FREENECT_DEPTH_MM_MAX_VALUE);
                Vec3b &pixel_color = depth_mat_tmp.at<Vec3b>(i,j);
                pixel_color[0] = color;
                pixel_color[1] = color;
                pixel_color[2] = color;
            } else {
                Vec3b &pixel_color = depth_mat_tmp.at<Vec3b>(i,j);
                pixel_color[0] = 0;
                pixel_color[1] = 0;
                pixel_color[2] = 0;
            }
        }
    cv::imshow("DEPTH", depth_mat_tmp);
}

// ---------------------------------------------------------
// Name:		show_rgb
// ---------------------------------------------------------
// Description:	show rgb frame as a video
// ---------------------------------------------------------

void show_rgb (uchar *rgb) {
    Mat img_bgr_mat;
    Mat img_rgb_mat = Mat(kinect,CV_8UC3,rgb);
    cvtColor(img_rgb_mat, img_bgr_mat, CV_RGB2BGR);
    imshow("RGB", img_bgr_mat);
}

// ---------------------------------------------------------
// Name:		clean_image
// ---------------------------------------------------------
// Description:	manually treshold a CV_16UC1 image, since
// the chances of opencv working for this are kind hit-or-miss
// between versions and operatins systems
// ---------------------------------------------------------

Mat clean_image(Mat input_image, int min_value, int max_value){
    Mat output_image;
    input_image.copyTo(output_image);
    for(int i = 0; i < output_image.size().height; ++i)
    {
        for(int j = 0; j < output_image.size().width; ++j)
        {
            uint16_t &pixel = output_image.at<uint16_t>(i, j);
            if(pixel > max_value || pixel < min_value){
                pixel = 0;
            }
        }
    }
    return output_image;
}

// ---------------------------------------------------------
// Name:		save_depth
// ---------------------------------------------------------
// Description:	create a file and save one depth frame
// ---------------------------------------------------------

int save_depth (uint16_t *depth) {
    Mat depth_mat = Mat(kinect,CV_16UC1, depth);
    std::ostringstream oss;
    oss << capture_name << "_depth_" << count_depth*capture_step << ".png";
    depth_mat = clean_image(depth_mat,depth_min,depth_max);
    imwrite(oss.str(), depth_mat);
    printf("%s saved!\n", oss.str().c_str());
    fflush(stdout);
    ++count_depth;
    return 1;
}

// ---------------------------------------------------------
// Name:		save_sr
// ---------------------------------------------------------
// Description:	create a file and save one sr frame
// ---------------------------------------------------------

int save_sr (Mat sr_image) {
    std::ostringstream oss;
    oss << capture_name << "_sr_" << count_sr*capture_step << ".png";
    Mat clean_mat(sr_image);
    sr_image.copyTo(clean_mat);
    clean_mat = clean_image(clean_mat,depth_min,depth_max);
    imwrite(oss.str(), clean_mat);
    printf("%s saved!\n", oss.str().c_str());
    fflush(stdout);
    ++count_sr;
    return 1;
}

// ---------------------------------------------------------
// Name:		save_rgb
// ---------------------------------------------------------
// Description:	create a file and save one rgb frame
// ---------------------------------------------------------

int save_rgb(uchar *rgb) {
    Mat img_rgb_mat = Mat(kinect,CV_8UC3,rgb);
    Mat img_bgr_mat;
    std::ostringstream oss;
    oss << capture_name << "_color_" << count_color*capture_step << ".png";
    cvtColor(img_rgb_mat, img_bgr_mat, CV_RGB2BGR);
    imwrite(oss.str(), img_bgr_mat);
    printf("%s saved!\n", oss.str().c_str());
    fflush(stdout);
    ++count_color;
    return 1;
}

// ---------------------------------------------------------
// Name:		save_ply
// ---------------------------------------------------------
// Description:	create a file and save a .ply polygonal mesh
// with both depth and color information (now working!)
// ---------------------------------------------------------
int save_ply(Mat depth_mat, Mat color_mat, int mesh_type, int count) {
    std::ostringstream oss;
    if(mesh_type == MESH_TYPE_REGULAR) {
        oss << capture_name << "_mesh_" << count*capture_step << ".ply";
    } else if(mesh_type == MESH_TYPE_SR) {
        oss << capture_name << "_srmesh_" << count*capture_step << ".ply";
    }
    FILE *fp;

    if ((fp = fopen(oss.str().c_str(), "w")) == NULL) {
        printf("\nError: while creating file %s", oss.str().c_str());
        return 0;
    }

    int i, j;

    //bilateral filtering of the depth image
    /*depth_mat.convertTo(depth_mat_float, CV_32FC1);
    bilateralFilter(depth_mat_float, depth_mat_float_filtered, -1, 30, 4.5);
    printf("Bilateral Filter Done!\n");
    depth_mat_float_filtered.convertTo(depth_mat_filtered,CV_16UC1);
    depth_mat = Mat(depth_mat_filtered);*/

    //normals via cross-product
    Mat normal_mat = Mat(depth_mat.size(), CV_32FC3);
    for(int i = 0; i < depth_mat.size().height; ++i)
    {
        for(int j = 0; j < depth_mat.size().width; ++j)
        {
            uint16_t right = 0, left = 0, down = 0, up = 0;

            if(i > 0 && j > 0 && i < depth_mat.size().height-1 && j < depth_mat.size().width-1) {
                right = depth_mat.at<uint16_t>(i+1, j);
                left = depth_mat.at<uint16_t>(i-1, j);
                down = depth_mat.at<uint16_t>(i, j+1);
                up = depth_mat.at<uint16_t>(i, j-1);
            }

            //threshold (as per Hinterstoisser et al (2011))
            if(abs(depth_mat.at<uint16_t>(i, j) - right) > 20) {
                right = 0;
            }
            if(abs(depth_mat.at<uint16_t>(i, j) - left) > 20) {
                left = 0;
            }
            if(abs(depth_mat.at<uint16_t>(i, j) - down) > 20) {
                down = 0;
            }
            if(abs(depth_mat.at<uint16_t>(i, j) - up) > 20) {
                up = 0;
            }

            double dzdx = (right - left) / 2.0;
            double dzdy = (down - up) / 2.0;

            Vec3d d(-dzdx, -dzdy, 1.0);
            Vec3d n = normalize(d);

            normal_mat.at<Vec3f>(i, j) = n;
        }
    }

    uint32_t num_vertices = 0;
    //iterate to count the number of vertices, needed for .ply header
    for (i = 0; i < depth_mat.size().height; i++) {
        for (j = 0; j < depth_mat.size().width; j++) {
            uint16_t z_in_mm = depth_mat.at<uint16_t>(i,j);

            if(z_in_mm != 0 && z_in_mm >= depth_min && z_in_mm <= depth_max) {
                ++num_vertices;
            }
        }
    }

    //then a second time to write the geometry into the .ply file
    fprintf(fp,"ply\n");
    fprintf(fp,"format ascii 1.0\n");
    fprintf(fp,"element vertex %d\n",num_vertices);
    fprintf(fp,"property float x\n");
    fprintf(fp,"property float y\n");
    fprintf(fp,"property float z\n");
    fprintf(fp,"property float nx\n");
    fprintf(fp,"property float ny\n");
    fprintf(fp,"property float nz\n");
    fprintf(fp,"property uchar red\n");
    fprintf(fp,"property uchar green\n");
    fprintf(fp,"property uchar blue\n");
    fprintf(fp,"end_header\n");
    for (i = 0; i < depth_mat.size().height; i++) {
        for (j = 0; j < depth_mat.size().width; j++) {
            uint16_t z_in_mm = depth_mat.at<uint16_t>(i,j);
            double cx = 313.68782938, cy = 259.01834898, fx_inv = 1 / 526.37013657, fy_inv = fx_inv;

            if(z_in_mm != 0 && z_in_mm >= depth_min && z_in_mm <= depth_max) {
                double vx = z_in_mm * (j - cx) * fx_inv;
                double vy = -z_in_mm * (i - cy) * fy_inv;
                //this is going to be a bit misaligned, not that bad if the
                //'centered' image is captured last
                Vec3b color = color_mat.at<Vec3b>(i,j);
                Vec3f normal = normal_mat.at<Vec3f>(i,j);
                fprintf(fp, "%.6lf %.6lf %.6lf %.6lf %.6lf %.6lf %d %d %d\n",
                        (double) vx, (double) vy, (double) -z_in_mm, (double) normal[0],
                        (double) normal[1], (double) normal[2], color[0], color[1], color[2]);
            }
        }
    }

    fclose(fp);
    fflush(fp);

    printf("%s saved!\n", oss.str().c_str());
    fflush(stdout);

    return 1;
}

Mat superresolve(Mat lr_images[], unsigned int image_count, int resample_factor) {
    Mat lr_images_upsampled[image_count]; //stored as float
    Mat alignment_matrices[image_count]; //affine transform matrices
    Mat hr_image;
    float global_min = std::numeric_limits<float>::max(), global_max = std::numeric_limits<float>::min();
    //Pre-processing: get minimum and maximum values from the LR images we use
    //this information to remove extreme values from the HR image because some
    //non-linear noise might be added on the resampling and fusion phases
    //(i.e. remove all values outside of the observed range)
    for(size_t i = 0; i < image_count; ++i) {
        lr_images[i] = clean_image(lr_images[i],depth_min,depth_max);
        lr_images[i].convertTo(lr_images[i],CV_32FC1);
        alignment_matrices[i] = Mat::eye(2, 3, CV_32F);
        double local_min, local_max;
        Mat mask = lr_images[i] > 0;

        minMaxLoc(lr_images[i],&local_min,&local_max,NULL,NULL,mask);

        global_min = local_min<global_min?local_min:global_min;
        global_max = local_max>global_max?local_max:global_max;
    }

    //Registration Phase - Subpixel registration of all LR images to the first
    #pragma omp parallel for
    for(size_t i = 0; i < image_count; ++i) {
        Mat template_image = lr_images[0]; //middle LR image
        findTransformECC(template_image,lr_images[i],alignment_matrices[i], MOTION_AFFINE, \
                         TermCriteria(TermCriteria::COUNT+TermCriteria::EPS, 200, 1E-12));
        std::cout << "Alignment " << i << "->" << 0 << " = " << alignment_matrices[i] << std::endl;
    }

    //Upsample + Warp Phase - Upsample all LR images and use registration information
    //technically the resize operation should happen before the warpAffine, but this
    //is not working in this version of the program, whilst it worked on an older
    //version, ignoring this for now since the results are good
    for(size_t i = 0; i < image_count; ++i) {
        //Warp - Simplified to rigid transform because we aim to have as little
        //translation and rotation between the LR images as possible while still
        //modifying the intrinsics enough to have complementary data
        if(i > 0) {
            warpAffine(lr_images[i],lr_images[i],alignment_matrices[i], lr_images[i].size(),CV_WARP_INVERSE_MAP);
        }

        //Upsample - can use pyramids or perform a simple scale operation
        //results were exactly the same, still gotta find out why
        cv::resize(lr_images[i],lr_images_upsampled[i],lr_images[i].size()*resample_factor,0,0,INTER_NEAREST);
    }

    //Reconstruction Phase - For now, simply averaging the images, this reduces the
    //impact of the additive noise from the kinect sensor on the HR image (Richardt).
    //Nonlinear noise generated during the other phases will be removed using the
    //previously established limits of the depth info; i.e., we can not generate or
    //reconstruct information outside of the observed volume. This implementation
    //performs poorly in case the images are not very well aligned. In fact, even
    //changing the ECC epsilon from 1E-12 to 1E-06 adversely affected the results
    hr_image = Mat(lr_images_upsampled[0].size(),CV_32FC1);
    for(size_t i = 0; i < image_count; ++i) {
        addWeighted(hr_image,1.0,lr_images_upsampled[i],(1.0/image_count),0,hr_image);
    }

    //Filter out samples outside of the observed volume (i.e. non-linear noise and
    //image processing artifacts)

    //Downsample the HR image back to 640x480 to keep it valid within the coordinate
    //system of the Microsoft Kinect sensor
    cv::resize(hr_image,hr_image,hr_image.size()/resample_factor,0,0,INTER_NEAREST);
    hr_image.convertTo(hr_image,CV_16UC1);
    hr_image = clean_image(hr_image,(int)global_min,(int)global_max);
    return hr_image;
}

int main(int argc, char **argv)
{
    if(argc > 1){
        capture_name = std::string(argv[1]);
    }else if(argc > 2){
        capture_step = std::stoi(std::string(argv[2]));
    }

    std::cout << capture_name << capture_step << std::endl;

    namedWindow("RGB", CV_WINDOW_AUTOSIZE);
    namedWindow("DEPTH", CV_WINDOW_AUTOSIZE);

    //Trackbar
    createTrackbar("Min Depth.", "DEPTH", &depth_min, FREENECT_DEPTH_MM_MAX_VALUE, NULL);
    createTrackbar("Max Depth.", "DEPTH", &depth_max, FREENECT_DEPTH_MM_MAX_VALUE, NULL);

    kinect.width = 640;
    kinect.height = 480;

    int key = 0;

    freenect_sync_set_tilt_degs(0,0);

    while(key = waitKey(1))
    {
        //std::cout << key << std::endl;
        //opencv apparently cant decide whether 'no key' is -1 or 255. (Yes, I tried masking and casting)
        if(key == 255 || key==-1) {
            uint32_t timestamp;
            uint32_t timestamp_depth;
            unsigned char *data;
            uint16_t *depth_data;
            #pragma omp parallel num_threads(2)
            {
                int thread_num = omp_get_thread_num();
                if(thread_num == 0) {
                    freenect_sync_get_video((void**)(&data), &timestamp, 0, FREENECT_VIDEO_RGB);
                    show_rgb(data);
                } else if(thread_num == 1) {
                    freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                    show_depth(depth_data);
                }
            }
        } else {
            if (key == CLOSE) {
                break;
            }
            if (key == DEPTH) {
                uint32_t timestamp_depth;
                uint16_t *depth_data;
                freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                save_depth(depth_data);
            }
            if (key == COLOR) {
                uint32_t timestamp;
                unsigned char *data;
                freenect_sync_get_video((void**)(&data), &timestamp, 0, FREENECT_VIDEO_RGB);
                save_rgb(data);
            }
            if (key == SR) {
                Mat sr_frames[SR_SIZE];
                for(int i = 0; i<SR_SIZE; ++i) {
                    uint32_t timestamp_depth;
                    uint16_t *depth_data;
                    freenect_sync_set_tilt_degs(i-3,0);
                    sleep(1);
                    freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                    std::cout << timestamp_depth << std::endl;
                    Mat(kinect,CV_16UC1,depth_data).copyTo(sr_frames[i]);
                }
                Mat sr_image = superresolve(sr_frames,SR_SIZE,4);
                save_sr(sr_image);
                freenect_sync_set_tilt_degs(0,0);
            }
            if (key == DEPTH_MESH) {
                uint32_t timestamp_depth;
                uint16_t *depth_data;
                freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                uint32_t timestamp_color;
                unsigned char *color_data;
                freenect_sync_get_video((void**)(&color_data), &timestamp_color, 0, FREENECT_VIDEO_RGB);
                save_ply(Mat(kinect,CV_16UC1,depth_data),Mat(kinect,CV_8UC3,color_data),MESH_TYPE_REGULAR,count_depth_mesh);
                ++count_depth_mesh;
            }
            if (key == SR_MESH) {
                uint32_t timestamp_color;
                unsigned char *color_data;
                freenect_sync_get_video((void**)(&color_data), &timestamp_color, 0, FREENECT_VIDEO_RGB);
                Mat sr_frames[SR_SIZE];
                for(int i = 0; i<SR_SIZE; ++i) {
                    uint32_t timestamp_depth;
                    uint16_t *depth_data;
                    freenect_sync_set_tilt_degs(i-3,0);
                    sleep(1);
                    freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                    std::cout << timestamp_depth << std::endl;
                    Mat(kinect,CV_16UC1,depth_data).copyTo(sr_frames[i]);
                }
                Mat sr_image = superresolve(sr_frames,SR_SIZE,4);
                save_ply(sr_image,Mat(kinect,CV_8UC3,color_data),MESH_TYPE_SR,count_sr_mesh);
                ++count_sr_mesh;
            }
            if (key == UP) {
                freenect_sync_set_tilt_degs(3,0);
            }
            if (key == DOWN) {
                freenect_sync_set_tilt_degs(-3,0);
            }
            if (key == ALL) {
                //depth
                uint32_t timestamp_depth;
                uint16_t *depth_data;
                freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                save_depth(depth_data);

                //color
                uint32_t timestamp;
                unsigned char *data;
                freenect_sync_get_video((void**)(&data), &timestamp, 0, FREENECT_VIDEO_RGB);
                save_rgb(data);

                //depth sr
                Mat sr_frames[SR_SIZE];
                for(int i = 0; i<SR_SIZE; ++i) {
                    uint32_t timestamp_depth;
                    uint16_t *depth_data;
                    freenect_sync_set_tilt_degs(i-3,0);
                    sleep(1);
                    freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                    std::cout << timestamp_depth << std::endl;
                    Mat(kinect,CV_16UC1,depth_data).copyTo(sr_frames[i]);
                }
                Mat sr_image = superresolve(sr_frames,SR_SIZE,4);
                save_sr(sr_image);
                freenect_sync_set_tilt_degs(0,0);

                //depth mesh
                freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                uint32_t timestamp_color;
                unsigned char *color_data;
                freenect_sync_get_video((void**)(&color_data), &timestamp_color, 0, FREENECT_VIDEO_RGB);
                save_ply(Mat(kinect,CV_16UC1,depth_data),Mat(kinect,CV_8UC3,color_data),MESH_TYPE_REGULAR,count_depth_mesh);
                ++count_depth_mesh;

                //depth_sr_mesh
                freenect_sync_get_video((void**)(&color_data), &timestamp_color, 0, FREENECT_VIDEO_RGB);
                for(int i = 0; i<SR_SIZE; ++i) {
                    uint32_t timestamp_depth;
                    uint16_t *depth_data;
                    freenect_sync_set_tilt_degs(i-3,0);
                    sleep(1);
                    freenect_sync_get_depth((void**)(&depth_data), &timestamp_depth, 0, FREENECT_DEPTH_REGISTERED);
                    std::cout << timestamp_depth << std::endl;
                    Mat(kinect,CV_16UC1,depth_data).copyTo(sr_frames[i]);
                }
                sr_image = superresolve(sr_frames,SR_SIZE,4);
                save_ply(sr_image,Mat(kinect,CV_8UC3,color_data),MESH_TYPE_SR,count_sr_mesh);
                ++count_sr_mesh;
            }
        }
    }

    return 0;
}