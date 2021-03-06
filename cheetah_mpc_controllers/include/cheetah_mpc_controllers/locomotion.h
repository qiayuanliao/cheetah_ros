//
// Created by qiayuan on 2021/11/15.
//

#pragma once
#include <cheetah_mpc_controllers/mpc_controller.h>
#include <realtime_tools/realtime_buffer.h>

#include "mpc_solver.h"
#include "gait.h"
#include "cheetah_mpc_controllers/WeightConfig.h"

namespace cheetah_ros
{
class LocomotionBase : public MpcController
{
public:
  LocomotionBase() = default;
  bool init(hardware_interface::RobotHW* robot_hw, ros::NodeHandle& controller_nh) override;
  using FeetController::updateData;
  void updateCommand(const ros::Time& time, const ros::Duration& period) override;

protected:
  // TODO: Add setFootPlace() and setGait()

private:
  void dynamicCallback(cheetah_ros::WeightConfig& config, uint32_t /*level*/);

  std::map<std::string, OffsetDurationGaitRos<double>::Ptr> name2gaits_;
  OffsetDurationGaitRos<double>::Ptr gait_;
};

}  // namespace cheetah_ros
