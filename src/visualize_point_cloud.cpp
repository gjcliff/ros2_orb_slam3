#include <chrono>
#include <functional>
#include <memory>
#include <pcl/filters/statistical_outlier_removal.h>
#include <sensor_msgs/msg/detail/point_field__struct.hpp>
#include <string>
#include <fstream>
#include <iostream>

#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/string.hpp>
#include <sensor_msgs/msg/point_cloud.hpp>
#include <sensor_msgs/msg/point_cloud2.hpp>
#include <sensor_msgs/msg/point_field.hpp>
#include <geometry_msgs/msg/point32.hpp>
#include <geometry_msgs/msg/quaternion.hpp>

#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>

#include "tf2/LinearMath/Quaternion.h"
#include "tf2_ros/transform_broadcaster.h"

#include <nav_msgs/msg/occupancy_grid.hpp>

#include "System.h"

using namespace std::chrono_literals;

class VisualizePointCloud : public rclcpp::Node
{
  public:
    VisualizePointCloud()
    : Node("visualize_point_cloud")
    {
      // declare parameters
      declare_parameter("map_file_name", "2024-05-05_01:44:37 PM_graham_apt_imu_map.csv");
      std::string file_name = get_parameter("map_file_name").as_string();

      map_file_path = static_cast<std::string>(PROJECT_PATH) + "/maps/"+ file_name;
      unscaled_map_file_path = static_cast<std::string>(PROJECT_PATH) + "/unscaled_maps/"+ file_name;

      // create publishers
      point_cloud2_publisher = create_publisher<sensor_msgs::msg::PointCloud2>("orb_point_cloud2", 10);

      unscaled_point_cloud2_publisher = create_publisher<sensor_msgs::msg::PointCloud2>("orb_unscaled_point_cloud2", 10);

      // Initialize the transform broadcaster
      tf_broadcaster =
        std::make_unique<tf2_ros::TransformBroadcaster>(*this);

      timer = create_wall_timer(
      500ms, std::bind(&VisualizePointCloud::timer_callback, this));

      load_point_cloud(map_file_path, point_cloud2);
      load_point_cloud(unscaled_map_file_path, unscaled_point_cloud2);
    }

  private:
    void load_point_cloud(std::string file_name, sensor_msgs::msg::PointCloud2 &point_cloud)
    {
      std::ifstream file(file_name);
      if (file.fail()) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to open file: " << file_name);
        rclcpp::shutdown();
      }

      std::string line;
      int line_num = 0;
      std::vector<geometry_msgs::msg::Point32> points;
      while (std::getline(file, line)) {
        std::istringstream iss(line);
        std::string token;
        int i = 0;
        if (line_num == 0) {
          // collect the orientation of the camera when the map was initialized
          // this will be used to set the orientation of the point cloud in relation
          // to the map
          ORB_SLAM3::IMU::Point initial_imu(0, 0, 0, 0, 0, 0, 0);
          while (std::getline(iss, token, ',')) {
            if (i == 0) {
              initial_imu.a[0] = std::stof(token);
            } else if (i == 1) {
              initial_imu.a[1] = std::stof(token);
            } else if (i == 2) {
              initial_imu.a[2] = std::stof(token);
            } else if (i == 3) {
              initial_imu.w[0] = std::stof(token);
            } else if (i == 4) {
              initial_imu.w[1] = std::stof(token);
            } else if (i == 5) {
              initial_imu.w[2] = std::stof(token);
            }
            i++;
          }
          Eigen::Vector3f accel = initial_imu.a.normalized();
          double pitch = std::atan2(accel.y(), sqrt(accel.x() * accel.x() + accel.z() * accel.z()));
          double roll = std::atan2(-accel.x(), accel.z());
          initial_orientation.setRPY(4.7124, 0.0, 0.0); // 270 degrees
          line_num++;
          continue;
        }
        geometry_msgs::msg::Point32 point;
        while (std::getline(iss, token, ',')) {
          if (i == 0) {
            point.x = std::stof(token);
          } else if (i == 1) {
            point.y = std::stof(token);
          } else if (i == 2) {
            point.z = std::stof(token);
          }
          i++;
        }
        points.push_back(point);
      }

