#include <stdio.h>
#include <opencv/cv.h>
#include <opencv/highgui.h>
#include <opencv2/calib3d/calib3d.hpp>
#include <opencv2/imgcodecs.hpp> 
#include <opencv2/imgproc.hpp>
#include <opencv2/photo.hpp>
#include <iostream>
#include <fstream>
#include <getopt.h>


// deltaE calcs
#include "CIEDE2000.h"

#define MACBETH_WIDTH   6
#define MACBETH_HEIGHT  4
#define MACBETH_SQUARES MACBETH_WIDTH * MACBETH_HEIGHT

#define MAX_CONTOUR_APPROX  7

#define MAX_RGB_DISTANCE 444

bool save_restore_data_to_file = false; // save grid locations in side file
bool restore_from_previous_run = false; // restore grid locations from a previous run
std::string save_restore_filename = "restore.csv";
std::string commandline_str = "";
std::string commentline_str = "";

// BabelColor averages in sRGB:
//   http://www.babelcolor.com/main_level/ColorChecker.htm
// (converted to BGR order for comparison)
CvScalar colorchecker_srgb[MACBETH_HEIGHT][MACBETH_WIDTH] =
    {
        {
            cvScalar(67,81,115),
            cvScalar(129,149,196),
            cvScalar(157,123,93),
            cvScalar(65,108,90),
            cvScalar(176,129,130),
            cvScalar(171,191,99)
        },
        {
            cvScalar(45,123,220),
            cvScalar(168,92,72),
            cvScalar(98,84,195),
            cvScalar(105,59,91),
            cvScalar(62,189,160),
            cvScalar(41,161,229)
        },
        {
            cvScalar(147,62,43),
            cvScalar(72,149,71),
            cvScalar(56,48,176),
            cvScalar(22,200,238),
            cvScalar(150,84,188),
            cvScalar(166,136,0)
        },
        {
            cvScalar(240,245,245),
            cvScalar(201,201,200),
            cvScalar(161,161,160),
            cvScalar(121,121,120),
            cvScalar(85,84,83),
            cvScalar(50,50,50)
        }
    };

double euclidean_distance(CvScalar p_1, CvScalar p_2)
{
    double sum = 0;
    for(int i = 0; i < 3; i++) {
        sum += pow(p_1.val[i]-p_2.val[i],2.);
    }
    /*
    printf("ED: ");
    for(int i = 0; i < 3; i++) {
        printf("%0.2f ", p_2.val[i]);
    }
    printf("\t -  ");
    for(int i = 0; i < 3; i++) {
        printf("%0.2f ", p_1.val[i]);
    }
    printf("\t = %f\n", sqrt(sum));
    */
    return sqrt(sum);
}

double orig_euclidean_distance(CvScalar p_1, CvScalar p_2)
{   
    double sum = 0;
    for(int i = 0; i < 3; i++) {
        sum += pow(p_1.val[i]-p_2.val[i],2.);
    }
    return sqrt(sum);
}

double euclidean_distance(CvPoint p_1, CvPoint p_2)
{
    //return euclidean_distance(cvScalar(p_1.x,p_1.y,0),cvScalar(p_2.x,p_2.y,0));
    double sum = 0;
    sum += pow(p_1.x-p_2.x,2.);
    sum += pow(p_1.y-p_2.y,2.);
    //printf("ED: %d %d  - %d %d = %0.2f\n", p_2.x, p_2.y, p_1.x, p_1.y, sqrt(sum));
    return sqrt(sum);
}

/*
https://stackoverflow.com/questions/26992476/cv-color-bgr2lab-gives-wrong-range

Normally:

    L* ranges from 0 to 100
    a* and b* from -128 to +127

Using float [0,1] images gives proper LAB values.
With 8-bit images, Opencv offsets ab and scales L in a weird way to fit back into 8-bit space:
     L is multiplied by 255/100
    a and b get an offset of 128

=> just convert to float before calling BGR2Lab

*/

double deltaE_distance(CvScalar p_1, CvScalar p_2) {
    // convert to Lab for better perceptual distance
    //IplImage * convert = cvCreateImage( cvSize(2,1), 8, 3);
    IplImage * convert = cvCreateImage( cvSize(2,1), IPL_DEPTH_32F, 3);
    for(int i = 0; i < 3; i++) {
        p_1.val[i] = p_1.val[i] / 255.;
        p_2.val[i] = p_2.val[i] / 255.;
    }
    cvSet2D(convert,0,0,p_1);
    cvSet2D(convert,0,1,p_2);
    /*
    printf("RGB2LAB: ");
    for(int i = 0; i < 3; i++) {
        printf("%0.2f ", p_2.val[i]);
    }
    printf("\t ->  ");
    */
    cvCvtColor(convert,convert,CV_BGR2Lab);
    p_1 = cvGet2D(convert,0,0);
    p_2 = cvGet2D(convert,0,1);
    /*
    for(int i = 0; i < 3; i++) {
        printf("%0.2f ", p_2.val[i]);
    }
    printf("\n");
    */
    cvReleaseImage(&convert);

    CIEDE2000::LAB lab1, lab2;
    lab1.l = p_1.val[0];
    lab1.a = p_1.val[1];
    lab1.b = p_1.val[2];

    lab2.l = p_2.val[0];
    lab2.a = p_2.val[1];
    lab2.b = p_2.val[2];

    double diff = CIEDE2000::CIEDE2000(lab1, lab2);
    //return euclidean_distance(p_1, p_2);
    return diff;
}
double deltaC_distance(CvScalar p_1, CvScalar p_2) {
    // convert to Lab and only look at a b diff
    IplImage * convert = cvCreateImage( cvSize(2,1), IPL_DEPTH_32F, 3);
    for(int i = 0; i < 3; i++) {
        p_1.val[i] = p_1.val[i] / 255.;
        p_2.val[i] = p_2.val[i] / 255.;
    }
    cvSet2D(convert,0,0,p_1);
    cvSet2D(convert,0,1,p_2);

    cvCvtColor(convert,convert,CV_BGR2Lab);
    p_1 = cvGet2D(convert,0,0);
    p_2 = cvGet2D(convert,0,1);

    cvReleaseImage(&convert);

    double sum = 0;
    sum += pow(p_1.val[1]-p_2.val[1],2);
    sum += pow(p_1.val[2]-p_2.val[2],2);
    //printf("ED: %d %d  - %d %d = %0.2f\n", p_2.x, p_2.y, p_1.x, p_1.y, sqrt(sum));
    return sqrt(sum);
}


