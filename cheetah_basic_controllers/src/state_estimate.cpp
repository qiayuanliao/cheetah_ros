//
// Created by qiayuan on 2021/11/15.
//
#include "cheetah_basic_controllers/state_estimate.h"
#include <cheetah_common/math_utilities.h>

namespace cheetah_ros
{
StateEstimateBase::StateEstimateBase(ros::NodeHandle& nh)
{
  odom_pub_ = std::make_shared<realtime_tools::RealtimePublisher<nav_msgs::Odometry>>(nh, "/odom", 100);
}

void StateEstimateBase::update(ros::Time time, RobotState& state)
{
  if (time < ros::Time(0.01))  // When simulate time reset
    last_publish_ = time;
  if (time - last_publish_ > ros::Duration(0.01))  // 100Hz
  {
    last_publish_ = time;
    if (odom_pub_->trylock())
    {
      odom_pub_->msg_.header.stamp = time;
      odom_pub_->msg_.pose.pose.orientation.x = state.quat_.x();
      odom_pub_->msg_.pose.pose.orientation.y = state.quat_.y();
      odom_pub_->msg_.pose.pose.orientation.z = state.quat_.z();
      odom_pub_->msg_.pose.pose.orientation.w = state.quat_.w();
      odom_pub_->msg_.pose.pose.position.x = state.pos_[0];
      odom_pub_->msg_.pose.pose.position.y = state.pos_[1];
      odom_pub_->msg_.pose.pose.position.z = state.pos_[2];
      odom_pub_->msg_.twist.twist.angular.x = state.angular_vel_[0];
      odom_pub_->msg_.twist.twist.angular.y = state.angular_vel_[1];
      odom_pub_->msg_.twist.twist.angular.z = state.angular_vel_[2];
      odom_pub_->msg_.twist.twist.linear.x = state.linear_vel_[0];
      odom_pub_->msg_.twist.twist.linear.y = state.linear_vel_[1];
      odom_pub_->msg_.twist.twist.linear.z = state.linear_vel_[2];
      odom_pub_->unlockAndPublish();
    }
    geometry_msgs::TransformStamped transform_stamped;
    transform_stamped.header.stamp = time;
    transform_stamped.header.frame_id = "odom";
    transform_stamped.child_frame_id = "base_link";
    transform_stamped.transform.translation.x = state.pos_[0];
    transform_stamped.transform.translation.y = state.pos_[1];
    transform_stamped.transform.translation.z = state.pos_[2];
    transform_stamped.transform.rotation.x = state.quat_.x();
    transform_stamped.transform.rotation.y = state.quat_.y();
    transform_stamped.transform.rotation.z = state.quat_.z();
    transform_stamped.transform.rotation.w = state.quat_.w();
    tf_br_.sendTransform(transform_stamped);
  }
}

FromTopicStateEstimate::FromTopicStateEstimate(ros::NodeHandle& nh) : StateEstimateBase(nh)
{
  sub_ = nh.subscribe<nav_msgs::Odometry>("/ground_truth/state", 100, &FromTopicStateEstimate::callback, this);
}

void FromTopicStateEstimate::callback(const nav_msgs::Odometry::ConstPtr& msg)
{
  buffer_.writeFromNonRT(*msg);
}

void FromTopicStateEstimate::update(ros::Time time, RobotState& state)
{
  nav_msgs::Odometry odom = *buffer_.readFromRT();
  state.pos_ << odom.pose.pose.position.x, odom.pose.pose.position.y, odom.pose.pose.position.z;
  state.quat_.coeffs() << odom.pose.pose.orientation.x, odom.pose.pose.orientation.y, odom.pose.pose.orientation.z,
      odom.pose.pose.orientation.w;
  state.linear_vel_ << odom.twist.twist.linear.x, odom.twist.twist.linear.y, odom.twist.twist.linear.z;
  state.angular_vel_ << odom.twist.twist.angular.x, odom.twist.twist.angular.y, odom.twist.twist.angular.z;
  StateEstimateBase::update(time, state);
}

LinearKFPosVelEstimator::LinearKFPosVelEstimator(ros::NodeHandle& nh) : StateEstimateBase(nh)
{
  double dt = 0.001;
  x_hat_.setZero();
  ps_.setZero();
  vs_.setZero();
  a_.setZero();
  a_.block(0, 0, 3, 3) = Eigen::Matrix<double, 3, 3>::Identity();
  a_.block(0, 3, 3, 3) = dt * Eigen::Matrix<double, 3, 3>::Identity();
  a_.block(3, 3, 3, 3) = Eigen::Matrix<double, 3, 3>::Identity();
  a_.block(6, 6, 12, 12) = Eigen::Matrix<double, 12, 12>::Identity();
  b_.setZero();
  b_.block(0, 0, 3, 3) = 0.5 * dt * dt * Eigen::Matrix<double, 3, 3>::Identity();
  b_.block(3, 0, 3, 3) = dt * Eigen::Matrix<double, 3, 3>::Identity();

  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> c1(3, 6);
  c1 << Eigen::Matrix<double, 3, 3>::Identity(), Eigen::Matrix<double, 3, 3>::Zero();
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic> c2(3, 6);
  c2 << Eigen::Matrix<double, 3, 3>::Zero(), Eigen::Matrix<double, 3, 3>::Identity();
  c_.setZero();
  c_.block(0, 0, 3, 6) = c1;
  c_.block(3, 0, 3, 6) = c1;
  c_.block(6, 0, 3, 6) = c1;
  c_.block(9, 0, 3, 6) = c1;
  c_.block(0, 6, 12, 12) = Eigen::Matrix<double, 12, 12>::Identity();
  c_.block(12, 0, 3, 6) = c2;
  c_.block(15, 0, 3, 6) = c2;
  c_.block(18, 0, 3, 6) = c2;
  c_.block(21, 0, 3, 6) = c2;
  c_(27, 17) = 1.0;
  c_(26, 14) = 1.0;
  c_(25, 11) = 1.0;
  c_(24, 8) = 1.0;
  p_.setIdentity();
  p_ = 100. * p_;
  q_.setIdentity();
  q_.block(0, 0, 3, 3) = (dt / 20.f) * Eigen::Matrix<double, 3, 3>::Identity();
  q_.block(3, 3, 3, 3) = (dt * 9.81f / 20.f) * Eigen::Matrix<double, 3, 3>::Identity();
  q_.block(6, 6, 12, 12) = dt * Eigen::Matrix<double, 12, 12>::Identity();
  r_.setIdentity();
}

void LinearKFPosVelEstimator::update(ros::Time time, RobotState& state)
{
  double imu_process_noise_position = 0.02;
  double imu_process_noise_velocity = 0.02;
  double foot_process_noise_position = 0.002;
  double foot_sensor_noise_position = 0.001;
  double foot_sensor_noise_velocity = 0.1;
  double foot_height_sensor_noise = 0.001;
  Eigen::Matrix<double, 18, 18> q = Eigen::Matrix<double, 18, 18>::Identity();
  q.block(0, 0, 3, 3) = q_.block(0, 0, 3, 3) * imu_process_noise_position;
  q.block(3, 3, 3, 3) = q_.block(3, 3, 3, 3) * imu_process_noise_velocity;
  q.block(6, 6, 12, 12) = q_.block(6, 6, 12, 12) * foot_process_noise_position;

  Eigen::Matrix<double, 28, 28> r = Eigen::Matrix<double, 28, 28>::Identity();
  r.block(0, 0, 12, 12) = r_.block(0, 0, 12, 12) * foot_sensor_noise_position;
  r.block(12, 12, 12, 12) = r_.block(12, 12, 12, 12) * foot_sensor_noise_velocity;
  r.block(24, 24, 4, 4) = r_.block(24, 24, 4, 4) * foot_height_sensor_noise;

  for (int i = 0; i < 4; i++)
  {
    int i1 = 3 * i;

    int q_index = 6 + i1;
    int r_index1 = i1;
    int r_index2 = 12 + i1;
    int r_index3 = 24 + i;

    double high_suspect_number(100);
    q.block(q_index, q_index, 3, 3) =
        (state.contact_state_[i] ? 1. : high_suspect_number) * q.block(q_index, q_index, 3, 3);
    r.block(r_index1, r_index1, 3, 3) = 1. * r.block(r_index1, r_index1, 3, 3);
    r.block(r_index2, r_index2, 3, 3) =
        (state.contact_state_[i] ? 1. : high_suspect_number) * r.block(r_index2, r_index2, 3, 3);
    r(r_index3, r_index3) = (state.contact_state_[i] ? 1. : high_suspect_number) * r(r_index3, r_index3);

    ps_.segment(3 * i, 3) = state.pos_ - state.foot_pos_[i];
    vs_.segment(3 * i, 3) = state.linear_vel_ - state.foot_vel_[i];
  }

  Vec3<double> g(0, 0, -9.81);
  Vec3<double> accel = quaternionToRotationMatrix(state.quat_) * state.accel_ + g;
  Vec4<double> pzs = -0.0265 * Vec4<double>::Ones();

  Eigen::Matrix<double, 28, 1> y;
  y << ps_, vs_, pzs;
  x_hat_ = a_ * x_hat_ + b_ * accel;
  Eigen::Matrix<double, 18, 18> at = a_.transpose();
  Eigen::Matrix<double, 18, 18> pm = a_ * p_ * at + q;
  Eigen::Matrix<double, 18, 28> ct = c_.transpose();
  Eigen::Matrix<double, 28, 1> y_model = c_ * x_hat_;
  Eigen::Matrix<double, 28, 1> ey = y - y_model;
  Eigen::Matrix<double, 28, 28> s = c_ * pm * ct + r;

  Eigen::Matrix<double, 28, 1> s_ey = s.lu().solve(ey);
  x_hat_ += pm * ct * s_ey;

  Eigen::Matrix<double, 28, 18> s_c = s.lu().solve(c_);
  p_ = (Eigen::Matrix<double, 18, 18>::Identity() - pm * ct * s_c) * pm;

  Eigen::Matrix<double, 18, 18> pt = p_.transpose();
  p_ = (p_ + pt) / 2.0;

  if (p_.block(0, 0, 2, 2).determinant() > 0.000001)
  {
    p_.block(0, 2, 2, 16).setZero();
    p_.block(2, 0, 16, 2).setZero();
    p_.block(0, 0, 2, 2) /= 10.;
  }

  state.pos_ = x_hat_.block(0, 0, 3, 1);
  state.linear_vel_ = x_hat_.block(3, 0, 3, 1);

  StateEstimateBase::update(time, state);
}

ImuSensorEstimator::ImuSensorEstimator(ros::NodeHandle& nh, hardware_interface::ImuSensorHandle imu)
  : StateEstimateBase(nh), imu_(std::move(imu)), initial_yaw_(0)
{
}

void ImuSensorEstimator::update(ros::Time time, RobotState& state)
{
  state.quat_.coeffs() << imu_.getOrientation()[0], imu_.getOrientation()[1], imu_.getOrientation()[2],
      imu_.getOrientation()[3];
  if (initial_yaw_ == 0)
    initial_yaw_ = quatToRPY(state.quat_)(2);

  Eigen::Quaternion<double> yaw = RpyToQuat(Vec3<double>(0., 0., -initial_yaw_));
  state.quat_ *= yaw;
  state.angular_vel_ << imu_.getAngularVelocity()[0], imu_.getAngularVelocity()[1], imu_.getAngularVelocity()[2];
  state.accel_ << imu_.getLinearAcceleration()[0], imu_.getLinearAcceleration()[1], imu_.getLinearAcceleration()[2];
}

}  // namespace cheetah_ros