      RCLCPP_INFO_STREAM(get_logger(), "Loaded point cloud with " << points.size() << " points");
      sensor_msgs::msg::PointField x_field;
      x_field.name = "x";
      x_field.offset = 0;
      x_field.datatype = sensor_msgs::msg::PointField::FLOAT32;
      x_field.count = 1;

      sensor_msgs::msg::PointField y_field;
      y_field.name = "y";
      y_field.offset = 4;
      y_field.datatype = sensor_msgs::msg::PointField::FLOAT32;
      y_field.count = 1;

      sensor_msgs::msg::PointField z_field;
      z_field.name = "z";
      z_field.offset = 8;
      z_field.datatype = sensor_msgs::msg::PointField::FLOAT32;
      z_field.count = 1;

      sensor_msgs::msg::PointField intensity_field;
      intensity_field.name = "intensity";
      intensity_field.offset = 12;
      intensity_field.datatype = sensor_msgs::msg::PointField::FLOAT32;
      intensity_field.count = 1;

      point_cloud.header.frame_id = "point_cloud";
      point_cloud.header.stamp = get_clock()->now();
      point_cloud.fields.push_back(x_field); 
      point_cloud.fields.push_back(y_field);
      point_cloud.fields.push_back(z_field);

      point_cloud.height = 1;
      point_cloud.width = points.size();
      point_cloud.point_step = 12;
      point_cloud.row_step = 12 * points.size();
      point_cloud.is_dense = true;
      point_cloud.is_bigendian = false;
      point_cloud.data.resize(point_cloud2.row_step);
      for (int i = 0; i < points.size(); i++) {
        memcpy(&point_cloud.data[i * 12], &points[i].x, 4);
        memcpy(&point_cloud.data[i * 12 + 4], &points[i].y, 4);
        memcpy(&point_cloud.data[i * 12 + 8], &points[i].z, 4);
      }

      // now i can filter the pointcloud
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud(new pcl::PointCloud<pcl::PointXYZ>);
      pcl::fromROSMsg(point_cloud, *cloud);
      pcl::PointCloud<pcl::PointXYZ>::Ptr cloud_filtered(new pcl::PointCloud<pcl::PointXYZ>);
      pcl::StatisticalOutlierRemoval<pcl::PointXYZ> sor;
      sor.setInputCloud(cloud);
      sor.setMeanK(75);
      sor.setStddevMulThresh(0.5);
      sor.filter(*cloud_filtered);
      pcl::toROSMsg(*cloud_filtered, point_cloud);

    }

    void timer_callback()
    {
      geometry_msgs::msg::TransformStamped t;
      t.header.stamp = get_clock()->now();
      t.header.frame_id = "map";
      t.child_frame_id = "point_cloud";

      t.transform.rotation.x = initial_orientation.x();
      t.transform.rotation.y = initial_orientation.y();
      t.transform.rotation.z = initial_orientation.z();
      t.transform.rotation.w = initial_orientation.w();
      t.transform.translation.z = 1.5;
      tf_broadcaster->sendTransform(t);
      
      point_cloud2.header.stamp = get_clock()->now();
      unscaled_point_cloud2.header.stamp = get_clock()->now();
      point_cloud2_publisher->publish(point_cloud2);
      unscaled_point_cloud2_publisher->publish(unscaled_point_cloud2);
    }
    rclcpp::TimerBase::SharedPtr timer;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud>::SharedPtr point_cloud_publisher;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr point_cloud2_publisher;
    rclcpp::Publisher<sensor_msgs::msg::PointCloud2>::SharedPtr unscaled_point_cloud2_publisher;
    rclcpp::Subscription<nav_msgs::msg::OccupancyGrid>::SharedPtr occupancy_grid_sub;
    sensor_msgs::msg::PointCloud2 point_cloud2;
    sensor_msgs::msg::PointCloud2 unscaled_point_cloud2;
    tf2::Quaternion initial_orientation;
    std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster;
    std::string map_file_path;
    std::string unscaled_map_file_path;

};

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
  rclcpp::spin(std::make_shared<VisualizePointCloud>());
  rclcpp::shutdown();
  return 0;
}