CvRect contained_rectangle(CvBox2D box)
{
    return cvRect(box.center.x - box.size.width/4,
                  box.center.y - box.size.height/4,
                  box.size.width/2,
                  box.size.height/2);
}

CvScalar rect_stddev(CvRect rect, IplImage* image)
{       

    cvSetImageROI(image, rect);
    IplImage *crop = cvCreateImage(cvGetSize(image),
                               image->depth,
                               image->nChannels);
    cvCopy(image, crop, NULL);
    cvResetImageROI(image);

    cv::Scalar mean;
    cv::Scalar stddev;
    cv::meanStdDev(cv::cvarrToMat(crop), mean, stddev);
    //std::cout << " mean = " << mean.val[0] << std::endl;
    //std::cout << " std  = " << stddev.val[0] << std::endl;

    bool DEBUG = true;
    if (DEBUG) {    
        char buffer [BUFSIZ];
        sprintf (buffer, "images2/img_%f_%f_%f.jpg", stddev.val[0], stddev.val[1], stddev.val[2]);
        std::string filename = buffer;
        cv::imwrite(filename, cv::cvarrToMat(crop));
    }
    
    return stddev;
}


CvScalar rect_average(CvRect rect, IplImage* image)
{       
    CvScalar average = cvScalarAll(0);
    int count = 0;
    for(int x = rect.x; x < (rect.x+rect.width); x++) {
        for(int y = rect.y; y < (rect.y+rect.height); y++) {
            if((x >= 0) && (y >= 0) && (x < image->width) && (y < image->height)) {
                CvScalar s = cvGet2D(image,y,x);
                average.val[0] += s.val[0];
                average.val[1] += s.val[1];
                average.val[2] += s.val[2];
            
                count++;
            }
        }
    }
    
    for(int i = 0; i < 3; i++){
        average.val[i] /= count;
    }
    
    return average;
}

CvScalar contour_average(CvContour* contour, IplImage* image)
{
    CvRect rect = ((CvContour*)contour)->rect;
    
    CvScalar average = cvScalarAll(0);
    int count = 0;
    for(int x = rect.x; x < (rect.x+rect.width); x++) {
        for(int y = rect.y; y < (rect.y+rect.height); y++) {
            if(cvPointPolygonTest(contour, cvPointTo32f(cvPoint(x,y)),0) == 100) {
                CvScalar s = cvGet2D(image,y,x);
                average.val[0] += s.val[0];
                average.val[1] += s.val[1];
                average.val[2] += s.val[2];
                
                count++;
            }
        }
    }
    
    for(int i = 0; i < 3; i++){
        average.val[i] /= count;
    }
    
    return average;
}

void rotate_box(CvPoint2D32f * box_corners)
{
    CvPoint2D32f last = box_corners[3];
    for(int i = 3; i > 0; i--) {
        box_corners[i] = box_corners[i-1];
    }
    box_corners[0] = last;
}

double check_colorchecker(CvMat * colorchecker)
{
    double difference = 0;
    
    for(int x = 0; x < MACBETH_WIDTH; x++) {
        for(int y = 0; y < MACBETH_HEIGHT; y++) {
            CvScalar known_value = colorchecker_srgb[y][x];
            CvScalar test_value = cvGet2D(colorchecker,y,x);
            difference += deltaE_distance(test_value,known_value);

        }
    }
    
    return difference;
}

// src: https://stackoverflow.com/questions/50353884/calculate-text-size
void drawtorect(IplImage * image, cv::Rect target, int face, int thickness, cv::Scalar color, const std::string & str)
{

    cv::Size rect = cv::getTextSize(str, face, 1.0, thickness, 0);
    double scalex = (double)target.width / (double)rect.width;
    double scaley = (double)target.height / (double)rect.height;
    double scale = std::min(scalex, scaley);
    int marginx = scale == scalex ? 0 : (int)((double)target.width * (scalex - scale) / scalex * 0.5);
    int marginy = scale == scaley ? 0 : (int)((double)target.height * (scaley - scale) / scaley * 0.5);
    // cv::putText(mat, str, cv::Point(target.x + marginx, target.y + target.height - marginy), face, scale, color, thickness, 8, false);

    CvFont font;
    cvInitFont(&font, face, scale, scale, 0, thickness);
    cvPutText(
        image,
        str.c_str(),
        cvPoint(target.x + marginx, target.y + target.height - marginy),
        &font,
        color
    );
}

