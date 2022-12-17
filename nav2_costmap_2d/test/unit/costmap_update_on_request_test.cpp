// Copyright (c) 2022 Samsung R&D Institute Russia
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License. Reserved.

#include <vector>
#include <memory>
#include <chrono>

#include <gtest/gtest.h>

#include <rclcpp/rclcpp.hpp>
#include <nav_msgs/msg/occupancy_grid.hpp>
#include <nav2_costmap_2d/cost_values.hpp>
#include <nav2_costmap_2d/costmap_2d_ros.hpp>
#include <nav2_util/occ_grid_values.hpp>

using namespace std::chrono_literals;

static constexpr double RESOLUTION = 0.05;
static constexpr double ORIGIN_X = 0.1;
static constexpr double ORIGIN_Y = 0.2;

// Wrapper around Costmap2DROS to get access to protected member variables
class Costmap2DROSWrapper : public nav2_costmap_2d::Costmap2DROS
{
public:
  Costmap2DROSWrapper(const std::string & name)
  : Costmap2DROS(name, "/", name, true) {}

  void setup()
  {
    // Workaround to avoid setting base_link->map transform
    this->set_parameter(rclcpp::Parameter("robot_base_frame", "map"));
    this->set_parameter(rclcpp::Parameter("update_frequency", 10.0));

    // Add static_layer plugin
    std::vector<std::string> plugins_str{"static_layer"};
    this->set_parameter(rclcpp::Parameter("plugins", plugins_str));
    this->declare_parameter(
      "static_layer.plugin",
      rclcpp::ParameterValue(std::string("nav2_costmap_2d::StaticLayer")));

    this->on_configure(this->get_current_state());
    this->on_activate(this->get_current_state());
  }

  void setUpdateOnRequestParam(const bool & update_on_request)
  {
    this->set_parameter(rclcpp::Parameter("update_on_request", update_on_request));
  }

  void waitForMap()
  {
    while (!this->isCurrent()) {
      rclcpp::spin_some(this->get_node_base_interface());
    }
  }

  void teardown()
  {
    this->on_deactivate(this->get_current_state());
    this->on_cleanup(this->get_current_state());
    this->on_shutdown(this->get_current_state());
  }

};

class OccGridPublisher : public rclcpp::Node
{
public:
  OccGridPublisher(
    const std::string & map_topic,
    const nav_msgs::msg::OccupancyGrid & occ_grid)
  : Node("occupancy_grid_pub")
  {
    publisher_ = this->create_publisher<nav_msgs::msg::OccupancyGrid>(
      map_topic,
      rclcpp::QoS(rclcpp::KeepLast(1)).transient_local().reliable());

    publisher_->publish(occ_grid);
  }

  ~OccGridPublisher()
  {
    publisher_.reset();
  }

private:
  rclcpp::Publisher<nav_msgs::msg::OccupancyGrid>::SharedPtr publisher_;
};

class TestNode : public ::testing::Test
{
public:
  TestNode()
  {
    node_ = std::make_shared<nav2_util::LifecycleNode>("test_node");
    costmap_ros_wrapper_ = std::make_shared<Costmap2DROSWrapper>("costmap_ros");
  }

  ~TestNode()
  {
    costmap_ros_wrapper_.reset();
    occ_grid_pub_.reset();
    occ_grid_.reset();
  }

protected:
  void createOccupancyGrid(
    const std::string & frame_id, const unsigned int & width,
    const unsigned int & height, const int8_t & default_val);

  void publishOccupancyGrid();

  bool checkCostmapValues(const unsigned char & value);

  void waitSome(const std::chrono::nanoseconds & duration);

  std::shared_ptr<Costmap2DROSWrapper> costmap_ros_wrapper_;

private:
  nav2_util::LifecycleNode::SharedPtr node_;

  std::shared_ptr<OccGridPublisher> occ_grid_pub_;
  std::shared_ptr<nav_msgs::msg::OccupancyGrid> occ_grid_;

};

