#include <algorithm>
#include <cmath>
#include <iostream>
#include <numeric>

#include <sys/types.h>
#include <dirent.h>

#include "opencv2/imgproc/imgproc.hpp"
#include "opencv2/imgcodecs.hpp"
#include "opencv2/highgui/highgui.hpp"

#include "FilteringContext.h"
#include "SIRParticleFilter.h"

using namespace cv;

const double percent_of_pixels_not_edges = 0.7;     // MATLAB: Used for selecting thresholds
const double threshold_ratio             = 0.4;     // MATLAB: Low threshold is this fraction of the high one
const double sigma                       = 1.4142;  // MATLAB: 1-D Gaussian filter standard deviation

const char* window_autocanny = "AutoCanny Edge Map";
const char* window_autodist  = "Distance Transform for AutoCanny";

void CumSum(const Mat& histogram, Mat& cumsum);

void AutoCanny(const Mat &src) {
    int filter_length = 8 * static_cast<int>(ceil(sigma));
    int n             = (filter_length - 1)/2;
    Mat GX;
    Mat GY;
    Mat gauss_filter_transpose;
    Mat gradient_gauss_filter_transpose;
    Mat mag_gradient;
    Mat mag_hist_3c;
    Mat mag_hist_cumsum_3c;
    const int channels[]      = {2};
    const float range[]       = {0.0, 1.01};
    const int hist_size[]     = {64};
    const float* hist_range[] = {range};
    double percentile = percent_of_pixels_not_edges * src.cols * src.rows;
    double high_threshold;
    double low_threshold;
    Mat edge;
    Mat dist;


    Mat gauss_filter = getGaussianKernel(filter_length, sigma);
    normalize(gauss_filter, gauss_filter, 1.0, 0.0, NORM_L1);

    Mat gradient_gauss_filter(gauss_filter.size(), gauss_filter.type());
    gradient_gauss_filter.at<double>(0)               = gauss_filter.at<double>(1) - gauss_filter.at<double>(0);
    gradient_gauss_filter.at<double>(filter_length-1) = -gradient_gauss_filter.at<double>(0);
    for (size_t i = 1; i < filter_length/2; ++i) {
        gradient_gauss_filter.at<double>(i)                 = (gauss_filter.at<double>(i+1) - gauss_filter.at<double>(i-1))/2.0;
        gradient_gauss_filter.at<double>(filter_length-1-i) = -gradient_gauss_filter.at<double>(i);
    }
    normalize(gradient_gauss_filter, gradient_gauss_filter, 2.0, 0.0, NORM_L1);

    flip(gauss_filter,          gauss_filter,          0);
    flip(gradient_gauss_filter, gradient_gauss_filter, 0);

    transpose(gauss_filter,          gauss_filter_transpose);
    transpose(gradient_gauss_filter, gradient_gauss_filter_transpose);

    filter2D(src, GX, CV_32F, gauss_filter,                    Point(0, n), 0, BORDER_REPLICATE);
    filter2D(GX,  GX, CV_32F, gradient_gauss_filter_transpose, Point(n, 0), 0, BORDER_REPLICATE);

    filter2D(src, GY, CV_32F, gauss_filter_transpose,          Point(n, 0), 0, BORDER_REPLICATE);
    filter2D(GY,  GY, CV_32F, gradient_gauss_filter ,          Point(0, n), 0, BORDER_REPLICATE);

    Mat GX_abs = abs(GX);
    Mat GY_abs = abs(GY);
    Mat G_t = min(GX_abs, GY_abs);
    Mat G_x = max(GX_abs, GY_abs);

    divide(G_t, G_x, G_t);
    pow(G_t, 2.0, G_t);
    sqrt(Scalar(1.0, 1.0, 1.0) + G_t, mag_gradient);
    multiply(G_x, mag_gradient, mag_gradient);

    GX.convertTo(GX, CV_16SC3);
    GY.convertTo(GY, CV_16SC3);

    //???: indagare accuratamente il calcolo dell'istogramma su più canali e come vengono calcolati i threshold
    double min;
    double max;
    minMaxLoc(mag_gradient, &min, &max);
    mag_gradient /= max;
    calcHist(&mag_gradient, 1, channels, Mat(), mag_hist_3c, 1, hist_size, hist_range);
    CumSum(mag_hist_3c, mag_hist_cumsum_3c);
    MatIterator_<float> up_bgr;
    up_bgr = std::upper_bound(mag_hist_cumsum_3c.begin<float>(), mag_hist_cumsum_3c.end<float>(), percentile);
    high_threshold = (static_cast<double>(up_bgr.lpos()) + 1.0) / static_cast<double>(hist_size[0]) * 255.0;
    low_threshold  = threshold_ratio * high_threshold;

    Canny(GX, GY, edge, low_threshold, high_threshold, true);
    distanceTransform(Scalar(255, 255, 255)-edge, dist, DIST_L2, DIST_MASK_5); //DIST_MASK_PRECISE


    /* ------------ PLOT ONLY ------------ */
    Mat edge_show;
    Mat dist_show;
    Mat edge_tmp;
    edge_tmp = Mat::zeros(edge.size(), edge.type());
    src.copyTo(edge_tmp, edge);
    normalize(dist, dist, 0.0, 1.0, NORM_MINMAX);

    edge_show = edge_tmp;
    dist_show = dist;

    imshow(window_autocanny, edge_show);
    imshow(window_autodist,  dist_show);
}


int main()
{
    namedWindow(window_autocanny, WINDOW_NORMAL | WINDOW_KEEPRATIO | CV_GUI_EXPANDED);
    namedWindow(window_autodist,  WINDOW_NORMAL | WINDOW_KEEPRATIO | CV_GUI_EXPANDED);

    while (waitKey(0) != 103);

    Mat src;
    const std::string img_dir("../../img/");
    DIR * dir = opendir(img_dir.c_str());
    if (dir!= nullptr)
    {
        while (struct dirent * ent = readdir(dir))
        {
            size_t len = strlen(ent->d_name);

            if (strcmp(ent->d_name, ".")  != 0 &&
                strcmp(ent->d_name, "..") != 0 &&
                len > 4 && strcmp(ent->d_name + len - 4, ".ppm") == 0)
            {

                src = imread(img_dir + std::string(ent->d_name), IMREAD_COLOR);

                AutoCanny(src);

                waitKey(1);
            }
        }
        closedir(dir);
    }
    else
    {
        perror("Could not open directory.");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

//int main()
//{
//    FilteringContext fc(new SIRParticleFilter);
//
//    fc.run();
//
//    fc.saveResult();
//    
//    return EXIT_SUCCESS;
//}

void CumSum(const Mat& histogram, Mat& cumsum)
{
    cumsum = Mat::zeros(histogram.rows, histogram.cols, histogram.type());
    
    cumsum.at<float>(0) = histogram.at<float>(0);
    for (int i = 1; i < histogram.total(); ++i)
    {
        cumsum.at<float>(i) = cumsum.at<float>(i - 1) + histogram.at<float>(i);
    }
}