void draw_colorchecker(CvMat * colorchecker_values, CvMat * colorchecker_points, IplImage * image, int size)
{


    for(int x = 0; x < MACBETH_WIDTH; x++) {
        for(int y = 0; y < MACBETH_HEIGHT; y++) {
            CvScalar this_color = cvGet2D(colorchecker_values,y,x);
            CvScalar this_point = cvGet2D(colorchecker_points,y,x);

            int difference = round(deltaE_distance(this_color,colorchecker_srgb[y][x]));
            std::string res = std::to_string(difference);

            
            cvCircle(
                image,
                cvPoint(this_point.val[0],this_point.val[1]),
                size,
                colorchecker_srgb[y][x],
                -1
            );
            
            cvCircle(
                image,
                cvPoint(this_point.val[0],this_point.val[1]),
                size/2,
                this_color,
                -1
            );

            // increase text contrast with 
            float threshold = 300;
            cv::Scalar text_color = cv::Scalar(0,0,0);
            float sum = this_color.val[0] + this_color.val[1] + this_color.val[2]; 
            if (sum < threshold)
                text_color = cv::Scalar(255,255,255);

            drawtorect( 
                image, 
                cv::Rect(this_point.val[0] - size / 2,this_point.val[1] - size / 2, size, size),
                cv::FONT_HERSHEY_TRIPLEX,
                1,
                text_color,
                res
            );

        }
    }
}

struct ColorChecker {
    double error;
    CvMat * values;
    CvMat * points;
    CvMat * stddev;
    double size;
};

ColorChecker find_colorchecker(CvSeq * quads, CvSeq * boxes, CvMemStorage *storage, IplImage *image, IplImage *original_image)
{
    CvPoint2D32f box_corners[4];
    bool passport_box_flipped = false;
    bool rotated_box = false;
    
    CvMat* points = cvCreateMat( boxes->total , 1, CV_32FC2 );
    for(int i = 0; i < boxes->total; i++)
    {
        CvBox2D box = (*(CvBox2D*)cvGetSeqElem(boxes, i));
        cvSet1D(points, i, cvScalar(box.center.x,box.center.y));
    }
    CvBox2D passport_box = cvMinAreaRect2(points,storage);
    fprintf(stderr,"Box:\n\tCenter: %f,%f\n\tSize: %f,%f\n\tAngle: %f\n",passport_box.center.x,passport_box.center.y,passport_box.size.width,passport_box.size.height,passport_box.angle);
    if(passport_box.angle < 0.0) {
      passport_box_flipped = true;
    }
    
    cvBoxPoints(passport_box, box_corners);
    // for(int i = 0; i < 4; i++)
    // {
    //   fprintf(stderr,"Box corner %d: %d,%d\n",i,cvPointFrom32f(box_corners[i]).x,cvPointFrom32f(box_corners[i]).y);
    // }
    
    // cvBox(passport_box, image, cvScalarAll(128), 10);
    
    if(euclidean_distance(cvPointFrom32f(box_corners[0]),cvPointFrom32f(box_corners[1])) <
       euclidean_distance(cvPointFrom32f(box_corners[1]),cvPointFrom32f(box_corners[2]))) {
        fprintf(stderr,"Box is upright, rotating\n");
        rotate_box(box_corners);
        rotated_box = true && passport_box_flipped;
    }

    double horizontal_spacing = euclidean_distance(
        cvPointFrom32f(box_corners[0]),cvPointFrom32f(box_corners[1]))/(double)(MACBETH_WIDTH-1);
    double vertical_spacing = euclidean_distance(
        cvPointFrom32f(box_corners[1]),cvPointFrom32f(box_corners[2]))/(double)(MACBETH_HEIGHT-1);
    double horizontal_slope = (box_corners[1].y - box_corners[0].y)/(box_corners[1].x - box_corners[0].x);
    double horizontal_mag = sqrt(1+pow(horizontal_slope,2));
    double vertical_slope = (box_corners[3].y - box_corners[0].y)/(box_corners[3].x - box_corners[0].x);
    double vertical_mag = sqrt(1+pow(vertical_slope,2));
    double horizontal_orientation = box_corners[0].x < box_corners[1].x ? -1 : 1;
    double vertical_orientation = box_corners[0].y < box_corners[3].y ? -1 : 1;
        
    fprintf(stderr,"Spacing is %f %f\n",horizontal_spacing,vertical_spacing);
    fprintf(stderr,"Slope is %f %f\n", horizontal_slope,vertical_slope);
    
    int average_size = 0;
    for(int i = 0; i < boxes->total; i++)
    {
        CvBox2D box = (*(CvBox2D*)cvGetSeqElem(boxes, i));
        
        CvRect rect = contained_rectangle(box);
        average_size += MIN(rect.width, rect.height);
    }
    average_size /= boxes->total;
    
    fprintf(stderr,"Average contained rect size is %d\n", average_size);
    
    CvMat * this_colorchecker_values = cvCreateMat(MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC3);
    CvMat * this_colorchecker_stddev = cvCreateMat(MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC3);
    CvMat * this_colorchecker_points = cvCreateMat( MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC2 );
    
    // calculate the averages for our oriented colorchecker
    for(int x = 0; x < MACBETH_WIDTH; x++) {
        for(int y = 0; y < MACBETH_HEIGHT; y++) {
            CvPoint2D32f row_start;
            
            if ( ((image->origin == IPL_ORIGIN_BL) || !rotated_box) && !((image->origin == IPL_ORIGIN_BL) && rotated_box) )
            {
                row_start.x = box_corners[0].x + vertical_spacing * y * (1 / vertical_mag);
                row_start.y = box_corners[0].y + vertical_spacing * y * (vertical_slope / vertical_mag);
            }
            else
            {
                row_start.x = box_corners[0].x - vertical_spacing * y * (1 / vertical_mag);
                row_start.y = box_corners[0].y - vertical_spacing * y * (vertical_slope / vertical_mag);
            }
            
            CvRect rect = cvRect(0,0,average_size,average_size);
            
            rect.x = row_start.x - horizontal_spacing * x * ( 1 / horizontal_mag ) * horizontal_orientation;
            rect.y = row_start.y - horizontal_spacing * x * ( horizontal_slope / horizontal_mag ) * vertical_orientation;
            
            cvSet2D(this_colorchecker_points, y, x, cvScalar(rect.x,rect.y));
            
            rect.x = rect.x - average_size / 2;
            rect.y = rect.y - average_size / 2;
            
            // cvRectangle(
            //     image,
            //     cvPoint(rect.x,rect.y),
            //     cvPoint(rect.x+rect.width, rect.y+rect.height),
            //     cvScalarAll(0),
            //     10
            // );
            
            CvScalar average_color = rect_average(rect, original_image);
            CvScalar stddev_color  = rect_stddev(rect, original_image);
            
            cvSet2D(this_colorchecker_values,y,x,average_color);
            cvSet2D(this_colorchecker_stddev,y,x,stddev_color);
        }
    }
    
    double orient_1_error = check_colorchecker(this_colorchecker_values);
    cvFlip(this_colorchecker_values,NULL,-1);
    double orient_2_error = check_colorchecker(this_colorchecker_values);
    
    fprintf(stderr,"Orientation 1: %f\n",orient_1_error);
    fprintf(stderr,"Orientation 2: %f\n",orient_2_error);
    
    if(orient_1_error < orient_2_error) {
        cvFlip(this_colorchecker_values,NULL,-1);
    }
    else {
        cvFlip(this_colorchecker_points,NULL,-1);
    }
    
    // draw_colorchecker(this_colorchecker_values,this_colorchecker_points,image,average_size);
    
    ColorChecker found_colorchecker;
    
    found_colorchecker.error = MIN(orient_1_error,orient_2_error);
    found_colorchecker.values = this_colorchecker_values;
    found_colorchecker.stddev = this_colorchecker_stddev;
    found_colorchecker.points = this_colorchecker_points;
    found_colorchecker.size = average_size;
    
    return found_colorchecker;
}