bool TestNode::checkCostmapValues(const unsigned char & value)
{
  nav2_costmap_2d::Costmap2D * costmap = costmap_ros_wrapper_->getCostmap();

  for (unsigned int i = 0; i < costmap->getSizeInCellsX() * costmap->getSizeInCellsY(); i++) {
    if (costmap->getCost(i) != value) {
      return false;
    }
  }

  return true;
}

void TestNode::createOccupancyGrid(
  const std::string & frame_id,
  const unsigned int & width,
  const unsigned int & height,
  const int8_t & default_val)
{
  // Create occ_grid_ map
  occ_grid_ = std::make_shared<nav_msgs::msg::OccupancyGrid>();

  occ_grid_->header.frame_id = frame_id;
  occ_grid_->info.resolution = RESOLUTION;
  occ_grid_->info.width = width;
  occ_grid_->info.height = height;
  occ_grid_->info.origin.position.x = ORIGIN_X;
  occ_grid_->info.origin.position.y = ORIGIN_Y;
  occ_grid_->info.origin.position.z = 0.0;
  occ_grid_->info.origin.orientation.x = 0.0;
  occ_grid_->info.origin.orientation.y = 0.0;
  occ_grid_->info.origin.orientation.z = 0.0;
  occ_grid_->info.origin.orientation.w = 1.0;

  occ_grid_->data.resize(width * height, default_val);
}

void TestNode::publishOccupancyGrid()
{
  occ_grid_pub_.reset();
  occ_grid_pub_ = std::make_shared<OccGridPublisher>("map", *occ_grid_);
}

void TestNode::waitSome(const std::chrono::nanoseconds & duration)
{
  rclcpp::Time start_time = node_->now();

  while (rclcpp::ok() && node_->now() - start_time <= rclcpp::Duration(duration)) {
    rclcpp::spin_some(costmap_ros_wrapper_->get_node_base_interface());
    std::this_thread::sleep_for(50ms);
  }
}

TEST_F(TestNode, testUpdateOnRequestTrue)
{
  costmap_ros_wrapper_->setUpdateOnRequestParam(true);
  costmap_ros_wrapper_->setup();
  ASSERT_EQ(costmap_ros_wrapper_->isUpdateOnRequest(), true);

  // Create and Publish fake occupancy grid
  createOccupancyGrid("map", 5, 5, nav2_util::OCC_GRID_OCCUPIED);
  publishOccupancyGrid();

  // Spin to check that background thread does not update the costmap
  waitSome(200ms);
  // rclcpp::spin_some(costmap_ros_wrapper_->get_node_base_interface());

  // Check that costmap has not been updated
  ASSERT_TRUE(checkCostmapValues(nav2_costmap_2d::FREE_SPACE));

  // Manually trigger updateAndPublishMap()
  costmap_ros_wrapper_->updateAndPublishMap();
  // Spin until the map is current
  costmap_ros_wrapper_->waitForMap();

  // Check that costmap has been updated with correct values
  ASSERT_TRUE(checkCostmapValues(nav2_costmap_2d::LETHAL_OBSTACLE));

  // Do cleanup
  costmap_ros_wrapper_->teardown();
}

TEST_F(TestNode, testUpdateOnRequestFalse)
{
  costmap_ros_wrapper_->setUpdateOnRequestParam(false);
  costmap_ros_wrapper_->setup();
  ASSERT_EQ(costmap_ros_wrapper_->isUpdateOnRequest(), false);

  // Create and Publish fake occupancy grid
  createOccupancyGrid("map", 4, 6, nav2_util::OCC_GRID_OCCUPIED);
  publishOccupancyGrid();

  // Spin until the background thread updates the map
  waitSome(200ms);

  // Spin until the map is current
  costmap_ros_wrapper_->waitForMap();

  // Check that costmap has been updated
  ASSERT_TRUE(checkCostmapValues(nav2_costmap_2d::LETHAL_OBSTACLE));

  // Do cleanup
  costmap_ros_wrapper_->teardown();
}

int main(int argc, char ** argv)
{
  // Initialize the system
  testing::InitGoogleTest(&argc, argv);
  rclcpp::init(argc, argv);

  // Actual testing
  bool test_result = RUN_ALL_TESTS();

  // Shutdown
  rclcpp::shutdown();

  return test_result;
}
