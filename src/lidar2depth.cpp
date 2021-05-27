#include <ros/ros.h>

// Messages
#include <message_filters/subscriber.h>
#include <message_filters/time_synchronizer.h>

// PCL specific includes
#include <sensor_msgs/PointCloud2.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <pcl/filters/passthrough.h>
#include <pcl/cloud_iterator.h>

// Camera Model
#include <image_geometry/pinhole_camera_model.h>
#include <sensor_msgs/CameraInfo.h>

// Image includes
#include <sensor_msgs/Image.h>
#include <image_transport/image_transport.h>
#include <cv_bridge/cv_bridge.h>
#include <sensor_msgs/image_encodings.h>

// TF2 Transform includes
#include <tf2_ros/transform_listener.h>
#include <tf2_sensor_msgs/tf2_sensor_msgs.h>
#include <geometry_msgs/TransformStamped.h>

typedef pcl::PointCloud<pcl::PointXYZ> PointCloud;

using namespace sensor_msgs;
using namespace message_filters;

// How the depth map is encoded in KITTI data:
// Depth maps (annotated and raw Velodyne scans) are saved as uint16 PNG images,
// which can be opened with either MATLAB, libpng++ or the latest version of
// Python's pillow (from PIL import Image). A 0 value indicates an invalid pixel
// (ie, no ground truth exists, or the estimation algorithm didn't produce an
// estimate for that pixel). Otherwise, the depth for a pixel can be computed
// in meters by converting the uint16 value to float and dividing it by 256.0:
//
// disp(u,v)  = ((float)I(u,v))/256.0;
// valid(u,v) = I(u,v)>0;

/**
Converts vector to depth measurement by calculating magnitude and scaling by 256.0
@param xyz vector from camera center to measurement location
@return magnitude of xyz * 256.0
*/
ushort depthFromVec(cv::Point3d xyz){
    float mag = sqrt(xyz.x*xyz.x + xyz.y*xyz.y + xyz.z*xyz.z);
    ushort pixel_val = (ushort)(mag*256.0);
    return pixel_val;
}

/**
* Convert Lidar 3D point cloud to depth map with same image plane as camera
*
* @author Cecilia Mauceri
* @date 26_05_2021
*/
class Lidar2Depth
{
    ros::NodeHandle nh_;

    tf2_ros::Buffer tf_buffer_;
    tf2_ros::TransformListener tf_listener_;

    TimeSynchronizer<PointCloud2, CameraInfo> sync_;
    message_filters::Subscriber<PointCloud2> lidar_sub;
    message_filters::Subscriber<CameraInfo> info_sub;

    image_transport::Publisher pub_;
    std::string target_frame_;


    /**
    * cloud_callback is called for each incoming point cloud message. It transforms the point cloud
    * to camera coordinates, and then projects the points on an image plane using a pinhole camera model
    * The value of each projected point is the distance from the point to the image plane producing
    * a depth map. The depth map is then published as a new message.
    */
    void cloud_callback (const PointCloud2::ConstPtr& cloud_msg,
                         const CameraInfoConstPtr& cam_info)
    {
        printf ("Cloud: width = %d, height = %d\n", cloud_msg->width, cloud_msg->height);
        printf ("Camera: width = %d, height = %d\n", cam_info->width, cam_info->height);

        // Camera Model
        image_geometry::PinholeCameraModel cam_model;
        cam_model.fromCameraInfo(cam_info);

//        //Transform the point cloud into camera coordinates
//        geometry_msgs::TransformStamped transform;
//        sensor_msgs::PointCloud2 cloud_msg_world;
//        try
//        {
//            transform = tf_buffer_.lookupTransform(
//                target_frame_,
//                cloud_msg->header.frame_id,
//                cloud_msg->header.stamp);
//            tf2::doTransform(*cloud_msg, cloud_msg_world, transform);
//        }
//        catch (tf2::TransformException& ex)
//        {
//            ROS_WARN("%s", ex.what());
//            return;
//        }
//
//        // Convert to PCL data type
//        pcl::PCLPointCloud2 cloud2;
//        pcl_conversions::toPCL(cloud_msg_world, cloud2);
//        PointCloud::Ptr cloud(new PointCloud);
//        pcl::fromPCLPointCloud2(cloud2, *cloud);
//
//        // Filter points in front of camera
//        PointCloud cloud_filtered;
//        pcl::PassThrough<pcl::PointXYZ> filter;
//        filter.setInputCloud(cloud);
//        filter.setFilterFieldName("x");
//        filter.setFilterLimits(0.0, 6.0);
//        filter.filter(cloud_filtered); // x is between 0-6
//
//        PointCloud::Ptr cloud_filtered_ptr(&cloud_filtered);
//        filter.setInputCloud(cloud_filtered_ptr);
//        filter.setFilterFieldName("y");
//        filter.filter(cloud_filtered); //x and y are between 0-6
//
//        // 1. Project each point into image plane
//        // 2. Map distance from image plane to pixel value
//        // 3. Reshape into image matrix
//        int rows = 600;
//        int cols = 800;
//        cv::Mat image = cv::Mat::zeros(rows, cols, CV_16UC1);
//        for(PointCloud::iterator it = cloud_filtered.begin(); it != cloud_filtered.end(); it++){
//             cv::Point3d xyz;
//             xyz.x = it->x;
//             xyz.y = it->y;
//             xyz.z = it->z;
//             cv::Point2d uv = cam_model.project3dToPixel(xyz);
//             image.at<ushort>((int)uv.x, (int)uv.y) = depthFromVec(xyz);
//        }
//
//        // Convert to ROS data type, copy the header from cloud_msg
//        std_msgs::Header header = cloud_msg -> header;
//        sensor_msgs::ImagePtr output = cv_bridge::CvImage(header, "mono16", image).toImageMsg();
//
//        // Publish the data
//        pub_.publish (output);
    }

public:
    /** Constructor
    @param camera_info message containing camera intrinsics
    @param target_frame string name of output coordinate frame
    */
    Lidar2Depth(std::string target_frame)
        : tf_listener_(tf_buffer_), sync_(lidar_sub, info_sub, 10)
    {
        target_frame_ = target_frame;

        // Subscribe to the lidar point cloud and camera_info topics
        lidar_sub.subscribe(nh_, "/X1/points", 1);
        info_sub.subscribe(nh_, "/X1/front/image_raw", 1);

        sync_.registerCallback(boost::bind(&Lidar2Depth::cloud_callback, this, _1, _2));

        // Create a ROS publisher for the output depth image
        image_transport::ImageTransport it(nh_);
        pub_ = it.advertise("depth_image", 1);

        printf ("Node ready\n");
    }
};

int main (int argc, char** argv)
{
    // Initialize ROS
    ros::init (argc, argv, "lidar2depth_node");

    // Construct class instance
    Lidar2Depth l2d("");

    // Spin
    ros::spin ();
}