void save_data(ColorChecker found_colorchecker) {

   printf("saving grid data to %s\n", save_restore_filename.c_str());
   FILE *fp = NULL;
   fp = fopen(save_restore_filename.c_str(), "w");
   fprintf(fp,"\"index\",\"x\",\"y\",\"ptx\",\"pty\",\"size\",\n");
    // dump found_colorchecker.points
    int i = 0;
    for(int y = 0; y < MACBETH_HEIGHT; y++) {            
        for(int x = 0; x < MACBETH_WIDTH; x++) {
            CvScalar this_point = cvGet2D(found_colorchecker.points,y,x);
            fprintf(fp,"%3d,%2d,%2d,%5.0f,%5.0f,%5.0f\n",
                    i,x,y,this_point.val[0], this_point.val[1], found_colorchecker.size);
            i++;
        }
    }

   fprintf(fp,"# %s\n",commandline_str.c_str());
   fclose(fp);
}

ColorChecker restore_data(IplImage *original_image) {

    printf("restoring data from %s\n", save_restore_filename.c_str());
    FILE *fp;
    fp = fopen(save_restore_filename.c_str(), "r");

    int index,x,y,ptx,pty,size;
    CvMat * this_colorchecker_points = cvCreateMat( MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC2);
    CvMat * this_colorchecker_values = cvCreateMat( MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC3);
    CvMat * this_colorchecker_stddev = cvCreateMat( MACBETH_HEIGHT, MACBETH_WIDTH, CV_32FC3);

    //while (fscanf(fp, "%i,%i,%i,%i,%i,%i", &index,&x,&y,&ptx,&pty,&size) > 0) {
   char buff[255];
   while(fgets(buff, 255, fp)) {
        int ret = sscanf(buff, "%d,%d,%d,%d,%d,%d", &index,&x,&y,&ptx,&pty,&size);
        printf("SCAN: %s => i=%d,x=%d,y=%d,ptx=%d,pty=%d,size=%d\n", buff, index,x,y,ptx,pty,size);
        if (ret == 6) {
            cvSet2D(this_colorchecker_points, y, x, cvScalar(  ptx, pty  ));
            CvRect rect = cvRect(ptx,pty,size,size);
            rect.x = rect.x - (float) size / 2.0;
            rect.y = rect.y - (float) size / 2.0;
            
            CvScalar average_color = rect_average(rect, original_image);
            CvScalar stddev_color  = rect_stddev(rect, original_image);
            
            cvSet2D(this_colorchecker_values,y,x,average_color);
            cvSet2D(this_colorchecker_stddev,y,x,stddev_color);
        }
   }

   fclose(fp);

   
   // already oriented when saved
   ColorChecker found_colorchecker;
   found_colorchecker.points = this_colorchecker_points;
   found_colorchecker.error  = check_colorchecker(this_colorchecker_values);
   found_colorchecker.values = this_colorchecker_values;
   found_colorchecker.stddev = this_colorchecker_stddev;
   found_colorchecker.size   = size; 

   printf("done restoring data\n");
   return found_colorchecker;

}



CvSeq * find_quad( CvSeq * src_contour, CvMemStorage *storage, int min_size)
{
    // stolen from icvGenerateQuads
    CvMemStorage * temp_storage = cvCreateChildMemStorage( storage );
    
    int flags = CV_CALIB_CB_FILTER_QUADS;
    CvSeq *dst_contour = 0;
    
    const int min_approx_level = 2, max_approx_level = MAX_CONTOUR_APPROX;
    int approx_level;
    for( approx_level = min_approx_level; approx_level <= max_approx_level; approx_level++ )
    {
        dst_contour = cvApproxPoly( src_contour, sizeof(CvContour), temp_storage,
                                    CV_POLY_APPROX_DP, (float)approx_level );
        // we call this again on its own output, because sometimes
        // cvApproxPoly() does not simplify as much as it should.
        dst_contour = cvApproxPoly( dst_contour, sizeof(CvContour), temp_storage,
                                    CV_POLY_APPROX_DP, (float)approx_level );

        if( dst_contour->total == 4 )
            break;
    }

    // reject non-quadrangles
    if( dst_contour->total == 4 && cvCheckContourConvexity(dst_contour) )
    {
        CvPoint pt[4];
        double d1, d2, p = cvContourPerimeter(dst_contour);
        double area = fabs(cvContourArea(dst_contour, CV_WHOLE_SEQ));
        double dx, dy;

        for( int i = 0; i < 4; i++ )
            pt[i] = *(CvPoint*)cvGetSeqElem(dst_contour, i);

        dx = pt[0].x - pt[2].x;
        dy = pt[0].y - pt[2].y;
        d1 = sqrt(dx*dx + dy*dy);

        dx = pt[1].x - pt[3].x;
        dy = pt[1].y - pt[3].y;
        d2 = sqrt(dx*dx + dy*dy);

        // philipg.  Only accept those quadrangles which are more square
        // than rectangular and which are big enough
        double d3, d4;
        dx = pt[0].x - pt[1].x;
        dy = pt[0].y - pt[1].y;
        d3 = sqrt(dx*dx + dy*dy);
        dx = pt[1].x - pt[2].x;
        dy = pt[1].y - pt[2].y;
        d4 = sqrt(dx*dx + dy*dy);
        if( !(flags & CV_CALIB_CB_FILTER_QUADS) ||
            (d3*1.1 > d4 && d4*1.1 > d3 && d3*d4 < area*1.5 && area > min_size &&
            d1 >= 0.15 * p && d2 >= 0.15 * p) )
        {
            // CvContourEx* parent = (CvContourEx*)(src_contour->v_prev);
            // parent->counter++;
            // if( !board || board->counter < parent->counter )
            //     board = parent;
            // dst_contour->v_prev = (CvSeq*)parent;
            //for( i = 0; i < 4; i++ ) cvLine( debug_img, pt[i], pt[(i+1)&3], cvScalar(200,255,255), 1, CV_AA, 0 );
            // cvSeqPush( root, &dst_contour );
            return dst_contour;
        }
    }
    
    return NULL;
}

// estimate greyness for white balance
// this metric gives more weight to white over black tint
float calc_grey_factor(CvScalar value) {
    float greyness = sqrt(
            pow(value.val[2]-value.val[1],2) + 
            pow(value.val[0]-value.val[1],2)
    );
    return greyness;
}

double rms(CvScalar x)
{
	double sum = 0;
    int n = 3;

	for (int i = 0; i < n; i++)
		sum += pow(x.val[i], 2);

	return sqrt(sum / n);
}


IplImage * find_macbeth( const char *img )
{
    IplImage * macbeth_img = cvLoadImage( img,
        CV_LOAD_IMAGE_ANYCOLOR|CV_LOAD_IMAGE_ANYDEPTH );
        
    IplImage * macbeth_original = cvCreateImage( cvSize(macbeth_img->width, macbeth_img->height), macbeth_img->depth, macbeth_img->nChannels );
    cvCopy(macbeth_img, macbeth_original);
        
    IplImage * macbeth_split[3];
    IplImage * macbeth_split_thresh[3];
    
    for(int i = 0; i < 3; i++) {
        macbeth_split[i] = cvCreateImage( cvSize(macbeth_img->width, macbeth_img->height), macbeth_img->depth, 1 );
        macbeth_split_thresh[i] = cvCreateImage( cvSize(macbeth_img->width, macbeth_img->height), macbeth_img->depth, 1 );
    }
    
    cvSplit(macbeth_img, macbeth_split[0], macbeth_split[1], macbeth_split[2], NULL);
    
    if( macbeth_img )
    {
        int adaptive_method = CV_ADAPTIVE_THRESH_MEAN_C;
        int threshold_type = CV_THRESH_BINARY_INV;
        int block_size = cvRound(
            MIN(macbeth_img->width,macbeth_img->height)*0.02)|1;
        fprintf(stdout,"Using %d as block size\n", block_size);
        
        double offset = 6;
        
        // do an adaptive threshold on each channel
        for(int i = 0; i < 3; i++) {
            cvAdaptiveThreshold(macbeth_split[i], macbeth_split_thresh[i], 255, adaptive_method, threshold_type, block_size, offset);
        }
        
        IplImage * adaptive = cvCreateImage( cvSize(macbeth_img->width, macbeth_img->height), IPL_DEPTH_8U, 1 );
        
        // OR the binary threshold results together
        cvOr(macbeth_split_thresh[0],macbeth_split_thresh[1],adaptive);
        cvOr(macbeth_split_thresh[2],adaptive,adaptive);
        
        for(int i = 0; i < 3; i++) {
            cvReleaseImage( &(macbeth_split[i]) );
            cvReleaseImage( &(macbeth_split_thresh[i]) );
        }
                
        int element_size = (block_size/10)+2;
        fprintf(stdout,"Using %d as element size\n", element_size);
        
        // do an opening on the threshold image
        IplConvKernel * element = cvCreateStructuringElementEx(element_size,element_size,element_size/2,element_size/2,CV_SHAPE_RECT);
        cvMorphologyEx(adaptive,adaptive,NULL,element,CV_MOP_OPEN);
        cvReleaseStructuringElement(&element);
        
        CvMemStorage* storage = cvCreateMemStorage(0);
        
        CvSeq* initial_quads = cvCreateSeq( 0, sizeof(*initial_quads), sizeof(void*), storage );
        CvSeq* initial_boxes = cvCreateSeq( 0, sizeof(*initial_boxes), sizeof(CvBox2D), storage );
        
        // find contours in the threshold image
        CvSeq * contours = NULL;
        cvFindContours(adaptive,storage,&contours);
        
        int min_size = (macbeth_img->width*macbeth_img->height)/
            (MACBETH_SQUARES*100);

        ColorChecker found_colorchecker;
        
        if(contours or restore_from_previous_run) {
          if (! restore_from_previous_run) {
            int count = 0;
            
            for( CvSeq* c = contours; c != NULL; c = c->h_next) {
                CvRect rect = ((CvContour*)c)->rect;
                // only interested in contours with these restrictions
                if(CV_IS_SEQ_HOLE(c) && rect.width*rect.height >= min_size) {
                    // only interested in quad-like contours
                    CvSeq * quad_contour = find_quad(c, storage, min_size);
                    if(quad_contour) {
                        cvSeqPush( initial_quads, &quad_contour );
                        count++;
                        rect = ((CvContour*)quad_contour)->rect;
                        
                        CvScalar average = contour_average((CvContour*)quad_contour, macbeth_img);
                        
                        CvBox2D box = cvMinAreaRect2(quad_contour,storage);
                        cvSeqPush( initial_boxes, &box );
                        
                        // fprintf(stderr,"Center: %f %f\n", box.center.x, box.center.y);
                        
                        double min_distance = MAX_RGB_DISTANCE;
                        CvPoint closest_color_idx = cvPoint(-1,-1);
                        for(int y = 0; y < MACBETH_HEIGHT; y++) {
                            for(int x = 0; x < MACBETH_WIDTH; x++) {
                                double distance = deltaE_distance(average,colorchecker_srgb[y][x]);
                                if(distance < min_distance) {
                                    closest_color_idx.x = x;
                                    closest_color_idx.y = y;
                                    min_distance = distance;
                                }
                            }
                        }
                        
                        CvScalar closest_color = colorchecker_srgb[closest_color_idx.y][closest_color_idx.x];
                        // fprintf(stderr,"Closest color: %f %f %f (%d %d)\n",
                        //     closest_color.val[2],
                        //     closest_color.val[1],
                        //     closest_color.val[0],
                        //     closest_color_idx.x,
                        //     closest_color_idx.y
                        // );
                        
                        // cvDrawContours(
                        //     macbeth_img,
                        //     quad_contour,
                        //     cvScalar(255,0,0),
                        //     cvScalar(0,0,255),
                        //     0,
                        //     element_size
                        // );
                        // cvCircle(
                        //     macbeth_img,
                        //     cvPointFrom32f(box.center),
                        //     element_size*6,
                        //     cvScalarAll(255),
                        //     -1
                        // );
                        // cvCircle(
                        //     macbeth_img,
                        //     cvPointFrom32f(box.center),
                        //     element_size*6,
                        //     closest_color,
                        //     -1
                        // );
                        // cvCircle(
                        //     macbeth_img,
                        //     cvPointFrom32f(box.center),
                        //     element_size*4,
                        //     average,
                        //     -1
                        // );
                        // CvRect rect = contained_rectangle(box);
                        // cvRectangle(
                        //     macbeth_img,
                        //     cvPoint(rect.x,rect.y),
                        //     cvPoint(rect.x+rect.width, rect.y+rect.height),
                        //     cvScalarAll(0),
                        //     element_size
                        // );
                    }
                }
            }
            
            

            fprintf(stderr,"%d initial quads found", initial_quads->total);
            if(count > MACBETH_SQUARES) {
                fprintf(stderr," (probably a Passport)\n");
                
                CvMat* points = cvCreateMat( initial_quads->total , 1, CV_32FC2 );
                CvMat* clusters = cvCreateMat( initial_quads->total , 1, CV_32SC1 );
                
                CvSeq* partitioned_quads[2];
                CvSeq* partitioned_boxes[2];
                for(int i = 0; i < 2; i++) {
                    partitioned_quads[i] = cvCreateSeq( 0, sizeof(**partitioned_quads), sizeof(void*), storage );
                    partitioned_boxes[i] = cvCreateSeq( 0, sizeof(**partitioned_boxes), sizeof(CvBox2D), storage );
                }
                
                // set up the points sequence for cvKMeans2, using the box centers
                for(int i = 0; i < initial_quads->total; i++) {
                    CvBox2D box = (*(CvBox2D*)cvGetSeqElem(initial_boxes, i));
                    
                    cvSet1D(points, i, cvScalar(box.center.x,box.center.y));
                }
                
                // partition into two clusters: passport and colorchecker
                cvKMeans2( points, 2, clusters, 
                           cvTermCriteria( CV_TERMCRIT_EPS+CV_TERMCRIT_ITER,
                                           10, 1.0 ) );
        
                for(int i = 0; i < initial_quads->total; i++) {
                    CvPoint2D32f pt = ((CvPoint2D32f*)points->data.fl)[i];
                    int cluster_idx = clusters->data.i[i];
                    
                    cvSeqPush( partitioned_quads[cluster_idx],
                               cvGetSeqElem(initial_quads, i) );
                    cvSeqPush( partitioned_boxes[cluster_idx],
                               cvGetSeqElem(initial_boxes, i) );

                    // cvCircle(
                    //     macbeth_img,
                    //     cvPointFrom32f(pt),
                    //     element_size*2,
                    //     cvScalar(255*cluster_idx,0,255-(255*cluster_idx)),
                    //     -1
                    // );
                }
                
                ColorChecker partitioned_checkers[2];
                
                // check each of the two partitioned sets for the best colorchecker
                for(int i = 0; i < 2; i++) {
                    partitioned_checkers[i] =
                        find_colorchecker(partitioned_quads[i], partitioned_boxes[i],
                                      storage, macbeth_img, macbeth_original);
                }
                
                // use the colorchecker with the lowest error
                found_colorchecker = partitioned_checkers[0].error < partitioned_checkers[1].error ?
                    partitioned_checkers[0] : partitioned_checkers[1];
                
                cvReleaseMat( &points );
                cvReleaseMat( &clusters );
            }
            else { // just one colorchecker to test
                fprintf(stderr,"\n");
                found_colorchecker = find_colorchecker(initial_quads, initial_boxes,
                                  storage, macbeth_img, macbeth_original);
            }

            // save found_colorchecker
            if (save_restore_data_to_file) {
                save_data(found_colorchecker);
            }
       
        } 

            if (restore_from_previous_run) {
                found_colorchecker = restore_data(macbeth_original);
            }
            
            // render the found colorchecker
            draw_colorchecker(found_colorchecker.values,found_colorchecker.points,macbeth_img,found_colorchecker.size);

            /*
            printf("M_T = np.array([\n");
            for(int x = 0; x < MACBETH_WIDTH; x++) {
                for(int y = 0; y < MACBETH_HEIGHT; y++) {
                    CvScalar known_value = colorchecker_srgb[y][x];
                    printf(" (%d,%d) [%.3f,%.3f,%.3f], \n",
                            x,y,
                          known_value.val[2]/f,known_value.val[1]/f,known_value.val[0]/f);
                }
            }
            printf("])\n");

            printf("# srgb cc values\n");
            for(int x = 0; x < MACBETH_WIDTH; x++) {
                for(int y = 0; y < MACBETH_HEIGHT; y++) {
                    CvScalar known_value = colorchecker_srgb[y][x];
                    printf("%f %f %f\n",
                          known_value.val[2],known_value.val[1],known_value.val[0]);
                }
            }
            printf("\n");
            */

            // print out the colorchecker info
            // should assert that found_colorchecker.error == total_error
            printf("  R , G , B ,   r , g , b , deltaE, deltaC, Noise, Greyness\n");

            float deltaE, deltaE_total_error = 0, deltaE_max_error = 0;
            float grey_factor, grey_total_error = 0, grey_max_error = 0;
            float deltaC, deltaC_total_error = 0, deltaC_max_error = 0;
            float stddev, stddev_total_error = 0, stddev_max_error = 0;

            for(int y = 0; y < MACBETH_HEIGHT; y++) {            
                for(int x = 0; x < MACBETH_WIDTH; x++) {
                    CvScalar known_value = colorchecker_srgb[y][x];
                    CvScalar this_value  = cvGet2D(found_colorchecker.values,y,x);
                    CvScalar this_stddev = cvGet2D(found_colorchecker.stddev,y,x);

                    deltaE = deltaE_distance(this_value,known_value);
                    deltaE_total_error += deltaE;
                    deltaE_max_error = std::max(deltaE_max_error, deltaE);

                    deltaC = deltaC_distance(this_value,known_value);
                    deltaC_total_error += deltaC;
                    deltaC_max_error = std::max(deltaC_max_error, deltaC);

                    static const float noise_fudge_factor = 10.0;
                    stddev = rms(this_stddev) * noise_fudge_factor;
                    stddev_total_error += stddev;
                    stddev_max_error = std::max(stddev_max_error, stddev);

                    
                    if (y == MACBETH_HEIGHT - 1) {
                        grey_factor = calc_grey_factor(this_value);
                        grey_total_error += grey_factor;
                        grey_max_error = std::max(grey_max_error, grey_factor);
                    }
                  
                    printf(" %3.0f,%3.0f,%3.0f,  %3.0f,%3.0f,%3.0f,  %3.0f,  %3.0f,  %3.0f",
                        known_value.val[2],known_value.val[1],known_value.val[0],
                         this_value.val[2], this_value.val[1], this_value.val[0],
                         deltaE, deltaC, stddev
                    );

                    if (y == MACBETH_HEIGHT - 1) {
                        printf(",   %3.0f\n", grey_factor);
                    } else {
                        printf("\n");
                    }


                }
            }
            printf("\n");
           
            float grey_average_error = grey_total_error / (float) MACBETH_WIDTH;

            //printf("   greyFC_total = %5.1f", grey_total_error);
            printf("     greyFC_max = %5.1f", grey_max_error);
            printf(" greyFC_average = %5.1f", grey_average_error);
            printf("\n");
                            
            float stddev_average_error = 
                    stddev_total_error / (float) (MACBETH_HEIGHT * MACBETH_WIDTH);

            //printf("   stddev_total = %5.1f", stddev_total_error);
            printf("      noise_max = %5.1f", stddev_max_error);
            printf("  noise_average = %5.1f", stddev_average_error);
            printf("\n");

                            
            float deltaC_average_error = 
                    deltaC_total_error / (float) (MACBETH_HEIGHT * MACBETH_WIDTH);

            //printf("   deltaC_total = %5.1f", deltaC_total_error);
            printf("     deltaC_max = %5.1f", deltaC_max_error);
            printf(" deltaC_average = %5.1f", deltaC_average_error);
            printf("\n");

                            
            float deltaE_average_error = 
                    deltaE_total_error / (float) (MACBETH_HEIGHT * MACBETH_WIDTH);

            //printf("   deltaE_total = %5.1f", deltaE_total_error);
            printf("     deltaE_max = %5.1f", deltaE_max_error);
            printf(" deltaE_average = %5.1f", deltaE_average_error);
            printf("\n");

            int size = found_colorchecker.size;
            int label_width  = macbeth_img->width  * 0.25;
            int label_height = macbeth_img->height * 0.05;
            int thickness    = label_width * 0.001;
            printf("label_width=%d label_height=%d thickness=%d\n",label_width,label_height,thickness);

            char buf[BUFSIZ];
            cv::Scalar text_color = cv::Scalar(250,250,250);
            sprintf (buf, "grey_average = %2.0f", grey_average_error);
            drawtorect( 
                macbeth_img, 
                cv::Rect(0,0,label_width ,label_height),
                cv::FONT_HERSHEY_TRIPLEX,
                thickness,
                text_color,
                buf
            );

            sprintf (buf, "noise_average = %2.0f", stddev_average_error);
            drawtorect( 
                macbeth_img, 
                cv::Rect(0,1.0 * label_height,label_width ,label_height),
                cv::FONT_HERSHEY_TRIPLEX,
                thickness,
                text_color,
                buf
            );


            sprintf (buf, "deltaC_average = %2.0f", deltaC_average_error);
            drawtorect( 
                macbeth_img, 
                cv::Rect(0,2.0 * label_height,label_width ,label_height),
                cv::FONT_HERSHEY_TRIPLEX,
                thickness,
                text_color,
                buf
            );

            sprintf (buf, "deltaE_average = %2.0f", deltaE_average_error);
            drawtorect( 
                macbeth_img, 
                cv::Rect(0,3.0 * label_height,label_width ,label_height),
                cv::FONT_HERSHEY_TRIPLEX,
                thickness,
                text_color,
                buf
            );

            int fudge = 10;
            int x = cvGetSize(macbeth_img).width  - label_width - fudge;
            int y = cvGetSize(macbeth_img).height - label_height - fudge;
            drawtorect( 
                macbeth_img, 
                cv::Rect(x,y,label_width ,label_height),
                cv::FONT_HERSHEY_TRIPLEX,
                thickness,
                text_color,
                commentline_str.c_str()
            );


        }
       
        
        if (! restore_from_previous_run) {
            cvReleaseMemStorage( &storage );
            if( macbeth_original ) cvReleaseImage( &macbeth_original );
            if( adaptive ) cvReleaseImage( &adaptive );
        }
        
        return macbeth_img;
    }

    if( macbeth_img ) cvReleaseImage( &macbeth_img );

    return NULL;
}

int main( int argc, char *argv[] )
{
    if( argc < 2 )
    {
        fprintf( stderr, "Usage: %s image_file [output_image]\n", argv[0] );
        return 1;
    }

    for (int i=0;i<argc;i++) 
        commandline_str.append(argv[i]).append(" ");

    static struct option long_options[] =
    {
        {"help",     no_argument,       0, 'h'},
        {"restore",  required_argument, 0, 'r'},
        {"save",     required_argument, 0, 's'},
        {"comment",  required_argument, 0, 'c'},
        {0, 0, 0, 0}
    };


    int opt;
    int option_index = 0;
    int i;
      while ((opt = getopt_long(argc - 2, argv + 2, "-hr:s:c:", long_options, &option_index)) != -1)
    {
        switch(opt)
        {
            case 'h': /* --help */
                printf("--help flag\n");
                break;
            case 's': /* --save */
                printf("--save flag (%s)\n", optarg);
                save_restore_filename = optarg;
                save_restore_data_to_file = true;
                break;
            case 'r': /* --restore */
                printf("--restore flag (%s)\n", optarg);
                save_restore_filename = optarg;
                restore_from_previous_run = true;
                break;
            case 'c': /* --comment */
                printf("--comment flag (%s)\n", optarg);
                commentline_str = optarg;
                break;
            case '\1':
                printf("File: %s\n", optarg);
                break;
            default: /* ??? */
                fprintf(stderr, "Invalid option %c\n", opt);
                return 1;
        }
    }

    for (i = optind; i < argc; i++)
        printf("Process: %d/%d %s\n", i, argc, argv[i]);

    char *img_file;
    if (argc > 1) 
        img_file = argv[1];

    char *out_file = strdup("check.jpg");
    if (argc > 2) 
        out_file = argv[2];

    if (save_restore_data_to_file) 
        printf( "saving grid data to file: %s\n", save_restore_filename.c_str());
        
    if (restore_from_previous_run) 
        printf( "restoring grid data from file: %s\n", save_restore_filename.c_str());

    printf("check input:%s results:%s\n", img_file, out_file);


    IplImage *out = find_macbeth( img_file );
    //if( argc > 2) {
        cvSaveImage( out_file, out );
    //}
    cvReleaseImage( &out );

    return 0;
}

